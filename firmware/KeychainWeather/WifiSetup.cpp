//==============================================================================
//  Date:     11/07/2026
//  Program:  Weather Keychain
//  File:     WifiSetup.cpp
//
//  Purpose:  Manage Wi-Fi provisioning, saved credentials, and reconnection.
//==============================================================================

#include "WifiSetup.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>

namespace {
constexpr uint32_t kConnectionTimeoutMs = 30000;
constexpr uint32_t kPortalRetryMs = 5000;
constexpr uint32_t kCredentialsMagic = 0x57494632;
constexpr char kNamespace[] = "weather-wifi";
constexpr char kCredentialKey[] = "credentials";
constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

// A single NVS blob commits the SSID and password together, including open
// networks whose password is empty. This is storage, not an HTTP response.
struct Credentials {
  uint32_t magic;
  uint8_t ssidLength;
  uint8_t passwordLength;
  char ssid[33];
  char password[64];
  char city[97];
};

//------------------------------------------------------------------------------
// validCredentials
// Purpose:  Check Wi-Fi name and password lengths before storing or using them.
// Args:     ssid - Wi-Fi network name
//           password - Wi-Fi network password
// Returns:  true if credential lengths are valid.
//------------------------------------------------------------------------------
bool validCredentials(const String& ssid, const String& password) {
  if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63 ||
      (password.length() != 0 && password.length() < 8)) {
    return false;
  }
  for (unsigned int i = 0; i < ssid.length(); ++i) {
    if (ssid[i] == '\0') {
      return false;
    }
  }
  for (unsigned int i = 0; i < password.length(); ++i) {
    if (password[i] == '\0') {
      return false;
    }
  }
  return true;
}

//------------------------------------------------------------------------------
// randomToken
// Purpose:  Make an unpredictable setup-session token using the hardware random generator.
// Args:     None.
// Returns:  a newly generated random session token.
//------------------------------------------------------------------------------
String randomToken() {
  char token[33];
  for (uint8_t i = 0; i < 4; ++i) {
    snprintf(token + i * 8, 9, "%08lx", static_cast<unsigned long>(esp_random()));
  }
  return String(token);
}

//------------------------------------------------------------------------------
// sameToken
// Purpose:  Compare session tokens without an early exit for each differing character.
// Args:     actual - submitted session token
//           expected - current session token
// Returns:  true if both tokens match.
//------------------------------------------------------------------------------
bool sameToken(const String& actual, const String& expected) {
  if (actual.length() != expected.length() || expected.length() != 32) {
    return false;
  }
  uint8_t difference = 0;
  for (unsigned int i = 0; i < expected.length(); ++i) {
    difference |= actual[i] ^ expected[i];
  }
  return difference == 0;
}

//------------------------------------------------------------------------------
// Setup page fragments
// Purpose:  Store the setup page in flash memory rather than writable RAM.
//           Raw strings (R"HTML") keep HTML quotes readable without escaping.
//           showForm() joins these pieces with the status and session token.
//------------------------------------------------------------------------------
const char kPageStart[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Weather keychain Wi-Fi</title>

    <style>
      /* Page background and default text. */
      body {
        font: 16px system-ui, sans-serif;
        background: #eef3f6;
        color: #182c3a;
        margin: 0;
        padding: 24px;
      }

      /* Center the setup card on phones and larger screens. */
      main {
        max-width: 420px;
        margin: 4vh auto;
        background: white;
        padding: 28px;
        border-radius: 18px;
      }

      h1 {
        font-size: 25px;
        margin: 0 0 14px;
      }

      p {
        line-height: 1.5;
      }

      label {
        display: block;
        margin-top: 18px;
      }

      /* Shared sizing for form controls. */
      input,
      button {
        box-sizing: border-box;
        width: 100%;
        font: inherit;
        padding: 12px;
        border-radius: 8px;
      }

      input {
        margin-top: 6px;
        border: 1px solid #8294a1;
      }

      button {
        margin-top: 24px;
        background: #176281;
        color: white;
        border: 0;
      }

      small {
        display: block;
        margin-top: 8px;
        color: #4a606f;
      }

      .status {
        background: #edf6fa;
        padding: 12px;
        border-radius: 8px;
      }
    </style>
  </head>

  <body>
    <main>
      <h1>Connect your weather keychain</h1>
      <p>
        Use a <strong>2.4 GHz home Wi-Fi network</strong>
        or a phone hotspot set to 2.4 GHz.
        Enter its network name and password below.
      </p>
      <p class="status">)HTML";

