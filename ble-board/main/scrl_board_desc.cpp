/*
 * scrl_board_desc.cpp — the board identity of this image. See scrl_board_desc.h.
 */

#include "scrl_board_desc.h"

#include "esp_app_desc.h"
#include "esp_app_format.h"

#include "scrl_board.h"

static_assert(SCRL_BOARD_DESC_OFFSET ==
                  sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) +
                      sizeof(esp_app_desc_t),
              "board descriptor offset");
static_assert(sizeof(CONFIG_SCRL_BOARD_ID) <= sizeof(ScrlBoardDesc::board), "board ID too long");

// External linkage comes from the declaration in the header.
__attribute__((used, section(".rodata_custom_desc")))
const ScrlBoardDesc scrl_board_desc = {
    SCRL_BOARD_DESC_MAGIC,
    SCRL_BOARD_DESC_VERSION,
    CONFIG_SCRL_BOARD_ID,
    SCRL_CAN_KIND,
};
