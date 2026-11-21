/* Common header for the TSL6 runtime rewrite. */
#ifndef TSL6_H
#define TSL6_H
#include "types.h"
#include "tsl6_port.h"
#include "resident.h"

/* QingKe "fast" interrupt: the hardware saves and restores the caller-saved
   registers (HPE), so the handler needs no prologue and ends in mret. Write the
   body as a normal function and wrap it. */
#define WCH_FAST_ISR(name, body)                                   \
    void body(void);                                               \
    __attribute__((naked)) void name(void)                         \
    {                                                              \
        __asm__ volatile("jal ra, " #body "\n\tmret");             \
    }

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

#endif
