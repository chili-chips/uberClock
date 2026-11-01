// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_FFT_H
#define UBERCLOCK_FFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Diagnostics describing the most recent call to
 *  trackq_fft_peak_vertex_estimate_mhz(), valid on both success and
 *  failure (so callers can log why a fit was rejected). */
struct uberclock_fft_peak_debug {
    unsigned k_peak; /**< Bin index of the located peak (0 if none). */
    int64_t y1;      /**< Power one bin below the peak. */
    int64_t y2;       /**< Power at the peak bin. */
    int64_t y3;       /**< Power one bin above the peak. */
    int64_t den;      /**< Parabola denominator used for the fit. */
};

/** @brief Set the DS sample rate (Hz) used for all fft_ds/fft_fs-derived
 *  frequency printouts and for the trackq quadratic-vertex estimator. */
void fft_set_sample_rate_hz(uint32_t hz);

/** @brief Current DS sample rate (Hz), 0 until fft_set_sample_rate_hz()
 *  has been called at least once. */
uint32_t fft_get_sample_rate_hz(void);

/** @brief Capture @p n real samples (x of @p channel, imaginary = 0)
 *  from the DS FIFO into the shared FFT input buffer, bracketed by
 *  ds_capture_begin()/ds_capture_end().
 *  @param channel DS FIFO channel index, 0..5 (5 = reference channel).
 *  @param n       Sample count, must be <= UBERCLOCK_FFT_MAX_N.
 *  @param settle  Samples to drain before trusting the capture.
 *  @return 1 on success, 0 on failure (diagnostic already printed). */
int capture_ds_fft_channel(unsigned channel, unsigned n, unsigned settle);

/** @brief Power of FFT output bin @p k (|re|^2 + |im|^2), from the most
 *  recent fft_run()/trackq_fft_peak_vertex_estimate_mhz() result. */
uint64_t uberclock_fft_band_power(unsigned k, unsigned half_bins, unsigned n);

/** @brief Hann-windowed, log-power-parabola peak frequency estimator.
 *  Searches for the dominant spectral peak (optionally bounded to a
 *  window around @p center_hz_milli_hint) and interpolates a sub-bin
 *  vertex estimate from it.
 *  @param samples               Real input samples (length @p n).
 *  @param n                     Sample count; must be a power of two,
 *                                8 <= n <= UBERCLOCK_FFT_MAX_N.
 *  @param center_hz_milli_hint  Prior frequency estimate (milli-Hz) to
 *                                bound the search around, or <= 0 for an
 *                                unbounded full-spectrum search.
 *  @param half_width_hz         Search half-width (Hz) around the hint.
 *  @param vertex_hz_milli_out   Receives the interpolated estimate
 *                                (milli-Hz) on success.
 *  @return 1 on success, 0 if no valid peak/vertex was found (debug
 *          fields still updated; see fft_peak_debug()). */
int trackq_fft_peak_vertex_estimate_mhz(const int16_t *samples, unsigned n,
                                         int64_t center_hz_milli_hint,
                                         unsigned half_width_hz,
                                         int64_t *vertex_hz_milli_out);

/** @brief Diagnostics from the most recent
 *  trackq_fft_peak_vertex_estimate_mhz() call. */
struct uberclock_fft_peak_debug fft_peak_debug(void);

/** @brief Read back one real sample from the shared FFT input buffer, as
 *  filled by capture_ds_fft_channel(). Lets uberclock_track.c's
 *  direct-form (Goertzel-style) correlators reuse a capture without
 *  needing their own copy or running an actual FFT. */
int32_t fft_get_input_sample(unsigned i);

/** @brief Run KISS FFT over the shared input buffer (as filled by
 *  capture_ds_fft_channel()) into the shared output buffer, allocating a
 *  plan sized for @p n. On failure, prints "<caller_label> fft cfg too
 *  big: need N bytes (max M)" or "<caller_label> kiss_fft_alloc failed".
 *  @return 1 on success, 0 on failure. */
int fft_run(unsigned n, const char *caller_label);

/** @brief "fft64_peak" command body: capture 64 complex (I=x, Q=y)
 *  channel-1 DS samples, run the FFT, and print only the peak bin. */
void fft_run_ch1_iq64_peak_and_print(void);

/** @brief "fft32_ds_y" command body: capture 32 real (y-only) channel-1
 *  DS samples at a fixed 10 kHz rate, run the FFT, and print every bin
 *  plus the peak summary. */
void fft_run_ch1_real32_and_print(void);

/** @brief "fft_ds"/"fft_ds_peak" command body: capture @p n real+imag
 *  (x,y) channel-1 DS samples at the configured sample rate, run the
 *  FFT, and print either every bin or only the peak.
 *  @return 1 on success, 0 on failure (diagnostic already printed). */
int fft_run_ds_capture_and_print(unsigned n, int peak_only);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_FFT_H */
