/*
 * The saved-location list, the same one ArkUIApp carries (data/LocationStore.ets).
 *
 * It holds only what Breezy's location database holds — id, name, coordinates, timezone — and no
 * weather. Nothing is seeded: a location has nothing to show until a real Open-Meteo refresh fills
 * it, which is how a freshly added location behaves on Android too.
 *
 * This is the one piece of app state the ArkTS shell keeps, because it is what the shell needs in
 * order to know which URL to fetch. Everything the response says is C++'s to decide.
 */

export class ShellLocation {
  readonly formattedId: string;
  readonly city: string;
  /**
   * Location.district — the sub-locality inside `city`, e.g. "Kotkavuori" inside Helsinki.
   *
   * Only the reverse geocoder fills it (AndroidGeocoderService.kt:43 maps it from
   * `Address.subLocality`), so a searched location has none and a current position usually does.
   * C++ prints "city, district" — the "Helsinki, Kotkavuori" Android's toolbar shows.
   */
  readonly district: string;
  readonly latitude: number;
  readonly longitude: number;
  /**
   * The admin area the city sits in — Location.admin1 on Android, which is what FMI's CAP
   * warnings name in their <areaDesc>. Empty for a location no alert source covers.
   */
  readonly region: string;
  /**
   * admin2 — the second-level area. Opaque here, but the catalogue's Atmo regional predicates test
   * its NAME, so a French location that lost it could only ever under-recommend.
   */
  readonly admin2: string;
  /** ISO 3166-1 alpha-2, which is what almost every source-catalogue predicate keys off. */
  readonly countryCode: string;
  /**
   * The eight per-feature source ids, comma separated, in SourceFeature order.
   *
   * OPAQUE TO THE SHELL. C++ owns the catalogue, computes the preset and edits it in the
   * weather-sources dialog; this string is stored and handed back untouched, which is what keeps
   * ~75 sources and their predicates out of the ArkTS build entirely.
   */
  readonly sources: string;
  /**
   * The one entry whose coordinates come from the device (ManagementFragment.kt:236-252).
   *
   * There can only ever be one, which is why it carries the fixed id `current-position`, and why
   * the my-location FAB disappears once it exists (kt:236).
   */
  readonly isCurrentPosition: boolean;
  /**
   * Location.customName (Location.kt:47) — the name typed over this location's own.
   *
   * Written by the per-location preferences dialog behind the main screen's edit icon, and read
   * FIRST by getPlace (Location.kt:33-35), ahead of "city, district". '' is Android's null.
   */
  readonly customName: string;
  /**
   * `parameters["openmeteo"]["weatherModels"]` (OpenMeteoService.kt:764-773) — a comma-joined list
   * of Open-Meteo model ids, or '' for the source's own default of `best_match` alone.
   *
   * OPAQUE TO THE SHELL in the same way `sources` is: C++ owns the model catalogue and the dialog
   * that edits this, and the shell only stores it and puts it on the request.
   */
  readonly weatherModels: string;
  /**
   * Location.needsGeocodeRefresh — this row's address has to be looked up again.
   *
   * RefreshHelper STEP 2 (kt:242-294) is gated on this flag ALONE, not on `isCurrentPosition`: the
   * doc comment above `getLocation` says otherwise but the code at kt:243 tests only the flag, and
   * STEP 1 is the current-position-only part. So a SAVED location is reverse-geocoded too whenever
   * its address needs refreshing, which is the only way any location gets a `district` — Open-Meteo
   * geocoding never fills one.
   *
   * Defaults TRUE for a row written before the column existed, which is exactly the set of rows
   * that have a city and no district. ArkUIApp's table column defaults to 1 for the same reason.
   */
  readonly needsGeocodeRefresh: boolean;

  constructor(
    formattedId: string,
    city: string,
    latitude: number,
    longitude: number,
    region: string = '',
    countryCode: string = '',
    sources: string = '',
    admin2: string = '',
    isCurrentPosition: boolean = false,
    district: string = '',
    customName: string = '',
    weatherModels: string = '',
    needsGeocodeRefresh: boolean = true
  ) {
    this.formattedId = formattedId;
    this.city = city;
    this.district = district;
    this.latitude = latitude;
    this.longitude = longitude;
    this.region = region;
    this.countryCode = countryCode;
    this.sources = sources;
    this.admin2 = admin2;
    this.isCurrentPosition = isCurrentPosition;
    this.customName = customName;
    this.weatherModels = weatherModels;
    this.needsGeocodeRefresh = needsGeocodeRefresh;
  }
}

/** kt:241-243 — `Location(isCurrentPosition = true)`, with NO coordinates until the first fix. */
export const CURRENT_POSITION_ID: string = 'current-position';

