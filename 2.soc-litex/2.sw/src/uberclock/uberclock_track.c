// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_track.c - track3 phase sweep/lock and trackq measurement-only
// validation: capture, triplet/band-power estimation, and the polling
// state machine.
//
// NOTE ON REMOVED DEAD CODE: the pre-refactor uberclock.c carried an
// "ORIGINAL multi-mode/ref tracking path kept commented for easy
// restore" block inside trackq_step() (~140 lines), guarded by an
// unconditional `return;` one statement above it - so it was already
// unreachable even if un-commented. Its only callees
// (trackq_vertex_confident, trackq_side_error_mhz, trackq_bin_vertex_
// estimate_mhz, trackq_clamp_step_hz/weak_step_hz, track_power_at_hz_
// samples/track_power_at_mhz_samples, track_band_power_at_hz_samples,
// trackq_center_tone_hz_milli) had no other callers and are dropped for
// the same reason. None of this is reachable from any live command or
// the poll loop; recover it from git history (pre-refactor uberclock.c)
// if the multi-mode/ref-correction path is ever revived.
#include <stdio.h>
#include <stdlib.h>

#include "uberclock/uberclock_channels.h"
#include "uberclock/uberclock_config.h"
#include "uberclock/uberclock_fft.h"
#include "uberclock/uberclock_fifo.h"
#include "uberclock/uberclock_hw.h"
#include "uberclock/uberclock_runtime.h"
#include "uberclock/uberclock_siggen.h"
#include "uberclock/uberclock_track.h"

#define TRACKQ_CH1_DELTA_HZ 10u
#define TRACKQ_CH2_DELTA_HZ 30u
#define TRACKQ_CH3_DELTA_HZ 30u

/* TRACK3_DEFAULT_STEP_HZ/MAX_STEPS/N/CENTER_HZ/DELTA_HZ come from
 * uberclock_track.h (shared with uberclock_commands.c's token parsing). */
#define TRACK3_DEFAULT_SETTLE     UBERCLOCK_DS_CAPTURE_SETTLE
#define TRACK3_DEFAULT_BAND_BINS  1u

#define TRACKQ_INTERVAL_TICKS  10000u
#define TRACKQ_CORR_SHIFT      10u
#define TRACKQ_SERVICE_STRIDE  16u

#define TRACK3_SIDE_MIN_PCT      2u
#define TRACK3_SIDE_MAX_PCT      95u
#define TRACK3_SIDE_BALANCE_PCT  40u

/* Trivial, intentionally duplicated here rather than exported from
 * uberclock_fft.c: that copy must stay `static` so the regression test's
 * text-extraction harness keeps matching it. */
static inline int is_pow2_u(unsigned x) {
    return (x != 0u) && ((x & (x - 1u)) == 0u);
}

/** @brief Per-channel trackq validation loop state. */
struct trackq_channel_state {
    int enabled;
    unsigned n;
    unsigned settle;
    uint32_t center_hz;
    uint32_t delta_hz;
    uint32_t next_tick;
};

static struct trackq_channel_state trackq[UBERCLOCK_TRACKQ_CHANNEL_COUNT] = {
    {0, TRACK3_DEFAULT_N, TRACK3_DEFAULT_SETTLE, TRACK3_DEFAULT_CENTER_HZ, TRACKQ_CH1_DELTA_HZ, 0u},
    {0, TRACK3_DEFAULT_N, TRACK3_DEFAULT_SETTLE, TRACK3_DEFAULT_CENTER_HZ, TRACKQ_CH2_DELTA_HZ, 0u},
    {0, TRACK3_DEFAULT_N, TRACK3_DEFAULT_SETTLE, TRACK3_DEFAULT_CENTER_HZ, TRACKQ_CH3_DELTA_HZ, 0u},
};

static uint32_t trackq_log_iteration = 0u;
static uint32_t trackq_last_capture_tick;
static int trackq_have_capture_tick;
static unsigned trackq_dump_n;
static uint32_t trackq_capture_nco, trackq_capture_down, trackq_capture_input, trackq_capture_fs;

