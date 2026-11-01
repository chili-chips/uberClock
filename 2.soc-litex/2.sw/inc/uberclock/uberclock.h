// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_H
#define UBERCLOCK_H

/** @file uberclock.h
 *  @brief Top-level facade for the UberClock subsystem: lifecycle entry
 *  points plus convenience includes of every module header.
 *
 *  ## Module layout
 *  uberclock.c          lifecycle only: runtime state instance, the
 *                       ce_down ISR, uberclock_init()/uberclock_poll(),
 *                       and CE-event service orchestration.
 *  uberclock_hw         config-link commit, Hz<->phase_inc conversion,
 *                       and the generic single-register mux/output/
 *                       debug/final_shift/capture_enable setters.
 *  uberclock_channels   indexed per-channel (phase_down/phase_cpu/
 *                       magnitude_cpu/gain) setters and default init.
 *  uberclock_fifo       DS/upsampler FIFO access patterns, the capture
 *                       bracket, and the non-blocking DSP pump.
 *  uberclock_fft        shared FFT scratch state, DS-FIFO FFT capture,
 *                       bin/band power, and the peak/vertex estimator.
 *  uberclock_track      track3 sweep/lock and trackq measurement-only
 *                       validation (capture + poll state machine).
 *  uberclock_siggen     software 3-tone generator.
 *  uberclock_capture    the low-speed on-chip debug capture buffer.
 *  uberclock_dma        UberDDR3 S2MM DMA control and the UDP streamer.
 *  uberclock_parse      generic CLI argument parsing helpers.
 *  uberclock_commands   command table and thin command_* wrappers.
 *
 *  ## Naming convention
 *  Only the handful of genuinely top-level, cross-module entry points
 *  are prefixed `uberclock_`: uberclock_init(), uberclock_poll(),
 *  uberclock_register_commands(), the indexed uberclock_channel_*()
 *  API, uberclock_commit_config(), uberclock_phase_inc_*() and
 *  uberclock_parse_*(). Prefixing every symbol in every file with
 *  `uberclock_` added noise without adding clarity, so the remaining
 *  cross-file (non-static) functions instead carry a short subsystem
 *  prefix that already says where they live and makes the call site
 *  self-documenting: hw_*() (uberclock_hw.c), fifo_*()/ds_*()/ups_*()
 *  (uberclock_fifo.c), fft_*()/capture_ds_fft_*() (uberclock_fft.c),
 *  track_*()/trackq_*() (uberclock_track.c), siggen_*() (uberclock_
 *  siggen.c), capture_*() (uberclock_capture.c, the low-speed buffer),
 *  dma_*() (uberclock_dma.c), command_*() (uberclock_commands.c, one
 *  thin wrapper per console command). File-local helpers are `static`
 *  and named for what they do, with no prefix requirement at all.
 */

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
#include "uberclock/uberclock_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialize the whole UberClock subsystem: apply power-on
 *  register defaults, start the software signal generator and DSP
 *  pump, and attach the ce_down interrupt. Call once from main(),
 *  after console_init()/uberclock_register_commands(). */
void uberclock_init(void);

/** @brief Service the parts of UberClock that need to run every pass of
 *  the foreground loop: the trackq validation poll and draining any CE
 *  events queued by the ISR since the last call. Call once per
 *  iteration of main()'s loop, alongside console_poll(). */
void uberclock_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_H */
