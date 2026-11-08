//==============================================================================
//  Date:     11/07/2026
//  Program:  Weather Keychain
//  File:     main.cpp
//
//  Purpose:  Coordinate the OLED, live weather updates, artwork rotation, and power saving.
//==============================================================================

#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <time.h>

#include "LandscapeArt.h"
#include "SceneRotation.h"
#include "WeatherClient.h"
#include "WeatherConfig.h"
#include "WeatherRenderer.h"
#include "WifiSetup.h"
constexpr uint32_t kWeatherRefreshMs = 15 * 60 * 1000;
constexpr uint32_t kSceneDurationMs = 60 * 1000;
constexpr uint32_t kNetworkTimeoutMs = 90 * 1000;
constexpr uint32_t kOfflineRetryMs = 5 * 60 * 1000;

// The main loop owns these flags. The network task sends results through a queue.
bool screenNeedsUpdate = true;
bool userRequestedSetup = false;
uint32_t networkStartedAt = 0;
uint32_t sleepCount = 0;
SceneRotation rotation;
int currentArtGroup = -1;
int currentArtIndex = -1;
uint32_t artStartedAt = 0;
//------------------------------------------------------------------------------
// groupForScene
// Purpose:  Convert a weather condition into the matching artwork group. Unknown has no group.
// Args:     scene - weather scene to display or classify
// Returns:  artwork group index, or -1 for an unknown condition.
//------------------------------------------------------------------------------
int groupForScene(WeatherScene scene) {
  switch (scene) {
    case WeatherScene::Sun:
      return 0;
    case WeatherScene::Moon:
      return 1;
    case WeatherScene::PartlyCloudy:
      return 2;
    case WeatherScene::Cloudy:
      return 3;
    case WeatherScene::Rain:
      return 4;
    case WeatherScene::Fog:
      return 5;
    case WeatherScene::Snow:
      return 6;
    case WeatherScene::Thunderstorm:
      return 7;
    default:
      return -1;
  }
}
WifiSetup wifi;
WeatherData weather;
struct WeatherResult {
  bool ok;
  bool locationNeedsSetup;
  WeatherData data;
  char error[80];
};
QueueHandle_t weatherResults;
TaskHandle_t weatherWorker;
bool fetchInProgress = false;
bool clockStarted = false;
uint32_t nextFetchAt = 0;
String lastError;
char requestedCity[97] = {};
String activeCity;
//------------------------------------------------------------------------------
// networkTask
// Purpose:  Wait for a request, fetch weather, then send a complete result back to the main
//           loop.
// Args:     unused - unused task argument
// Returns:  None.
//------------------------------------------------------------------------------
void networkTask(void*) {
  String cachedCity;
  double latitude = 0;
  double longitude = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    WeatherResult result{};
    String error;
    String city = requestedCity;
    bool located = city == cachedCity && cachedCity.length() > 0;
    if (!located) {
      located = resolveCity(city, latitude, longitude, error);
      if (located) {
        cachedCity = city;
      }
    }
    result.locationNeedsSetup = error == "CITY NOT FOUND: USE SETUP" ||
                                error == "ADD REGION: USE SETUP" || error == "ENTER CITY IN SETUP";
    result.ok = located && fetchWeather(latitude, longitude, result.data, error);
    error.toCharArray(result.error, sizeof(result.error));
    xQueueOverwrite(weatherResults, &result);
  }
}

Adafruit_SSD1306 display(WeatherConfig::kWidth, WeatherConfig::kHeight, &Wire, -1,
                         WeatherConfig::kBusHz, WeatherConfig::kBusHz);
bool screenReady = false;
uint8_t oledAddress = 0;
uint32_t sceneSince = 0;
uint32_t lastFrame = 0;
uint32_t lastAttempt = 0;
uint32_t lastHealthCheck = 0;
uint32_t framesSubmitted = 0;