/* track_samples/track_samples_ref are deliberately plain file-scope
 * statics (not struct fields): test_loopback.py's capture_ds_track_multi
 * extraction references track_samples by this exact bare name. */
static int16_t track_samples[UBERCLOCK_TRACKQ_CHANNEL_COUNT][UBERCLOCK_FFT_MAX_N];
static int16_t track_samples_ref[UBERCLOCK_FFT_MAX_N];

uint32_t track_default_delta_hz(unsigned channel) {
    switch (channel) {
        case 0: return TRACKQ_CH1_DELTA_HZ;
        case 1: return TRACKQ_CH2_DELTA_HZ;
        case 2: return TRACKQ_CH3_DELTA_HZ;
        default: return TRACKQ_CH1_DELTA_HZ;
    }
}

int track_any_channel_enabled(void) {
    for (unsigned i = 0; i < UBERCLOCK_TRACKQ_CHANNEL_COUNT; i++)
        if (trackq[i].enabled)
            return 1;
    return 0;
}

static int capture_ds_track_multi(unsigned n, unsigned settle) {
    if (n > UBERCLOCK_FFT_MAX_N || !ds_capture_begin(settle)) return 0;

    for (unsigned i = 0; i < n; i++) {
        iq6_frame_t frame;

        if (!track3_wait_ds_fifo("capture", i, n))
            return 0;
        ds_fifo_read_frame(&frame);
        /* VALIDATION MODE: use downsampled_y1 for channel-1 frequency estimation.
         * Restore the original multi-channel/ref assignments below after the measurement-validation experiment.
         */
        track_samples[0][i] = frame.y[0];
        track_samples[1][i] = 0;
        track_samples[2][i] = 0;
        track_samples_ref[i] = 0;
        /*
        track_samples[0][i] = frame.x[0];
        track_samples[1][i] = frame.x[1];
        track_samples[2][i] = frame.x[2];
        track_samples_ref[i] = frame.y[5];
        */
        ce_service_background_budget(4);
    }

    return ds_capture_end();
}

/* Only track_probe and track_band_power_at_hz (both in this file) use
 * these; everything bin/vertex related otherwise goes through
 * uberclock_fft.c's trackq_fft_peak_vertex_estimate_mhz. */
static inline void trackq_service_background_sample(unsigned sample_idx) {
    /* Keep the software-fed upsampler moving during long correlation sweeps. */
    if ((sample_idx & (TRACKQ_SERVICE_STRIDE - 1u)) == (TRACKQ_SERVICE_STRIDE - 1u))
        ce_service_background_budget(1u);
}

static uint64_t track_power_at_hz(uint32_t f_hz, unsigned n) {
    uint32_t phase_acc = 0u;
    uint32_t phase_inc = siggen_phase_inc_from_hz(f_hz, fft_get_sample_rate_hz());
    int64_t acc_i = 0;
    int64_t acc_q = 0;

    for (unsigned i = 0; i < n; i++) {
        int32_t sample = fft_get_input_sample(i);
        int32_t cos_q15 = (int32_t)siggen_sine_u32(phase_acc + 0x40000000u);
        int32_t sin_q15 = (int32_t)siggen_sine_u32(phase_acc);

        acc_i += ((int64_t)sample * (int64_t)cos_q15) >> TRACKQ_CORR_SHIFT;
        acc_q -= ((int64_t)sample * (int64_t)sin_q15) >> TRACKQ_CORR_SHIFT;
        phase_acc += phase_inc;
        trackq_service_background_sample(i);
    }

    return (uint64_t)(acc_i * acc_i) + (uint64_t)(acc_q * acc_q);
}

