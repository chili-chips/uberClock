// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_RUNTIME_H
#define UBERCLOCK_RUNTIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Shared "heartbeat" state driven by the ce_down interrupt:
 *  elapsed ticks, pending (not-yet-serviced) CE events, and the most
 *  recently observed CORDIC magnitude/phase. Owned and updated by the
 *  top-level uberclock module (uberclock.c); other subsystems read it
 *  only through the accessor/service functions below. */
struct uberclock_runtime {
    volatile uint32_t ce_ticks;
    volatile uint32_t ce_event;
    int16_t magnitude;
    int32_t phase;
};

/** @brief Current ce_down tick count, free-running since uberclock_init(). */
uint32_t runtime_ticks(void);

/** @brief Most recent CORDIC magnitude sample, latched each serviced CE
 *  event. */
int16_t runtime_magnitude(void);

/** @brief Most recent CORDIC phase sample (currently unused by hardware,
 *  kept for parity with the pre-refactor global). */
int32_t runtime_phase(void);

/** @brief Service at most one pending CE event if one is already queued
 *  (pushes one software-generator sample, latches magnitude, re-arms the
 *  EVM); does nothing if the queue is empty (non-blocking). This is the
 *  single place "CE background work" happens outside uberclock_poll();
 *  FIFO/track code call it while they block on hardware, so long capture
 *  loops don't starve the signal generator. */
void ce_service_background(void);

/** @brief Service up to @p budget pending CE events, non-blocking. */
void ce_service_background_budget(unsigned budget);

/** @brief Busy-wait, servicing background CE events, until @p wait_ticks
 *  ce_down ticks have elapsed. */
void ce_wait_ticks(uint32_t wait_ticks);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_RUNTIME_H */