// showForm() inserts the status text before this fragment closes its paragraph.
// The final quote stays open so the session token can be inserted as the value.
const char kFormStart[] PROGMEM = R"HTML(</p>
      <form method="post" action="/save">
        <input type="hidden" name="csrf" value=")HTML";

// Close the token's value attribute, then add the visible fields and page ending.
const char kFormEnd[] PROGMEM = R"HTML(">
        <label>
          Wi-Fi network name
          <input
            name="ssid"
            type="text"
            maxlength="32"
            required
            autocomplete="off"
            autocapitalize="none"
            spellcheck="false"
          >
        </label>

        <label>
          Wi-Fi password
          <input
            name="password"
            type="password"
            maxlength="63"
            autocomplete="new-password"
          >
        </label>
        <small>Use 8–63 characters, or leave blank for an open network.</small>

        <label>
          City or postal code
          <input
            name="city"
            type="text"
            maxlength="96"
            required
            placeholder="City, state or country"
          >
        </label>
        <small>
          Use a full state/region name or country after a comma
          if the city name is shared.
        </small>

        <button type="submit">Save and connect</button>
      </form>

      <p>
        <small>
          The keychain saves these details after connecting.
          It never displays your saved Wi-Fi password.
          This setup network closes when the connection succeeds.
        </small>
      </p>
    </main>
  </body>
</html>
)HTML";
}  // namespace

//------------------------------------------------------------------------------
// connected
// Purpose:  Report whether the device has joined the saved Wi-Fi network.
// Args:     None.
// Returns:  true if Wi-Fi is connected.
//------------------------------------------------------------------------------
bool WifiSetup::connected() const {
  return !portalActive_ && !requestPending_ && WiFi.status() == WL_CONNECTED;
}

//------------------------------------------------------------------------------
// begin
// Purpose:  Configure the setup routes and try saved credentials.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::begin() {
  if (started_) {
    return;
  }
  started_ = true;
  // This must precede the first mode/begin call so the driver uses RAM storage.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  const char* headers[] = {"Origin"};
  server_.collectHeaders(headers, 1);
  server_.on("/", HTTP_GET, [this]() { showForm(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  server_.onNotFound([this]() { redirectToPortal(); });
  if (loadCredentials()) {
    queueConnection(0, false);
  } else {
    startPortal();
  }
}

//------------------------------------------------------------------------------
// loadCredentials
// Purpose:  Read saved credentials and reject incomplete or invalid stored data.
// Args:     None.
// Returns:  true if valid saved credentials were found.
//------------------------------------------------------------------------------
bool WifiSetup::loadCredentials() {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return false;
  }
  Credentials stored{};
  const bool read = preferences.getBytesLength(kCredentialKey) == sizeof(stored) &&
                    preferences.getBytes(kCredentialKey, &stored, sizeof(stored)) == sizeof(stored);
  preferences.end();
  if (!read || stored.magic != kCredentialsMagic || stored.ssidLength > 32 ||
      stored.passwordLength > 63 || stored.ssid[stored.ssidLength] != '\0' ||
      stored.password[stored.passwordLength] != '\0' || stored.city[96] != '\0' ||
      stored.city[0] == '\0') {
    return false;
  }
  city_ = stored.city;
  requestedSsid_ = stored.ssid;
  requestedPassword_ = stored.password;
  return requestedSsid_.length() == stored.ssidLength &&
         requestedPassword_.length() == stored.passwordLength &&
         validCredentials(requestedSsid_, requestedPassword_);
}

//------------------------------------------------------------------------------
// saveCredentials
// Purpose:  Save credentials only after a successful connection.
// Args:     None.
// Returns:  true if credentials were saved successfully.
//------------------------------------------------------------------------------
bool WifiSetup::saveCredentials() {
  Credentials stored{};
  stored.magic = kCredentialsMagic;
  stored.ssidLength = requestedSsid_.length();
  stored.passwordLength = requestedPassword_.length();
  requestedSsid_.toCharArray(stored.ssid, sizeof(stored.ssid));
  requestedPassword_.toCharArray(stored.password, sizeof(stored.password));
  city_.toCharArray(stored.city, sizeof(stored.city));
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }
  const bool saved =
      preferences.putBytes(kCredentialKey, &stored, sizeof(stored)) == sizeof(stored);
  preferences.end();
  return saved;
}

