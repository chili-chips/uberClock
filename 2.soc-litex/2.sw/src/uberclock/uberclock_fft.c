// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_fft.c - shared FFT scratch buffers/sample rate, DS-FIFO FFT
// capture, bin/band power helpers, the Hann+log-power peak/vertex
// frequency estimator, and the fft64_peak/fft32_ds_y/fft_ds[_peak]
// command bodies.
//
// fft_in/fft_out/fft_cfg_mem/fft_fs_hz and the trackq_dbg_* diagnostics
// below are deliberately kept as plain file-scope statics rather than
// folded into a struct: 2.sw/tests/test_loopback.py extracts several of
// this file's functions verbatim (by regex) and recompiles them against
// its own mock globals of exactly these bare names. See fft_peak_debug()
// for the struct-based public accessor other subsystems should use.
#include <math.h>
#include <stdio.h>

#include "kiss_fft.h"
#include "uberclock/uberclock_config.h"
#include "uberclock/uberclock_fft.h"
#include "uberclock/uberclock_fifo.h"
#include "uberclock/uberclock_runtime.h"
#include "uberclock/uberclock_types.h"

/* Not all libc's define M_PI; keep our own constant rather than depend on it. */
#define TRACKQ_PI 3.14159265358979323846

static kiss_fft_cpx fft_in[UBERCLOCK_FFT_MAX_N];
static kiss_fft_cpx fft_out[UBERCLOCK_FFT_MAX_N];
static uint8_t fft_cfg_mem[UBERCLOCK_FFT_CFG_MAX_BYTES] __attribute__((aligned(8)));
static uint32_t fft_fs_hz = 10000u;

/* Diagnostics describe the current FFT peak search only, including on
 * failure (see trackq_fft_peak_vertex_estimate_mhz and fft_peak_debug,
 * below). */
static unsigned trackq_dbg_k_peak = 0u;
static int64_t trackq_dbg_y1 = 0, trackq_dbg_y2 = 0, trackq_dbg_y3 = 0, trackq_dbg_den = 0;

static inline int is_pow2_u(unsigned x) {
    return (x != 0u) && ((x & (x - 1u)) == 0u);
}

void fft_set_sample_rate_hz(uint32_t hz) {
    fft_fs_hz = hz;
}

uint32_t fft_get_sample_rate_hz(void) {
    return fft_fs_hz;
}

int capture_ds_fft_channel(unsigned channel, unsigned n, unsigned settle) {
    if (channel >= 6u || n > UBERCLOCK_FFT_MAX_N || !ds_capture_begin(settle)) return 0;

    for (unsigned i = 0; i < n; i++) {
        iq6_frame_t frame;
        int16_t sx;

        if (!track3_wait_ds_fifo("capture", i, n)) {
            return 0;
        }
        ds_fifo_read_frame(&frame);
        sx = frame.x[channel];

        fft_in[i].r = (kiss_fft_scalar)sx;
        fft_in[i].i = (kiss_fft_scalar)0;
        ce_service_background_budget(4);
    }

    return ds_capture_end();
}

/* File-local: only uberclock_fft_band_power and
 * trackq_fft_peak_vertex_estimate_mhz (both below, in this file) need the
 * per-bin power; everything else goes through uberclock_fft_band_power. */
static uint64_t uberclock_fft_bin_power(unsigned k) {
    int32_t re = (int32_t)fft_out[k].r;
    int32_t im = (int32_t)fft_out[k].i;
    return (uint64_t)((int64_t)re * re) + (uint64_t)((int64_t)im * im);
}

uint64_t uberclock_fft_band_power(unsigned k, unsigned half_bins, unsigned n) {
    uint64_t pwr = 0;
    unsigned start = (k > half_bins) ? (k - half_bins) : 0u;
    unsigned stop = k + half_bins;
    unsigned bins = n / 2u;

    if (stop >= bins)
        stop = bins - 1u;

    for (unsigned i = start; i <= stop; i++)
        pwr += uberclock_fft_bin_power(i);

    return pwr;
}

int32_t fft_get_input_sample(unsigned i) {
    return (int32_t)fft_in[i].r;
}

