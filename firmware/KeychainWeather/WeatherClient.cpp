//==============================================================================
//  Date:     11/07/2026
//  Program:  Weather Keychain
//  File:     WeatherClient.cpp
//
//  Purpose:  Download and validate current weather over HTTPS.
//==============================================================================

#include "WeatherClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>

namespace {
constexpr size_t kMaxResponseBytes = 2048;
constexpr uint16_t kIoTimeoutMs = 8000;
constexpr unsigned long kHandshakeTimeoutSeconds = 10;

// The full Mozilla CA bundle is present in Arduino ESP32 3.3.11's C6 SDK.
// Keep the trust store current by updating the board core with the firmware.
extern const uint8_t kCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t kCaBundleEnd[] asm("_binary_x509_crt_bundle_end");

//------------------------------------------------------------------------------
// fail
// Purpose:  Set a readable error message and return failure to the caller.
// Args:     error - output error description
//           message - failure description
// Returns:  false.
//------------------------------------------------------------------------------
bool fail(String& error, const char* message) {
  error = message;
  return false;
}

struct JsonReader {
  const char* data;
  size_t length;
  size_t position = 0;

  //------------------------------------------------------------------------------
  // read
  // Purpose:  Read one byte from the bounded JSON response.
  // Args:     None.
  // Returns:  next response byte, or -1 at end of input.
  //------------------------------------------------------------------------------
  int read() {
    return position < length ? static_cast<unsigned char>(data[position++]) : -1;
  }

  //------------------------------------------------------------------------------
  // readBytes
  // Purpose:  Copy available response bytes into a caller-provided buffer.
  // Args:     buffer - destination byte buffer
  //           requested - maximum number of bytes to read
  // Returns:  number of bytes copied.
  //------------------------------------------------------------------------------
  size_t readBytes(char* buffer, size_t requested) {
    const size_t remaining = length - position;
    const size_t count = requested < remaining ? requested : remaining;
    memcpy(buffer, data + position, count);
    position += count;
    return count;
  }
};
}  // namespace

//------------------------------------------------------------------------------
// parseWeatherResponse
// Purpose:  Validate the API response before replacing the last known weather snapshot.
// Args:     json - response bytes to parse
//           length - number of response bytes
//           nowUnix - current UTC time in seconds
//           out - output weather snapshot, changed only on success
//           error - output error description
// Returns:  true if the response contains valid, recent weather.
//------------------------------------------------------------------------------
bool parseWeatherResponse(const char* json, size_t length, int64_t nowUnix, WeatherData& out,
                          String& error) {
  if (!json || length == 0 || length > kMaxResponseBytes) {
    return fail(error, "INVALID RESPONSE SIZE");
  }
  if (nowUnix < kWeatherEarliestUnix) {
    return fail(error, "CLOCK NOT READY");
  }

  JsonDocument document;
  JsonReader reader{json, length};
  const DeserializationError parseError =
      deserializeJson(document, reader, DeserializationOption::NestingLimit(4));
  if (parseError || !document.is<JsonObject>()) {
    return fail(error, "INVALID WEATHER JSON");
  }
  while (reader.position < length) {
    const int trailing = reader.read();
    if (trailing != ' ' && trailing != '\t' && trailing != '\r' && trailing != '\n') {
      return fail(error, "TRAILING WEATHER DATA");
    }
  }
  if (!document["error"].isNull() && document["error"] != false) {
    return fail(error, "WEATHER API ERROR");
  }

  JsonObjectConst current = document["current"].as<JsonObjectConst>();
  JsonObjectConst units = document["current_units"].as<JsonObjectConst>();
  if (current.isNull() || units.isNull() || !current["temperature_2m"].is<double>() ||
      !current["weather_code"].is<uint16_t>() || !current["is_day"].is<uint8_t>() ||
      !current["time"].is<int64_t>()) {
    return fail(error, "MISSING WEATHER DATA");
  }
  if (units["temperature_2m"] !=
          "\xC2\xB0"
          "C" ||
      units["time"] != "unixtime" || units["weather_code"] != "wmo code" ||
      !document["utc_offset_seconds"].is<int>() || document["utc_offset_seconds"].as<int>() != 0) {
    return fail(error, "UNEXPECTED WEATHER UNITS");
  }

  const double temperature = current["temperature_2m"].as<double>();
  const uint16_t code = current["weather_code"].as<uint16_t>();
  const uint8_t day = current["is_day"].as<uint8_t>();
  const int64_t observedUnix = current["time"].as<int64_t>();
  if (!isfinite(temperature) || temperature < -100.0 || temperature > 70.0 ||
      !isKnownWeatherCode(code) || day > 1) {
    return fail(error, "INVALID WEATHER VALUES");
  }
  WeatherData candidate;
  candidate.valid = true;
  candidate.tempC = static_cast<float>(temperature);
  candidate.code = code;
  candidate.isDay = day == 1;
  candidate.observedUnix = observedUnix;
  if (!weatherDataFresh(candidate, nowUnix)) {
    return fail(error, "WEATHER TIME OUT OF RANGE");
  }
  out = candidate;
  error = "";
  return true;
}