export function buildCurrentPositionLocation(sources: string): ShellLocation {
  return new ShellLocation(CURRENT_POSITION_ID, '', 0, 0, '', '', sources, '', true);
}

/**
 * The same location with the position fix and the address it reverse-geocoded to.
 *
 * A coordinate CHANGE clears the address (RefreshHelper.kt:215-233) — "Don't keep old data as the
 * user can have changed position" — and the country goes with it, or a fix that has crossed a
 * border would keep matching the old country's national sources.
 */
export function withPosition(location: ShellLocation, latitude: number, longitude: number,
  city: string, district: string, region: string, countryCode: string): ShellLocation {
  // The two per-location PREFERENCES survive a position change: they are the user's, not the
  // geocoder's, which is exactly the distinction that clears the address above.
  return new ShellLocation(location.formattedId, city, latitude, longitude, region, countryCode,
    location.sources, '', location.isCurrentPosition, district, location.customName,
    location.weatherModels, false);
}

/**
 * FMI covers Finland only (FmiServiceStub's FMI_FORECAST_BBOX), so a location outside it gets no
 * warnings — which is why Berlin and Tokyo show no alert card however the feed reads.
 */
export function coveredByFmi(location: ShellLocation): boolean {
  return location.latitude >= 59.0 && location.latitude <= 70.5 &&
    location.longitude >= 19.0 && location.longitude <= 32.0;
}

/**
 * Helsinki first, to match the location the Android build opens on. Coordinates are what
 * Open-Meteo's own geocoding returns for each city, so all three builds resolve the same grid cell.
 *
 * Only a SEED now: the list is persisted (WeatherCache.readLocationList) and this is what goes in
 * when that file is empty, which is what ArkUIApp's buildLocations() also became once its own
 * location table existed.
 */
export function buildLocations(): ShellLocation[] {
  /*
   * The ids are `<forecastSource>-<lat>,<lon>` — Android's Location.formattedId, and what the
   * search screen now produces for a hit. They used to be hand-written (`saved-helsinki`), which
   * meant a seeded city and the same city found by search were two different entries.
   */
  return [
    new ShellLocation('openmeteo-60.1695,24.9354', 'Helsinki', 60.1695, 24.9354, 'Uusimaa', 'FI'),
    new ShellLocation('openmeteo-52.5244,13.4105', 'Berlin', 52.5244, 13.4105, '', 'DE'),
    new ShellLocation('openmeteo-35.6895,139.6917', 'Tokyo', 35.6895, 139.6917, '', 'JP')
  ];
}

/** The saved ids, for the weather-sources dialog's duplicate warning. */
export function locationIds(locations: ShellLocation[]): string {
  const ids: string[] = [];
  for (let i = 0; i < locations.length; i++) {
    ids.push(locations[i].formattedId);
  }
  return ids.join('\n');
}

/**
 * What screen 2's weather-sources dialog needs and the scenes do not carry — one line per location.
 *
 * The scenes hold what a WEATHER page draws: city, kind, daylight, the forecast. Where the location
 * IS, and which sources it is on, are the shell's, so they cross separately rather than being
 * bolted onto every forecast.
 */
export function locationDetails(locations: ShellLocation[]): string {
  const lines: string[] = [];
  for (let i = 0; i < locations.length; i++) {
    const location: ShellLocation = locations[i];
    lines.push([
      location.formattedId,
      location.countryCode,
      location.region,
      location.admin2,
      location.latitude.toString(),
      location.longitude.toString(),
      location.sources,
      // Appended, so the field positions DetailsFor already reads stay put. The city is what a row
      // is titled, and a location with no forecast yet has no scene to take it from.
      location.city,
      // C++ needs it twice over: to prefix the title with "⊙ ", and to reveal the tertiary pair
      // rather than the secondary one on a left swipe (LocationHolder.kt:60-64).
      location.isCurrentPosition ? '1' : '0',
      // Appended after the current-position flag, so the field positions above stay put. C++ joins
      // it to the city with `CityAndDistrict` when it titles a row (Location.kt:261-274).
      location.district,
      // Appended again, same reasoning. Both feed the per-location preferences dialog: the custom
      // name is what its edit field seeds and what getPlace prefers, the models are what its
      // second row lists.
      location.customName,
      location.weatherModels,
      location.needsGeocodeRefresh ? '1' : '0'
    ].join(FIELD_SEPARATOR));
  }
  return lines.join('\n');
}

/** The same location with a different source list — ShellLocation's fields are readonly. */
export function withSources(location: ShellLocation, sources: string): ShellLocation {
  return new ShellLocation(location.formattedId, location.city, location.latitude,
    location.longitude, location.region, location.countryCode, sources, location.admin2,
    location.isCurrentPosition, location.district, location.customName, location.weatherModels,
    location.needsGeocodeRefresh);
}

