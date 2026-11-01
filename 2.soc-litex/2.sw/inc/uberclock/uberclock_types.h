// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_TYPES_H
#define UBERCLOCK_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One replicated/independent 5-channel I/Q frame as pushed into
 *  the upsampler write FIFO (main_ups_fifo_*). */
typedef struct {
    int16_t x[5];
    int16_t y[5];
} iq5_frame_t;

/** @brief One 6-"channel" I/Q frame as popped from the downsample read
 *  FIFO (main_ds_fifo_*): 5 tracked channels plus the reference channel
 *  in slot [5]. */
typedef struct {
    int16_t x[6];
    int16_t y[6];
} iq6_frame_t;

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_TYPES_H */
