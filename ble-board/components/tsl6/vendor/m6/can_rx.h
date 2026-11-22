#ifndef CAN_RX_H
#define CAN_RX_H
#include "types.h"

#define CAN_RING_N 32

/* RX ring entry (12 bytes). id 0 marks a free entry. */
typedef struct {
    uint16_t id;
    uint8_t dlc;
    uint8_t data[8];
    uint8_t flag;
} can_rx_entry_t;

extern can_rx_entry_t can_rx_ring[CAN_RING_N];  /* 0x2000C054 */

void can_ring_push(uint32_t id, void *data, uint32_t dlc, uint32_t flag);
void can_rx0_isr(void);
#endif