//------------------------------------------------------------------------------
// fetchWeather
// Purpose:  Fetch current conditions over verified HTTPS with bounded response size and
//           timeouts.
// Args:     latitude - location latitude in degrees
//           longitude - location longitude in degrees
//           out - output weather snapshot, changed only on success
//           error - output error description
// Returns:  true on success; false with an error description otherwise.
//------------------------------------------------------------------------------
bool fetchWeather(double latitude, double longitude, WeatherData& out, String& error) {
  if (!isfinite(latitude) || !isfinite(longitude) || latitude < -90.0 || latitude > 90.0 ||
      longitude < -180.0 || longitude > 180.0) {
    return fail(error, "INVALID LOCATION");
  }
  if (WiFi.status() != WL_CONNECTED) {
    return fail(error, "WIFI DISCONNECTED");
  }
  if (static_cast<int64_t>(time(nullptr)) < kWeatherEarliestUnix) {
    return fail(error, "CLOCK NOT READY");
  }

  char url[288];
  const int urlLength =
      snprintf(url, sizeof(url),
               "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f"
               "&current=temperature_2m,weather_code,is_day"
               "&timezone=GMT&timeformat=unixtime&temperature_unit=celsius",
               latitude, longitude);
  if (urlLength < 0 || static_cast<size_t>(urlLength) >= sizeof(url)) {
    return fail(error, "INVALID REQUEST");
  }

  NetworkClientSecure client;
  client.setCACertBundle(kCaBundleStart, kCaBundleEnd - kCaBundleStart);
  client.setHandshakeTimeout(kHandshakeTimeoutSeconds);
  client.setTimeout(kIoTimeoutMs);
  HTTPClient http;
  http.setConnectTimeout(kIoTimeoutMs);
  http.setTimeout(kIoTimeoutMs);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  // A small, uncompressed HTTP/1.0 response can be read with a strict size and
  // total body deadline, without allocating an unbounded String.
  http.useHTTP10(true);
  if (!http.begin(client, url)) {
    return fail(error, "HTTP SETUP FAILED");
  }
  const char* headers[] = {"Content-Type", "Content-Encoding", "Transfer-Encoding"};
  http.collectHeaders(headers, 3);
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    http.end();
    if (status <= 0) {
      return fail(error, "HTTPS REQUEST FAILED");
    }
    if (status == HTTP_CODE_TOO_MANY_REQUESTS) {
      return fail(error, "WEATHER RATE LIMITED");
    }
    return fail(error, "WEATHER SERVICE UNAVAILABLE");
  }
  String contentType = http.header("Content-Type");
  contentType.toLowerCase();
  const String contentEncoding = http.header("Content-Encoding");
  const String transferEncoding = http.header("Transfer-Encoding");
  const int expectedLength = http.getSize();
  if (!contentType.startsWith("application/json") ||
      (contentEncoding.length() && !contentEncoding.equalsIgnoreCase("identity")) ||
      (transferEncoding.length() && !transferEncoding.equalsIgnoreCase("identity")) ||
      expectedLength == 0 || expectedLength > static_cast<int>(kMaxResponseBytes)) {
    http.end();
    return fail(error, "UNEXPECTED HTTP RESPONSE");
  }

  char body[kMaxResponseBytes + 1];
  size_t received = 0;
  const uint32_t bodyStarted = millis();
  NetworkClient& stream = http.getStream();
  bool complete = false;
  while (millis() - bodyStarted < kIoTimeoutMs) {
    int available = stream.available();
    if (available > 0) {
      if (received == kMaxResponseBytes) {
        break;
      }
      size_t toRead = static_cast<size_t>(available);
      if (toRead > kMaxResponseBytes - received) {
        toRead = kMaxResponseBytes - received;
      }
      if (expectedLength >= 0 && toRead > static_cast<size_t>(expectedLength) - received) {
        toRead = static_cast<size_t>(expectedLength) - received;
      }
      const int count = stream.read(reinterpret_cast<uint8_t*>(body) + received, toRead);
      if (count < 0) {
        break;
      }
      received += static_cast<size_t>(count);
      if (expectedLength >= 0 && received == static_cast<size_t>(expectedLength)) {
        complete = true;
        break;
      }
    } else if (!stream.connected()) {
      complete = expectedLength < 0 || received == static_cast<size_t>(expectedLength);
      break;
    }
    delay(1);  // Let the display task run during network waits.
  }
  http.end();
  if (!complete || received == 0) {
    return fail(error, "INCOMPLETE WEATHER REPLY");
  }
  body[received] = '\0';
  WeatherData candidate;
  if (!parseWeatherResponse(body, received, static_cast<int64_t>(time(nullptr)), candidate,
                            error)) {
    return false;
  }
  candidate.fetchedAtMs = millis();
  out = candidate;
  return true;
}

