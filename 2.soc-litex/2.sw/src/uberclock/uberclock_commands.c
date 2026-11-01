// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_commands.c - console command table and thin command_*
// wrappers. Each wrapper only parses/validates its CLI arguments and
// calls a real subsystem API; no DSP/hardware-sequencing logic lives
// here. See uberclock.h for the project's naming convention.
//
// Legacy per-channel command names (phase_down_1..5, phase_cpu1..5,
// mag_cpu1..5, gain1..5) are kept as thin aliases forwarding to the new
// indexed phase_down/phase_cpu/mag_cpu/gain commands, so existing
// console scripts/history (LoopbackRetest.md, captures/README.md, ...)
// keep working unchanged.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <generated/csr.h>
#include "ubddr3.h"

#include "console.h"
#include "uberclock/uberclock_capture.h"
#include "uberclock/uberclock_channels.h"
#include "uberclock/uberclock_commands.h"
#include "uberclock/uberclock_config.h"
#include "uberclock/uberclock_dma.h"
#include "uberclock/uberclock_fft.h"
#include "uberclock/uberclock_fifo.h"
#include "uberclock/uberclock_hw.h"
#include "uberclock/uberclock_parse.h"
#include "uberclock/uberclock_runtime.h"
#include "uberclock/uberclock_siggen.h"
#include "uberclock/uberclock_track.h"

/* ========================================================================
 * Help text.
 * ======================================================================== */

static void command_help(char *args) {
    (void)args;
    puts_help_header("UberClock commands");

    puts("  phase_nco        <val>      (0..67108863)");
    puts("  nco_mag          <val>      (signed 12-bit: -2048..2047)");

    printf("  phase_down <channel:1..%u> <val>   (0..67108863)\n", UBERCLOCK_CHANNEL_COUNT);
    puts("  phase_down_1 <val> ... phase_down_5 <val>  (legacy aliases, 0..67108863)");
    puts("  phase_down_ref   <val>      (0..67108863)");

    printf("  phase_cpu  <channel:1..%u> <val>   (0..67108863)\n", UBERCLOCK_CHANNEL_COUNT);
    puts("  phase_cpu1 <val> ... phase_cpu5 <val>      (legacy aliases, 0..67108863)");

    printf("  mag_cpu    <channel:1..%u> <val>   (signed 12-bit: -2048..2047)\n", UBERCLOCK_CHANNEL_COUNT);
    puts("  mag_cpu1 <val> ... mag_cpu5 <val>          (legacy aliases, signed 12-bit)");

    puts("  input_select         <0..3> (0=ADC,1=NCO,2=SUM,3=reserved)");
    puts("  upsampler_input_mux  <0..2> (0=Gain,1=CPU,2=CPU NCO)");

    puts("  output_select_ch1    <0..15> (14=downsampled_y_ref, 15=sum)");
    puts("  output_select_ch2    <0..15> (14=downsampled_y_ref, 15=sum)");
    printf("  gain <channel:1..%u> <int32>  (gain1 .. gain%u legacy aliases)\n",
           UBERCLOCK_CHANNEL_COUNT, UBERCLOCK_CHANNEL_COUNT);
    puts("  final_shift          <0..7>");

    puts("  lowspeed_dbg_select  <0..7> (6=downsampled_y_ref)");
    puts("  highspeed_dbg_select <0..3> (1=filter_in_1)");

    puts("  upsampler_x          <val>  (signed 16-bit, replicated to ch1..ch5)");
    puts("  upsampler_y          <val>  (signed 16-bit, replicated to ch1..ch5)");
    puts("  sig3_start                  (start 5 independent 3-tone generators)");
    puts("  sig3_stop                   (stop 5 independent 3-tone generators)");
    puts("  sig3_amp <val> | <ch> <val> (set per-tone amplitude for all or one channel)");
    puts("  sig3_freqs <ch> <f1> <f2> <f3> (set 3-tone frequencies for one channel)");
    puts("  sig3_enable_ch     <ch>     (enable one sig3 channel)");
    puts("  sig3_disable_ch    <ch>     (disable one sig3 channel)");
    puts("  ds_pop                      (pop one 6-channel downsampled frame, ch1..ch5 + ref)");
    puts("  ds_status                   (read downsample FIFO flags/overflow)");
    puts("  ups_push <x> <y>             (enqueue one 5-channel frame, replicated)");
    puts("  ups_status                  (read upsampler FIFO flags/overflow)");
    puts("  dsp_run <0|1>               (enable/disable non-blocking DSP pump)");
    puts("  fft_ds [N]                  (FFT over DS FIFO IQ samples, N=8..2048, prints bins)");
    puts("  fft_ds_peak [N]             (FFT over DS FIFO IQ samples, N=8..2048, peak only)");
    puts("  fft_fs <Hz>                 (set DS sample rate used for fft_ds Hz print)");
    puts("  track3 <ch> <start_hz> [step_hz] [max_steps] [N] [center_hz] [delta_hz]");
    puts("  trackq_start <f1> <f2> <f3> [N] [center_hz] [delta_ch1_hz] [delta_ch2_hz] [delta_ch3_hz]");
    puts("  trackq_probe [N] [center_hz] [delta_hz]");
    puts("  trackq_stop                 (stop validation)\n  trackq_dump                 (stop and dump last raw Y1 capture)");

    puts("  cap_arm              (pulse arm capture)");
    puts("  cap_done             (read cap_done)");
    puts("  cap_rd <idx>          (read cap_data at idx)");
    puts("  cap_enable   <0|1>    (0=ramp, 1=capture design to DDR)");
    puts("  cap_beats    <N>      (# of 256-bit beats captured by the gateware)");
    puts("");
}

