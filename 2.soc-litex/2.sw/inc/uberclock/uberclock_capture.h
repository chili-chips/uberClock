// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_CAPTURE_H
#define UBERCLOCK_CAPTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Pulse the low-speed on-chip debug capture buffer's arm signal
 *  (0 -> 1 -> 0), committing each edge. */
void capture_arm_pulse(void);

/** @brief Whether the low-speed capture buffer has finished filling. */
unsigned capture_is_done(void);

/** @brief Read one sample from the low-speed capture buffer.
 *  @param idx Index, 0..2047. */
int16_t capture_read_sample(unsigned idx);

/** @brief Print every sample in the 2048-entry low-speed capture buffer
 *  as "idx,value" rows. No-op (prints a hint) if the capture isn't done. */
void capture_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_CAPTURE_H */
