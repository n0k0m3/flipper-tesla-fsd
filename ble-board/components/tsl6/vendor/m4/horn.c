/* Horn driver (stock 0xEA50..0xEDE0).
   The horn is bit 2 of data[0] of the 0x3C2 page-0 frame (frame slot 9).
   Each toggle sends a copy of the last car frame with that bit changed.
   Also the links between the horn and the high beam (config byte 8). */
#include "tsl6.h"
#include "m4.h"

/* Step times in ms of the three horn melodies (stock 0x19460, 0x19484, 0x194A8). */
static const uint16_t horn_melody1[17] = {
    0x0032, 0x0096, 0x0032, 0x0096, 0x0032, 0x015e, 0x0032, 0x0096,
    0x0032, 0x0096, 0x0032, 0x0190, 0x0096, 0x00fa, 0x0096, 0x00fa,
    0x0096,
};
static const uint16_t horn_melody2[17] = {
    0x0028, 0x0096, 0x0028, 0x0032, 0x0028, 0x0032, 0x00c8, 0x028a,
    0x00c8, 0x0190, 0x0028, 0x0064, 0x00c8, 0x00c8, 0x00c8, 0x00c8,
    0x00c8,
};
static const uint16_t horn_melody3[53] = {
    0x0028, 0x0096, 0x0028, 0x0096, 0x0028, 0x0096, 0x0028, 0x0096,
    0x0028, 0x0096, 0x00c8, 0x01c2, 0x0028, 0x0096, 0x0028, 0x0096,
    0x0028, 0x0096, 0x0028, 0x0096, 0x0028, 0x0096, 0x00c8, 0x01c2,
    0x0028, 0x0032, 0x0028, 0x0032, 0x0028, 0x0032, 0x0028, 0x0032,
    0x0028, 0x0032, 0x0028, 0x0032, 0x0028, 0x0032, 0x0028, 0x0032,
    0x0028, 0x0096, 0x0028, 0x0096, 0x0028, 0x0096, 0x0028, 0x0096,
    0x0096, 0x00fa, 0x0096, 0x00fa, 0x0096,
};

/* Stock 0xEA50. "High beam also sounds horn" (config byte 8 bit 6).
   A change of the high beam between 0 and 1 sets the horn to the same level.
   Other values are only stored. */
void high_beam_links_horn(uint32_t on)
{
    m4_frame_t buf;
    uint32_t last;

    if (!(g_cfg[8] & 0x40))
        return;
    last = g_2000412c;
    if (last == on)
        return;
    if ((last == 1 && on == 0) || (last == 0 && on == 1)) {
        bf33_set(on != 0);
        frame_slot_copy(1, 9, &buf);
        buf.data[0] = (buf.data[0] & ~4) | ((on & 1) << 2);
        if (buf.dlc)
            can_tx(&buf);
    }
    g_2000412c = (uint8_t)on;
}

/* Stock 0xEAC0. "Horn also flashes high beam" (config byte 8 bit 5).
   Horn on: high beam on if it was off. Horn off: high beam off if this
   function turned it on. Returns the stock a0 (no meaning). */
uint32_t horn_links_high_beam(uint32_t on)
{
    if (g_2000412e == on)
        return on;
    g_2000412e = (uint8_t)on;
    if (!(g_cfg[8] & 0x20))
        return on;
    if (on) {
        g_2000412d = (uint8_t)(highbeam_get() ^ 1);
        if (!g_2000412d)
            return 0;
        return highbeam_set(1);
    }
    if (g_2000412d)
        return highbeam_set(0);
    return on;
}

/* Stock 0xEB0C (also a timer callback). Inverts the horn level and sends the
   0x3C2 page-0 frame with it. */
void horn_toggle(uint8_t *arg)
{
    m4_frame_t buf;

    frame_slot_copy(1, 9, &buf);
    g_horn.level ^= 1;
    buf.data[0] = (buf.data[0] & ~4) | ((g_horn.level & 1) << 2);
    can_tx(&buf);
}

