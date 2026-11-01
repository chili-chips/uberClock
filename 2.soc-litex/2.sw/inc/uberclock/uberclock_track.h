// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_TRACK_H
#define UBERCLOCK_TRACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Build/window identifiers stamped into every trackq diagnostic line and
 * the top-level init banner. Keep these two macros in sync with reality
 * when the windowing/estimator in uberclock_fft.c changes - that is the
 * fix this refactor was branched to preserve; do not let it regress. */
#define TRACKQ_BUILD_ID "hann-logpower-v1"
#define TRACKQ_WINDOW "hann"

/* Optional-argument defaults shared with uberclock_commands.c's
 * track3/trackq_start/trackq_probe token parsing. */
#define TRACK3_DEFAULT_STEP_HZ    5u
#define TRACK3_DEFAULT_MAX_STEPS  400u
#define TRACK3_DEFAULT_N          2048u
#define TRACK3_DEFAULT_CENTER_HZ  1000u
#define TRACK3_DEFAULT_DELTA_HZ   10u

/** @brief Poll the trackq validation state machine: if any channel is
 *  due, capture one record and print its frequency-estimate diagnostic.
 *  Call once per uberclock_poll(). */
void track_poll(void);

/** @brief "track3" command body: sweep channel @p channel_index's
 *  downconversion phase increment from @p start_hz in @p step_hz steps,
 *  looking for the 3-tone {center-delta, center, center+delta} pattern.
 *  Prints per-step power/lock diagnostics itself. */
void track_sweep_lock(unsigned channel_index, uint32_t start_hz, uint32_t step_hz,
                       unsigned max_steps, unsigned n, uint32_t center_hz, uint32_t delta_hz);

/** @brief "trackq_start" command body: validate arguments, (re)configure
 *  the 3-tone generator and downconversion for channels 1-3, and arm
 *  measurement-only Y1 validation. Prints its own diagnostics/usage
 *  errors. */
void track_validate_start(uint32_t f1_hz, uint32_t f2_hz, uint32_t f3_hz, unsigned n,
                           uint32_t center_hz, uint32_t delta1_hz, uint32_t delta2_hz, uint32_t delta3_hz);

/** @brief "trackq_probe" command body: capture one snapshot on channel 1
 *  and print the {left,center,right} band-power vertex estimate. */
void track_probe(unsigned n, uint32_t center_hz, uint32_t delta_hz);

/** @brief "trackq_stop" command body: disable validation, keep the last
 *  raw capture available for track_dump(). */
void track_stop(void);

/** @brief "trackq_dump" command body: stop validation and print the
 *  last successful raw channel-1 Y capture. */
void track_dump(void);

/** @brief Historical per-channel default delta (Hz) for trackq_start's
 *  optional arguments. */
uint32_t track_default_delta_hz(unsigned channel);

/** @brief Whether any trackq channel is currently enabled; used to gate
 *  changes to the shared FFT sample rate while validation is running. */
int track_any_channel_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_TRACK_H */