//------------------------------------------------------------------------------
// addressResponds
// Purpose:  Check whether an I2C device acknowledges this address.
// Args:     address - I2C address to check
// Returns:  true if the device acknowledges the address.
//------------------------------------------------------------------------------
bool addressResponds(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

//------------------------------------------------------------------------------
// printHelp
// Purpose:  List the USB serial commands and the OLED wiring.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void printHelp() {
  Serial.println("KeychainWeather: 148 scenes, 60-second shuffle rotation.");
  Serial.println("Live weather: w=Wi-Fi setup, u=refresh, p=status.");
  Serial.println("OLED: SDA D10/GPIO18, SCL D9/GPIO20, VCC 3V3, GND GND.");
}

//------------------------------------------------------------------------------
// startScreen
// Purpose:  Find and initialize the OLED. Return false so the loop can retry if it is absent.
// Args:     None.
// Returns:  true if the OLED framebuffer was initialized.
//------------------------------------------------------------------------------
bool startScreen() {
  lastAttempt = millis();
  oledAddress = 0;
  for (uint8_t address : {uint8_t(0x3C), uint8_t(0x3D)}) {
    if (addressResponds(address)) {
      oledAddress = address;
      break;
    }
  }
  if (!oledAddress) {
    Serial.println("OLED not detected at 0x3C/0x3D; retrying in 3 seconds.");
    return false;
  }
  // Keep explicitly configured GPIO18/20; do not call default Wire.begin().
  // This return value reports allocation, not success of every I2C write.
  if (!display.begin(SSD1306_SWITCHCAPVCC, oledAddress, false, false)) {
    Serial.println("OLED framebuffer allocation failed.");
    return false;
  }
  display.setRotation(WeatherConfig::kRotation);
  display.clearDisplay();
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(WeatherConfig::kContrast);
  display.display();
  sceneSince = lastHealthCheck = millis();
  lastFrame = sceneSince - WeatherConfig::kFrameMs;
  Serial.printf("Weather started: OLED address 0x%02X; I2C %lu Hz. Confirm animation visually.\n",
                oledAddress, static_cast<unsigned long>(WeatherConfig::kBusHz));
  return true;
}

//------------------------------------------------------------------------------
// printStatus
// Purpose:  Print OLED connection information for troubleshooting.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void printStatus() {
  Serial.printf("OLED=0x%02X ACK=%s frame_submissions=%lu\n", oledAddress,
                oledAddress && addressResponds(oledAddress) ? "yes" : "no",
                (unsigned long)framesSubmitted);
}

//------------------------------------------------------------------------------
// handleSerial
// Purpose:  Handle optional commands typed into the USB serial monitor.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void handleSerial() {
  while (Serial.available()) {
    char command = Serial.read();
    if (command == 'w' && !fetchInProgress) {
      userRequestedSetup = true;
      wifi.startPortal();
      screenNeedsUpdate = true;
    }
    if (command == 'u' && !fetchInProgress) {
      nextFetchAt = millis();
    }
    if (command == 'p') {
      Serial.printf("Artwork=%d group=%d wifi_sleep=%d sleeps=%lu\n", currentArtIndex + 1,
                    currentArtGroup, wifi.sleeping(), (unsigned long)sleepCount);
      printStatus();
      Serial.printf("WiFi=%s weather=%s error=%s\n", wifi.connected() ? "connected" : "offline",
                    weather.valid ? "available" : "pending", lastError.c_str());
    }
  }
}

//------------------------------------------------------------------------------
// setup
// Purpose:  Initialize hardware once at startup, then begin Wi-Fi setup or reconnection.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);  // Battery startup must not wait for a USB host.
  pinMode(WeatherConfig::kSda, INPUT_PULLUP);
  pinMode(WeatherConfig::kScl, INPUT_PULLUP);
  Wire.begin(WeatherConfig::kSda, WeatherConfig::kScl, WeatherConfig::kBusHz);
  Wire.setTimeOut(WeatherConfig::kBusTimeoutMs);
  printHelp();
  screenReady = startScreen();
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);
  pinMode(14, OUTPUT);
  digitalWrite(14, LOW);
  // A one-result queue safely passes data between the network task and this loop.
  weatherResults = xQueueCreate(1, sizeof(WeatherResult));
  if (weatherResults) {
    xTaskCreate(networkTask, "weather", 12288, nullptr, 1, &weatherWorker);
  }
  wifi.begin();
}