static void command_dma_help(char *args) {
    (void)args;
    puts_help_header("UberDDR3/S2MM commands");
    puts("  ub_info");
    puts("      Print DDR calibration state and CSR base.");
    puts("  ub_mode");
    puts("      Print current capture mode (cap_enable).");
    puts("  ub_setmode <0|1>");
    puts("      Set cap_enable (0=ramp, 1=capture design) and commit.");
    puts("  ub_ramp <addr_hex> [beats] [size]");
    puts("      FORCE ramp mode (cap_enable=0), then start S2MM into DDR.");
    puts("  ub_cap  <addr_hex> [beats] [size]");
    puts("      FORCE capture mode (cap_enable=1), then start S2MM into DDR.");
    puts("  ub_start <addr_hex> [beats] [size]");
    puts("      Start S2MM using CURRENT cap_enable mode.");
    puts("  ub_wait");
    puts("      Poll until DMA not busy; flush caches; print error if any.");
    puts("  ub_hexdump <addr_hex> <bytes>");
    puts("      Dump memory to verify write.");
    puts("  ub_send <addr_hex> <bytes> <dst_ip> <dst_port>");
    puts("      Send DDR memory region via UDP to PC.");
    puts("      Example: ub_send 0xA0000000 8192 192.168.0.2 5000");
    puts("");
}

/* ========================================================================
 * Phase/NCO/downconversion.
 * ======================================================================== */

static void command_phase_nco(char *a) {
    unsigned p = uberclock_parse_unsigned(a, 1u << 26, "phase_nco");
    if (p >= (1u << 26)) return;
    hw_set_phase_nco(p);
    uberclock_commit_config();
    printf("Input NCO phase increment set to %u\n", p);
}

static void command_nco_mag(char *a) {
    int v = uberclock_parse_signed(a, -2048, 2047, "nco_mag");
    if (v < -2048 || v > 2047) return;
    hw_set_nco_mag(v);
    uberclock_commit_config();
    printf("nco_mag set to %d\n", v);
}

static void command_phase_down_ref(char *a) {
    unsigned p = uberclock_parse_unsigned(a, 1u << 26, "phase_down_ref");
    if (p >= (1u << 26)) return;
    hw_set_phase_down_ref(p);
    uberclock_commit_config();
    printf("Downconversion phase ref increment set to %u\n", p);
}

/* Shared by the indexed phase_down/phase_cpu/mag_cpu/gain commands and
 * their legacy per-channel aliases below; channel_1based is the
 * CLI-facing 1..UBERCLOCK_CHANNEL_COUNT number, converted to the
 * 0-based index the subsystem APIs expect. */
static void phase_down_set(unsigned channel_1based, const char *value_token) {
    unsigned p = uberclock_parse_unsigned(value_token, 1u << 26, "phase_down");
    if (p >= (1u << 26)) return;
    uberclock_channel_set_phase_down(channel_1based - 1u, p);
    uberclock_commit_config();
    printf("Downconversion phase ch%u increment set to %u\n", channel_1based, p);
}

static void phase_cpu_set(unsigned channel_1based, const char *value_token) {
    unsigned p = uberclock_parse_unsigned(value_token, 1u << 26, "phase_cpu");
    if (p >= (1u << 26)) return;
    uberclock_channel_set_phase_cpu(channel_1based - 1u, p);
    uberclock_commit_config();
    printf("CPU phase increment ch%u set to %u\n", channel_1based, p);
}

static void mag_cpu_set(unsigned channel_1based, const char *value_token) {
    int v = uberclock_parse_signed(value_token, -2048, 2047, "mag_cpu");
    if (v < -2048 || v > 2047) return;
    uberclock_channel_set_magnitude_cpu(channel_1based - 1u, (int16_t)v);
    uberclock_commit_config();
    printf("mag_cpu%u set to %d\n", channel_1based, v);
}

static void gain_set(unsigned channel_1based, const char *value_token) {
    int32_t g = (int32_t)strtol(value_token ? value_token : "0", NULL, 0);
    uberclock_channel_set_gain(channel_1based - 1u, g);
    uberclock_commit_config();
    printf("Gain%u register set to %ld (0x%08lX)\n", channel_1based, (long)g, (unsigned long)g);
}

/* Parses "<channel> <value>" and dispatches to @p setter, used by the
 * four new indexed commands (phase_down/phase_cpu/mag_cpu/gain). */
static void dispatch_indexed_channel_command(char *args, const char *name,
                                              void (*setter)(unsigned, const char *)) {
    char *tok_ch = strtok(args, " \t");
    char *tok_val = strtok(NULL, " \t");
    unsigned ch;

    if (!tok_ch || !tok_val) {
        printf("Usage: %s <channel:1..%u> <value>\n", name, UBERCLOCK_CHANNEL_COUNT);
        return;
    }

    ch = (unsigned)strtoul(tok_ch, NULL, 0);
    if (ch < 1u || ch > UBERCLOCK_CHANNEL_COUNT) {
        printf("%s channel must be 1..%u\n", name, UBERCLOCK_CHANNEL_COUNT);
        return;
    }

    setter(ch, tok_val);
}

