//==============================================================================
//  Date:     11/07/2026
//  Program:  Weather Keychain
//  File:     WeatherClient.h
//
//  Purpose:  Declare the weather download and response parsing functions.
//==============================================================================

#pragma once

#include <Arduino.h>

#include "WeatherData.h"

//------------------------------------------------------------------------------
// fetchWeather
// Purpose:  Blocking network operation: call only from the network worker, never from the OLED
//           loop. Requires connected Wi-Fi and a synchronized system clock. Uses the ESP32
//           core's CA bundle and hostname verification. No API key. On failure, out is
//           unchanged; the caller decides when old data expires.
// Args:     latitude - location latitude in degrees
//           longitude - location longitude in degrees
//           out - output weather snapshot, changed only on success
//           error - output error description
// Returns:  true on success; false with an error description otherwise.
//------------------------------------------------------------------------------
bool fetchWeather(double latitude, double longitude, WeatherData& out, String& error);

//------------------------------------------------------------------------------
// parseWeatherResponse
// Purpose:  Separately callable so malformed, missing and stale responses can be tested without
//           a network connection. nowUnix must be synchronized UTC seconds. This does not set
//           fetchedAtMs; fetchWeather stamps the successful download.
// Args:     json - response bytes to parse
//           length - number of response bytes
//           nowUnix - current UTC time in seconds
//           out - output weather snapshot, changed only on success
//           error - output error description
// Returns:  true if the response contains valid, recent weather.
//------------------------------------------------------------------------------
bool parseWeatherResponse(const char* json, size_t length, int64_t nowUnix, WeatherData& out,
                          String& error);

//------------------------------------------------------------------------------
// resolveCity
// Purpose:  Find coordinates for one unambiguous city search.
// Args:     city - city or postal code, optionally followed by a region or country
//           latitude, longitude - output coordinates
//           error - output error description
// Returns:  true if exactly one valid location matches.
//------------------------------------------------------------------------------
bool resolveCity(const String& city, double& latitude, double& longitude, String& error);
