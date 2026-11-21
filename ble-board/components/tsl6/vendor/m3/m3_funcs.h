/* Prototypes of the m3 functions (stock 0xC05A..0xDB8E). */
#ifndef M3_FUNCS_H
#define M3_FUNCS_H

/* Stock calls these m3 functions; it does not inline them. Keep them as
   calls, so that the registers at return (a0) are the same as in stock. */
#define M3_CALL __attribute__((noinline))

/* swc_state.c */
M3_CALL uint8_t swc_mode32_get(void);
void swc_modes_off(void);
void swc_edit50_start(void);
void swc_state_clear(void);

/* can_3c3_3e2.c */
void can_3e2_rx(m3_frame_t *f);
uint32_t can_3e2_mode_is1(void);
void can_3c3_rx(m3_frame_t *f);
void can_3c3_edit(m3_frame_t *f);
void can_3c3_3e2_clear(void);

/* can_679.c */
M3_CALL void can_679_edit(m3_frame_t *f);
void can_679_set(void *cfg);
void can_679_clear(void);

/* left_stalk.c */
M3_CALL void flash_set(uint32_t on);
M3_CALL uint32_t flash_step(uint8_t *dirty);
void can_249_rx(m3_frame_t *f);
void can_249_edit(m3_frame_t *f);
M3_CALL void beam_request(void);
void flash_start3(void);
void highbeam_set(uint8_t on);
M3_CALL uint8_t highbeam_get(void);
M3_CALL void flash_mode_set(uint32_t on, uint32_t beep);
M3_CALL void flash_cancel(void);
void highbeam_toggle(void);
M3_CALL uint8_t flash_active_get(void);
void lstalk_inject(uint8_t code, uint8_t frames);
void turn_signal(uint32_t full, uint32_t dir);
void lstalk_button(void);
void lstalk_clear(void);

/* right_stalk.c */
void can_229_rx(m3_frame_t *f);
void can_229_edit(m3_frame_t *f);
void can_118_rx(m3_frame_t *f);
uint8_t accel_pedal_get(void);
M3_CALL uint8_t gear_get(void);
M3_CALL void rstalk_request(uint8_t row, uint8_t count, uint8_t every);
void gear_request(uint32_t gear);
void rstalk_clear(void);
void park_timer_cb(uint8_t *arg);
void park_timer_set(uint32_t on);

/* tpms_vin.c */
void can_25a_tpms(m3_frame_t *f);
void can_31f_tpms(m3_frame_t *f);
uint8_t *tpms_get(void);
void tpms_clear(void);
uint8_t *vin_get(void);
void can_405_vin(m3_frame_t *f);
void vin_clear(void);

/* battery.c */
void can_082_heat(m3_frame_t *f);
M3_CALL void heat_request_set(uint8_t on);
uint32_t heat_request_get(void);
uint32_t heat_status_get(void);
void can_25d_cp(m3_frame_t *f);
void can_333_cp_edit(m3_frame_t *f);
void cp_request(uint32_t open);
uint32_t cp_state_get(void);
void can_332_bms(m3_frame_t *f);
uint16_t brick_v_get(void);
void can_312_pack_temp(m3_frame_t *f);
uint16_t pack_temp_get(void);
void can_33a_range(m3_frame_t *f);
uint8_t soc_get(void);
uint16_t range_get(void);
void battery_clear(void);

/* brake.c */
M3_CALL void brake_event_update(void);
void can_39d_brake(m3_frame_t *f);
uint8_t brake_ev_get(void);
void brake_clear(void);
void can_3fe_brake_temp(m3_frame_t *f);
uint32_t *brake_temp_get(void);
void brake_temp_clear(void);

/* doors.c */
M3_CALL uint32_t doors_all_closed(void);
void door_cb_a(uint8_t *arg);
void door_cb_b(uint8_t *arg);
void door_cb_c(uint8_t *arg);
void door_cb_d(uint8_t *arg);
M3_CALL void can_3b3_send_bit(uint32_t sel);
M3_CALL void door_cmd_run(uint8_t *arg);
uint32_t hold_check(uint32_t secs, uint32_t cond, uint8_t *st, uint32_t *t0);
M3_CALL uint32_t door_field_release(m3_timer_cb_t cb);
M3_CALL uint32_t door_field_value(uint32_t st, m3_timer_cb_t cb);
uint32_t door_field_timer(uint32_t unused, m3_timer_cb_t cb);
M3_CALL void door_update(uint8_t *raw, uint8_t *gauge, uint32_t v);
void door_output_set(uint32_t on);
void can_2e1_frunk(m3_frame_t *f);
void can_103_doors(m3_frame_t *f);
void can_1f9_door_edit(m3_frame_t *f);
void door_cmd_schedule(uint32_t ms);

#endif
