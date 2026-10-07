#include "data/AboutCatalogue.h"

#include <algorithm>

#include "data/SourceCatalogue.h"

namespace breezy {
namespace {

/** github = "x" → https://github.com/x (Contributors.kt:30). */
std::string Gh(const char* handle) {
    return std::string("https://github.com/") + handle;
}

/**
 * The base letter for one Latin-1 / Latin-Extended-A code point, or nullptr to leave it alone.
 *
 * Only the ranges the catalogue actually uses are covered — every accented letter in a source name
 * is a two-byte UTF-8 sequence in U+00C0..U+017F.
 */
const char* FoldLatin(unsigned int cp) {
    switch (cp) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5:
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5:
        case 0x0100: case 0x0101: case 0x0102: case 0x0103: case 0x0104: case 0x0105:
            return "a";
        case 0x00C7: case 0x00E7:
        case 0x0106: case 0x0107: case 0x010C: case 0x010D:
            return "c";
        case 0x010E: case 0x010F: case 0x0110: case 0x0111:
            return "d";
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB:
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB:
        case 0x0112: case 0x0113: case 0x0116: case 0x0117: case 0x0118: case 0x0119:
        case 0x011A: case 0x011B:
            return "e";
        case 0x011E: case 0x011F: case 0x0122: case 0x0123:
            return "g";
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF:
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF:
        case 0x012A: case 0x012B: case 0x012E: case 0x012F: case 0x0130: case 0x0131:
            return "i";
        case 0x0136: case 0x0137:
            return "k";
        case 0x013B: case 0x013C: case 0x0141: case 0x0142:
            return "l";
        case 0x00D1: case 0x00F1: case 0x0143: case 0x0144: case 0x0145: case 0x0146:
        case 0x0147: case 0x0148:
            return "n";
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: case 0x00D8:
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: case 0x00F8:
        case 0x014C: case 0x014D: case 0x0150: case 0x0151:
            return "o";
        case 0x0154: case 0x0155: case 0x0158: case 0x0159:
            return "r";
        case 0x015A: case 0x015B: case 0x015E: case 0x015F: case 0x0160: case 0x0161:
            return "s";
        case 0x0162: case 0x0163: case 0x0164: case 0x0165:
            return "t";
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC:
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC:
        case 0x016A: case 0x016B: case 0x016E: case 0x016F: case 0x0172: case 0x0173:
            return "u";
        case 0x00DD: case 0x00FD: case 0x00FF: case 0x0176: case 0x0177: case 0x0178:
            return "y";
        case 0x0179: case 0x017A: case 0x017B: case 0x017C: case 0x017D: case 0x017E:
            return "z";
        default:
            return nullptr;
    }
}

/**
 * The sort key for the case- and accent-insensitive collation the privacy list uses.
 *
 * `Collator.getInstance(locale)` (PrivacyPolicyActivity.kt:75-82) folds BOTH case and accents, and
 * so does ArkUIApp's `localeCompare`. This was ASCII-lower-case only, with a note saying accents
 * "matter only if a source with one is ever added" — and then the privacy table was completed and
 * three arrived at once: with the old key the NDK sorted "MET Éireann" after "Met Office" (É is
 * 0xC3 0x89 in UTF-8, above 'o') and "Météo-France" after "MGM", where Android and ArkUIApp put
 * both earlier. Six rows out of 47 were in the wrong place.
 */
std::string CollationKey(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size();) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c < 0x80) {
            out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a')
                                               : static_cast<char>(c));
            ++i;
            continue;
        }
        // Decode the two-byte forms; anything longer is left as its own bytes.
        if ((c & 0xE0) == 0xC0 && i + 1 < value.size()) {
            const unsigned int cp =
                ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(value[i + 1]) & 0x3Fu);
            const char* folded = FoldLatin(cp);
            if (folded != nullptr) {
                out.append(folded);
                i += 2;
                continue;
            }
            out.push_back(static_cast<char>(c));
            out.push_back(value[i + 1]);
            i += 2;
            continue;
        }
        out.push_back(static_cast<char>(c));
        ++i;
    }
    return out;
}