//------------------------------------------------------------------------------
// queueConnection
// Purpose:  Schedule a connection attempt without blocking the setup page.
// Args:     delayMs - delay before attempting the connection
//           saveOnSuccess - whether to save credentials after connecting
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::queueConnection(uint32_t delayMs, bool saveOnSuccess) {
  requestPending_ = true;
  disconnectRequested_ = false;
  connecting_ = false;
  saveOnSuccess_ = saveOnSuccess;
  requestedAt_ = millis();
  requestDelayMs_ = delayMs;
  status_ = "Connecting to Wi-Fi. Watch the keychain for the result.";
}

//------------------------------------------------------------------------------
// startPortal
// Purpose:  Start the temporary Wi-Fi network and local configuration page.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::startPortal() {
  if (!started_) {
    begin();
    if (portalActive_) {
      return;
    }
  }
  if (portalActive_) {
    return;
  }
  sleeping_ = false;
  portalWanted_ = true;
  lastPortalAttempt_ = millis();
  requestPending_ = false;
  connecting_ = false;
  wasConnected_ = false;
  saveOnSuccess_ = false;
  // Pause automatic reconnect while the setup portal is changing networks.
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false, 0);  // Neither erase NVS nor wait for disconnect.
  WiFi.mode(WIFI_AP_STA);
  if (portalSsid_.isEmpty()) {
    char suffix[5];
    snprintf(suffix, sizeof(suffix), "%04X",
             static_cast<unsigned int>((ESP.getEfuseMac() >> 32) & 0xFFFF));
    portalSsid_ = "Weather-" + String(suffix);
  }
  char password[9];
  for (uint8_t i = 0; i < 8; ++i) {
    password[i] = kAlphabet[esp_random() & 31];
  }
  password[8] = '\0';
  portalPassword_ = password;
  csrfToken_ = randomToken();
  const IPAddress ip(192, 168, 4, 1);
  if (!WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0)) ||
      !WiFi.softAP(portalSsid_.c_str(), portalPassword_.c_str(), 1, 0, 4, false,
                   WIFI_AUTH_WPA2_PSK)) {
    status_ = "Setup network could not start. Retrying shortly.";
    return;
  }
  portalActive_ = true;
  const bool dnsReady = dns_.start(53, "*", WiFi.softAPIP());
  server_.begin();
  status_ = dnsReady ? "Enter your Wi-Fi details to connect."
                     : "Open http://192.168.4.1 to enter your Wi-Fi details.";
}

//------------------------------------------------------------------------------
// stopPortal
// Purpose:  Stop the setup web server and temporary access point.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::stopPortal() {
  server_.stop();
  dns_.stop();
  WiFi.softAPdisconnect(true);
  portalActive_ = false;
  portalWanted_ = false;
  portalPassword_ = "";
  csrfToken_ = "";
}

