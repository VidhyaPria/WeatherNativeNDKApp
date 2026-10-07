/*
 * The About screen's static tables — the transcription of ArkUIApp's model/AboutCatalogue.ets,
 * which is itself data/Contributors.kt plus AboutViewModel's link list.
 *
 * One file per port carrying the same rows, the arrangement `SourceCatalogue` already has, so the
 * two cannot drift.
 */
#ifndef BREEZY_DATA_ABOUTCATALOGUE_H
#define BREEZY_DATA_ABOUTCATALOGUE_H

#include <string>
#include <vector>

namespace breezy {

/** ContributorItem (Contributors.kt:22-37). `link` is the first non-empty of the four. */
struct ContributorItem {
    std::string name;
    /** The resolved link, or "" when there is none — a row with none is not tappable. */
    std::string link;
    /** The `contribution` line, or "" — only two contributors have one. */
    std::string contribution;
};

/** One tappable link row — AboutAppLinkItem (AboutScreen.kt:80-84). */
struct AboutLinkItem {
    /** A rawfile asset name, e.g. "ic_code.svg". */
    std::string icon;
    std::string title;
    /** A URL to confirm and open, or "" when the row opens a screen instead. */
    std::string url;
    /** The screen this row opens when it has no URL — routed to the placeholder. */
    std::string screen;
};

const std::vector<ContributorItem>& AppContributors();
/** Filtered to the device language, which on this port is English (AboutScreen.kt:103-112). */
const std::vector<ContributorItem>& AppTranslators();
const std::vector<AboutLinkItem>& AboutContactLinks();
const std::vector<AboutLinkItem>& AboutAppLinks();

/** One row of the Privacy policy screen: a source's name over its policy URL. */
struct PrivacyPolicyRow {
    std::string name;
    std::string url;
};

/**
 * The Privacy policy screen's rows — the app's own first, then every HTTP source that declares a
 * policy, sorted by name (PrivacyPolicyActivity.kt:73-83).
 */
const std::vector<PrivacyPolicyRow>& PrivacyPolicyRows();

/** One AboutLibraries row: name, author, version and its licence badge. */
struct DependencyItem {
    std::string name;
    std::string author;
    std::string version;
    std::string license;
};

/**
 * What THIS app is built on — see the note on the definition. It is NOT Android's dependency graph,
 * which neither port links.
 */
const std::vector<DependencyItem>& AppDependencies();

/** BuildConfig.RELEASES_LINK — where the update row goes without an in-app checker. */
extern const char* const kReleasesLink;

}  // namespace breezy

#endif  // BREEZY_DATA_ABOUTCATALOGUE_H