static void command_phase_down(char *args) { dispatch_indexed_channel_command(args, "phase_down", phase_down_set); }
static void command_phase_cpu(char *args)  { dispatch_indexed_channel_command(args, "phase_cpu", phase_cpu_set); }
static void command_mag_cpu(char *args)    { dispatch_indexed_channel_command(args, "mag_cpu", mag_cpu_set); }
static void command_gain(char *args)       { dispatch_indexed_channel_command(args, "gain", gain_set); }

/* Legacy per-channel aliases (see file header). */
static void command_phase_down_1(char *a) { phase_down_set(1, a); }
static void command_phase_down_2(char *a) { phase_down_set(2, a); }
static void command_phase_down_3(char *a) { phase_down_set(3, a); }
static void command_phase_down_4(char *a) { phase_down_set(4, a); }
static void command_phase_down_5(char *a) { phase_down_set(5, a); }

static void command_phase_cpu1(char *a) { phase_cpu_set(1, a); }
static void command_phase_cpu2(char *a) { phase_cpu_set(2, a); }
static void command_phase_cpu3(char *a) { phase_cpu_set(3, a); }
static void command_phase_cpu4(char *a) { phase_cpu_set(4, a); }
static void command_phase_cpu5(char *a) { phase_cpu_set(5, a); }

static void command_mag_cpu1(char *a) { mag_cpu_set(1, a); }
static void command_mag_cpu2(char *a) { mag_cpu_set(2, a); }
static void command_mag_cpu3(char *a) { mag_cpu_set(3, a); }
static void command_mag_cpu4(char *a) { mag_cpu_set(4, a); }
static void command_mag_cpu5(char *a) { mag_cpu_set(5, a); }

static void command_gain1(char *a) { gain_set(1, a); }
static void command_gain2(char *a) { gain_set(2, a); }
static void command_gain3(char *a) { gain_set(3, a); }
static void command_gain4(char *a) { gain_set(4, a); }
static void command_gain5(char *a) { gain_set(5, a); }

/* ========================================================================
 * Muxes / gains / debug selects.
 * ======================================================================== */

static void command_output_sel_ch1(char *a) {
    unsigned v = (unsigned)strtoul(a ? a : "0", NULL, 0) & 0x0fu;
    hw_set_output_select_ch1(v);
    uberclock_commit_config();
    printf("output_select_ch1 set to %u\n", v);
}

static void command_output_sel_ch2(char *a) {
    unsigned v = (unsigned)strtoul(a ? a : "0", NULL, 0) & 0x0fu;
    hw_set_output_select_ch2(v);
    uberclock_commit_config();
    printf("output_select_ch2 set to %u\n", v);
}

static void command_input_select(char *a) {
    unsigned v = (unsigned)strtoul(a ? a : "0", NULL, 0);
    hw_set_input_select(v);
    uberclock_commit_config();
    printf("Main input select register set to %u\n", v);
}

static void command_ups_in_mux(char *a) {
    unsigned v = (unsigned)strtoul(a ? a : "0", NULL, 0);
    hw_set_upsampler_input_mux(v);
    uberclock_commit_config();
    printf("Upsampler input mux register set to %u\n", v);
}

static void command_lowspeed_dbg_select(char *a) {
    unsigned v = (unsigned)strtoul(a ? a : "0", NULL, 0);
    if (v > 7) { puts("lowspeed_dbg_select must be 0..7"); return; }
    hw_set_lowspeed_dbg_select(v);
    uberclock_commit_config();
    printf("lowspeed_dbg_select = %u\n", v);
}

static void command_highspeed_dbg_select(char *a) {
    unsigned v = (unsigned)strtoul(a ? a : "0", NULL, 0);
    if (v > 3) { puts("highspeed_dbg_select must be 0..3"); return; }
    hw_set_highspeed_dbg_select(v);
    uberclock_commit_config();
    printf("highspeed_dbg_select = %u\n", v);
}

static void command_final_shift(char *a) {
    int32_t fs = (int32_t)strtol(a ? a : "0", NULL, 0);
    hw_set_final_shift(fs);
    uberclock_commit_config();
    printf("final_shift set to %ld (0x%08lX)\n", (long)fs, (unsigned long)fs);
}

static void command_cap_enable(char *a) {
    unsigned v = (unsigned)strtoul(a ? a : "0", NULL, 0);
    v = v ? 1u : 0u;
    hw_set_cap_enable(v);
    uberclock_commit_config();
    printf("cap_enable = %u (%s)\n", v, v ? "CAPTURE(design)->DDR" : "RAMP->DDR");
}

static void command_upsampler_x(char *a) {
    int v = uberclock_parse_signed(a, -32768, 32767, "upsampler_x");
    if (v < -32768 || v > 32767) return;
    hw_write_upsampler_inputs_all_x((int16_t)v);
    uberclock_commit_config();
    printf("upsampler_input_x[1..5] = %d\n", v);
}

static void command_upsampler_y(char *a) {
    int v = uberclock_parse_signed(a, -32768, 32767, "upsampler_y");
    if (v < -32768 || v > 32767) return;
    hw_write_upsampler_inputs_all_y((int16_t)v);
    uberclock_commit_config();
    printf("upsampler_input_y[1..5] = %d\n", v);
}

/* ========================================================================
 * Low-speed on-chip debug capture buffer.
 * ======================================================================== */

static void command_cap_start(char *a) {
    (void)a;
    capture_arm_pulse();
    puts("Capture started.");
}

static void command_cap_status(char *a) {
    (void)a;
    printf("Capture %s\n", capture_is_done() ? "DONE" : "IN-PROGRESS");
}

