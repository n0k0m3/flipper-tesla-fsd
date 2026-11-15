#pragma once
/*
 * scrl_logbuf.h — the event log of the board: the ring from scrl_log.h behind
 * a lock, with an uptime stamp on each line. The BLE Log characteristic reads
 * it. The housekeeping task copies new lines to the USB console: scrl_log()
 * itself never touches the console, so the CAN task cannot wait on USB.
 */

#include <stddef.h>
#include <stdint.h>

void scrl_logbuf_init(void);

/** Add one line: "<uptime s>.<ms> <text>". Safe from any task, not from an interrupt. */
void scrl_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/** Number of the next line that scrl_log() will write. */
uint32_t scrl_log_next_seq(void);

/** Number of the oldest line that the ring still holds. */
uint32_t scrl_log_oldest_seq(void);

/** Copy line `seq` to out. Returns false when the ring does not hold it. */
bool scrl_log_read(uint32_t seq, char *out, size_t size);