static uint64_t track_band_power_at_hz(uint32_t f_hz, unsigned n) {
    uint32_t df_hz;
    uint64_t center_pwr;
    uint64_t lower_pwr;
    uint64_t upper_pwr;

    center_pwr = track_power_at_hz(f_hz, n);
    if (fft_get_sample_rate_hz() == 0u || n == 0u) {
        return center_pwr;
    }

    df_hz = (uint32_t)((((uint64_t)fft_get_sample_rate_hz()) + (n / 2u)) / (uint64_t)n);
    if (df_hz == 0u) {
        return center_pwr;
    }

    lower_pwr = track_power_at_hz((f_hz > df_hz) ? (f_hz - df_hz) : 0u, n);
    upper_pwr = track_power_at_hz(f_hz + df_hz, n);

    /* Narrow 3-bin band: center + 0.5*(lower + upper). */
    return center_pwr + ((lower_pwr + upper_pwr) >> 1);
}

static int track3_triplet_match(uint64_t left_pwr, uint64_t center_pwr, uint64_t right_pwr) {
    uint64_t min_side;
    uint64_t max_side;

    if (center_pwr <= left_pwr || center_pwr <= right_pwr)
        return 0;
    if (left_pwr == 0u || right_pwr == 0u)
        return 0;

    min_side = (left_pwr < right_pwr) ? left_pwr : right_pwr;
    max_side = (left_pwr > right_pwr) ? left_pwr : right_pwr;

    if ((min_side * 100u) < (center_pwr * TRACK3_SIDE_MIN_PCT))
        return 0;
    if ((max_side * 100u) > (center_pwr * TRACK3_SIDE_MAX_PCT))
        return 0;
    if ((min_side * 100u) < (max_side * TRACK3_SIDE_BALANCE_PCT))
        return 0;

    return 1;
}

void track_sweep_lock(unsigned channel, uint32_t start_hz, uint32_t step_hz, unsigned max_steps,
                       unsigned n, uint32_t center_hz, uint32_t delta_hz) {
    unsigned band_bins = TRACK3_DEFAULT_BAND_BINS;
    unsigned settle = TRACK3_DEFAULT_SETTLE;
    uint32_t original_phase_inc;
    uint32_t sweep_hz;

    if (step_hz == 0u || max_steps == 0u) {
        puts("track3 requires step_hz > 0 and max_steps > 0");
        return;
    }
    if (!is_pow2_u(n) || n < 8u || n > UBERCLOCK_FFT_MAX_N) {
        printf("track3 requires N to be power-of-2 and <= %u\n", UBERCLOCK_FFT_MAX_N);
        return;
    }
    if (fft_get_sample_rate_hz() == 0u) {
        puts("track3 requires fft_fs > 0");
        return;
    }
    if (center_hz <= delta_hz) {
        puts("track3 requires center_hz > delta_hz");
        return;
    }

    siggen_set_channel_symmetric(channel, center_hz, delta_hz);
    siggen_update_increments();
    siggen_enable_channel(channel);

    original_phase_inc = uberclock_channel_get_phase_down(channel);
    sweep_hz = start_hz;
    printf("track3: ch=%u start=%lu Hz step=%lu Hz max_steps=%u N=%u center=%lu Hz delta=%lu Hz Fs=%lu Hz sig3={%lu,%lu,%lu} Hz\n",
           channel + 1u,
           (unsigned long)start_hz,
           (unsigned long)step_hz,
           max_steps,
           n,
           (unsigned long)center_hz,
           (unsigned long)delta_hz,
           (unsigned long)fft_get_sample_rate_hz(),
           (unsigned long)(center_hz - delta_hz),
           (unsigned long)center_hz,
           (unsigned long)(center_hz + delta_hz));

    for (unsigned step = 0; step < max_steps; step++) {
        uint32_t phase_inc = uberclock_phase_inc_from_hz(sweep_hz, UBERCLOCK_TRACK3_RF_FS_HZ);
        unsigned left_k;
        unsigned center_k;
        unsigned right_k;
        uint64_t left_pwr;
        uint64_t center_pwr;
        uint64_t right_pwr;

        uberclock_channel_set_phase_down(channel, phase_inc);
        uberclock_commit_config();

        if (!capture_ds_fft_channel(channel, n, settle) || !fft_run(n, "track3")) {
            uberclock_channel_set_phase_down(channel, original_phase_inc);
            uberclock_commit_config();
            return;
        }

        left_k   = (unsigned)((((uint64_t)(center_hz - delta_hz) * (uint64_t)n) + (fft_get_sample_rate_hz() / 2u)) / (uint64_t)fft_get_sample_rate_hz());
        center_k = (unsigned)((((uint64_t)center_hz * (uint64_t)n) + (fft_get_sample_rate_hz() / 2u)) / (uint64_t)fft_get_sample_rate_hz());
        right_k  = (unsigned)((((uint64_t)(center_hz + delta_hz) * (uint64_t)n) + (fft_get_sample_rate_hz() / 2u)) / (uint64_t)fft_get_sample_rate_hz());

        if (right_k >= (n / 2u)) {
            puts("track3 expected bins exceed FFT Nyquist range");
            uberclock_channel_set_phase_down(channel, original_phase_inc);
            uberclock_commit_config();
            return;
        }

        left_pwr = uberclock_fft_band_power(left_k, band_bins, n);
        center_pwr = uberclock_fft_band_power(center_k, band_bins, n);
        right_pwr = uberclock_fft_band_power(right_k, band_bins, n);

        printf("track3 step=%u phase_down_%u=%lu Hz inc=%lu bins={%u,%u,%u} pwr={%llu,%llu,%llu}\n",
               step,
               channel + 1u,
               (unsigned long)sweep_hz,
               (unsigned long)phase_inc,
               left_k,
               center_k,
               right_k,
               (unsigned long long)left_pwr,
               (unsigned long long)center_pwr,
               (unsigned long long)right_pwr);

        if (track3_triplet_match(left_pwr, center_pwr, right_pwr)) {
            printf("track3 lock: phase_down_%u=%lu Hz inc=%lu center=%lu left=%lu right=%lu\n",
                   channel + 1u,
                   (unsigned long)sweep_hz,
                   (unsigned long)phase_inc,
                   (unsigned long)center_hz,
                   (unsigned long)(center_hz - delta_hz),
                   (unsigned long)(center_hz + delta_hz));
            return;
        }

        ce_wait_ticks(TRACKQ_INTERVAL_TICKS);
        sweep_hz += step_hz;
    }

    uberclock_channel_set_phase_down(channel, original_phase_inc);
    uberclock_commit_config();
    printf("track3 no lock found in %u steps; restored phase_down_%u=%lu Hz\n",
           max_steps,
           channel + 1u,
           (unsigned long)uberclock_phase_inc_to_hz(original_phase_inc, UBERCLOCK_TRACK3_RF_FS_HZ));
}