static void command_cap_dump(char *a) {
    (void)a;
    capture_dump();
}

static void command_cap_arm_pulse(char *a) {
    (void)a;
    capture_arm_pulse();
    puts("cap_arm pulsed");
}

static void command_cap_done(char *a) {
    (void)a;
    printf("cap_done = %u\n", capture_is_done());
}

static void command_cap_rd(char *args) {
    char *tok = strtok(args, " \t");
    unsigned idx;
    int16_t s;

    if (!tok) { puts("Usage: cap_rd <idx>"); return; }
    idx = (unsigned)strtoul(tok, NULL, 0);
    if (idx > 2047) { puts("idx must be 0..2047"); return; }

    s = capture_read_sample(idx);
    printf("cap[%u] = %d (0x%04x)\n", idx, (int)s, (unsigned)((uint16_t)s));
}

static void command_cap_beats(char *a) {
    uint32_t v = (uint32_t)strtoul(a ? a : "256", NULL, 0);
    if (v == 0) { puts("cap_beats must be >= 1"); return; }
    hw_set_cap_beats(v);
    uberclock_commit_config();
    printf("cap_beats = %u\n", (unsigned)v);
}

static void command_phase_print(char *a) {
    (void)a;
    printf("Phase %ld\n", (long)runtime_phase());
}

static void command_magnitude(char *a) {
    (void)a;
    printf("Magnitude %d\n", runtime_magnitude());
}

/* ========================================================================
 * DS/upsampler FIFO + DSP pump test harness.
 * ======================================================================== */

static void command_ds_pop(char *a) {
    (void)a;
    iq6_frame_t frame;
    ds_fifo_read_frame(&frame);
    printf("ds_fifo:"
           " ch1=(%d,%d)"
           " ch2=(%d,%d)"
           " ch3=(%d,%d)"
           " ch4=(%d,%d)"
           " ch5=(%d,%d)"
           " ref=(%d,%d)\n",
           (int)frame.x[0], (int)frame.y[0],
           (int)frame.x[1], (int)frame.y[1],
           (int)frame.x[2], (int)frame.y[2],
           (int)frame.x[3], (int)frame.y[3],
           (int)frame.x[4], (int)frame.y[4],
           (int)frame.x[5], (int)frame.y[5]);
}

static void command_ds_status(char *a) {
    (void)a;
    unsigned readable, overflow, underflow;
    fifo_ds_status(&readable, &overflow, &underflow);
    printf("ds_fifo: readable=%u overflow=%u underflow=%u\n", readable, overflow, underflow);
    fifo_ds_clear();
}

static void command_ups_push(char *args) {
    char *tokx = strtok(args, " \t");
    char *toky = strtok(NULL, " \t");
    int x, y;

    if (!tokx || !toky) { puts("Usage: ups_push <x> <y>"); return; }
    x = uberclock_parse_signed(tokx, -32768, 32767, "ups_x");
    y = uberclock_parse_signed(toky, -32768, 32767, "ups_y");
    if (x < -32768 || x > 32767 || y < -32768 || y > 32767) return;
    ups_fifo_write_replicated((int16_t)x, (int16_t)y);
    printf("ups_fifo push: replicated x=%d y=%d to ch1..ch5\n", x, y);
}

static void command_ups_status(char *a) {
    (void)a;
    unsigned writable, overflow, underflow;
    fifo_ups_status(&writable, &overflow, &underflow);
    printf("ups_fifo: writable=%u overflow=%u underflow=%u\n", writable, overflow, underflow);
    fifo_ups_clear();
}

static void command_dsp_run(char *a) {
    unsigned v = (unsigned)strtoul(a ? a : "0", NULL, 0);
    dsp_pump_set_enabled(v ? 1 : 0);
    printf("dsp_run = %u\n", dsp_pump_is_enabled());
}

static void command_dsp_test(char *args) {
    char *tok = strtok(args, " \t");
    unsigned limit = tok ? (unsigned)strtoul(tok, NULL, 0) : 0;
    unsigned processed, stall;

    fifo_run_dsp_test(limit, &processed, &stall);
    printf("dsp_test processed %u samples (stall=%u)\n", processed, stall);
}

/* ========================================================================
 * FFT / tracking.
 * ======================================================================== */

static void command_fft64_peak(char *a) {
    (void)a;
    fft_run_ch1_iq64_peak_and_print();
}

static void command_fft32_ds_y(char *a) {
    (void)a;
    fft_run_ch1_real32_and_print();
}

static void command_fft_fs(char *a) {
    uint32_t v = (uint32_t)strtoul(a ? a : "0", NULL, 0);
    if (v == 0u) {
        puts("Usage: fft_fs <Hz>, Hz must be > 0");
        return;
    }
    if (track_any_channel_enabled()) {
        puts("Stop validation before changing fft_fs");
        return;
    }
    fft_set_sample_rate_hz(v);
    printf("fft_fs = %lu Hz\n", (unsigned long)v);
}

static void command_fft_ds(char *args) {
    char *tok = strtok(args, " \t");
    unsigned n = tok ? (unsigned)strtoul(tok, NULL, 0) : 32u;
    (void)fft_run_ds_capture_and_print(n, 0);
}

static void command_fft_ds_peak(char *args) {
    char *tok = strtok(args, " \t");
    unsigned n = tok ? (unsigned)strtoul(tok, NULL, 0) : 32u;
    (void)fft_run_ds_capture_and_print(n, 1);
}

