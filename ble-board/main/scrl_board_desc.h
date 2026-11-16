#pragma once
/*
 * scrl_board_desc.h — board identity inside the app image.
 *
 * ESP-IDF places the section .rodata_custom_desc directly after
 * esp_app_desc_t, at a fixed offset from the image start (see
 * docs/en/api-reference/system/app_image_format.rst). A BLE update reads it
 * from the new image and refuses an image for another board: the chip ID
 * alone does not tell an ATOM image from a DevKit image, and the pins differ.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SCRL_BOARD_DESC_MAGIC 0x4C524353u  // "SCRL", little-endian
#define SCRL_BOARD_DESC_VERSION 1u

struct ScrlBoardDesc {
    uint32_t magic;
    uint32_t version;
    char board[32];  // CONFIG_SCRL_BOARD_ID, NUL-terminated
    char can[16];    // SCRL_CAN_KIND
};

// Offset from the image start: image header (24) + first segment header (8)
// + esp_app_desc_t (256).
#define SCRL_BOARD_DESC_OFFSET (24u + 8u + 256u)

/** True when `img` (the bytes at SCRL_BOARD_DESC_OFFSET) names the same board as `self`. */
static inline bool scrl_board_desc_match(const ScrlBoardDesc &img, const ScrlBoardDesc &self) {
    if (img.magic != SCRL_BOARD_DESC_MAGIC || img.version != SCRL_BOARD_DESC_VERSION) return false;
    if (memchr(img.board, '\0', sizeof(img.board)) == nullptr) return false;
    return strncmp(img.board, self.board, sizeof(img.board)) == 0;
}

extern const ScrlBoardDesc scrl_board_desc;
