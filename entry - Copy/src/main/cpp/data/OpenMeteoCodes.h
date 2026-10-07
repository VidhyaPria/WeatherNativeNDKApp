/*
 * Open-Meteo WMO weather codes → WeatherKind.
 *
 * Ported from OpenMeteoService.kt:609-631 (via ArkUIApp's data/openmeteo/OpenMeteoMapper.ets
 * weatherKind()). The rest of the Open-Meteo mapping lands in this directory as it is ported.
 */
#ifndef BREEZY_DATA_OPENMETEOCODES_H
#define BREEZY_DATA_OPENMETEOCODES_H

#include "render/Theme.h"

namespace breezy {

/** `code < 0` stands for a null code, which the Kotlin maps to WeatherKind.NULL. */
WeatherKind WeatherKindForCode(int code);

}  // namespace breezy

#endif  // BREEZY_DATA_OPENMETEOCODES_H
