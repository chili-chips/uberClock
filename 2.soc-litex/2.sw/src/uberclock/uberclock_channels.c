// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_channels.c - indexed per-channel setters/getters and
// loop-based default initialization, replacing the original's five
// near-identical phase_down_1..5 / phase_cpu1..5 / mag_cpu1..5 / gain1..5
// families with a single switch-routed implementation per operation.
#include <generated/csr.h>

#include "uberclock/uberclock_channels.h"
#include "uberclock/uberclock_config.h"
#include "uberclock/uberclock_hw.h"

/* Historical startup frequencies for the Hz-derived channel defaults
 * (channel 1 and 3 below); channels 4 and 5 were always configured with a
 * raw phase increment rather than a frequency, and are preserved as such. */
#define CHANNEL_DEFAULT_PHASE_DOWN_1_HZ 9999000u
#define CHANNEL_DEFAULT_PHASE_DOWN_2_HZ 3386370u
#define CHANNEL_DEFAULT_PHASE_DOWN_3_HZ 3727990u

void uberclock_channel_set_phase_down(unsigned channel_index, uint32_t phase_inc) {
    switch (channel_index) {
        case 0: main_phase_inc_down_1_write(phase_inc); break;
        case 1: main_phase_inc_down_2_write(phase_inc); break;
        case 2: main_phase_inc_down_3_write(phase_inc); break;
        case 3: main_phase_inc_down_4_write(phase_inc); break;
        case 4: main_phase_inc_down_5_write(phase_inc); break;
        default: break;
    }
}

uint32_t uberclock_channel_get_phase_down(unsigned channel_index) {
    switch (channel_index) {
        case 0: return main_phase_inc_down_1_read();
        case 1: return main_phase_inc_down_2_read();
        case 2: return main_phase_inc_down_3_read();
        case 3: return main_phase_inc_down_4_read();
        case 4: return main_phase_inc_down_5_read();
        default: return 0u;
    }
}

void uberclock_channel_set_phase_cpu(unsigned channel_index, uint32_t phase_inc) {
    switch (channel_index) {
        case 0: main_phase_inc_cpu1_write(phase_inc); break;
        case 1: main_phase_inc_cpu2_write(phase_inc); break;
        case 2: main_phase_inc_cpu3_write(phase_inc); break;
        case 3: main_phase_inc_cpu4_write(phase_inc); break;
        case 4: main_phase_inc_cpu5_write(phase_inc); break;
        default: break;
    }
}

void uberclock_channel_set_magnitude_cpu(unsigned channel_index, int16_t magnitude) {
    uint32_t w = (uint32_t)((int32_t)magnitude & 0x0fff);
    switch (channel_index) {
        case 0: main_mag_cpu1_write(w); break;
        case 1: main_mag_cpu2_write(w); break;
        case 2: main_mag_cpu3_write(w); break;
        case 3: main_mag_cpu4_write(w); break;
        case 4: main_mag_cpu5_write(w); break;
        default: break;
    }
}

void uberclock_channel_set_gain(unsigned channel_index, int32_t gain) {
    uint32_t w = (uint32_t)gain;
    switch (channel_index) {
        case 0: main_gain1_write(w); break;
        case 1: main_gain2_write(w); break;
        case 2: main_gain3_write(w); break;
        case 3: main_gain4_write(w); break;
        case 4: main_gain5_write(w); break;
        default: break;
    }
}

void uberclock_channels_apply_default_init(void) {
    /* 0 in the Hz table means "use the raw increment table instead"; this
     * preserves the original's mix of Hz-derived (ch1-3) and raw register
     * (ch4-5) defaults exactly. */
    static const uint32_t default_phase_down_hz[UBERCLOCK_CHANNEL_DEFAULTS_COUNT] = {
        CHANNEL_DEFAULT_PHASE_DOWN_1_HZ, CHANNEL_DEFAULT_PHASE_DOWN_2_HZ,
        CHANNEL_DEFAULT_PHASE_DOWN_3_HZ, 0u, 0u
    };
    static const uint32_t default_phase_down_raw_inc[UBERCLOCK_CHANNEL_DEFAULTS_COUNT] = {
        0u, 0u, 0u, 80644u, 80640u
    };
    static const uint32_t default_phase_cpu_inc[UBERCLOCK_CHANNEL_DEFAULTS_COUNT] = {
        52429u, 52429u, 52429u, 52429u, 52429u
    };
    static const int16_t default_magnitude_cpu[UBERCLOCK_CHANNEL_DEFAULTS_COUNT] = {
        0, 0, 0, 0, 0
    };
    static const int32_t default_gain[UBERCLOCK_CHANNEL_DEFAULTS_COUNT] = {
        0x40000000, 0x40000000, 0x40000000, 0x00000000, 0x00000000
    };
    unsigned n = (UBERCLOCK_CHANNEL_COUNT < UBERCLOCK_CHANNEL_DEFAULTS_COUNT)
                     ? UBERCLOCK_CHANNEL_COUNT
                     : UBERCLOCK_CHANNEL_DEFAULTS_COUNT;

    for (unsigned ch = 0; ch < n; ch++) {
        uint32_t phase_down_inc = default_phase_down_hz[ch]
            ? uberclock_phase_inc_from_hz(default_phase_down_hz[ch], UBERCLOCK_TRACK3_RF_FS_HZ)
            : default_phase_down_raw_inc[ch];

        uberclock_channel_set_phase_down(ch, phase_down_inc);
        uberclock_channel_set_phase_cpu(ch, default_phase_cpu_inc[ch]);
        uberclock_channel_set_magnitude_cpu(ch, default_magnitude_cpu[ch]);
        uberclock_channel_set_gain(ch, default_gain[ch]);
    }
}
