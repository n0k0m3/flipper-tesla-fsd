# TSL6 feature code import

Source: tsl6-firmware `911bf5c`, `fw/src` and `fw/include`.
Made by `tools/tsl6-import.py`. Do not edit `vendor/` or `gen/` by hand.

Globals: 273 names in 3 regions, 3228 bytes. Names without a stock RAM address (functions or not used): 31.

Patches:
- `include/tsl6.h`: #include "ch32v20x.h"
- `m1/m1.h`: #include "ch32v20x.h"
- `m1/m1.h`: gpio_init_pin(GPIO_TypeDef *port,
- `m5/m5.h`: void memset(void *dst, uint32_t val, uint32_t len);
- `include/resident.h`: #define RESIDENT_SLOT(off) (*(void *const *)(0x40000u + (off)))
- `m3/m3.h`: register uint32_t a0_ __asm__("a0") = (uint32_t)(v);    \
- `m5/m5.h`: register uint32_t a0_ __asm__("a0") = (uint32_t)(v);     \
- `m1/epas.c`: c[10] = frame_checksum_last(c);
- `m4/keys.c`: if (key == 2) {
- `m8/light.c`: `event_fire` in plain C (no pinned registers)
- `m1/main_helpers.c`: fixed_mode_get, turn_state_get, epas_torque_mode_get, bus_age_clear, bus_alive, range_edge, car_frames_seen, platform_get, frame_slot_copy, tick_is_on from `m1/main.c`
