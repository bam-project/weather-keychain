//==============================================================================
//  Date:     11/07/2026
//  Program:  Weather Keychain
//  File:     WeatherRenderer.h
//
//  Purpose:  Draw monochrome weather scenes and animated sky effects.
//==============================================================================

#pragma once

#include <Adafruit_SSD1306.h>
#include <math.h>
#include <stdint.h>

enum class WeatherScene : uint8_t {
  Sun,
  Rain,
  Moon,
  PartlyCloudy,
  Cloudy,
  Fog,
  Snow,
  Thunderstorm,
  Unknown
};

//------------------------------------------------------------------------------
// weatherSceneForCode
// Purpose:  Open-Meteo's WMO weather interpretation codes. An unsupported value must remain
//           visibly unknown instead of silently reporting clear weather.
// Args:     code - WMO weather condition code
//           isDay - true for daytime conditions
// Returns:  matching scene, or Unknown for an unsupported code.
//------------------------------------------------------------------------------
inline WeatherScene weatherSceneForCode(int code, bool isDay) {
  switch (code) {
    case 0:
      return isDay ? WeatherScene::Sun : WeatherScene::Moon;
    case 1:
    case 2:
      return WeatherScene::PartlyCloudy;
    case 3:
      return WeatherScene::Cloudy;
    case 45:
    case 48:
      return WeatherScene::Fog;
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
    case 80:
    case 81:
    case 82:
      return WeatherScene::Rain;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return WeatherScene::Snow;
    case 95:
    case 96:
    case 97:
    case 99:
      return WeatherScene::Thunderstorm;
    default:
      return WeatherScene::Unknown;
  }
}

namespace keychain_weather_detail {

//------------------------------------------------------------------------------
// drawUnknown
// Purpose:  Show a question mark when conditions are missing or too old.
// Args:     display - OLED framebuffer to draw into
// Returns:  None.
//------------------------------------------------------------------------------
inline void drawUnknown(Adafruit_SSD1306& display) {
  const int16_t centerX = display.width() / 2;
  display.drawCircle(centerX, 23, 17, SSD1306_WHITE);
  for (uint8_t stroke = 0; stroke < 2; ++stroke) {
    const int16_t x = centerX + stroke;
    display.drawLine(x - 6, 17, x - 3, 13, SSD1306_WHITE);
    display.drawLine(x - 3, 13, x + 4, 13, SSD1306_WHITE);
    display.drawLine(x + 4, 13, x + 6, 16, SSD1306_WHITE);
    display.drawLine(x + 6, 16, x + 6, 21, SSD1306_WHITE);
    display.drawLine(x + 6, 21, x, 25, SSD1306_WHITE);
    display.drawLine(x, 25, x, 28, SSD1306_WHITE);
  }
  display.fillCircle(centerX, 34, 1, SSD1306_WHITE);
}

}  // namespace keychain_weather_detail

