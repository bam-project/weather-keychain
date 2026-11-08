//==============================================================================
//  Date:     11/07/2026
//  Program:  Weather Keychain
//  File:     WeatherConfig.h
//
//  Purpose:  Define the OLED wiring, display settings, and timing constants.
//==============================================================================

#pragma once
#include <stdint.h>

namespace WeatherConfig {
constexpr int kSda = 18;  // XIAO D10.
constexpr int kScl = 20;  // XIAO D9.
constexpr uint8_t kWidth = 128;
constexpr uint8_t kHeight = 64;
constexpr uint32_t kBusHz = 100000;
constexpr uint16_t kBusTimeoutMs = 200;
constexpr uint8_t kContrast = 0x20;
constexpr uint8_t kRotation = 0;  // 2 for upside-down landscape mounting.
constexpr uint32_t kFrameMs = 125;
constexpr uint32_t kSceneMs = 6000;
constexpr uint32_t kRetryMs = 3000;
constexpr uint32_t kHealthCheckMs = 5000;
}