//------------------------------------------------------------------------------
// updateWeatherConnection
// Purpose:  Manage connections and pass completed network results to the display state. The
//           worker performs HTTPS separately so the setup page stays responsive.
// Args:     now - current uptime in milliseconds
// Returns:  None.
//------------------------------------------------------------------------------
void updateWeatherConnection(uint32_t now) {
  // Signed subtraction keeps deadline checks working when millis() wraps around.
  if (wifi.sleeping() && static_cast<int32_t>(now - nextFetchAt) >= 0) {
    wifi.resume();
    networkStartedAt = now;
  }
  if (weather.valid && !fetchInProgress && !userRequestedSetup && !wifi.sleeping() &&
      now - networkStartedAt > kNetworkTimeoutMs) {
    wifi.suspend();
    nextFetchAt = now + kOfflineRetryMs;
    screenNeedsUpdate = true;
  }
  if (wifi.connected() && !clockStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    clockStarted = true;
  }
  WeatherResult result;
  if (weatherResults && xQueueReceive(weatherResults, &result, 0) == pdTRUE) {
    fetchInProgress = false;
    lastError = result.error;
    if (result.ok) {
      weather = result.data;
    }
    screenNeedsUpdate = true;
    userRequestedSetup = false;
    if (result.locationNeedsSetup) {
      userRequestedSetup = true;
      wifi.startPortal();
    } else {
      wifi.suspend();
    }
    nextFetchAt = now + (result.ok ? kWeatherRefreshMs : kSceneDurationMs);
    Serial.printf("Weather fetch: %s %s\n", result.ok ? "success" : "failed", result.error);
  }
  if (weatherWorker && wifi.connected() && time(nullptr) > 1704067200 && !fetchInProgress &&
      static_cast<int32_t>(now - nextFetchAt) >= 0) {
    String city = wifi.city();
    if (city != activeCity) {
      weather = WeatherData{};
      currentArtGroup = -1;
      currentArtIndex = -1;
      screenNeedsUpdate = true;
      activeCity = city;
    }
    city.toCharArray(requestedCity, sizeof(requestedCity));
    fetchInProgress = true;
    xTaskNotifyGive(weatherWorker);
  }
}

//------------------------------------------------------------------------------
// drawTemperature
// Purpose:  Place a Fahrenheit reading in the top-right corner over a black background.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void drawTemperature() {
  char temp[10];
  snprintf(temp, sizeof(temp), "%.0f", weather.tempC * 1.8f + 32);
  const int digits = strlen(temp);
  const int width = digits * 6 + 13;
  display.fillRect(128 - width - 2, 0, width + 2, 11, SSD1306_BLACK);
  display.setCursor(128 - width, 2);
  display.print(temp);
  display.drawCircle(128 - width + digits * 6 + 1, 3, 1, SSD1306_WHITE);
  display.setCursor(122, 2);
  display.print("F");
}

