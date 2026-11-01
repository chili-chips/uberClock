// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_siggen.c - software 3-tone generator: per-channel DDS phase
// accumulators, the shared sine LUT, and servicing the upsampler FIFO.
#include <stdio.h>
#include <string.h>

#include "uberclock/uberclock_config.h"
#include "uberclock/uberclock_fifo.h"
#include "uberclock/uberclock_siggen.h"
#include "uberclock/uberclock_types.h"

#define SIGGEN_FIFO_WAIT_POLLS 100000u

/** @brief All software 3-tone generator state, grouped from what were
 *  five parallel arrays of file-scope globals in the original. */
struct uberclock_siggen_state {
    volatile int enabled;
    uint8_t channel_enabled[UBERCLOCK_CHANNEL_COUNT];
    uint32_t phase[UBERCLOCK_CHANNEL_COUNT][UBERCLOCK_SIGGEN_TONE_COUNT];
    uint32_t phase_inc[UBERCLOCK_CHANNEL_COUNT][UBERCLOCK_SIGGEN_TONE_COUNT];
    uint32_t tone_freq_hz[UBERCLOCK_CHANNEL_COUNT][UBERCLOCK_SIGGEN_TONE_COUNT];
    int16_t tone_amplitude[UBERCLOCK_CHANNEL_COUNT];
};

static struct uberclock_siggen_state g_siggen;

/* 256-entry sine LUT, one full cycle, Q15-ish signed values (only the
 * first quadrant is stored; siggen_sine_u32() mirrors it). */
static const int16_t sine_q64[64] = {
      0,   804,  1608,  2410,  3212,  4011,  4808,  5602,
   6393,  7179,  7962,  8739,  9512, 10278, 11039, 11793,
  12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
  18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
  23170, 23731, 24279, 24811, 25329, 25831, 26318, 26789,
  27244, 27683, 28105, 28510, 28898, 29269, 29622, 29957,
  30274, 30572, 30852, 31113, 31356, 31579, 31783, 31968,
  32133, 32279, 32405, 32512, 32598, 32665, 32713, 32740
};

int16_t siggen_sine_u32(uint32_t ph) {
    uint8_t q = (uint8_t)(ph >> 30);            /* quadrant 0..3 */
    uint8_t idx = (uint8_t)((ph >> 24) & 0x3f);  /* 0..63 within quadrant */

    switch (q) {
        case 0: return sine_q64[idx];
        case 1: return sine_q64[63 - idx];
        case 2: return (int16_t)(-sine_q64[idx]);
        default: return (int16_t)(-sine_q64[63 - idx]);
    }
}

uint32_t siggen_phase_inc_from_hz(uint32_t f_hz, uint32_t fs_hz) {
    return (uint32_t)(((uint64_t)f_hz << 32) / fs_hz);
}

uint32_t siggen_phase_inc_from_mhz(int64_t f_hz_milli, uint32_t fs_hz) {
    if (f_hz_milli <= 0)
        return 0u;
    return (uint32_t)((((uint64_t)f_hz_milli) << 32) / ((uint64_t)fs_hz * 1000ull));
}

void siggen_update_increments(void) {
    for (unsigned ch = 0; ch < UBERCLOCK_CHANNEL_COUNT; ch++) {
        for (unsigned tone = 0; tone < UBERCLOCK_SIGGEN_TONE_COUNT; tone++) {
            g_siggen.phase_inc[ch][tone] = siggen_phase_inc_from_hz(g_siggen.tone_freq_hz[ch][tone], 10000u);
        }
    }
}

void siggen_set_channel_freqs(unsigned channel, uint32_t f1_hz, uint32_t f2_hz, uint32_t f3_hz) {
    if (channel >= UBERCLOCK_CHANNEL_COUNT)
        return;

    g_siggen.tone_freq_hz[channel][0] = f1_hz;
    g_siggen.tone_freq_hz[channel][1] = f2_hz;
    g_siggen.tone_freq_hz[channel][2] = f3_hz;
}

void siggen_set_channel_symmetric(unsigned channel, uint32_t center_hz, uint32_t delta_hz) {
    if (center_hz <= delta_hz)
        return;

    siggen_set_channel_freqs(channel, center_hz - delta_hz, center_hz, center_hz + delta_hz);
}

void siggen_enable_channel(unsigned channel) {
    if (channel >= UBERCLOCK_CHANNEL_COUNT)
        return;

    g_siggen.enabled = 1;
    g_siggen.channel_enabled[channel] = 1u;
}

static int siggen_wait_and_push(const iq5_frame_t *frame) {
    unsigned limit = SIGGEN_FIFO_WAIT_POLLS;

    while (limit--) {
        unsigned writable = 0;
        fifo_ups_status(&writable, NULL, NULL);
        if (writable) {
            ups_fifo_write_frame(frame);
            return 1;
        }
    }
    return 0;
}

/* Forward-declared: defined after uberclock_siggen_step() below. Advances
 * every enabled channel's phase by one sample and busy-waits (bounded) for
 * the FIFO to become writable, unlike uberclock_siggen_service_push()
 * which skips the step entirely when the FIFO isn't immediately ready. */
