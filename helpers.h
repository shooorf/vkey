#pragma once
#include <Arduino.h>
#include <esp_mac.h>
#include <esp_efuse.h>
#include <esp_flash.h>
#include <mbedtls/sha256.h>

#ifndef CAPS_LOOKAHEAD_WINDOW
#define CAPS_LOOKAHEAD_WINDOW 16
#endif

#ifndef CAPS_MIN_UPPER_RUN
#define CAPS_MIN_UPPER_RUN 5
#endif

inline String buildDeviceHostname() {
  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);
  char buf[16];
  snprintf(buf, sizeof(buf), "vkey%02x%02x", mac[4], mac[5]);
  return String(buf);
}

inline String generateApPassword() {
  // Deterministic per-board WPA2 password derived from internal factory-burned hardware IDs
  // (eFuse Block 2 128-bit Unique ID, eFuse Block 1 calibration data, and 64-bit SPI Flash Unique ID)
  // that are never broadcast over radio and remain constant across full NVS/Flash erases.
  struct {
    uint8_t efuseBlk2[32];
    uint8_t efuseBlk1[24];
    uint64_t flashUid;
    uint8_t mac[6];
    char salt[20];
  } seed;
  memset(&seed, 0, sizeof(seed));

  esp_efuse_read_block(EFUSE_BLK2, seed.efuseBlk2, 0, sizeof(seed.efuseBlk2) * 8);
  esp_efuse_read_block(EFUSE_BLK1, seed.efuseBlk1, 0, sizeof(seed.efuseBlk1) * 8);
  esp_flash_read_unique_chip_id(NULL, &seed.flashUid);
  esp_efuse_mac_get_default(seed.mac);
  memcpy(seed.salt, "vkey-hw-ap-wpa2-v1", 18);

  uint8_t hash[32] = {0};
  mbedtls_sha256((const unsigned char*)&seed, sizeof(seed), hash, 0);

  // 10 characters: lowercase Latin letters and digits excluding ambiguous characters (0, o, 1, l, i)
  const char charset[] = "abcdefghjkmnpqrstuvwxyz23456789";
  const size_t charsetLen = sizeof(charset) - 1;
  char buf[11];
  for (int i = 0; i < 10; i++) {
    buf[i] = charset[hash[i] % charsetLen];
  }
  buf[10] = '\0';
  return String(buf);
}

inline String jsonEscape(const String& s) {
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if ((unsigned char)c >= 32) out += c;
  }
  return out;
}

inline bool isUpperLetter(char c) {
  return (c >= 'A' && c <= 'Z');
}

inline bool isLowerLetter(char c) {
  return (c >= 'a' && c <= 'z');
}

inline bool isShiftedSymbol(char c) {
  // Special characters that require Shift on a US English keyboard layout
  const char* shifted = "~!@#$%^&*()_+{}|:\"<>?";
  for (int i = 0; shifted[i] != '\0'; i++) {
    if (c == shifted[i]) return true;
  }
  return false;
}

inline bool shouldEnableCapsLock(const String& text, int curIdx, int& nextCapsCheckIndex) {
  if (curIdx < nextCapsCheckIndex) return false;

  int endIdx = min((int)text.length(), curIdx + CAPS_LOOKAHEAD_WINDOW);
  int upperCount = 0;

  for (int i = curIdx; i < endIdx; i++) {
    char ch = text[i];
    if (isUpperLetter(ch)) {
      upperCount++;
      if (upperCount >= CAPS_MIN_UPPER_RUN) {
        return true;
      }
    } else if (isLowerLetter(ch) || isShiftedSymbol(ch)) {
      // Stopper reached before accumulating 5 uppercase letters; skip re-checking up to this index
      nextCapsCheckIndex = i + 1;
      return false;
    }
  }

  // Since text[curIdx] is uppercase and leaves the window on the next step, at least
  // (CAPS_MIN_UPPER_RUN + 1 - upperCount) new characters must enter the window to reach 5.
  nextCapsCheckIndex = (endIdx >= (int)text.length())
                         ? (int)text.length()
                         : (curIdx + max(1, CAPS_MIN_UPPER_RUN + 1 - upperCount));
  return false;
}

