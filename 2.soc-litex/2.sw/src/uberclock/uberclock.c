// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock.c - top-level lifecycle: the shared runtime state instance,
// the ce_down ISR, CE-event service orchestration, and
// uberclock_init()/uberclock_poll(). Everything DSP/hardware-specific
// lives in the subsystem modules this file wires together.
#include <stdio.h>

#include <irq.h>
#include <generated/csr.h>

#include "uberclock/uberclock.h"

static struct uberclock_runtime g_runtime;

uint32_t runtime_ticks(void) {
    return g_runtime.ce_ticks;
}

int16_t runtime_magnitude(void) {
    return g_runtime.magnitude;
}

int32_t runtime_phase(void) {
    return g_runtime.phase;
}

/* The one place a queued CE event is actually serviced: push the next
 * software-generator sample and latch the CORDIC magnitude, then re-arm
 * the EVM for the next ce_down tick. */
static void service_one_ce_event(void) {
    uberclock_siggen_service_push();
    g_runtime.magnitude = hw_read_magnitude();
    evm_pending_write(1);
    evm_enable_write(1);
}

void ce_service_background(void) {
    if (g_runtime.ce_event == 0u)
        return;

    g_runtime.ce_event--;
    service_one_ce_event();
}

void ce_service_background_budget(unsigned budget) {
    while (budget-- && g_runtime.ce_event) {
        g_runtime.ce_event--;
        service_one_ce_event();
    }
}

void ce_wait_ticks(uint32_t wait_ticks) {
    uint32_t start_tick = g_runtime.ce_ticks;

    while ((uint32_t)(g_runtime.ce_ticks - start_tick) < wait_ticks) {
        ce_service_background();
    }
}

static void ce_down_isr(void) {
    evm_pending_write(1);
    evm_enable_write(0);
    if (g_runtime.ce_event < 0xffffffffu) g_runtime.ce_event++;
    g_runtime.ce_ticks++;
}

void uberclock_init(void) {
    hw_set_phase_nco(10324440);

    uberclock_channels_apply_default_init();
    hw_set_phase_down_ref(uberclock_phase_inc_from_hz(9999000u, UBERCLOCK_TRACK3_RF_FS_HZ));
    hw_set_nco_mag(300);

    hw_set_input_select(1);
    hw_set_upsampler_input_mux(1);

    hw_set_output_select_ch1(15);
    hw_set_output_select_ch2(0);

    hw_set_final_shift(2);

    hw_set_lowspeed_dbg_select(5);
    hw_set_highspeed_dbg_select(0);

    hw_write_upsampler_inputs_all_x(0);
    hw_write_upsampler_inputs_all_y(0);

    hw_set_upsampler_input_mux(1);
    hw_set_cap_enable(1);
    dsp_pump_set_enabled(1);

    siggen_apply_default_init();
    uberclock_siggen_start();
    uberclock_commit_config();

    fifo_clear_all();

    evm_pending_write(1);
    evm_enable_write(1);
    irq_attach(EVM_INTERRUPT, ce_down_isr);
    irq_setmask(irq_getmask() | (1u << EVM_INTERRUPT));

    printf("UberClock init done. build=" TRACKQ_BUILD_ID " compiled=" __DATE__ " " __TIME__ " window=" TRACKQ_WINDOW "\n");
}

void uberclock_poll(void) {
    track_poll();

    while (g_runtime.ce_event) {
        g_runtime.ce_event--;
        service_one_ce_event();
    }
}
