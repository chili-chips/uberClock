// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_SIGGEN_H
#define UBERCLOCK_SIGGEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One-time application of the historical per-channel 3-tone
 *  defaults (tone frequencies, amplitude, enabled mask). Call once from
 *  uberclock_init(), before uberclock_siggen_start(). */
void siggen_apply_default_init(void);

/** @brief Enable the software 3-tone generator on channels 1-3 (0-based
 *  0..2), disable channels 4-5, and reset all phase accumulators. */
void uberclock_siggen_start(void);

/** @brief Disable the generator on all channels and push one zero frame
 *  once the upsampler FIFO is writable, so downstream consumers see a
 *  clean stop rather than a stale last sample. */
void uberclock_siggen_stop(void);

/** @brief If a CE event is pending service, synthesize and push the next
 *  sample for every enabled channel into the upsampler FIFO. No-op if
 *  the generator is disabled or the FIFO is not currently writable. */
void uberclock_siggen_service_push(void);

/** @brief Set channel @p channel's three tone frequencies directly. */
void siggen_set_channel_freqs(unsigned channel, uint32_t f1_hz, uint32_t f2_hz, uint32_t f3_hz);

/** @brief Set channel @p channel's tones to {center-delta, center,
 *  center+delta} Hz. No-op if center_hz <= delta_hz. */
void siggen_set_channel_symmetric(unsigned channel, uint32_t center_hz, uint32_t delta_hz);

/** @brief Recompute every channel/tone's DDS phase increment from its
 *  configured frequency (fixed 10 kHz synth rate). Call after changing
 *  any tone frequency. */
void siggen_update_increments(void);

/** @brief Enable channel @p channel's generator output (and the
 *  generator overall, if it was fully stopped). */
void siggen_enable_channel(unsigned channel);

/** @brief Disable channel @p channel's generator output. If no channel
 *  remains enabled, stop the generator and push a zero frame; otherwise
 *  push an immediate updated frame so the FIFO reflects the change. */
void siggen_disable_channel(unsigned channel);

/** @brief Set the per-tone amplitude for every channel. */
void siggen_set_amplitude_all(int16_t amplitude);

/** @brief Set the per-tone amplitude for one channel. */
void siggen_set_amplitude(unsigned channel, int16_t amplitude);

/** @brief Compute a 32-bit DDS phase increment for @p f_hz at synth rate
 *  @p fs_hz. */
uint32_t siggen_phase_inc_from_hz(uint32_t f_hz, uint32_t fs_hz);

/** @brief Compute a 32-bit DDS phase increment for a milli-Hz frequency
 *  at synth rate @p fs_hz. Returns 0 for a non-positive frequency. */
uint32_t siggen_phase_inc_from_mhz(int64_t f_hz_milli, uint32_t fs_hz);

/** @brief Evaluate the generator's 256-entry sine LUT at a 32-bit DDS
 *  phase, Q15-ish signed output. Shared by the generator itself and by
 *  uberclock_track.c's software correlators. */
int16_t siggen_sine_u32(uint32_t phase);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_SIGGEN_H */