void track_validate_start(uint32_t f1_hz, uint32_t f2_hz, uint32_t f3_hz, unsigned n,
                           uint32_t center_hz, uint32_t delta1_hz, uint32_t delta2_hz, uint32_t delta3_hz) {
    if (!is_pow2_u(n) || n < 8u || n > UBERCLOCK_FFT_MAX_N) {
        printf("trackq_start requires N to be power-of-2 and <= %u\n", UBERCLOCK_FFT_MAX_N);
        return;
    }
    if (delta1_hz == 0u || delta2_hz == 0u || delta3_hz == 0u) {
        puts("trackq_start requires delta_hz > 0");
        return;
    }
    if (center_hz <= delta1_hz || center_hz <= delta2_hz || center_hz <= delta3_hz) {
        puts("trackq_start requires center_hz > delta_hz for all tracked channels");
        return;
    }
    if (fft_get_sample_rate_hz() != 10000u) {
        puts("trackq_start requires fft_fs 10000 (physical downsample rate)");
        return;
    }
    if (f1_hz >= UBERCLOCK_TRACK3_RF_FS_HZ || f2_hz >= UBERCLOCK_TRACK3_RF_FS_HZ || f3_hz >= UBERCLOCK_TRACK3_RF_FS_HZ) {
        puts("trackq_start frequencies must be below 65000000 Hz; 0 preserves a tuning word");
        return;
    }

    siggen_set_channel_symmetric(0u, center_hz, delta1_hz);
    siggen_set_channel_symmetric(1u, center_hz, delta2_hz);
    siggen_set_channel_symmetric(2u, center_hz, delta3_hz);
    siggen_update_increments();
    siggen_enable_channel(0u);
    siggen_enable_channel(1u);
    siggen_enable_channel(2u);

    if (f1_hz) uberclock_channel_set_phase_down(0, uberclock_phase_inc_from_hz(f1_hz, UBERCLOCK_TRACK3_RF_FS_HZ));
    if (f2_hz) uberclock_channel_set_phase_down(1, uberclock_phase_inc_from_hz(f2_hz, UBERCLOCK_TRACK3_RF_FS_HZ));
    if (f3_hz) uberclock_channel_set_phase_down(2, uberclock_phase_inc_from_hz(f3_hz, UBERCLOCK_TRACK3_RF_FS_HZ));
    if (f1_hz || f2_hz || f3_hz)
        uberclock_commit_config();

    trackq_have_capture_tick = 0;
    trackq_dump_n = 0u;
    trackq_log_iteration = 0u;
    printf("trackq config: build=" TRACKQ_BUILD_ID " compiled=" __DATE__ " " __TIME__
           " mode=validate-ch1-y window=" TRACKQ_WINDOW " estimator=power-parabola Fs=%lu N=%u input=%lu pnco=%lu pdown=%lu\n",
           (unsigned long)fft_get_sample_rate_hz(), n, (unsigned long)hw_get_input_select(),
           (unsigned long)hw_get_phase_nco(), (unsigned long)uberclock_channel_get_phase_down(0));

    /* VALIDATION MODE: enable only channel 1 tracking state. */
    for (unsigned i = 0; i < UBERCLOCK_TRACKQ_CHANNEL_COUNT; i++) {
        trackq[i].enabled = (i == 0u) ? 1u : 0u;
        trackq[i].n = n;
        trackq[i].settle = TRACK3_DEFAULT_SETTLE;
        trackq[i].center_hz = center_hz;
        switch (i) {
            case 0: trackq[i].delta_hz = delta1_hz; break;
            case 1: trackq[i].delta_hz = delta2_hz; break;
            default: trackq[i].delta_hz = delta3_hz; break;
        }
        trackq[i].next_tick = runtime_ticks() + TRACKQ_INTERVAL_TICKS;
    }

    printf("trackq_start: ch1=%lu Hz ch2=%lu Hz ch3=%lu Hz N=%u center=%lu Hz delta={%lu,%lu,%lu} Hz sig3={{%lu,%lu,%lu},{%lu,%lu,%lu},{%lu,%lu,%lu}} Hz interval=1 s\n",
           (unsigned long)uberclock_phase_inc_to_hz(uberclock_channel_get_phase_down(0), UBERCLOCK_TRACK3_RF_FS_HZ),
           (unsigned long)uberclock_phase_inc_to_hz(uberclock_channel_get_phase_down(1), UBERCLOCK_TRACK3_RF_FS_HZ),
           (unsigned long)uberclock_phase_inc_to_hz(uberclock_channel_get_phase_down(2), UBERCLOCK_TRACK3_RF_FS_HZ),
           n,
           (unsigned long)center_hz,
           (unsigned long)trackq[0].delta_hz,
           (unsigned long)trackq[1].delta_hz,
           (unsigned long)trackq[2].delta_hz,
           (unsigned long)(center_hz - trackq[0].delta_hz),
           (unsigned long)center_hz,
           (unsigned long)(center_hz + trackq[0].delta_hz),
           (unsigned long)(center_hz - trackq[1].delta_hz),
           (unsigned long)center_hz,
           (unsigned long)(center_hz + trackq[1].delta_hz),
           (unsigned long)(center_hz - trackq[2].delta_hz),
           (unsigned long)center_hz,
           (unsigned long)(center_hz + trackq[2].delta_hz));
}