static void command_track3(char *args) {
    char *tok_ch     = strtok(args, " \t");
    char *tok_start  = strtok(NULL, " \t");
    char *tok_step   = strtok(NULL, " \t");
    char *tok_steps  = strtok(NULL, " \t");
    char *tok_n      = strtok(NULL, " \t");
    char *tok_center = strtok(NULL, " \t");
    char *tok_delta  = strtok(NULL, " \t");
    unsigned ch;

    if (!tok_ch || !tok_start) {
        puts("Usage: track3 <ch:1..5> <start_phase_down_hz> [step_hz] [max_steps] [N] [center_hz] [delta_hz]");
        return;
    }

    ch = (unsigned)strtoul(tok_ch, NULL, 0);
    if (ch < 1u || ch > UBERCLOCK_CHANNEL_COUNT) {
        printf("track3 channel must be 1..%u\n", UBERCLOCK_CHANNEL_COUNT);
        return;
    }

    track_sweep_lock(ch - 1u,
                      (uint32_t)strtoul(tok_start, NULL, 0),
                      tok_step   ? (uint32_t)strtoul(tok_step, NULL, 0) : TRACK3_DEFAULT_STEP_HZ,
                      tok_steps  ? (unsigned)strtoul(tok_steps, NULL, 0) : TRACK3_DEFAULT_MAX_STEPS,
                      tok_n      ? (unsigned)strtoul(tok_n, NULL, 0) : TRACK3_DEFAULT_N,
                      tok_center ? (uint32_t)strtoul(tok_center, NULL, 0) : TRACK3_DEFAULT_CENTER_HZ,
                      tok_delta  ? (uint32_t)strtoul(tok_delta, NULL, 0) : TRACK3_DEFAULT_DELTA_HZ);
}

static void command_trackq_start(char *args) {
    char *tok_f1     = strtok(args, " \t");
    char *tok_f2     = strtok(NULL, " \t");
    char *tok_f3     = strtok(NULL, " \t");
    char *tok_n      = strtok(NULL, " \t");
    char *tok_center = strtok(NULL, " \t");
    char *tok_delta1 = strtok(NULL, " \t");
    char *tok_delta2 = strtok(NULL, " \t");
    char *tok_delta3 = strtok(NULL, " \t");

    track_validate_start(
        tok_f1 ? (uint32_t)strtoul(tok_f1, NULL, 0) : 0u,
        tok_f2 ? (uint32_t)strtoul(tok_f2, NULL, 0) : 0u,
        tok_f3 ? (uint32_t)strtoul(tok_f3, NULL, 0) : 0u,
        tok_n ? (unsigned)strtoul(tok_n, NULL, 0) : TRACK3_DEFAULT_N,
        tok_center ? (uint32_t)strtoul(tok_center, NULL, 0) : TRACK3_DEFAULT_CENTER_HZ,
        tok_delta1 ? (uint32_t)strtoul(tok_delta1, NULL, 0) : track_default_delta_hz(0u),
        tok_delta2 ? (uint32_t)strtoul(tok_delta2, NULL, 0) : track_default_delta_hz(1u),
        tok_delta3 ? (uint32_t)strtoul(tok_delta3, NULL, 0) : track_default_delta_hz(2u));
}

static void command_trackq_probe(char *args) {
    char *tok_n      = strtok(args, " \t");
    char *tok_center = strtok(NULL, " \t");
    char *tok_delta  = strtok(NULL, " \t");

    track_probe(tok_n ? (unsigned)strtoul(tok_n, NULL, 0) : TRACK3_DEFAULT_N,
                tok_center ? (uint32_t)strtoul(tok_center, NULL, 0) : TRACK3_DEFAULT_CENTER_HZ,
                tok_delta ? (uint32_t)strtoul(tok_delta, NULL, 0) : track_default_delta_hz(0u));
}

static void command_trackq_stop(char *a) { (void)a; track_stop(); }
static void command_trackq_dump(char *a) { (void)a; track_dump(); }

/* ========================================================================
 * Software 3-tone generator.
 * ======================================================================== */

static void command_sig3_start(char *a) { (void)a; uberclock_siggen_start(); }
static void command_sig3_stop(char *a)  { (void)a; uberclock_siggen_stop(); }

static void command_sig3_amp(char *a) {
    char *tok1 = strtok(a, " \t");
    char *tok2 = strtok(NULL, " \t");
    char *tok3 = strtok(NULL, " \t");
    int v;
    unsigned ch;

    if (!tok1 || tok3) {
        puts("Usage: sig3_amp <val> | sig3_amp <ch:1..5> <val>");
        return;
    }

    if (!tok2) {
        v = uberclock_parse_signed(tok1, 1, 10000, "sig3_amp");
        if (v < 1 || v > 10000) return;
        siggen_set_amplitude_all((int16_t)v);
        printf("sig3 amplitude per tone = %d for all channels\n", v);
        return;
    }

    ch = (unsigned)strtoul(tok1, NULL, 0);
    if (ch < 1u || ch > UBERCLOCK_CHANNEL_COUNT) {
        printf("sig3_amp channel must be 1..%u\n", UBERCLOCK_CHANNEL_COUNT);
        return;
    }

    v = uberclock_parse_signed(tok2, 1, 10000, "sig3_amp");
    if (v < 1 || v > 10000) return;

    siggen_set_amplitude(ch - 1u, (int16_t)v);
    printf("sig3 ch%u amplitude per tone = %d\n", ch, v);
}

