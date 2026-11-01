// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_CONFIG_H
#define UBERCLOCK_CONFIG_H

/** @file uberclock_config.h
 *  @brief Compile-time configuration constants shared across the UberClock
 *  subsystems.
 *
 *  Everything in this header is a preprocessor constant, not runtime state:
 *  it can be overridden with -D on the compiler command line (see the
 *  project Makefile) without editing source.
 */

/** @brief Number of logical downconversion/CPU-NCO/gain channels the
 *  indexed channel API (uberclock_channels.c) manages, 0 .. N-1.
 *
 *  The generated CSRs backing these channels (main_phase_inc_down_1..5,
 *  main_phase_inc_cpu1..5, main_mag_cpu1..5, main_gain1..5) are fixed at
 *  5 physical register banks by the gateware build this firmware targets.
 *  Raising UBERCLOCK_CHANNEL_COUNT beyond 5 without adding matching CSRs
 *  (and matching switch cases in uberclock_channels.c) simply makes the
 *  extra channel indices silent no-ops, mirroring how the original
 *  per-channel switch statements already behaved for out-of-range indices.
 *  Lowering it is always safe: it just stops exposing the higher indices.
 */
#ifndef UBERCLOCK_CHANNEL_COUNT
#define UBERCLOCK_CHANNEL_COUNT 5u
#endif

/** @brief Number of channel-indexed default-init table entries that are
 *  actually populated with historical defaults; see
 *  uberclock_channels_apply_default_init() and uberclock_siggen's default
 *  tables. Kept separate from UBERCLOCK_CHANNEL_COUNT so a smaller channel
 *  count never causes an "excess elements in initializer" build failure. */
#define UBERCLOCK_CHANNEL_DEFAULTS_COUNT 5u

/** @brief Largest FFT length (in samples) any UberClock subsystem will
 *  request from KISS FFT. Drives the static sizing of fft_in/fft_out. */
#define UBERCLOCK_FFT_MAX_N 2048u

/** @brief Static byte budget handed to kiss_fft_alloc() for its twiddle/
 *  config blob. Large enough for UBERCLOCK_FFT_MAX_N-point real/complex
 *  transforms without any dynamic allocation. */
#define UBERCLOCK_FFT_CFG_MAX_BYTES 12288u

/** @brief Number of simultaneous tones the software 3-tone generator
 *  synthesizes per channel. */
#define UBERCLOCK_SIGGEN_TONE_COUNT 3u

/** @brief Number of quadrature tracking loops uberclock_track.c runs
 *  (one per RF input currently fed to the 3-tone validation setup). */
#define UBERCLOCK_TRACKQ_CHANNEL_COUNT 3u

/** @brief Physical RF sample rate (Hz) the hardware downconversion NCOs
 *  (phase_inc_down_*, phase_inc_nco, phase_inc_down_ref) run at. Used
 *  throughout uberclock_hw.c's phase<->Hz conversions and as the default
 *  conversion base for channel initialization. */
#define UBERCLOCK_TRACK3_RF_FS_HZ 65000000u

/** @brief Default settle time (samples) drained after arming a DS FIFO
 *  capture before trusting the data, used by every capture path. */
#define UBERCLOCK_DS_CAPTURE_SETTLE 256u

#endif /* UBERCLOCK_CONFIG_H */