void track_probe(unsigned n, uint32_t center_hz, uint32_t delta_hz) {
    uint64_t left_pwr;
    uint64_t center_pwr;
    uint64_t right_pwr;
    int64_t num;
    int64_t den;
    int64_t center_hz_milli;
    int64_t h_hz_milli;
    int64_t vertex_hz_milli;

    if (!is_pow2_u(n) || n < 8u || n > UBERCLOCK_FFT_MAX_N) {
        printf("trackq_probe requires N to be power-of-2 and <= %u\n", UBERCLOCK_FFT_MAX_N);
        return;
    }
    if (center_hz <= delta_hz) {
        puts("trackq_probe requires center_hz > delta_hz");
        return;
    }
    if (fft_get_sample_rate_hz() == 0u) {
        puts("trackq_probe requires fft_fs > 0");
        return;
    }

    if (!capture_ds_fft_channel(0u, n, TRACK3_DEFAULT_SETTLE)) {
        puts("trackq_probe capture failed");
        return;
    }

    left_pwr = track_band_power_at_hz(center_hz - delta_hz, n);
    center_pwr = track_band_power_at_hz(center_hz, n);
    right_pwr = track_band_power_at_hz(center_hz + delta_hz, n);

    center_hz_milli = (int64_t)center_hz * 1000ll;
    vertex_hz_milli = center_hz_milli;
    if (track3_triplet_match(left_pwr, center_pwr, right_pwr)) {
        num = (int64_t)left_pwr - (int64_t)right_pwr;
        den = 2ll * ((int64_t)left_pwr - (2ll * (int64_t)center_pwr) + (int64_t)right_pwr);
        if (den < 0ll) {
            h_hz_milli = (int64_t)delta_hz * 1000ll;
            vertex_hz_milli = center_hz_milli + ((h_hz_milli * num) / den);
        }
    }

    printf("trackq_probe: phase_down_1=%lu Hz N=%u center=%lu Hz delta=%lu Hz pwr={%llu,%llu,%llu} vertex=%ld.%03ldHz\n",
           (unsigned long)uberclock_phase_inc_to_hz(uberclock_channel_get_phase_down(0), UBERCLOCK_TRACK3_RF_FS_HZ),
           n,
           (unsigned long)center_hz,
           (unsigned long)delta_hz,
           (unsigned long long)left_pwr,
           (unsigned long long)center_pwr,
           (unsigned long long)right_pwr,
           (long)(vertex_hz_milli / 1000ll),
           (long)labs(vertex_hz_milli % 1000ll));
}