/* strings.xml — the only two contribution lines in the list. */
const char* const kContributionGeometric = "Developer of the original project Geometric Weather";
const char* const kContributionDesigner = "Logo designer";

}  // namespace

const char* const kReleasesLink = "https://github.com/breezy-weather/breezy-weather/releases";

/** Contributors.kt:47-170, in order. The screen does not sort it. */
const std::vector<ContributorItem>& AppContributors() {
    static const std::vector<ContributorItem> kItems = {
        {"Julien Papasian", Gh("papjul"), ""},
        {"WangDaYeeeeee", Gh("WangDaYeeeeee"), kContributionGeometric},
        {"min7-i", Gh("min7-i"), ""},
        {"chunshek", Gh("chunshek"), ""},
        {"Cod3d.", Gh("Cod3dDOT"), ""},
        {"ecawthorne", Gh("ecawthorne"), ""},
        {"Romain Théry", Gh("rthery"), ""},
        {"Mark Bestavros", Gh("mbestavros"), ""},
        {"Suyash Gupta", Gh("suyashgupta25"), ""},
        {"Coelacanthus", Gh("CoelacanthusHex"), ""},
        {"danielzhang130", Gh("danielzhang130"), ""},
        {"majjejjam", Gh("majjejjam"), ""},
        {"bunburya", Gh("bunburya"), ""},
        {"Willem Schipper", Gh("willem640"), ""},
        {"Sven", Gh("SKBotNL"), ""},
        {"jayyuz", Gh("jayyuz"), ""},
        {"JiunnTarn", Gh("JiunnTarn"), ""},
        {"mags0ft", Gh("mags0ft"), ""},
        {"Devy Ballard", Gh("devycarol"), ""},
        {"Mushfiq1060", Gh("Mushfiq1060"), ""},
        {"ccyybn", Gh("ccyybn"), ""},
        {"Doğaç Tanrıverdi", Gh("DogacTanriverdi"), ""},
        {"Nero Nguyen", Gh("neronguyenvn"), ""},
        {"Dipesh Pal", Gh("codewithdipesh"), ""},
        {"Cactric", Gh("Cactric"), ""},
        {"ntzb", Gh("ntzb"), ""},
        {"mu", Gh("4eUeP"), ""},
        {"Anthony Dégrange", "https://anthony-degrange-design.fr/", kContributionDesigner},
    };
    return kItems;
}

/**
 * The English three of `appTranslators`.
 *
 * The full table is 296 rows covering every language Breezy ships; the screen shows only those whose
 * `lang` array contains the current language. Every other row is unreachable on an English-only
 * build — see the note in ArkUIApp's model/AboutCatalogue.ets.
 */
const std::vector<ContributorItem>& AppTranslators() {
    static const std::vector<ContributorItem> kItems = {
        {"Julien Papasian", Gh("papjul"), ""},
        {"WangDaYeeeeee", Gh("WangDaYeeeeee"), ""},
        {"kilimov25", Gh("kilimov25"), ""},
    };
    return kItems;
}

/** The "Contact" group (AboutScreen.kt:114-151). */
const std::vector<AboutLinkItem>& AboutContactLinks() {
    static const std::vector<AboutLinkItem> kItems = {
        {"ic_code.svg", "Source code", "https://github.com/breezy-weather/breezy-weather", ""},
        {"ic_forum.svg", "Matrix room", "https://matrix.to/#/#breezy-weather:matrix.org", ""},
    };
    return kItems;
}

/**
 * The "About app" group — `getAboutAppLinks` (AboutViewModel.kt:33-48).
 *
 * Both rows are real screens; neither goes to the placeholder.
 */
