// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_FIFO_H
#define UBERCLOCK_FIFO_H

#include "uberclock/uberclock_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Snapshot of the bookkeeping counters from the most recent
 *  ds_capture_begin()/ds_capture_end() bracketed capture. */
struct uberclock_ds_capture_stats {
    unsigned drained;      /**< Samples drained while flushing the FIFO. */
    unsigned old_overflow; /**< Overflow flag observed before the clear. */
    unsigned overflow;     /**< Overflow flag observed after the capture. */
    unsigned underflow;    /**< Underflow flag observed after the capture. */
};

/** @brief Pop and return one 6-"channel" I/Q frame from the DS read FIFO
 *  (channels 1-5 plus the reference channel in slot [5]). */
void ds_fifo_read_frame(iq6_frame_t *frame);

/** @brief Push one 5-channel I/Q frame into the upsampler write FIFO. */
void ups_fifo_write_frame(const iq5_frame_t *frame);

/** @brief Push one (x,y) pair, replicated to all 5 upsampler channels. */
void ups_fifo_write_replicated(int16_t x, int16_t y);

/** @brief Clear both the DS and upsampler FIFOs' sticky error flags. */
void fifo_clear_all(void);

/** @brief Clear only the DS FIFO's sticky error flags. */
void fifo_ds_clear(void);

/** @brief Clear only the upsampler FIFO's sticky error flags. */
void fifo_ups_clear(void);

/** @brief Read the DS FIFO's readable/overflow/underflow status.
 *  @param readable  Non-NULL to receive the readable flag (bit 0).
 *  @param overflow  Non-NULL to receive the overflow flag.
 *  @param underflow Non-NULL to receive the underflow flag. */
void fifo_ds_status(unsigned *readable, unsigned *overflow, unsigned *underflow);

/** @brief Read the upsampler FIFO's writable/overflow/underflow status.
 *  @param writable  Non-NULL to receive the writable flag (bit 1).
 *  @param overflow  Non-NULL to receive the overflow flag.
 *  @param underflow Non-NULL to receive the underflow flag. */
void fifo_ups_status(unsigned *writable, unsigned *overflow, unsigned *underflow);

/** @brief Block (servicing background CE events) until the DS FIFO has a
 *  readable sample, up to a bounded number of polls.
 *  @param phase      Label used in the timeout diagnostic ("capture",
 *                     "settle", ...).
 *  @param sample_idx Current sample index, for the diagnostic message.
 *  @param total      Total samples expected, for the diagnostic message.
 *  @return 1 if a sample became available, 0 on timeout (already
 *          printed a diagnostic). */
int track3_wait_ds_fifo(const char *phase, unsigned sample_idx, unsigned total);

/** @brief Begin a bracketed DS FIFO capture: drain stale samples, clear
 *  sticky error flags, and drain @p settle_samples more to let the
 *  pipeline settle.
 *  @return 1 on success, 0 if the drain/clear failed (diagnostic already
 *          printed; capture must not proceed). */
int ds_capture_begin(unsigned settle_samples);

/** @brief End a bracketed DS FIFO capture: confirm no overflow/underflow
 *  occurred during it.
 *  @return 1 if clean, 0 if an error flag was set (diagnostic already
 *          printed). */
int ds_capture_end(void);

/** @brief Retrieve the stats recorded by the most recent capture bracket,
 *  for callers (e.g. track_poll()'s diagnostics) that want to report them. */
struct uberclock_ds_capture_stats fifo_ds_capture_stats(void);

/** @brief Run (or stop) the non-blocking DSP pump: when enabling, resets
 *  the software sample queue and clears the DS FIFO. */
void dsp_pump_set_enabled(int enable);

/** @brief Whether the non-blocking DSP pump is currently enabled. */
int dsp_pump_is_enabled(void);

/** @brief Pop up to @p max_in available DS FIFO frames, run them through
 *  the (currently passthrough) DSP hook, and append them to the
 *  software history queue.
 *  @return Number of frames popped. */
unsigned uberclock_dsp_pump_step(unsigned max_in, unsigned max_out);

/** @brief Run the DSP pump synchronously, with its own stall timeout,
 *  for the dsp_test diagnostic command.
 *  @param limit         Stop after this many processed samples (0 = run
 *                         until the stall timeout).
 *  @param processed_out Receives the number of samples processed.
 *  @param stall_out     Receives the consecutive-empty-poll count at exit. */
void fifo_run_dsp_test(unsigned limit, unsigned *processed_out, unsigned *stall_out);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_FIFO_H */
