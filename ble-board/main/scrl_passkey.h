#pragma once
/*
 * scrl_passkey.h — reader for the "set passkey" command of the BLE board.
 * Header-only and free of ESP-IDF, so the host test covers it
 * (test/test_scrl_settings.cpp).
 *
 * The command is SCRL_CMD_SET_PASSKEY followed by 6 ASCII digits. Text, not a
 * number: a passkey can start with zeros, and each of the 6 digits counts.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCRL_PASSKEY_DIGITS 6u
#define SCRL_PASSKEY_MAX 999999u

/** Read 6 ASCII digits. Returns false for another length or another character. */
static inline bool scrl_passkey_from_digits(const uint8_t *text, size_t len, uint32_t *out) {
    if (len != SCRL_PASSKEY_DIGITS) return false;
    uint32_t value = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10u + (uint32_t)(text[i] - '0');
    }
    *out = value;
    return true;
}
