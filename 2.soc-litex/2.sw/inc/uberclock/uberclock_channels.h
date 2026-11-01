// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_CHANNELS_H
#define UBERCLOCK_CHANNELS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Set channel @p channel_index's downconversion phase increment
 *  (main_phase_inc_down_1..5). Out-of-range indices are a silent no-op.
 *  @param channel_index 0-based channel, 0 .. UBERCLOCK_CHANNEL_COUNT-1.
 *  @param phase_inc     26-bit NCO phase increment. */
void uberclock_channel_set_phase_down(unsigned channel_index, uint32_t phase_inc);

/** @brief Read channel @p channel_index's downconversion phase increment.
 *  @return The phase increment, or 0 for an out-of-range index. */
uint32_t uberclock_channel_get_phase_down(unsigned channel_index);

/** @brief Set channel @p channel_index's CPU-NCO phase increment
 *  (main_phase_inc_cpu1..5). Out-of-range indices are a silent no-op. */
void uberclock_channel_set_phase_cpu(unsigned channel_index, uint32_t phase_inc);

/** @brief Set channel @p channel_index's CPU-NCO magnitude, signed
 *  12-bit (main_mag_cpu1..5). Out-of-range indices are a silent no-op. */
void uberclock_channel_set_magnitude_cpu(unsigned channel_index, int16_t magnitude);

/** @brief Set channel @p channel_index's gain register (main_gain1..5).
 *  Out-of-range indices are a silent no-op. */
void uberclock_channel_set_gain(unsigned channel_index, int32_t gain);

/** @brief Apply the historical per-channel power-on defaults (phase_down,
 *  phase_cpu, magnitude_cpu, gain) to every channel in one loop, for
 *  up to UBERCLOCK_CHANNEL_DEFAULTS_COUNT channels. Replaces the
 *  original's five near-identical init call sequences. */
void uberclock_channels_apply_default_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_CHANNELS_H */