const std::vector<AboutLinkItem>& AboutAppLinks() {
    static const std::vector<AboutLinkItem> kItems = {
        {"ic_shield_lock.svg", "Privacy policy", "", "Privacy policy"},
        {"ic_contract.svg", "Dependencies", "", "Dependencies"},
    };
    return kItems;
}

/**
 * What THIS app is built on.
 *
 * The Kotlin renders `R.raw.aboutlibraries`, generated by the AboutLibraries Gradle plugin from the
 * app's own dependency graph — Retrofit, OkHttp, Vico, Hilt and the rest. **Transcribing that would
 * be a lie about this app**: neither port links any of it. Both have no package dependencies at all,
 * a hand-written JSON reader, and exactly one bundled third-party asset — the Roboto family.
 *
 * So this lists what the ports actually ship, which is what the screen MEANS. It is short because
 * the ports are, not because anything was left out.
 */
const std::vector<DependencyItem>& AppDependencies() {
    static const std::vector<DependencyItem> kItems = {
        {"Roboto", "Christian Robertson", "", "Apache-2.0"},
    };
    return kItems;
}

/**
 * `HttpSource.privacyPolicyUrl` by source id — the transcription of ArkUIApp's
 * `PRIVACY_POLICY_URLS`.
 *
 * ENGLISH branches: MET Norway, China, FMI, IPMA and JMA pick theirs from the locale, and this port
 * is English-only. A source with "" is absent here and the screen skips it, which is what
 * `filter { it.privacyPolicyUrl.startsWith("http") }` does.
 */