void track_stop(void) {
    for (unsigned i = 0; i < UBERCLOCK_TRACKQ_CHANNEL_COUNT; i++)
        trackq[i].enabled = 0;
    trackq_have_capture_tick = 0;
    puts("trackq_stop: validation stopped; last raw capture retained for trackq_dump");
}

void track_dump(void) {
    if (!trackq_dump_n) {
        puts("trackq_dump: no successful capture available");
        return;
    }
    /* Command processing and acquisition share the foreground thread. Stop
     * acquisition so the dump is reproducible and the UART cannot mix logs. */
    for (unsigned i = 0; i < UBERCLOCK_TRACKQ_CHANNEL_COUNT; ++i) trackq[i].enabled = 0;
    printf("trackq_dump begin: build=" TRACKQ_BUILD_ID " N=%u Fs=%lu window=" TRACKQ_WINDOW
           " input=%lu pnco=%lu pdown=%lu\n", trackq_dump_n, (unsigned long)trackq_capture_fs,
           (unsigned long)trackq_capture_input, (unsigned long)trackq_capture_nco,
           (unsigned long)trackq_capture_down);
    puts("sample,y1");
    for (unsigned i = 0; i < trackq_dump_n; ++i) {
        printf("%u,%d\n", i, (int)track_samples[0][i]);
        ce_service_background_budget(4);
    }
    puts("trackq_dump end");
}

