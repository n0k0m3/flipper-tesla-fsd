/* Small getters and setters of the m2 state (stock 0xA8AC..0xB3E2). */
#include "m2.h"

/* Stock 0xAAA8. Returns the AP flag. */
uint32_t ap_active(void)
{
    return g_ap_active;
}

/* Stock 0xB3D8. Sets the gesture code. Stock leaves a0 unchanged. */
uint32_t gesture_code_set(uint32_t code)
{
    g_gesture_code = (uint8_t)code;
    return code;
}
