/* Tesla (platform 1) speed (0x257) and steering angle (0x129) handlers
   (stock 0xFF9A..0x1024C). They fire event codes (event_fire) when the value
   crosses thresholds. event_fire runs the actions that the user linked to
   an event code. */
#include "tsl6.h"
#include "m5.h"

extern uint16_t g_20004070;  /* u16: speed from 0x257 (0x1FF = no data, stored as 0) */
extern uint8_t g_20004072;   /* u8: 0x257 data[4] bit 1 */
extern uint8_t g_20004158;   /* u8: "above the speed limit" event state */
extern uint8_t g_20004159;   /* u8: "moving" event state */
extern uint8_t g_2000415a[6];/* u8[6]: speed band states, 5-10 .. 55-60 */
extern uint8_t g_20004160;   /* u8: steering angle band (0..3) */
extern int16_t g_20004164;   /* s16: steering angle from 0x129 */
extern uint8_t *g_cfg;       /* 0x20004074: config block */

/* Stock 0xFF9A. 0x257: speed = data[3] | (data[4] & 1) << 8. */
void can_257_speed(m5_frame_t *f)
{
    uint32_t v = ((f->data[4] & 1) << 8) | f->data[3];

    if (v == 0x1FF)
        v = 0;
    if (g_20004070 != v) {
        g_20004070 = (uint16_t)v;
        if (v == 0) {
            g_20004159 = 0;
            event_fire(0x8A);              /* stopped */
        } else if (g_20004159 == 0) {
            g_20004159 = 1;
            if (gear_get() == 4)
                event_fire(0x95);          /* starts to move */
        }
        range_edge(v, 5, 10, &g_2000415a[0], 0x8B);
        range_edge(v, 15, 20, &g_2000415a[1], 0x8C);
        range_edge(v, 25, 30, &g_2000415a[2], 0x8D);
        range_edge(v, 35, 40, &g_2000415a[3], 0x8E);
        range_edge(v, 45, 50, &g_2000415a[4], 0x8F);
        range_edge(v, 55, 60, &g_2000415a[5], 0x90);
        gauge_push();
        uint32_t lim = (uint8_t)(speed_limit_index() * 5 + 10);
        if (g_20004158 == 0) {
            if (v > lim && lim >= 60) {
                g_20004158 = 1;
                event_fire(0x9F);          /* above the limit */
            }
        } else if (v <= lim) {
            g_20004158 = 0;
            event_fire(0x9E);              /* back below the limit */
        }
    }
    uint8_t b = (f->data[4] >> 1) & 1;
    if (g_20004072 == b)
        return;
    g_20004072 = b;
    gauge_push();
}

/* Stock 0x100E0. */
M5_CALLED uint32_t speed_get(void)
{
    return g_20004070;
}

/* Stock 0x100E6. */
uint32_t flag_257_get(void)
{
    return g_20004072;
}

/* Stock 0x100EC. Fires the event only when car frames come in,
   0x2000BF60 is 4 and AP is off. */
M5_CALLED void steer_event(uint32_t snd)
{
    if (car_frames_seen() == 0)
        return;
    if (gear_get() != 4)
        return;
    if (ap_active() != 0)
        return;
    event_fire(snd);
}

/* Stock 0x1011E. 0x129: angle = ((data[3] & 0x3F) << 8 | data[2]) - 0x2000.
   Bands: |a| <= 60 is centre, > 200, > 450, > 900 fire events per side. */
void can_129_steer(m5_frame_t *f)
{
    g_20004164 = (int16_t)((((f->data[3] & 0x3F) << 8) | f->data[2]) + 0xE000);
    if ((int8_t)g_cfg[7] < 0 && speed_get() <= 30 &&
        (uint16_t)(g_20004164 + 249) > 498)
        cornering_light_on();

    /* The event call sits in each branch. Then a0 at the "no event" returns
       is the same as in stock (the test compares a0). */
    int32_t a = g_20004164;
    if ((uint16_t)(a + 60) <= 120) {
        if (g_20004160 == 0)
            return;
        g_20004160 = 0;
        steer_event(0xAA);
    } else if (g_20004160 == 0) {
        if (a < -200) {
            g_20004160 = 1;
            steer_event(0xAB);
        } else if (a > 200) {
            g_20004160 = 1;
            steer_event(0xAE);
        } else {
            return;
        }
    } else if (g_20004160 == 1) {
        if (a < -450) {
            g_20004160 = 2;
            steer_event(0xAC);
        } else if (a > 450) {
            g_20004160 = 2;
            steer_event(0xAF);
        } else {
            return;
        }
    }

    if (g_20004160 != 2)
        return;
    if (g_20004164 < -900) {
        g_20004160 = 3;
        steer_event(0xAD);
    } else if (g_20004164 > 900) {
        g_20004160 = 3;
        steer_event(0xB0);
    }
}

/* Stock 0x10234. Returns |angle| as u16. */
uint32_t steer_abs(void)
{
    int32_t a = g_20004164;
    return (uint16_t)(a < 0 ? -a : a);
}