//------------------------------------------------------------------------------
// drawSkyArt
// Purpose:  Ordered shading stays anchored to each cloud as it moves: no random flicker.
// Args:     display - OLED framebuffer to draw into
//           scene - weather scene to display or classify
//           elapsedMs - milliseconds since the scene started
// Returns:  None.
//------------------------------------------------------------------------------
inline void drawSkyArt(Adafruit_SSD1306& display, WeatherScene scene, uint32_t elapsedMs) {
  static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
  struct Puff {
    float x;
    float y;
    float radiusX;
    float radiusY;
  };
  static const Puff clouds[] = {{-5, 11, 23, 14}, {14, 3, 19, 12},   {30, -4, 17, 11},
                                {12, 43, 28, 14}, {35, 38, 23, 17},  {51, 45, 22, 13},
                                {99, 39, 24, 18}, {120, 31, 21, 16}, {139, 19, 24, 17},
                                {81, 54, 28, 17}, {113, 55, 29, 17}};
  const bool wet = scene == WeatherScene::Rain || scene == WeatherScene::Thunderstorm;
  const bool night = scene == WeatherScene::Moon;
  const bool clear = scene == WeatherScene::Sun || night;
  int drift = int((elapsedMs / 700) % 16) - 8;
  for (int y = 0; y < 64; y++) {
    for (int x = 0; x < 128; x++) {
      float light = 0;
      for (const auto& cloud : clouds) {
        float cloudY = cloud.y + (cloud.y > 25 ? 14 : 0) + (clear ? 9 : 0);
        float relativeX = (x - cloud.x - drift) / cloud.radiusX;
        float relativeY = (y - cloudY) / cloud.radiusY;
        float distanceSquared = relativeX * relativeX + relativeY * relativeY;
        if (distanceSquared < 1) {
          // Bright upper-left lobes and broad shaded lower-right bellies.
          float brightness = 0.55f - 0.27f * relativeX - 0.36f * relativeY;
          if (distanceSquared > 0.83f) {
            brightness *= 0.65f;
          }
          if (wet) {
            brightness *= 0.66f;
          }
          if (brightness > light) {
            light = brightness;
          }
        }
      }
      if (scene == WeatherScene::Cloudy) {
        float relativeX = (x - 72 - drift) / 31.0f;
        float relativeY = (y - 9) / 16.0f;
        if (relativeX * relativeX + relativeY * relativeY < 1) {
          light = fmaxf(light, 0.55f - 0.3f * relativeY - 0.15f * relativeX);
        }
      }
      if (light * 16 > bayer[((y & 3) << 2) | ((x - drift) & 3)]) {
        display.drawPixel(x, y, SSD1306_WHITE);
      }
    }
  }
  if (clear || scene == WeatherScene::PartlyCloudy) {
    int centerX = 77;
    int centerY = 14;
    for (int y = 4; y <= 24; y++) {
      for (int x = 67; x <= 87; x++) {
        int distanceSquared = (x - centerX) * (x - centerX) + (y - centerY) * (y - centerY);
        bool moonCut =
            night &&
            ((x - centerX - 5) * (x - centerX - 5) + (y - centerY + 4) * (y - centerY + 4) < 90);
        if (distanceSquared < 100 && !moonCut) {
          display.drawPixel(x, y, SSD1306_WHITE);
        }
      }
    }
    if (night) {
      display.drawPixel(50, 9, 1);
      display.drawPixel(105, 7, 1);
      display.drawPixel(96, 19, 1);
    }
  }
  if (wet) {
    for (int i = 0; i < 18; i++) {
      int y = (int(elapsedMs / 90) + i * 11) % 62;
      int x = (i * 23 + int(elapsedMs / 180)) % 128;
      display.drawLine(x, y, x - 1, y + 1, SSD1306_WHITE);
    }
  }
  if (scene == WeatherScene::Snow) {
    for (int i = 0; i < 13; i++) {
      int y = (int(elapsedMs / 180) + i * 7) % 62;
      int x = (i * 31 + int(elapsedMs / 900)) % 128;
      display.drawPixel(x, y, 1);
      display.drawPixel(x + 1, y + 1, 1);
    }
  }
  if (scene == WeatherScene::Fog) {
    for (int y = 29; y < 63; y += 6) {
      for (int x = 0; x < 128; x++) {
        if ((x + int(elapsedMs / 500) + y) % 31 < 22) {
          display.drawPixel(x, y, 1);
        }
      }
    }
  }
  if (scene == WeatherScene::Thunderstorm && elapsedMs % 4000 < 350) {
    display.fillTriangle(76, 13, 63, 28, 74, 28, 1);
    display.fillTriangle(70, 25, 64, 42, 83, 25, 1);
  }
}

//------------------------------------------------------------------------------
// drawWeatherScene
// Purpose:  Clear the framebuffer and draw one procedural weather scene.
// Args:     display - OLED framebuffer to draw into
//           scene - weather scene to display or classify
//           elapsedMs - milliseconds since the scene started
// Returns:  None.
//------------------------------------------------------------------------------
inline void drawWeatherScene(Adafruit_SSD1306& display, WeatherScene scene, uint32_t elapsedMs) {
  display.clearDisplay();
  if (scene == WeatherScene::Unknown) {
    keychain_weather_detail::drawUnknown(display);
  } else {
    drawSkyArt(display, scene, elapsedMs);
  }
}