/** The same location with the two per-location preferences replaced. */
export function withPreferences(location: ShellLocation, customName: string,
  weatherModels: string): ShellLocation {
  return new ShellLocation(location.formattedId, location.city, location.latitude,
    location.longitude, location.region, location.countryCode, location.sources, location.admin2,
    location.isCurrentPosition, location.district, customName, weatherModels,
    location.needsGeocodeRefresh);
}

/** The same location with a freshly reverse-geocoded address, and the flag cleared. */
export function withAddress(location: ShellLocation, city: string, district: string, region: string,
  countryCode: string): ShellLocation {
  return new ShellLocation(location.formattedId, city, location.latitude, location.longitude,
    region, countryCode.length > 0 ? countryCode : location.countryCode, location.sources,
    location.admin2, location.isCurrentPosition, district, location.customName,
    location.weatherModels, false);
}

/** The separator the persisted list uses — see WeatherCache.readLocationList for why a tab. */
const FIELD_SEPARATOR: string = '\t';

/**
 * The picked search hit, as C++ encoded it:
 * `id|place|region|admin2|countryCode|latitude|longitude|eight,source,ids`.
 *
 * The shell has never parsed a response body in this port and does not start here — C++ read the
 * geocoding JSON, applied Location.kt's getPlace rule and ran the source catalogue, so these
 * fields arrive ready. Returns null for anything malformed rather than saving a half-built
 * location.
 *
 * `admin2` and `countryCode` used to be dropped on the way through, which left a searched location
 * presenting an empty country and an empty admin2 the next time the catalogue was asked about it.
 */
export function decodePickedLocation(encoded: string): ShellLocation | null {
  const parts: string[] = encoded.split('|');
  if (parts.length < 8) {
    return null;
  }
  const latitude: number = Number.parseFloat(parts[5]);
  const longitude: number = Number.parseFloat(parts[6]);
  if (Number.isNaN(latitude) || Number.isNaN(longitude)) {
    return null;
  }
  // The ninth field arrived with the my-location FAB; a dialog saved before it has eight. A
  // current-position entry comes through here with 0,0 and no name — the fix fills both in.
  const current: boolean = parts.length > 8 && parts[8] === '1';
  const city: string = current ? '' : parts[1];
  return new ShellLocation(parts[0], city, latitude, longitude, parts[2], parts[4], parts[7],
    parts[3], current);
}

export function encodeLocationList(locations: ShellLocation[]): string {
  const lines: string[] = [];
  for (let i = 0; i < locations.length; i++) {
    const location: ShellLocation = locations[i];
    lines.push([
      location.formattedId,
      location.city,
      location.region,
      location.latitude.toString(),
      location.longitude.toString(),
      location.countryCode,
      location.sources,
      // Appended rather than inserted, so a file written before these columns still decodes.
      location.admin2,
      location.isCurrentPosition ? '1' : '0',
      // Appended again, for the same reason: a file written before this column still decodes.
      location.district,
      // And again, for the two per-location preferences.
      location.customName,
      location.weatherModels,
      location.needsGeocodeRefresh ? '1' : '0'
    ].join(FIELD_SEPARATOR));
  }
  return lines.join('\n');
}

/** Returns an empty list for an empty or unreadable file, which the caller treats as "seed me". */
export function decodeLocationList(text: string): ShellLocation[] {
  const locations: ShellLocation[] = [];
  const lines: string[] = text.split('\n');
  for (let i = 0; i < lines.length; i++) {
    const parts: string[] = lines[i].split(FIELD_SEPARATOR);
    if (parts.length < 5) {
      continue;
    }

    const latitude: number = Number.parseFloat(parts[3]);
    const longitude: number = Number.parseFloat(parts[4]);
    if (Number.isNaN(latitude) || Number.isNaN(longitude)) {
      continue;
    }
    // The later columns arrived with the source catalogue and then with admin2; a file written
    // before either has five or seven.
    const countryCode: string = parts.length > 5 ? parts[5] : '';
    const sources: string = parts.length > 6 ? parts[6] : '';
    const admin2: string = parts.length > 7 ? parts[7] : '';
    const current: boolean = parts.length > 8 ? parts[8] === '1' : false;
    const district: string = parts.length > 9 ? parts[9] : '';
    const customName: string = parts.length > 10 ? parts[10] : '';
    const weatherModels: string = parts.length > 11 ? parts[11] : '';
    // TRUE for a row written before this column, which is the set that has a city and no district.
    const needsGeocode: boolean = parts.length > 12 ? parts[12] === '1' : true;
    locations.push(new ShellLocation(parts[0], parts[1], latitude, longitude, parts[2], countryCode,
      sources, admin2, current, district, customName, weatherModels, needsGeocode));
  }
  return locations;
}