static void command_sig3_freqs(char *args) {
    char *tok_ch = strtok(args, " \t");
    char *tok_f1 = strtok(NULL, " \t");
    char *tok_f2 = strtok(NULL, " \t");
    char *tok_f3 = strtok(NULL, " \t");
    unsigned ch;
    unsigned f1, f2, f3;

    if (!tok_ch || !tok_f1 || !tok_f2 || !tok_f3) {
        puts("Usage: sig3_freqs <ch:1..5> <f1_hz> <f2_hz> <f3_hz>");
        return;
    }

    ch = (unsigned)strtoul(tok_ch, NULL, 0);
    if (ch < 1u || ch > UBERCLOCK_CHANNEL_COUNT) {
        printf("sig3_freqs channel must be 1..%u\n", UBERCLOCK_CHANNEL_COUNT);
        return;
    }

    f1 = (unsigned)strtoul(tok_f1, NULL, 0);
    f2 = (unsigned)strtoul(tok_f2, NULL, 0);
    f3 = (unsigned)strtoul(tok_f3, NULL, 0);
    if (f1 == 0u || f2 == 0u || f3 == 0u) {
        puts("sig3_freqs frequencies must be > 0 Hz");
        return;
    }

    siggen_set_channel_freqs(ch - 1u, f1, f2, f3);
    siggen_update_increments();

    printf("sig3 ch%u freqs = %u, %u, %u Hz\n", ch, f1, f2, f3);
}

static void command_sig3_enable_ch(char *a) {
    unsigned ch = (unsigned)strtoul(a ? a : "0", NULL, 0);

    if (ch < 1u || ch > UBERCLOCK_CHANNEL_COUNT) {
        printf("sig3_enable_ch channel must be 1..%u\n", UBERCLOCK_CHANNEL_COUNT);
        return;
    }

    siggen_enable_channel(ch - 1u);
    printf("sig3 channel %u enabled\n", ch);
}

static void command_sig3_disable_ch(char *a) {
    unsigned ch = (unsigned)strtoul(a ? a : "0", NULL, 0);

    if (ch < 1u || ch > UBERCLOCK_CHANNEL_COUNT) {
        printf("sig3_disable_ch channel must be 1..%u\n", UBERCLOCK_CHANNEL_COUNT);
        return;
    }

    siggen_disable_channel(ch - 1u);
    printf("sig3 channel %u disabled\n", ch);
}

/* ========================================================================
 * UberDDR3 / S2MM (capture-to-DDR) + UDP streamer.
 * ======================================================================== */

static void command_ub_info(char *a) {
    (void)a;
    int cal = ubddr3_calib_done_read();
#ifdef CSR_UBDDR3_BASE
    printf("UBDDR3 CSR base: 0x%08lx  calib_done: %d",
           (unsigned long)CSR_UBDDR3_BASE, cal);
#else
    printf("UBDDR3 CSR base: <not exported>  calib_done: %d", cal);
#endif
    printf("\n");
}

static void command_ub_mode(char *a) {
    (void)a;
    unsigned v = hw_get_cap_enable();
    printf("cap_enable = %u (%s)\n", v, v ? "CAPTURE(design)->DDR" : "RAMP->DDR");
}

static void command_ub_setmode(char *a) {
    unsigned v = (unsigned)strtoul(a ? a : "0", NULL, 0);
    v = v ? 1u : 0u;
    hw_set_cap_enable(v);
    uberclock_commit_config();
    printf("cap_enable = %u (%s)\n", v, v ? "CAPTURE(design)->DDR" : "RAMP->DDR");
}

/* Shared by ub_start/ub_ramp/ub_cap: they all take the same
 * "<addr_hex> [beats] [size]" argument shape. */
static int parse_ub_start_args(char *args, uint64_t *addr, uint32_t *beats, uint8_t *size_code) {
    char *tok_addr  = strtok(args, " \t");
    char *tok_beats = strtok(NULL, " \t");
    char *tok_size  = strtok(NULL, " \t");

    if (!tok_addr) {
        puts("Usage: ub_start <addr_hex> [beats] [size]");
        return 0;
    }

    *addr = strtoull(tok_addr, NULL, 0);
    *beats = (uint32_t)(tok_beats ? strtoul(tok_beats, NULL, 0) : 256u);
    *size_code = dma_size_code_from_string(tok_size);
    return 1;
}

static void command_ub_start(char *args) {
    uint64_t addr; uint32_t beats; uint8_t sz;
    if (!parse_ub_start_args(args, &addr, &beats, &sz)) return;
    dma_start(addr, beats, sz);
}

static void command_ub_ramp2(char *args) {
    uint64_t addr; uint32_t beats; uint8_t sz;
    if (!parse_ub_start_args(args, &addr, &beats, &sz)) return;
    dma_force_ramp_and_start(addr, beats, sz);
}

static void command_ub_cap(char *args) {
    uint64_t addr; uint32_t beats; uint8_t sz;
    if (!parse_ub_start_args(args, &addr, &beats, &sz)) return;
    dma_force_capture_and_start(addr, beats, sz);
}

static void command_ub_wait(char *a) { (void)a; dma_wait(); }

static void command_ub_hexdump(char *a) {
    char *tok_addr = strtok(a, " \t");
    char *tok_len  = strtok(NULL, " \t");
    if (!tok_addr || !tok_len) {
        puts("Usage: ub_hexdump <addr_hex> <bytes>");
        return;
    }
    dma_hexdump(strtoull(tok_addr, NULL, 0), (uint32_t)strtoul(tok_len, NULL, 0));
}

