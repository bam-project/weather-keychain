//==============================================================================
//  Date:     11/07/2026
//  Program:  Weather Keychain
//  File:     WeatherData.h
//
//  Purpose:  Define weather data and validate condition codes and observation times.
//==============================================================================

#pragma once

#include <math.h>
#include <stdint.h>

// Plain data so the network worker can hand a complete snapshot to the UI.
// Open-Meteo current conditions are model estimates valid at observedUnix.
struct WeatherData {
  bool valid = false;
  float tempC = NAN;
  uint16_t code = UINT16_MAX;
  bool isDay = false;
  int64_t observedUnix = 0;
  uint32_t fetchedAtMs = 0;
};

constexpr int64_t kWeatherEarliestUnix = 1704067200;  // 2024-01-01 UTC.
constexpr uint32_t kWeatherMaxAgeSeconds = 3600;
constexpr uint32_t kWeatherFutureToleranceSeconds = 900;

//------------------------------------------------------------------------------
// weatherDataFresh
// Purpose:  Accept only recent observations with a plausible timestamp.
// Args:     data - weather snapshot to check
//           nowUnix - current UTC time in seconds
//           maxAgeSeconds - maximum accepted observation age
// Returns:  true if the observation is valid and recent.
//------------------------------------------------------------------------------
inline bool weatherDataFresh(const WeatherData& data, int64_t nowUnix,
                             uint32_t maxAgeSeconds = kWeatherMaxAgeSeconds) {
  return data.valid && nowUnix >= kWeatherEarliestUnix &&
         data.observedUnix >= kWeatherEarliestUnix &&
         data.observedUnix <= nowUnix + kWeatherFutureToleranceSeconds &&
         nowUnix - data.observedUnix <= static_cast<int64_t>(maxAgeSeconds);
}

//------------------------------------------------------------------------------
// isKnownWeatherCode
// Purpose:  Reject codes that the renderer cannot interpret.
// Args:     code - WMO weather condition code
// Returns:  true if the condition code is supported.
//------------------------------------------------------------------------------
inline bool isKnownWeatherCode(uint16_t code) {
  switch (code) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 45:
    case 48:
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 71:
    case 73:
    case 75:
    case 77:
    case 80:
    case 81:
    case 82:
    case 85:
    case 86:
    case 95:
    case 96:
    case 97:
    case 99:
      return true;
    default:
      return false;
  }
}

//------------------------------------------------------------------------------
// weatherDescription
// Purpose:  Compact labels fit a 128-pixel display at the default text size.
// Args:     code - WMO weather condition code
// Returns:  short description of the condition.
//------------------------------------------------------------------------------
inline const char* weatherDescription(uint16_t code) {
  switch (code) {
    case 0:
      return "CLEAR";
    case 1:
      return "MAINLY CLEAR";
    case 2:
      return "PARTLY CLOUDY";
    case 3:
      return "OVERCAST";
    case 45:
    case 48:
      return "FOG";
    case 51:
    case 53:
    case 55:
      return "DRIZZLE";
    case 56:
    case 57:
      return "FREEZING DRIZZLE";
    case 61:
    case 63:
    case 65:
      return "RAIN";
    case 66:
    case 67:
      return "FREEZING RAIN";
    case 71:
    case 73:
    case 75:
    case 77:
      return "SNOW";
    case 80:
    case 81:
    case 82:
      return "RAIN SHOWERS";
    case 85:
    case 86:
      return "SNOW SHOWERS";
    case 95:
    case 97:
      return "THUNDERSTORM";
    case 96:
    case 99:
      return "THUNDER / HAIL";
    default:
      return "UNAVAILABLE";
  }
}