/* Stock 0xEB4A (timer callback). One melody step. */
void horn_melody_step(uint8_t *arg)
{
    horn_toggle(0);
    if (g_horn.idx < g_horn.len) {
        app_timer_start(horn_melody_step, 0, g_horn.pattern[g_horn.idx], 0);
    } else {
        g_horn.len = 0;
        *bf30_ptr() = 0;
    }
    g_horn.idx++;
}

/* Stock 0xEBB4. Stops a melody. Returns 1 when one was playing. */
uint32_t horn_melody_stop(void)
{
    uint8_t *playing = bf30_ptr();

    if (*playing == 0)
        return 0;
    *playing = 0;
    g_horn.idx = 0;
    g_horn.len = 0;
    app_timer_stop(horn_melody_step);
    g_horn.level = 1;
    horn_toggle(0);
    return 1;
}

/* Stock 0xEBFA (timer callback). One chirp: horn on for 11 ms (fast) or
   50 ms, then the next chirp after 100 or 200 ms.
   After the last chirp it runs once more with arg = 1 (a number in the
   pointer argument) to clear the busy flag. */
void horn_chirp_step(uint8_t *arg)
{
    uint32_t on_ms;

    if (arg) {
        *bf31_ptr() = 0;
        return;
    }
    g_horn.level = 0;
    horn_toggle(0);
    on_ms = g_horn.fast ? 11 : 50;
    app_timer_start(horn_toggle, 0, on_ms, 0);
    g_horn.count--;
    if (g_horn.count)
        app_timer_start(horn_chirp_step, 0, g_horn.fast ? 100 : 200, 0);
    else
        app_timer_start(horn_chirp_step, 0, on_ms, (uint8_t *)1);
}

/* Stock 0xEC86. */
uint8_t horn_level(void)
{
    return g_horn.level;
}

/* Stock 0xEC90. Starts melody n + 1 (1..3). A second call stops it. */
void horn_melody_start(uint32_t n)
{
    uint8_t *playing;

    if (horn_melody_stop())
        return;
    playing = bf30_ptr();
    *playing = (uint8_t)(n + 1);
    switch (*playing) {
    case 1:
        g_horn.pattern = horn_melody1;
        g_horn.len = 17;
        break;
    case 2:
        g_horn.pattern = horn_melody2;
        g_horn.len = 17;
        break;
    case 3:
        g_horn.pattern = horn_melody3;
        g_horn.len = 53;
        break;
    default:
        return;
    }
    g_horn.idx = 0;
    g_horn.level = 0;
    app_timer_start(horn_melody_step, 0, 20, 0);
}

/* Stock 0xED30. "Soft horn": count chirps, fast = short chirps. */
void horn_chirps_start(uint32_t count, uint32_t fast)
{
    g_horn.fast = (uint8_t)fast;
    horn_melody_stop();
    g_horn.count = (uint8_t)count;
    *bf31_ptr() = 1;
    app_timer_start(horn_chirp_step, 0, 20, 0);
}

/* Stock 0xED72. "Pulse horn": toggles the horn every 25, 32 or 40 ms
   (config byte 3 bits 7:6 = 1, other, 3). Off: horn off. */
void horn_pulse(uint32_t on)
{
    uint32_t ms;

    if (!on) {
        app_timer_stop(horn_toggle);
        g_horn.level = 1;
        horn_toggle(0);
        return;
    }
    ms = 25;
    if ((g_cfg[3] >> 6) != 1) {
        ms = 40;
        if ((g_cfg[3] >> 6) != 3)
            ms = 32;
    }
    app_timer_start(horn_toggle, 1, ms, 0);
}

/* Stock 0xEDCA. */
void horn_reset(void)
{
    tmos_memset(&g_horn, 0, sizeof(g_horn));
}