//------------------------------------------------------------------------------
// tick
// Purpose:  Advance the connection state a little each loop; avoid long blocking waits.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::tick() {
  if (!started_ || sleeping_) {
    return;
  }
  if (portalActive_) {
    dns_.processNextRequest();
    server_.handleClient();
  }
  // A form handler may have just set requestedAt_; sample time afterwards.
  const uint32_t now = millis();
  if (requestPending_) {
    if (now - requestedAt_ < requestDelayMs_) {
      return;
    }
    if (!disconnectRequested_) {
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false, false, 0);
      disconnectRequested_ = true;
      return;  // Allow the disconnect event to complete before WiFi.begin().
    }
    if (WiFi.status() == WL_CONNECTED) {
      if (now - requestedAt_ >= kConnectionTimeoutMs) {
        requestPending_ = false;
        if (!portalActive_) {
          startPortal();
        }
        status_ = "Could not switch networks. Please try again.";
      }
      return;
    }
    WiFi.mode(portalActive_ ? WIFI_AP_STA : WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(requestedSsid_.c_str(),
               requestedPassword_.isEmpty() ? nullptr : requestedPassword_.c_str());
    requestPending_ = false;
    connecting_ = true;
    attemptStarted_ = millis();
    return;
  }
  if (connecting_) {
    if (WiFi.status() == WL_CONNECTED) {
      const bool saved = !saveOnSuccess_ || saveCredentials();
      connecting_ = false;
      wasConnected_ = true;
      saveOnSuccess_ = false;
      requestedPassword_ = "";
      if (portalActive_) {
        stopPortal();
      }
      status_ = saved ? "Wi-Fi connected" : "Wi-Fi connected; saving settings failed";
    } else if (now - attemptStarted_ >= kConnectionTimeoutMs) {
      connecting_ = false;
      WiFi.setAutoReconnect(false);
      WiFi.disconnect(false, false, 0);
      if (!portalActive_) {
        startPortal();
      }
      status_ = "Connection failed. Check the network name and password, then try again.";
    }
    return;
  }
  if (wasConnected_ && WiFi.status() != WL_CONNECTED) {
    wasConnected_ = false;
    connecting_ = true;  // The driver's automatic reconnect remains enabled.
    attemptStarted_ = now;
    status_ = "Reconnecting to Wi-Fi";
  }
  if (portalWanted_ && !portalActive_ && now - lastPortalAttempt_ >= kPortalRetryMs) {
    startPortal();
  }
}

//------------------------------------------------------------------------------
// responseHeaders
// Purpose:  Prevent setup pages and sensitive form data from being cached.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::responseHeaders() {
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("Pragma", "no-cache");
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.sendHeader("Referrer-Policy", "same-origin");
  server_.sendHeader("Content-Security-Policy",
                     "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; "
                     "frame-ancestors 'none'; base-uri 'none'");
  server_.sendHeader("Connection", "close");
}

//------------------------------------------------------------------------------
// allowApRequest
// Purpose:  Accept setup requests only through the device access point.
// Args:     None.
// Returns:  true if the request arrived through the setup access point.
//------------------------------------------------------------------------------
bool WifiSetup::allowApRequest() {
  if (portalActive_ && server_.client().localIP() == WiFi.softAPIP()) {
    return true;
  }
  responseHeaders();
  server_.send(403, "text/plain", "Wi-Fi setup is only available on the keychain setup network.");
  return false;
}

//------------------------------------------------------------------------------
// canonicalHost
// Purpose:  Check that the browser is addressing the setup portal directly.
// Args:     None.
// Returns:  true if the request uses the portal host.
//------------------------------------------------------------------------------
bool WifiSetup::canonicalHost() const {
  const String host = server_.hostHeader();
  return host == "192.168.4.1" || host == "192.168.4.1:80";
}