void track_poll(void) {
    unsigned capture_n = 0u;
    unsigned capture_settle = TRACK3_DEFAULT_SETTLE;
    int any_due = 0;
    int any_enabled = 0;

    for (unsigned i = 0; i < UBERCLOCK_TRACKQ_CHANNEL_COUNT; i++) {
        if (!trackq[i].enabled)
            continue;
        any_enabled = 1;
        if (fft_get_sample_rate_hz() == 0u) {
            puts("trackq stopped: fft_fs must be > 0");
            trackq[i].enabled = 0;
            continue;
        }
        if (trackq[i].center_hz <= trackq[i].delta_hz) {
            puts("trackq stopped: center_hz must exceed delta_hz");
            trackq[i].enabled = 0;
            continue;
        }
        if ((int32_t)(runtime_ticks() - trackq[i].next_tick) >= 0) {
            any_due = 1;
            if (capture_n == 0u) {
                capture_n = trackq[i].n;
                capture_settle = trackq[i].settle;
            }
        }
    }

    if (!any_enabled || !any_due)
        return;

    trackq_dump_n = 0u;
    trackq_capture_nco = hw_get_phase_nco();
    trackq_capture_down = uberclock_channel_get_phase_down(0);
    trackq_capture_input = hw_get_input_select();
    trackq_capture_fs = fft_get_sample_rate_hz();
    if (!capture_ds_track_multi(capture_n, capture_settle)) {
        for (unsigned i = 0; i < UBERCLOCK_TRACKQ_CHANNEL_COUNT; i++)
            trackq[i].enabled = 0;
        return;
    }

    /* Measurement-only validation. Never write an NCO correction here. */
    {
        unsigned meas_n = capture_n;
        uint32_t capture_tick = runtime_ticks();
        uint32_t dticks = trackq_have_capture_tick ? capture_tick - trackq_last_capture_tick : 0u;
        int64_t bb_hz_milli = 0ll;
        int64_t nco_hz_milli = uberclock_phase_inc_to_mhz(trackq_capture_nco, UBERCLOCK_TRACK3_RF_FS_HZ);
        uint32_t difference = trackq_capture_nco > trackq_capture_down ?
            trackq_capture_nco - trackq_capture_down : trackq_capture_down - trackq_capture_nco;
        int64_t expected = uberclock_phase_inc_to_mhz(difference, UBERCLOCK_TRACK3_RF_FS_HZ);
        int expected_valid = trackq_capture_input == 1u && fft_get_sample_rate_hz() == 10000u &&
                             expected > 0ll && expected < 5000000ll;
        /* Search independently on every record: a bad estimate or manual
         * register step must not trap later records in a stale search window. */
        int bb_valid = trackq_fft_peak_vertex_estimate_mhz(track_samples[0], meas_n,
                                                          0ll, 0u, &bb_hz_milli);
        struct uberclock_fft_peak_debug dbg = fft_peak_debug();
        struct uberclock_ds_capture_stats stats = fifo_ds_capture_stats();

        trackq_dump_n = meas_n;
        trackq_last_capture_tick = capture_tick;
        trackq_have_capture_tick = 1;
        trackq_log_iteration++;
        printf("trackq validate: nco=%ld.%03ldHz bb=%s%ld.%03ldHz dticks=%lu k=%u y={%lld,%lld,%lld} den=%lld",
               (long)(nco_hz_milli / 1000ll), (long)llabs(nco_hz_milli % 1000ll),
               bb_valid ? "" : "(na)",
               (long)(bb_hz_milli / 1000ll), (long)llabs(bb_hz_milli % 1000ll),
               (unsigned long)dticks, dbg.k_peak,
               (long long)dbg.y1, (long long)dbg.y2, (long long)dbg.y3,
               (long long)dbg.den);
        printf(" N=%u window=" TRACKQ_WINDOW " input=%lu pnco=%lu pdown=%lu drained=%u oldov=%u ov=%u uf=%u expected=%s%ld.%03ldHz\n",
               meas_n, (unsigned long)trackq_capture_input,
               (unsigned long)trackq_capture_nco, (unsigned long)trackq_capture_down,
               stats.drained, stats.old_overflow, stats.overflow, stats.underflow,
               expected_valid ? "" : "(na)", (long)(expected / 1000ll), (long)(expected % 1000ll));
        for (unsigned i = 0; i < UBERCLOCK_TRACKQ_CHANNEL_COUNT; ++i) {
            if (trackq[i].enabled)
                trackq[i].next_tick = runtime_ticks() + TRACKQ_INTERVAL_TICKS;
        }
    }
}
