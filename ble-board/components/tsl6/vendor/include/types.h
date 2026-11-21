/* Basic types for the TSL6 runtime rewrite. */
#ifndef TYPES_H
#define TYPES_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    void *getClockValue;
    uint32_t ClockMaxCount;
    uint16_t ClockFrequency;
    uint16_t ClockAccuracy;
    uint8_t irqEnable;
} bleClockConfig_t;

typedef struct {
    uint32_t MEMAddr;
    uint16_t MEMLen;
    uint32_t SNVAddr;
    uint16_t SNVBlock;
    uint8_t SNVNum;
    uint8_t BufNumber;
    uint16_t BufMaxLen;
    uint8_t TxNumEvent;
    uint8_t RxNumEvent;
    uint8_t TxPower;
    uint8_t ConnectNumber;
    uint8_t WindowWidening;
    uint8_t WaitWindow;
    uint8_t MacAddr[6];
    uint16_t ClockFrequency;
    uint16_t ClockAccuracy;
    void *srandCB;
    void *idleCB;
    void *tsCB;
    void *rcCB;
    void *staCB;
    void *readFlashCB;
    void *writeFlashCB;
} bleConfig_t;

/* Register access by absolute address. */
#define REG8(a) (*(volatile uint8_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))

#endif