//------------------------------------------------------------------------------
// updateScreen
// Purpose:  Draw either setup instructions, weather artwork, or the startup status.
// Args:     now - current uptime in milliseconds
//           renderDue - whether the screen needs a new frame
// Returns:  None.
//------------------------------------------------------------------------------
void updateScreen(uint32_t now, bool renderDue) {
  if (screenReady && renderDue) {
    screenNeedsUpdate = false;
    lastFrame = now;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    if (wifi.portalActive()) {
      display.println(lastError.startsWith("CITY NOT") || lastError.startsWith("ADD REGION")
                          ? lastError
                          : "JOIN WI-FI:");
      display.println(wifi.portalSsid());
      display.println("PASSWORD:");
      display.println(wifi.portalPassword());
      display.println("OPEN IN BROWSER:");
      display.println("192.168.4.1");
    } else if (weather.valid) {
      bool fresh = weatherDataFresh(weather, time(nullptr));
      int group = fresh ? groupForScene(weatherSceneForCode(weather.code, weather.isDay)) : -1;
      if (group >= 0) {
        if (group != currentArtGroup || currentArtIndex < 0 ||
            now - artStartedAt >= kSceneDurationMs) {
          currentArtIndex = rotation.next(group, []() { return esp_random(); });
          currentArtGroup = group;
          artStartedAt = now;
          Serial.printf("Artwork=%d group=%d\n", currentArtIndex + 1, currentArtGroup);
        }
        // First four groups have 19 scenes; the remaining groups have 18.
        const int offset = group < 4 ? group * 19 : 76 + (group - 4) * 18;
        const int local = currentArtIndex - offset;
        // The last entry in each group is animated rather than a stored bitmap.
        const int bitmapCount = group < 4 ? 18 : 17;
        if (local == bitmapCount) {
          drawWeatherScene(display, weatherSceneForCode(weather.code, weather.isDay),
                           now - artStartedAt);
        } else {
          const int bitmapOffset = group < 4 ? group * 18 : 72 + (group - 4) * 17;
          display.drawBitmap(0, 0, kLandscapeArt[bitmapOffset + local], 128, 64, SSD1306_WHITE);
        }
        drawTemperature();
      } else {
        currentArtGroup = -1;
        drawWeatherScene(display, WeatherScene::Unknown, now);
      }

    } else {
      display.println("WEATHER KEYCHAIN");
      display.println();
      display.println(!wifi.connected()            ? "Connecting Wi-Fi"
                      : time(nullptr) < 1704067200 ? "Setting clock..."
                                                   : "Loading weather...");
      if (lastError.length()) {
        display.println();
        display.println(lastError);
      }
    }
    display.display();
    framesSubmitted++;
  }
}

//------------------------------------------------------------------------------
// restUntilNextUpdate
// Purpose:  Keep the OLED image visible while the processor rests between updates.
// Args:     animated - whether the current artwork needs animation updates
// Returns:  None.
//------------------------------------------------------------------------------
void restUntilNextUpdate(bool animated) {
  // No Wi-Fi transaction is in flight here. Light sleep retains RAM and OLED RAM.
  // Keep USB debugging responsive; actual sleep is used when no serial host is open.
  if (wifi.sleeping() && !fetchInProgress && !Serial) {
    esp_sleep_enable_timer_wakeup(animated ? 500000 : 1000000);
    if (esp_light_sleep_start() == ESP_OK) {
      ++sleepCount;
    }
  } else {
    delay(10);
  }
}

//------------------------------------------------------------------------------
// isAnimatedArtwork
// Purpose:  Every group ends with one animated scene after its static bitmap scenes.
// Args:     None.
// Returns:  true if the selected scene is procedural animation.
//------------------------------------------------------------------------------
bool isAnimatedArtwork() {
  if (currentArtGroup < 0) {
    return false;
  }
  int firstScene;
  int animationPosition;
  if (currentArtGroup < 4) {
    firstScene = currentArtGroup * 19;
    animationPosition = 18;
  } else {
    firstScene = 76 + (currentArtGroup - 4) * 18;
    animationPosition = 17;
  }
  return currentArtIndex - firstScene == animationPosition;
}

//------------------------------------------------------------------------------
// loop
// Purpose:  Arduino calls this repeatedly. Each helper handles one part of the device.
// Args:     None.
// Returns:  None.
//------------------------------------------------------------------------------
void loop() {
  handleSerial();
  wifi.tick();
  uint32_t now = millis();
  // A newly selected city should load immediately, not at the next scheduled refresh.
  if (!fetchInProgress && wifi.connected() && wifi.city() != activeCity) {
    nextFetchAt = now;
  }
  updateWeatherConnection(now);
  if (!screenReady && now - lastAttempt >= 3000) {
    screenReady = startScreen();
  }
  bool animated = isAnimatedArtwork();
  bool freshNow = weatherDataFresh(weather, time(nullptr));
  bool changed =
      weather.valid &&
      (groupForScene(weatherSceneForCode(weather.code, weather.isDay)) != currentArtGroup ||
       !freshNow);
  bool renderDue = screenNeedsUpdate || changed || now - artStartedAt >= kSceneDurationMs ||
                   (animated && now - lastFrame >= 500) ||
                   ((!weather.valid || wifi.portalActive()) && now - lastFrame >= 1000);

  updateScreen(now, renderDue);
  restUntilNextUpdate(animated);
}
