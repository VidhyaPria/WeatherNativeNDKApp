/*
 * The soft keyboard for the add-location search field.
 *
 * This is the ONE thing on screen 2b that cannot be done in C++: only ArkTS can reach
 * @ohos.inputMethod, and an XComponent surface has no IME of its own.
 *
 * It is deliberately not an ArkUI TextInput. A TextInput would bring an ArkUI text component, its
 * measurement pass and its own render node into the very screen the two ports are being weighed
 * against — and the field would then be the only thing on the surface ArkUI drew. Attaching the
 * input method directly keeps the split the rest of this build has: ArkTS holds the keystrokes,
 * C++ draws the glyphs, the placeholder and the caret (view/LocationSearchScreen.cpp).
 *
 * What the framework gives us is an edit session with three events — insertText, deleteLeft and
 * sendFunctionKey — which is exactly the three a single-line search field needs. The text itself is
 * ours to keep: the IME reports edits, it does not hold the buffer.
 */

import { inputMethod } from '@kit.IMEKit';
import { hilog } from '@kit.PerformanceAnalysisKit';

const DOMAIN: number = 0x0000;
const TAG: string = 'BreezyShell';

/** Called with the field's full contents after every edit. */
export type TextChanged = (text: string) => void;
/** The IME's Search key — `onSearch` (SearchActivity.kt:175-181). */
export type SearchSubmitted = (text: string) => void;

export class TextEntry {
  private controller: inputMethod.InputMethodController = inputMethod.getController();
  private attached: boolean = false;
  private subscribed: boolean = false;
  /** What has actually been committed. */
  private text: string = '';
  /**
   * The composing run, and where it sits in `text`.
   *
   * A pinyin or handwriting IME shows what it is about to commit as PREVIEW text, not as an insert,
   * and a TextInput renders that inline and underlined. Without handling it the field stayed on the
   * placeholder for the whole of a Chinese word and only jumped once the candidate was chosen. The
   * preview is spliced into the display string at `previewStart`; committing arrives afterwards as
   * an ordinary insertText over the same range.
   */
  private preview: string = '';
  private previewStart: number = 0;
  private previewEnd: number = 0;
  private onChanged: TextChanged;
  private onSubmit: SearchSubmitted;
  /**
   * NUMBER + DONE rather than TEXT + SEARCH, for the time picker's TimeInput
   * (TimePickerPreference.kt:216). The search field keeps the defaults, so its behaviour is
   * untouched — this is the only thing that differs between the two consumers of this class.
   */
  private numeric: boolean;

  constructor(onChanged: TextChanged, onSubmit: SearchSubmitted, numeric: boolean = false) {
    this.onChanged = onChanged;
    this.onSubmit = onSubmit;
    this.numeric = numeric;
  }

  /** What the field should show: the committed text with any composing run spliced in. */
  private displayed(): string {
    if (this.preview.length === 0) {
      return this.text;
    }
    const start: number = Math.min(Math.max(0, this.previewStart), this.text.length);
    const end: number = Math.min(Math.max(start, this.previewEnd), this.text.length);
    return this.text.substring(0, start) + this.preview + this.text.substring(end);
  }

  private publish(): void {
    this.onChanged(this.displayed());
  }

  /**
   * Pre-fills the buffer with a value the app already holds, so an edit STARTS from it.
   *
   * `attach()` clears the buffer — that is what the numeric time fields need, since moving focus
   * between hour and minute must start empty. A stored API key or instance URL is the opposite
   * case: the dialog draws the stored value, so the first keystroke has to append to it rather than
   * replace it. Without this the NDK behaved differently from ArkUIApp, whose `TextInput` is seeded
   * with the same value.
   *
   * Deliberately does NOT publish: C++ already has this text (it is what it sent), and echoing it
   * back would be a redundant round trip.
   */
  seed(text: string): void {
    this.text = text;
    this.preview = '';
    this.previewStart = 0;
    this.previewEnd = 0;
  }