//------------------------------------------------------------------------------
// resolveCity
// Purpose:  Resolve a city through Open-Meteo without silently choosing between matches.
// Args:     city - search text entered in setup
//           latitude, longitude - coordinates written only after validation
//           error - output error description
// Returns:  true when exactly one location is found.
//------------------------------------------------------------------------------
bool resolveCity(const String& city, double& latitude, double& longitude, String& error) {
  if (city.length() < 2 || city.length() > 96) {
    return fail(error, "ENTER CITY IN SETUP");
  }
  String name = city;
  String region;
  int comma = city.indexOf(',');
  if (comma >= 0) {
    name = city.substring(0, comma);
    region = city.substring(comma + 1);
    name.trim();
    region.trim();
  }
  String encoded;
  for (unsigned int i = 0; i < name.length(); ++i) {
    unsigned char c = name[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      encoded += static_cast<char>(c);
    } else {
      char escaped[4];
      snprintf(escaped, sizeof(escaped), "%%%02X", c);
      encoded += escaped;
    }
  }
  NetworkClientSecure client;
  client.setCACertBundle(kCaBundleStart, kCaBundleEnd - kCaBundleStart);
  client.setHandshakeTimeout(kHandshakeTimeoutSeconds);
  client.setTimeout(kIoTimeoutMs);
  HTTPClient http;
  http.setConnectTimeout(kIoTimeoutMs);
  http.setTimeout(kIoTimeoutMs);
  http.useHTTP10(true);
  String url = "https://geocoding-api.open-meteo.com/v1/search?name=" + encoded +
               "&count=10&language=en&format=json";
  if (!http.begin(client, url)) {
    return fail(error, "CITY LOOKUP FAILED");
  }
  http.addHeader("Accept-Encoding", "identity");
  if (http.GET() != HTTP_CODE_OK) {
    http.end();
    return fail(error, "CITY SERVICE UNAVAILABLE");
  }
  // Bound the body size and overall read time before parsing external JSON.
  static char body[8193];
  size_t received = 0;
  int expected = http.getSize();
  if (expected > 8192 || expected == 0) {
    http.end();
    return fail(error, "INVALID CITY RESPONSE");
  }
  uint32_t started = millis();
  NetworkClient& stream = http.getStream();
  bool complete = false;
  while (millis() - started < kIoTimeoutMs) {
    if (stream.available()) {
      if (received == 8192) {
        break;
      }
      int value = stream.read();
      if (value >= 0) {
        body[received++] = static_cast<char>(value);
      }
      if (expected >= 0 && received == static_cast<size_t>(expected)) {
        complete = true;
        break;
      }
    } else if (!stream.connected()) {
      complete = expected < 0 || received == static_cast<size_t>(expected);
      break;
    } else {
      delay(1);
    }
  }
  http.end();
  if (!complete || received == 0) {
    return fail(error, "INCOMPLETE CITY RESPONSE");
  }
  body[received] = 0;
  JsonDocument document;
  if (deserializeJson(document, body, received, DeserializationOption::NestingLimit(5))) {
    return fail(error, "INVALID CITY RESPONSE");
  }
  JsonArray matches = document["results"].as<JsonArray>();
  if (matches.size() == 0) {
    return fail(error, "CITY NOT FOUND: USE SETUP");
  }
  JsonObject selected;
  int matchingCount = 0;
  for (JsonObject candidate : matches) {
    String candidateRegion = candidate["admin1"] | "";
    String candidateCountry = candidate["country"] | "";
    String candidateCode = candidate["country_code"] | "";
    if (region.length() > 0 && !region.equalsIgnoreCase(candidateRegion) &&
        !region.equalsIgnoreCase(candidateCountry) && !region.equalsIgnoreCase(candidateCode)) {
      continue;
    }
    selected = candidate;
    ++matchingCount;
  }
  if (matchingCount == 0) return fail(error, "CITY NOT FOUND: USE SETUP");
  // Broad searches need a region when the result page is full.
  if (matchingCount != 1 || (region.isEmpty() && matches.size() == 10))
    return fail(error, "ADD REGION: USE SETUP");
  if (!selected["latitude"].is<double>() || !selected["longitude"].is<double>())
    return fail(error, "INVALID CITY COORDINATES");
  double lat = selected["latitude"].as<double>();
  double lon = selected["longitude"].as<double>();
  if (!isfinite(lat) || !isfinite(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) {
    return fail(error, "INVALID CITY COORDINATES");
  }
  latitude = lat;
  longitude = lon;
  error = "";
  return true;
}