static int siggen_step_and_push_blocking(void);

void siggen_disable_channel(unsigned channel) {
    unsigned any_enabled = 0u;

    if (channel >= UBERCLOCK_CHANNEL_COUNT)
        return;

    g_siggen.channel_enabled[channel] = 0u;
    for (unsigned i = 0; i < UBERCLOCK_CHANNEL_COUNT; i++) {
        if (g_siggen.channel_enabled[i]) {
            any_enabled = 1u;
            break;
        }
    }

    if (any_enabled) {
        g_siggen.enabled = 1;
        (void)siggen_step_and_push_blocking();
    } else {
        iq5_frame_t frame = {0};
        (void)siggen_wait_and_push(&frame);
        g_siggen.enabled = 0;
    }
}

void siggen_set_amplitude_all(int16_t amplitude) {
    for (unsigned ch = 0; ch < UBERCLOCK_CHANNEL_COUNT; ch++)
        g_siggen.tone_amplitude[ch] = amplitude;
}

void siggen_set_amplitude(unsigned channel, int16_t amplitude) {
    if (channel >= UBERCLOCK_CHANNEL_COUNT)
        return;
    g_siggen.tone_amplitude[channel] = amplitude;
}

void siggen_apply_default_init(void) {
    static const uint32_t default_freq_hz[UBERCLOCK_CHANNEL_DEFAULTS_COUNT][UBERCLOCK_SIGGEN_TONE_COUNT] = {
        { 990u, 1000u, 1010u},
        { 970u, 1000u, 1030u},
        { 970u, 1000u, 1030u},
        { 990u, 1000u, 1010u},
        { 990u, 1000u, 1010u},
    };
    static const int16_t default_amplitude[UBERCLOCK_CHANNEL_DEFAULTS_COUNT] = {
        3000, 10000, 10000, 3000, 3000
    };
    static const uint8_t default_channel_enabled[UBERCLOCK_CHANNEL_DEFAULTS_COUNT] = {
        1u, 1u, 1u, 0u, 0u
    };
    unsigned n = (UBERCLOCK_CHANNEL_COUNT < UBERCLOCK_CHANNEL_DEFAULTS_COUNT)
                     ? UBERCLOCK_CHANNEL_COUNT
                     : UBERCLOCK_CHANNEL_DEFAULTS_COUNT;

    for (unsigned ch = 0; ch < n; ch++) {
        siggen_set_channel_freqs(ch, default_freq_hz[ch][0], default_freq_hz[ch][1], default_freq_hz[ch][2]);
        g_siggen.tone_amplitude[ch] = default_amplitude[ch];
        g_siggen.channel_enabled[ch] = default_channel_enabled[ch];
    }
}

void uberclock_siggen_start(void) {
    memset(g_siggen.phase, 0, sizeof(g_siggen.phase));
    siggen_update_increments();
    for (unsigned ch = 0; ch < UBERCLOCK_CHANNEL_COUNT; ch++)
        g_siggen.channel_enabled[ch] = (ch < 3u) ? 1u : 0u;

    g_siggen.enabled = 1;
    puts("sig3 enabled on ch1..ch3");
}

void uberclock_siggen_stop(void) {
    for (unsigned ch = 0; ch < UBERCLOCK_CHANNEL_COUNT; ch++)
        g_siggen.channel_enabled[ch] = 0u;

    {
        iq5_frame_t frame = {0};
        (void)siggen_wait_and_push(&frame);
    }

    g_siggen.enabled = 0;
    puts("5-channel 3-tone software generator disabled");
}

static inline int16_t siggen_clamp_s16(int32_t x) {
    if (x >  32767) return  32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

/* Synthesize the next sample for every channel into *frame.
 * @return 1 if the generator is enabled (frame populated), 0 otherwise. */
static int uberclock_siggen_step(iq5_frame_t *frame) {
    if (!g_siggen.enabled) return 0;

    for (unsigned ch = 0; ch < UBERCLOCK_CHANNEL_COUNT; ch++) {
        int32_t acc = 0;

        if (!g_siggen.channel_enabled[ch]) {
            frame->x[ch] = 0;
            frame->y[ch] = 0;
            continue;
        }

        for (unsigned tone = 0; tone < UBERCLOCK_SIGGEN_TONE_COUNT; tone++) {
            g_siggen.phase[ch][tone] += g_siggen.phase_inc[ch][tone];
            acc += ((int32_t)g_siggen.tone_amplitude[ch] * (int32_t)siggen_sine_u32(g_siggen.phase[ch][tone])) / 32767;
        }

        frame->x[ch] = siggen_clamp_s16(acc);
        frame->y[ch] = 0;
    }
    return 1;
}

static int siggen_step_and_push_blocking(void) {
    iq5_frame_t frame;

    if (!uberclock_siggen_step(&frame))
        return 0;
    return siggen_wait_and_push(&frame);
}

void uberclock_siggen_service_push(void) {
    unsigned writable = 0;
    iq5_frame_t frame;

    fifo_ups_status(&writable, NULL, NULL);
    if (!writable)
        return;

    if (!uberclock_siggen_step(&frame))
        return;

    ups_fifo_write_frame(&frame);
}