  /**
   * Opens an edit session and raises the keyboard.
   *
   * `enterKeyType: SEARCH` is what puts a Search key on it rather than a Return —
   * `keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search)` (Widgets.kt:155).
   *
   * THE ORDER OF THE TWO SUBSCRIPTION CALLS IS NOT COSMETIC. The preview events have to be
   * subscribed BEFORE attach — the framework decides at attach time whether this editor supports
   * preview, from what is subscribed — while insertText, deleteLeft and sendFunctionKey throw
   * 12800009 "input method client detached" if they are subscribed before it. Registering all five
   * up front therefore left the app with no keyboard at all, because the throw took the attach call
   * with it.
   */
  async attach(): Promise<void> {
    if (this.attached) {
      return;
    }
    this.text = '';
    this.preview = '';
    this.previewStart = 0;
    this.previewEnd = 0;
    this.subscribePreview();
    try {
      await this.controller.attach(true, {
        inputAttribute: {
          textInputType: this.numeric
            ? inputMethod.TextInputType.NUMBER
            : inputMethod.TextInputType.TEXT,
          enterKeyType: this.numeric
            ? inputMethod.EnterKeyType.DONE
            : inputMethod.EnterKeyType.SEARCH
        }
      });
      this.attached = true;
      this.subscribeEdits();
    } catch (error) {
      const failure = error as Error;
      hilog.error(DOMAIN, TAG, 'could not attach the input method: %{public}s', failure.message);
    }
  }

  private subscribeEdits(): void {
    this.controller.on('insertText', (inserted: string) => {
      // A commit replaces whatever was being previewed, so the run goes with it.
      if (this.preview.length > 0) {
        const start: number = Math.min(Math.max(0, this.previewStart), this.text.length);
        const end: number = Math.min(Math.max(start, this.previewEnd), this.text.length);
        this.text = this.text.substring(0, start) + this.text.substring(end);
        this.preview = '';
      }
      this.text += inserted;
      this.publish();
    });
    this.controller.on('deleteLeft', (length: number) => {
      // The IME counts UTF-16 units, which is what String.length and substring() count too.
      this.text = this.text.substring(0, Math.max(0, this.text.length - length));
      this.publish();
    });
    this.controller.on('sendFunctionKey', (key: inputMethod.FunctionKey) => {
      if (key.enterKeyType === inputMethod.EnterKeyType.SEARCH ||
        key.enterKeyType === inputMethod.EnterKeyType.DONE) {
        this.onSubmit(this.displayed());
      }
    });
  }

  /** Subscribed once and left in place: these two are legal only while nothing is attached. */
  private subscribePreview(): void {
    if (this.subscribed) {
      return;
    }
    this.subscribed = true;
    try {
      this.controller.on('setPreviewText', (text: string, range: inputMethod.Range) => {
        this.preview = text;
        this.previewStart = range.start;
        this.previewEnd = range.end;
        this.publish();
      });
      this.controller.on('finishTextPreview', () => {
        this.preview = '';
        this.publish();
      });
    } catch (error) {
      // Preview text is API 17; on an older device the field simply fills in on commit, which is
      // what it did before this existed.
      hilog.info(DOMAIN, TAG, 'preview text unavailable on this device');
    }
  }

  /**
   * Drops the keyboard WITHOUT ending the session, for as long as a dialog covers the screen.
   *
   * Detaching instead would clear the typed query, and the query is what the results behind the
   * dialog belong to.
   */
  async setKeyboardVisible(visible: boolean): Promise<void> {
    if (!this.attached) {
      return;
    }
    try {
      if (visible) {
        await this.controller.showTextInput();
      } else {
        await this.controller.hideTextInput();
      }
    } catch (error) {
      const failure = error as Error;
      hilog.warn(DOMAIN, TAG, 'could not toggle the keyboard: %{public}s', failure.message);
    }
  }

  /** Closes the session and drops the keyboard. Safe to call when nothing is attached. */
  async detach(): Promise<void> {
    if (!this.attached) {
      return;
    }
    this.attached = false;
    this.text = '';
    this.preview = '';
    try {
      // The three edit events go with the session; the preview pair outlives it (see attach).
      this.controller.off('insertText');
      this.controller.off('deleteLeft');
      this.controller.off('sendFunctionKey');
      await this.controller.detach();
    } catch (error) {
      const failure = error as Error;
      hilog.warn(DOMAIN, TAG, 'could not detach the input method: %{public}s', failure.message);
    }
  }
}