const std::vector<PrivacyPolicyRow>& PrivacyPolicyRows() {
    static const std::vector<PrivacyPolicyRow> kRows = [] {
        static const std::vector<std::pair<std::string, std::string>> kByIdRaw = {
            {"ipsb", "https://ip.sb/privacy-policy/"},
            {"baidu_ip", "https://lbs.baidu.com/index.php?title=openprivacy"},
            {"nominatim", "https://osmfoundation.org/wiki/Privacy_Policy"},
            {"openmeteo", "https://open-meteo.com/en/terms#privacy"},
            {"accu", "https://www.accuweather.com/en/privacy"},
            {"fpas",
             "https://invent.kde.org/webapps/foss-public-alert-server/-/wikis/Terms-of-Service"},
            {"infoplaza", "https://www.infoplaza.com/en/disclaimer-and-privacy"},
            {"metno", "https://www.met.no/en/About-us/privacy"},
            {"ncei", "https://www.ncei.noaa.gov/privacy"},
            {"openweather", "https://openweather.co.uk/privacy-policy"},
            {"pirateweather", "https://pirate-weather.apiable.io/privacy"},
            {"wmosevereweather", "https://wmo.int/privacy-policy"},
            {"atmofrance", "https://www.atmo-france.org/article/politique-de-confidentialite"},
            {"china", "https://privacy.mi.com/all/en_US"},
            {"dmi", "https://www.dmi.dk/om-hjemmesiden/privatliv/"},
            {"fmi", "https://en.ilmatieteenlaitos.fi/data-protection"},
            {"geosphereat", "https://www.geosphere.at/de/legal"},
            {"knmi", "https://www.knmi.nl/privacy"},
            {"metoffice", "https://www.metoffice.gov.uk/policies/privacy"},
            {"meteoam", "https://www.meteoam.it/it/privacy-policy"},
            {"mf",
             "https://meteofrance.com/application-meteo-france-politique-de-confidentialite"},
            {"ncdr", "https://ncdr.nat.gov.tw/Page?itemid=40&mid=7"},
            {"nlsc", "https://www.nlsc.gov.tw/cp.aspx?n=1633"},
            {"polleninfo", "https://www.polleninformation.at/en/our-terms-of-use"},
            {"recosante", "https://recosante.beta.gouv.fr/donnees-personnelles/"},

            /*
             * The remaining national and regional sources.
             *
             * This table used to stop at 25 rows, blamed on "the port's catalogue carries 29
             * sources where the reference has ~60". Wrong twice over: the source catalogue is
             * COMPLETE (all 75 ids, verified against the Kotlin in BOTH ports), and the short
             * screen was this table. Android's row set is every source whose `privacyPolicyUrl`
             * starts with "http" — 47 of the 75; the other 28 declare "" and are skipped.
             */
            {"aemet", "https://www.aemet.es/es/nota_legal"},
            {"atmoaura",
             "https://www.atmo-auvergnerhonealpes.fr/article/politique-de-confidentialite"},
            {"atmograndest", "https://www.atmo-grandest.eu/article/politique-de-confidentialite"},
            {"atmohdf", "https://www.atmo-hdf.fr/article/donnees-personnelles"},
            {"atmosud", "https://www.atmosud.org/article/politique-de-confidentialite"},
            {"brightsky", "https://brightsky.dev/"},
            {"cwa", "https://www.cwa.gov.tw/V8/E/private.html"},
            {"eccc", "https://app.weather.gc.ca/privacy-en.html"},
            {"epdhk", "https://www.aqhi.gov.hk/en/privacy-policy.html"},
            {"hko", "https://www.hko.gov.hk/en/privacy/policy.htm"},
            {"ilmateenistus", "https://www.ilmateenistus.ee/meist/kasutustingimused/?lang=en"},
            {"ims", "https://ims.gov.il/en/termOfuse"},
            {"ipma", "https://www.ipma.pt/en/siteinfo/index.html"},
            {"jma", "https://www.jma.go.jp/jma/en/copyright.html"},
            {"lhmt", "https://www.meteo.lt/istaiga/asmens-duomenu-apsauga/privatumo-politika/"},
            {"meteolux",
             "https://ana.gouvernement.lu/en/support/politique-confidentialite-meteolux.html"},
            {"metie", "https://www.met.ie/about-us/privacy"},
            {"mgm", "https://www.mgm.gov.tr/site/gizlilik-politikasi.aspx"},
            {"nws", "https://www.weather.gov/privacy"},
            {"smg", "https://www.smg.gov.mo/en/subpage/21/page/21"},
            {"smhi",
             "https://www.smhi.se/om-smhi/smhis-hantering-av-personuppgifter-och-integritetspolicy"},
            {"veduris", "https://vedur.is/um-vi/vefurinn/personuvernd/"},
        };

        std::vector<PrivacyPolicyRow> rows;
        // BuildConfig.PRIVACY_POLICY_LINK — the app's own row leads (kt:96-119).
        rows.push_back({"Breezy Weather",
                        "https://github.com/breezy-weather/breezy-weather/blob/main/PRIVACY.md"});

        // The NAMES come from the shared catalogue, so this screen cannot disagree with the source
        // pickers about what a source is called.
        std::vector<PrivacyPolicyRow> sources;
        for (const auto& entry : kByIdRaw) {
            const SourceInfo* source = GetSource(entry.first);
            if (source != nullptr) {
                sources.push_back({source->name, entry.second});
            }
        }
        /*
         * `Collator.getInstance(locale).compare` (kt:75-82) — a CASE- AND ACCENT-insensitive
         * collation, not a byte comparison. Plain `<` puts "IP.SB" before "Infoplaza", because
         * 'P' (0x50) sorts below 'n' (0x6E); ArkUIApp's `localeCompare` puts Infoplaza first, and
         * so does Android. See `CollationKey` for the accent half, which cost six more rows.
         */
        std::sort(sources.begin(), sources.end(),
                  [](const PrivacyPolicyRow& a, const PrivacyPolicyRow& b) {
                      const std::string lhs = CollationKey(a.name);
                      const std::string rhs = CollationKey(b.name);
                      return lhs != rhs ? lhs < rhs : a.name < b.name;
                  });
        rows.insert(rows.end(), sources.begin(), sources.end());
        return rows;
    }();
    return kRows;
}

}  // namespace breezy