int fft_run(unsigned n, const char *caller_label) {
    size_t cfg_need = 0;
    size_t cfg_len;
    kiss_fft_cfg cfg;

    (void)kiss_fft_alloc((int)n, 0, NULL, &cfg_need);
    if (cfg_need > (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES) {
        printf("%s fft cfg too big: need %lu bytes (max %u)\n",
               caller_label, (unsigned long)cfg_need, UBERCLOCK_FFT_CFG_MAX_BYTES);
        return 0;
    }

    cfg_len = (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES;
    cfg = kiss_fft_alloc((int)n, 0, fft_cfg_mem, &cfg_len);
    if (!cfg) {
        printf("%s kiss_fft_alloc failed\n", caller_label);
        return 0;
    }

    kiss_fft(cfg, fft_in, fft_out);
    return 1;
}

struct uberclock_fft_peak_debug fft_peak_debug(void) {
    struct uberclock_fft_peak_debug dbg;
    dbg.k_peak = trackq_dbg_k_peak;
    dbg.y1 = trackq_dbg_y1;
    dbg.y2 = trackq_dbg_y2;
    dbg.y3 = trackq_dbg_y3;
    dbg.den = trackq_dbg_den;
    return dbg;
}

int trackq_fft_peak_vertex_estimate_mhz(const int16_t *samples,
                                         unsigned n,
                                         int64_t center_hz_milli_hint,
                                         unsigned half_width_hz,
                                         int64_t *vertex_hz_milli_out) {
    size_t cfg_need = 0;
    size_t cfg_len;
    kiss_fft_cfg cfg;
    unsigned bins;
    unsigned k_peak = 0u;
    uint64_t p_peak = 0u;
    unsigned k;
    unsigned k_lo, k_hi;
    int64_t x1, x2, x3;
    int64_t y1, y2, y3;
    int64_t den;

    trackq_dbg_k_peak = 0u;
    trackq_dbg_y1 = trackq_dbg_y2 = trackq_dbg_y3 = trackq_dbg_den = 0ll;
    if (vertex_hz_milli_out) *vertex_hz_milli_out = 0ll;

    if (!samples || !vertex_hz_milli_out || !is_pow2_u(n) || n < 8u || n > UBERCLOCK_FFT_MAX_N || fft_fs_hz == 0u)
        return 0;

    /* Hann window, computed on the fly for whatever N is in use (rather than
     * a fixed-size precomputed table) so N=64/256/1024 get the same coverage
     * as N=2048. Bench-verified (LoopbackRetest.md dumps, offline analysis):
     * combined with the log-power parabola below, this cuts interpolation
     * bias from ~0.28-0.31 bin down to ~0.01-0.02 bin, consistently across
     * N=64..2048 and at both mid-bin and bin-boundary operating points. */
    for (k = 0u; k < n; k++) {
        double w = 0.5 - 0.5 * cos((2.0 * TRACKQ_PI * (double)k) / (double)(n - 1));
        double windowed = (double)samples[k] * w;
        fft_in[k].r = (kiss_fft_scalar)windowed;
        fft_in[k].i = (kiss_fft_scalar)0;
    }

    (void)kiss_fft_alloc((int)n, 0, NULL, &cfg_need);
    if (cfg_need > (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES)
        return 0;

    cfg_len = (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES;
    cfg = kiss_fft_alloc((int)n, 0, fft_cfg_mem, &cfg_len);
    if (!cfg)
        return 0;

    kiss_fft(cfg, fft_in, fft_out);

    bins = n / 2u;

    /* Bound the search to a window around the expected frequency when a hint
     * is available (steady-state tracking, where the last estimate is a
     * strong prior); fall back to a full-spectrum search only when there is
     * no prior (first call after (re)acquisition). An unbounded search during
     * tracking lets any unrelated, momentarily-stronger spectral component
     * (image, harmonic, filter leakage) hijack the peak pick even though the
     * real tone has barely moved since the previous update. */
    k_lo = 1u;
    k_hi = bins - 2u;
    if (center_hz_milli_hint > 0) {
        int64_t lo_hz_milli = center_hz_milli_hint - ((int64_t)half_width_hz * 1000ll);
        int64_t hi_hz_milli = center_hz_milli_hint + ((int64_t)half_width_hz * 1000ll);
        int64_t k_lo_s = (lo_hz_milli * (int64_t)n) / ((int64_t)fft_fs_hz * 1000ll);
        /* Include both bins bracketing the upper edge, also for N=64. */
        int64_t k_hi_s = (hi_hz_milli * (int64_t)n + (int64_t)fft_fs_hz * 1000ll - 1ll) /
                         ((int64_t)fft_fs_hz * 1000ll);
        if (k_lo_s < 1) k_lo_s = 1;
        if (k_hi_s > (int64_t)(bins - 2u)) k_hi_s = (int64_t)(bins - 2u);
        if (k_lo_s <= k_hi_s) {
            k_lo = (unsigned)k_lo_s;
            k_hi = (unsigned)k_hi_s;
        }
    }

    for (k = k_lo; k <= k_hi; k++) {
        uint64_t pwr = uberclock_fft_bin_power(k);
        if (pwr > p_peak) {
            p_peak = pwr;
            k_peak = k;
        }
    }

    if (k_peak == 0u || (k_peak + 1u) >= bins)
        return 0;

    y1 = (int64_t)uberclock_fft_bin_power(k_peak - 1u);
    y2 = (int64_t)uberclock_fft_bin_power(k_peak);
    y3 = (int64_t)uberclock_fft_bin_power(k_peak + 1u);
    trackq_dbg_k_peak = k_peak;
    trackq_dbg_y1 = y1; trackq_dbg_y2 = y2; trackq_dbg_y3 = y3;
    den = y1 - (2ll * y2) + y3;
    trackq_dbg_den = den;
    /* Equal adjacent maxima at a half-bin position are valid; a flat triplet
     * is not. Requiring strict dominance rejects a legitimate half-bin tone.
     * y1,y3 > 0 is required below for log(); a real FFT bin power is only
     * ever exactly zero for a pathological all-zero capture. */
    if (y2 < y1 || y2 < y3 || den >= 0ll || y1 <= 0ll || y3 <= 0ll)
        return 0;

    x1 = (int64_t)((((uint64_t)(k_peak - 1u) * (uint64_t)fft_fs_hz * 1000ull) + ((uint64_t)n / 2ull)) / (uint64_t)n);
    x2 = (int64_t)((((uint64_t)k_peak * (uint64_t)fft_fs_hz * 1000ull) + ((uint64_t)n / 2ull)) / (uint64_t)n);
    x3 = (int64_t)((((uint64_t)(k_peak + 1u) * (uint64_t)fft_fs_hz * 1000ull) + ((uint64_t)n / 2ull)) / (uint64_t)n);

    {
        /* Fit the parabola to log(power), not raw power: a linear-power fit
         * assumes a mainlobe shape that neither a rectangular nor a Hann
         * window actually has, and is the confirmed source of the ~0.3-bin
         * bias measured before this change. log-power matches a windowed
         * mainlobe's real shape far better (bench-verified across N=64..2048
         * via LoopbackRetest.md's raw-dump analysis). */
        double ly1 = log((double)y1);
        double ly2 = log((double)y2);
        double ly3 = log((double)y3);
        double log_den = ly1 - 2.0 * ly2 + ly3;
        double log_num = (double)(x3 - x1) * (ly1 - ly3);
        /* Local-maximum geometry bounds the correction to half a bin. */
        int64_t half_bin_hz_milli = ((int64_t)fft_fs_hz * 1000ll) / (2ll * (int64_t)n);
        int64_t correction;
        if (!(log_den < 0.0))
            return 0;
        correction = (int64_t)(log_num / (4.0 * log_den));
        if (correction > half_bin_hz_milli)
            correction = half_bin_hz_milli;
        if (correction < -half_bin_hz_milli)
            correction = -half_bin_hz_milli;
        *vertex_hz_milli_out = x2 + correction;
    }
    return 1;
}

/* ========================================================================
 * Command-facing capture+run+print bodies.
 * ======================================================================== */

void fft_run_ch1_iq64_peak_and_print(void) {
    const unsigned n = 64u;

    if (!ds_capture_begin(UBERCLOCK_DS_CAPTURE_SETTLE)) return;
    for (unsigned i = 0; i < n; i++) {
        if (!track3_wait_ds_fifo("capture", i, n)) {
            printf("Not enough DS FIFO samples: got %u/%u\n", i, n);
            return;
        }

        iq6_frame_t frame;
        ds_fifo_read_frame(&frame);
        int16_t sx = frame.x[0];
        int16_t sy = frame.y[0];

        fft_in[i].r = (kiss_fft_scalar)sx;
        fft_in[i].i = (kiss_fft_scalar)sy;
    }

    if (!ds_capture_end()) return;

    size_t cfg_need = 0;
    (void)kiss_fft_alloc((int)n, 0, NULL, &cfg_need);
    if (cfg_need > (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES) {
        printf("fft cfg too big: need %lu bytes (max %u)\n",
               (unsigned long)cfg_need, UBERCLOCK_FFT_CFG_MAX_BYTES);
        return;
    }

    size_t cfg_len = (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES;
    kiss_fft_cfg cfg = kiss_fft_alloc((int)n, 0, fft_cfg_mem, &cfg_len);
    if (!cfg) {
        puts("kiss_fft_alloc failed");
        return;
    }

    kiss_fft(cfg, fft_in, fft_out);

    uint64_t peak_pwr = 0;
    unsigned peak_k = 0;
    unsigned bins = (n / 2u);

    for (unsigned k = 1; k < bins; k++) { /* skip DC */
        int32_t re = (int32_t)fft_out[k].r;
        int32_t im = (int32_t)fft_out[k].i;
        uint64_t pwr = (uint64_t)((int64_t)re * re) + (uint64_t)((int64_t)im * im);

        if (pwr > peak_pwr) {
            peak_pwr = pwr;
            peak_k = k;
        }
    }

    {
        uint64_t f_hz = ((uint64_t)peak_k * (uint64_t)fft_fs_hz) / (uint64_t)n;
        printf("fft64 peak: bin=%u f=%llu Hz pwr=%llu (Fs=%lu, N=%u, df=%lu Hz)\n",
               peak_k,
               (unsigned long long)f_hz,
               (unsigned long long)peak_pwr,
               (unsigned long)fft_fs_hz,
               n,
               (unsigned long)(fft_fs_hz / n));
    }
}

void fft_run_ch1_real32_and_print(void) {
    const unsigned n = 32u;
    const uint32_t fs_hz = 10000u; /* ce_down rate */

    size_t cfg_need = 0;
    (void)kiss_fft_alloc((int)n, 0, NULL, &cfg_need);
    if (cfg_need > (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES) {
        printf("fft32 cfg too big: need %lu bytes\n", (unsigned long)cfg_need);
        return;
    }

    size_t cfg_len = (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES;
    kiss_fft_cfg cfg = kiss_fft_alloc((int)n, 0, fft_cfg_mem, &cfg_len);
    if (!cfg) {
        puts("kiss_fft_alloc failed");
        return;
    }

    if (!ds_capture_begin(UBERCLOCK_DS_CAPTURE_SETTLE)) return;
    for (unsigned i = 0; i < n; i++) {
        if (!track3_wait_ds_fifo("capture", i, n)) {
            printf("Not enough DS FIFO samples: got %u/%u\n", i, n);
            return;
        }

        iq6_frame_t frame;
        ds_fifo_read_frame(&frame);
        int16_t sy = frame.y[0];

        fft_in[i].r = (kiss_fft_scalar)sy;
        fft_in[i].i = (kiss_fft_scalar)0;
    }

    if (!ds_capture_end()) return;
    kiss_fft(cfg, fft_in, fft_out);

    puts("bin,freq_hz,re,im,pwr");

    uint64_t peak_pwr = 0;
    unsigned peak_k = 0;

    for (unsigned k = 0; k < (n / 2u); k++) {
        int32_t re = (int32_t)fft_out[k].r;
        int32_t im = (int32_t)fft_out[k].i;
        uint64_t pwr = (uint64_t)((int64_t)re * re) + (uint64_t)((int64_t)im * im);
        uint64_t f_hz = ((uint64_t)k * (uint64_t)fs_hz) / (uint64_t)n;

        printf("%2u,%5llu,%8ld,%8ld,%12llu\n",
               k,
               (unsigned long long)f_hz,
               (long)re,
               (long)im,
               (unsigned long long)pwr);

        if (k > 0u && pwr > peak_pwr) {
            peak_pwr = pwr;
            peak_k = k;
        }
    }

    {
        uint64_t peak_f_hz = ((uint64_t)peak_k * (uint64_t)fs_hz) / (uint64_t)n;
        printf("fft32_ds_y peak: bin=%u f=%llu Hz pwr=%llu  (Fs=%lu, N=%u, df=%lu Hz)\n",
               peak_k,
               (unsigned long long)peak_f_hz,
               (unsigned long long)peak_pwr,
               (unsigned long)fs_hz,
               n,
               (unsigned long)(fs_hz / n));
    }
}

int fft_run_ds_capture_and_print(unsigned n, int peak_only) {
    if (!is_pow2_u(n) || n < 8u || n > UBERCLOCK_FFT_MAX_N) {
        printf("Usage: fft_ds [N], N must be power-of-2 and <= %u\n", UBERCLOCK_FFT_MAX_N);
        return 0;
    }

    if (!ds_capture_begin(UBERCLOCK_DS_CAPTURE_SETTLE)) return 0;
    for (unsigned i = 0; i < n; i++) {
        if (!track3_wait_ds_fifo("capture", i, n)) {
            printf("Not enough DS FIFO samples: got %u/%u\n", i, n);
            return 0;
        }
        iq6_frame_t frame;
        ds_fifo_read_frame(&frame);
        int16_t sx = frame.x[0];
        int16_t sy = frame.y[0];
        fft_in[i].r = (kiss_fft_scalar)sx;
        fft_in[i].i = (kiss_fft_scalar)sy;
    }

    if (!ds_capture_end()) return 0;

    size_t cfg_need = 0;
    (void)kiss_fft_alloc((int)n, 0, NULL, &cfg_need);
    if (cfg_need > (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES) {
        printf("fft cfg too big: need %lu bytes (max %u)\n",
               (unsigned long)cfg_need, UBERCLOCK_FFT_CFG_MAX_BYTES);
        return 0;
    }

    size_t cfg_len = (size_t)UBERCLOCK_FFT_CFG_MAX_BYTES;
    kiss_fft_cfg cfg = kiss_fft_alloc((int)n, 0, fft_cfg_mem, &cfg_len);
    if (!cfg) {
        puts("kiss_fft_alloc failed (static cfg)");
        return 0;
    }

    kiss_fft(cfg, fft_in, fft_out);

    uint64_t peak_pwr = 0;
    unsigned peak_k = 0;
    unsigned bins = (n / 2u);
    for (unsigned k = 0; k < bins; k++) {
        int32_t re = (int32_t)fft_out[k].r;
        int32_t im = (int32_t)fft_out[k].i;
        uint64_t pwr = (uint64_t)(re * re) + (uint64_t)(im * im);
        if (!peak_only) {
            printf("bin[%4u] re=%7ld im=%7ld pwr=%10llu\n",
                   k, (long)re, (long)im, (unsigned long long)pwr);
        }
        if (k > 0u && pwr > peak_pwr) {
            peak_pwr = pwr;
            peak_k = k;
        }
    }

    if (bins > 1u) {
        uint64_t f_hz = ((uint64_t)peak_k * (uint64_t)fft_fs_hz) / (uint64_t)n;
        printf("peak: bin=%u f=%llu Hz (Fs=%lu, N=%u, pwr=%llu)\n",
               peak_k,
               (unsigned long long)f_hz,
               (unsigned long)fft_fs_hz,
               n,
               (unsigned long long)peak_pwr);
    }
    return 1;
}
