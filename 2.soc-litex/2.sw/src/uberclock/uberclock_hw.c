// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_hw.c - config-link commit, Hz<->phase_inc conversion, and the
// generic single-register mux/output/debug/final_shift/capture_enable
// setters. Everything here is a thin, named wrapper around a generated
// CSR accessor; no DSP or state-machine logic lives in this file.
#include <generated/csr.h>

#include "uberclock/uberclock_hw.h"

void uberclock_commit_config(void) {
    cfg_link_commit_write(1);
}

uint32_t uberclock_phase_inc_from_hz(uint32_t f_hz, uint32_t fs_hz) {
    if (fs_hz == 0u)
        return 0u;
    return (uint32_t)((((uint64_t)f_hz << 26) + fs_hz / 2u) / fs_hz);
}

uint32_t uberclock_phase_inc_to_hz(uint32_t phase_inc, uint32_t fs_hz) {
    return (uint32_t)((((uint64_t)phase_inc * (uint64_t)fs_hz) + (1u << 25)) >> 26);
}

int64_t uberclock_phase_inc_to_mhz(uint32_t phase_inc, uint32_t fs_hz) {
    return (int64_t)((((uint64_t)phase_inc * (uint64_t)fs_hz * 1000ull) + (1u << 25)) >> 26);
}

void hw_set_phase_nco(uint32_t phase_inc) {
    main_phase_inc_nco_write(phase_inc);
}

uint32_t hw_get_phase_nco(void) {
    return main_phase_inc_nco_read();
}

void hw_set_nco_mag(int32_t magnitude) {
    main_nco_mag_write((uint32_t)(magnitude & 0x0fff));
}

void hw_set_phase_down_ref(uint32_t phase_inc) {
    main_phase_inc_down_ref_write(phase_inc);
}

void hw_set_input_select(uint32_t value) {
    main_input_select_write(value);
}

uint32_t hw_get_input_select(void) {
    return main_input_select_read();
}

void hw_set_upsampler_input_mux(uint32_t value) {
    main_upsampler_input_mux_write(value);
}

void hw_set_output_select_ch1(uint32_t value) {
    main_output_select_ch1_write(value & 0x0fu);
}

void hw_set_output_select_ch2(uint32_t value) {
    main_output_select_ch2_write(value & 0x0fu);
}

void hw_set_lowspeed_dbg_select(uint32_t value) {
    main_lowspeed_dbg_select_write(value);
}

void hw_set_highspeed_dbg_select(uint32_t value) {
    main_highspeed_dbg_select_write(value);
}

void hw_set_final_shift(int32_t value) {
    main_final_shift_write((uint32_t)value);
}

void hw_set_cap_enable(uint32_t enable) {
    main_cap_enable_write(enable ? 1u : 0u);
}

uint32_t hw_get_cap_enable(void) {
    return main_cap_enable_read() & 1u;
}

void hw_set_cap_beats(uint32_t beats) {
    main_cap_beats_write(beats);
}

void hw_write_upsampler_inputs_all_x(int16_t value) {
    uint32_t w = (uint32_t)((int32_t)value & 0xffff);
    main_upsampler_input_x1_write(w);
    main_upsampler_input_x2_write(w);
    main_upsampler_input_x3_write(w);
    main_upsampler_input_x4_write(w);
    main_upsampler_input_x5_write(w);
}

void hw_write_upsampler_inputs_all_y(int16_t value) {
    uint32_t w = (uint32_t)((int32_t)value & 0xffff);
    main_upsampler_input_y1_write(w);
    main_upsampler_input_y2_write(w);
    main_upsampler_input_y3_write(w);
    main_upsampler_input_y4_write(w);
    main_upsampler_input_y5_write(w);
}

int16_t hw_read_magnitude(void) {
    return (int16_t)(main_magnitude_read() & 0xffffu);
}