//------------------------------------------------------------------------------
// redirectToPortal
// Purpose:  Send captive-portal browser requests to the local setup page.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::redirectToPortal() {
  if (!allowApRequest()) {
    return;
  }
  responseHeaders();
  if (server_.method() != HTTP_GET && server_.method() != HTTP_HEAD) {
    server_.send(405, "text/plain", "Use the setup form at http://192.168.4.1/.");
    return;
  }
  server_.sendHeader("Location", portalUrl());
  server_.send(302, "text/plain", "Open the keychain Wi-Fi setup page.");
}

//------------------------------------------------------------------------------
// showForm
// Purpose:  Serve the Wi-Fi form with the current session token.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::showForm() {
  if (!allowApRequest()) {
    return;
  }
  if (!canonicalHost()) {
    redirectToPortal();
    return;
  }
  String page;
  page.reserve(2600);
  page += FPSTR(kPageStart);
  page += status_;  // Only fixed internal strings; never reflected credentials.
  page += FPSTR(kFormStart);
  page += csrfToken_;
  page += FPSTR(kFormEnd);
  responseHeaders();
  server_.send(200, "text/html; charset=utf-8", page);
}

//------------------------------------------------------------------------------
// handleSave
// Purpose:  Validate the submitted session and credentials before trying the network.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::handleSave() {
  if (!allowApRequest()) {
    return;
  }
  responseHeaders();
  // Captive browsers and no-referrer forms can send an opaque (null) Origin.
  // The AP-only interface, canonical Host and unpredictable form token remain required.
  const String origin = server_.header("Origin");
  if (!canonicalHost() ||
      (!origin.isEmpty() && origin != "null" && origin != "http://192.168.4.1" &&
       origin != "http://192.168.4.1:80") ||
      !sameToken(server_.arg("csrf"), csrfToken_)) {
    server_.send(403, "text/plain",
                 "Setup session expired. Reload http://192.168.4.1/ and try again.");
    return;
  }
  if (requestPending_) {
    server_.send(409, "text/plain", "Connection request already queued. Please wait.");
    return;
  }
  const String ssid = server_.arg("ssid");
  const String password = server_.arg("password");
  String city = server_.arg("city");
  city.trim();
  if (city.length() < 2 || city.length() > 96) {
    server_.send(400, "text/plain", "Enter a city or postal code (2-96 bytes).");
    return;
  }
  for (unsigned int i = 0; i < city.length(); ++i) {
    if (static_cast<unsigned char>(city[i]) < 32) {
      server_.send(400, "text/plain", "City contains an invalid character.");
      return;
    }
  }
  if (!validCredentials(ssid, password)) {
    server_.send(400, "text/plain",
                 "Use a network name of 1–32 bytes and a password of 8–63 bytes, or blank for an "
                 "open network. Go back and try again.");
    return;
  }
  city_ = city;
  requestedSsid_ = ssid;
  requestedPassword_ = password;
  queueConnection(400, true);
  server_.send(200, "text/html; charset=utf-8",
               "<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'>"
               "<title>Connecting</title><h1>Connecting to Wi-Fi</h1>"
               "<p>Watch the keychain. Setup closes when connected; reconnect your phone to its "
               "usual network.</p>"
               "<p>If it cannot connect within 30 seconds, <a href='/'>return to setup</a> and try "
               "again.</p>");
}

//------------------------------------------------------------------------------
// suspend
// Purpose:  Turn off the radio between weather updates to reduce power use.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::suspend() {
  if (portalActive_) {
    stopPortal();
  }
  sleeping_ = true;
  connecting_ = false;
  requestPending_ = false;
  wasConnected_ = false;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false, 0);
  WiFi.mode(WIFI_OFF);
  status_ = "Wi-Fi sleeping between updates";
}
//------------------------------------------------------------------------------
// resume
// Purpose:  Wake the radio and reconnect using saved credentials.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void WifiSetup::resume() {
  if (!sleeping_) {
    return;
  }
  sleeping_ = false;
  WiFi.mode(WIFI_STA);
  if (loadCredentials()) {
    queueConnection(0, false);
  } else {
    startPortal();
  }
}