static void command_ub_send(char *args) {
    char *tok_addr = strtok(args, " \t");
    char *tok_len  = strtok(NULL, " \t");
    char *tok_ip   = strtok(NULL, " \t");
    char *tok_port = strtok(NULL, " \t");
    uint32_t dst_ip = 0;

    if (!tok_addr || !tok_len || !tok_ip || !tok_port) {
        puts("Usage: ub_send <addr_hex> <bytes> <dst_ip> <dst_port>");
        return;
    }

    if (uberclock_parse_ipv4(tok_ip, &dst_ip) != 0) {
        puts("Error: bad dst_ip format (use a.b.c.d)");
        return;
    }

    dma_send_udp(strtoull(tok_addr, NULL, 0),
                 (uint32_t)strtoul(tok_len, NULL, 0),
                 dst_ip,
                 (uint16_t)strtoul(tok_port, NULL, 0));
}

/* ========================================================================
 * Registration table.
 * ======================================================================== */

static const struct cmd_entry uberclock_cmd_tbl[] = {
    {"help_uc",              command_help,                "UberClock help", "none"},
    {"fft64_peak",           command_fft64_peak,          "64-point FFT over DS FIFO IQ samples, print peak only", "none"},

    {"phase_nco",            command_phase_nco,           "Set input CORDIC NCO phase increment", "int:0:67108863"},
    {"nco_mag",               command_nco_mag,             "Set NCO magnitude (signed 12-bit)", "int:-2048:2047"},

    {"phase_down",           command_phase_down,          "Set downconversion phase inc: phase_down <ch> <value>", NULL},
    {"phase_down_1",         command_phase_down_1,        "Set downconversion ch1 phase inc (legacy alias)", "int:0:67108863"},
    {"phase_down_2",         command_phase_down_2,        "Set downconversion ch2 phase inc (legacy alias)", "int:0:67108863"},
    {"phase_down_3",         command_phase_down_3,        "Set downconversion ch3 phase inc (legacy alias)", "int:0:67108863"},
    {"phase_down_4",         command_phase_down_4,        "Set downconversion ch4 phase inc (legacy alias)", "int:0:67108863"},
    {"phase_down_5",         command_phase_down_5,        "Set downconversion ch5 phase inc (legacy alias)", "int:0:67108863"},
    {"phase_down_ref",       command_phase_down_ref,      "Set downconversion ref phase inc", "int:0:67108863"},

    {"phase_cpu",            command_phase_cpu,           "Set CPU NCO phase inc: phase_cpu <ch> <value>", NULL},
    {"phase_cpu1",           command_phase_cpu1,          "Set CPU NCO phase inc ch1 (legacy alias)", "int:0:67108863"},
    {"phase_cpu2",           command_phase_cpu2,          "Set CPU NCO phase inc ch2 (legacy alias)", "int:0:67108863"},
    {"phase_cpu3",           command_phase_cpu3,          "Set CPU NCO phase inc ch3 (legacy alias)", "int:0:67108863"},
    {"phase_cpu4",           command_phase_cpu4,          "Set CPU NCO phase inc ch4 (legacy alias)", "int:0:67108863"},
    {"phase_cpu5",           command_phase_cpu5,          "Set CPU NCO phase inc ch5 (legacy alias)", "int:0:67108863"},

    {"mag_cpu",              command_mag_cpu,              "Set CPU NCO magnitude: mag_cpu <ch> <value>", NULL},
    {"mag_cpu1",             command_mag_cpu1,            "Set CPU NCO magnitude ch1 (legacy alias)", "int:-2048:2047"},
    {"mag_cpu2",             command_mag_cpu2,            "Set CPU NCO magnitude ch2 (legacy alias)", "int:-2048:2047"},
    {"mag_cpu3",             command_mag_cpu3,            "Set CPU NCO magnitude ch3 (legacy alias)", "int:-2048:2047"},
    {"mag_cpu4",             command_mag_cpu4,            "Set CPU NCO magnitude ch4 (legacy alias)", "int:-2048:2047"},
    {"mag_cpu5",             command_mag_cpu5,            "Set CPU NCO magnitude ch5 (legacy alias)", "int:-2048:2047"},

    {"output_select_ch1",    command_output_sel_ch1,      "Select DAC1 source (0..15, 14=ref_y, 15=sum)", "int:0:15"},
    {"output_select_ch2",    command_output_sel_ch2,      "Select DAC2 source (0..15, 14=ref_y, 15=sum)", "int:0:15"},
    {"input_select",         command_input_select,        "Set input select register", "enum:0=ADC,1=NCO,2=SUM,3=reserved"},
    {"upsampler_input_mux",  command_ups_in_mux,          "Set upsampler input mux (0..2)", "enum:0=Gain,1=CPU,2=CPU NCO"},

    {"lowspeed_dbg_select",  command_lowspeed_dbg_select, "Select low-speed debug source (0..7, 6=ref_y)", "int:0:7"},
    {"highspeed_dbg_select", command_highspeed_dbg_select,"Select high-speed debug source (0..3, 1=filter_in_1)", "int:0:3"},

    {"upsampler_x",          command_upsampler_x,         "Write upsampler_input_x1..x5 (signed 16-bit)", "int:-32768:32767"},
    {"upsampler_y",          command_upsampler_y,         "Write upsampler_input_y1..y5 (signed 16-bit)", "int:-32768:32767"},
    {"ds_pop",               command_ds_pop,              "Pop one 6-channel downsampled frame from FIFO", "none"},
    {"ds_status",            command_ds_status,           "Show downsample FIFO readable/overflow", "none"},
    {"ups_push",             command_ups_push,            "Push one replicated 5-channel frame into upsampler FIFO", NULL},
    {"ups_status",           command_ups_status,          "Show upsampler FIFO writable/overflow", "none"},
    {"dsp_test",             command_dsp_test,            "Run DSP loop over FIFO samples (optional N)", NULL},
    {"dsp_run",              command_dsp_run,             "Enable/disable non-blocking DSP pump", "enum:0=disable,1=enable"},
    {"fft_fs",               command_fft_fs,              "Set DS sample rate (Hz) used by fft_ds", NULL},
    {"fft_ds",               command_fft_ds,              "Run FFT over downsample FIFO IQ samples and print bins", NULL},
    {"fft_ds_peak",          command_fft_ds_peak,         "Run FFT over downsample FIFO IQ samples and print peak only", NULL},
    {"track3",               command_track3,              "Sweep phase_down_<ch> until the 3-tone pattern is found", NULL},
    {"trackq_start",         command_trackq_start,        "Start measurement-only Y1 validation (no correction): trackq_start <f1> <f2> <f3> [N] [center] [delta1] [delta2] [delta3]", NULL},
    {"trackq_probe",         command_trackq_probe,        "Capture one 3-point tracking snapshot", NULL},
    {"trackq_dump",          command_trackq_dump,         "Stop validation and dump last raw Y1 capture", "none"},
    {"trackq_stop",          command_trackq_stop,         "Stop Y1 validation", "none"},

    {"gain",                 command_gain,                 "Set gain: gain <ch> <int32>", NULL},
    {"gain1",                command_gain1,               "Set gain1 (legacy alias)", NULL},
    {"gain2",                command_gain2,               "Set gain2 (legacy alias)", NULL},
    {"gain3",                command_gain3,               "Set gain3 (legacy alias)", NULL},
    {"gain4",                command_gain4,               "Set gain4 (legacy alias)", NULL},
    {"gain5",                command_gain5,               "Set gain5 (legacy alias)", NULL},
    {"final_shift",          command_final_shift,         "Set final shift", "int:0:7"},

    {"cap_arm",              command_cap_arm_pulse,       "Pulse cap_arm", "none"},
    {"cap_done",             command_cap_done,            "Read cap_done", "none"},
    {"cap_rd",               command_cap_rd,              "Read cap_data at index", "int:0:2047"},

    {"cap_enable",           command_cap_enable,          "0=ramp, 1=capture design to DDR", "enum:0=ramp,1=capture"},
    {"cap_beats",            command_cap_beats,           "Set capture length in 256-bit beats", NULL},

    {"phase",                command_phase_print,         "Print current CORDIC phase (if wired)", "none"},
    {"magnitude",            command_magnitude,           "Print current CORDIC magnitude (if wired)", "none"},
    {"cap_start",            command_cap_start,           "Start LS debug", "none"},
    {"cap_status",           command_cap_status,          "LS debug status", "none"},
    {"cap_dump",             command_cap_dump,             "Ls dump cmd", "none"},

    {"ub_help",              command_dma_help,            "UberDDR3/S2MM help", "none"},
    {"ub_info",               command_ub_info,             "Show UBDDR3 info/state", "none"},
    {"ub_mode",               command_ub_mode,             "Show current cap_enable mode", "none"},
    {"ub_setmode",            command_ub_setmode,          "Set cap_enable (0=ramp,1=capture)", "enum:0=ramp,1=capture"},
    {"ub_start",              command_ub_start,            "Start S2MM using current mode", NULL},
    {"ub_ramp",               command_ub_ramp2,            "Force ramp mode then start S2MM", NULL},
    {"ub_cap",                command_ub_cap,              "Force capture mode then start S2MM", NULL},
    {"ub_wait",               command_ub_wait,             "Wait until DMA done", "none"},
    {"ub_hexdump",            command_ub_hexdump,          "Hexdump DDR memory", NULL},
    {"ub_send",               command_ub_send,             "Send DDR memory region via UDP", NULL},
    {"fft32_ds_y",            command_fft32_ds_y,          "Real FFT of 32 Y samples from DS FIFO", "none"},
    {"sig3_start",            command_sig3_start,          "Start 5 independent 3-tone software generators", "none"},
    {"sig3_stop",             command_sig3_stop,           "Stop 5 independent 3-tone software generators", "none"},
    {"sig3_amp",              command_sig3_amp,            "Set 3-tone per-tone amplitude: sig3_amp <val> | <ch> <val>", NULL},
    {"sig3_freqs",            command_sig3_freqs,          "Set channel 3-tone frequencies: sig3_freqs <ch> <f1> <f2> <f3>", NULL},
    {"sig3_enable_ch",        command_sig3_enable_ch,      "Enable one sig3 channel: sig3_enable_ch <ch>", NULL},
    {"sig3_disable_ch",       command_sig3_disable_ch,     "Disable one sig3 channel: sig3_disable_ch <ch>", NULL},
};

void uberclock_register_commands(void) {
    console_register(uberclock_cmd_tbl, sizeof(uberclock_cmd_tbl) / sizeof(uberclock_cmd_tbl[0]));
}
