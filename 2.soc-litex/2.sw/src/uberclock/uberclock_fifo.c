// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_fifo.c - DS/upsampler FIFO access patterns: frame read/write
// helpers, the capture-bracket (drain/clear/settle/verify) used by every
// FFT/track capture, and the non-blocking DSP pump test harness.
#include <stdio.h>

#include <generated/csr.h>

#include "uberclock/uberclock_fifo.h"
#include "uberclock/uberclock_runtime.h"

/* Bounded poll count for track3_wait_ds_fifo, below; matches the FIFO's
 * depth many times over, so a real timeout only fires on a genuinely
 * stuck pipeline, not a slow producer. */
#define TRACK3_FIFO_WAIT_POLLS 1000000u

/* These four are intentionally plain file-scope statics, not grouped into
 * a struct: uberclock/2.sw/tests/test_loopback.py extracts ds_capture_begin/
 * ds_capture_end verbatim and recompiles them against its own mock globals
 * of exactly these names (see fifo_ds_capture_stats() below for the public,
 * struct-based accessor other subsystems should use instead). */
static unsigned ds_capture_drained, ds_capture_old_overflow;
static unsigned ds_capture_overflow, ds_capture_underflow;

void ds_fifo_read_frame(iq6_frame_t *frame) {
    main_ds_fifo_pop_write(1);
    frame->x[0] = (int16_t)(main_ds_fifo_x1_read() & 0xffffu);
    frame->y[0] = (int16_t)(main_ds_fifo_y1_read() & 0xffffu);
    frame->x[1] = (int16_t)(main_ds_fifo_x2_read() & 0xffffu);
    frame->y[1] = (int16_t)(main_ds_fifo_y2_read() & 0xffffu);
    frame->x[2] = (int16_t)(main_ds_fifo_x3_read() & 0xffffu);
    frame->y[2] = (int16_t)(main_ds_fifo_y3_read() & 0xffffu);
    frame->x[3] = (int16_t)(main_ds_fifo_x4_read() & 0xffffu);
    frame->y[3] = (int16_t)(main_ds_fifo_y4_read() & 0xffffu);
    frame->x[4] = (int16_t)(main_ds_fifo_x5_read() & 0xffffu);
    frame->y[4] = (int16_t)(main_ds_fifo_y5_read() & 0xffffu);
    frame->x[5] = (int16_t)(main_ds_fifo_xref_read() & 0xffffu);
    frame->y[5] = (int16_t)(main_ds_fifo_yref_read() & 0xffffu);
}

void ups_fifo_write_frame(const iq5_frame_t *frame) {
    main_ups_fifo_x1_write((uint32_t)((int32_t)frame->x[0] & 0xffff));
    main_ups_fifo_y1_write((uint32_t)((int32_t)frame->y[0] & 0xffff));
    main_ups_fifo_x2_write((uint32_t)((int32_t)frame->x[1] & 0xffff));
    main_ups_fifo_y2_write((uint32_t)((int32_t)frame->y[1] & 0xffff));
    main_ups_fifo_x3_write((uint32_t)((int32_t)frame->x[2] & 0xffff));
    main_ups_fifo_y3_write((uint32_t)((int32_t)frame->y[2] & 0xffff));
    main_ups_fifo_x4_write((uint32_t)((int32_t)frame->x[3] & 0xffff));
    main_ups_fifo_y4_write((uint32_t)((int32_t)frame->y[3] & 0xffff));
    main_ups_fifo_x5_write((uint32_t)((int32_t)frame->x[4] & 0xffff));
    main_ups_fifo_y5_write((uint32_t)((int32_t)frame->y[4] & 0xffff));
    main_ups_fifo_push_write(1);
}

void ups_fifo_write_replicated(int16_t x, int16_t y) {
    iq5_frame_t frame = {
        .x = {x, x, x, x, x},
        .y = {y, y, y, y, y},
    };
    ups_fifo_write_frame(&frame);
}

void fifo_clear_all(void) {
    main_ds_fifo_clear_write(1);
    main_ups_fifo_clear_write(1);
}

void fifo_ds_clear(void) {
    main_ds_fifo_clear_write(1);
}

void fifo_ups_clear(void) {
    main_ups_fifo_clear_write(1);
}

void fifo_ds_status(unsigned *readable, unsigned *overflow, unsigned *underflow) {
    unsigned flags = (unsigned)(main_ds_fifo_flags_read() & 0xffu);
    if (readable) *readable = flags & 1u;
    if (overflow) *overflow = (unsigned)(main_ds_fifo_overflow_read() & 1u);
    if (underflow) *underflow = (unsigned)(main_ds_fifo_underflow_read() & 1u);
}

void fifo_ups_status(unsigned *writable, unsigned *overflow, unsigned *underflow) {
    unsigned flags = (unsigned)(main_ups_fifo_flags_read() & 0xffu);
    if (writable) *writable = (flags >> 1) & 1u;
    if (overflow) *overflow = (unsigned)(main_ups_fifo_overflow_read() & 1u);
    if (underflow) *underflow = (unsigned)(main_ups_fifo_underflow_read() & 1u);
}

/* File-local: only ds_capture_begin, below, drains the FIFO this way. */
static unsigned uberclock_ds_fifo_flush(unsigned max_samples) {
    unsigned flushed = 0;

    while (flushed < max_samples && (main_ds_fifo_flags_read() & 0x1u)) {
        /* Track/FFT are for channel 1 on this SoC, so consume the ch1 sample. */
        main_ds_fifo_pop_write(1);
        (void)main_ds_fifo_x1_read();
        (void)main_ds_fifo_y1_read();
        flushed++;
        ce_service_background_budget(4);
    }

    return flushed;
}

int track3_wait_ds_fifo(const char *phase, unsigned sample_idx, unsigned total) {
    unsigned stall = 0;

    while ((main_ds_fifo_flags_read() & 0x1u) == 0u) {
        ce_service_background();
        if (++stall >= TRACK3_FIFO_WAIT_POLLS) {
            printf("track3 %s timeout at sample %u/%u after %u polls\n",
                   phase, sample_idx, total, stall);
            return 0;
        }
    }

    return 1;
}

/* The CSR clear pulse clears sticky errors, NOT queued frames. Drain until
 * empty before every record. A bounded drain that does not reach empty fails.
 * 65536 pops covers the 16384-frame FIFO plus producer activity while draining.
 */
int ds_capture_end(void) {
    /* Allow write-domain overflow to cross the synchronizer before checking. */
    for (unsigned i = 0; i < 32u; ++i)
        (void)main_ds_fifo_flags_read();
    ds_capture_overflow = main_ds_fifo_overflow_read();
    ds_capture_underflow = main_ds_fifo_underflow_read();
    if (ds_capture_overflow || ds_capture_underflow) {
        printf("capture rejected: overflow=%u underflow=%u drained=%u\n",
               ds_capture_overflow, ds_capture_underflow, ds_capture_drained);
        return 0;
    }
    return 1;
}

int ds_capture_begin(unsigned settle) {
    ds_capture_old_overflow = main_ds_fifo_overflow_read();
    ds_capture_overflow = ds_capture_underflow = 0u;
    ds_capture_drained = uberclock_ds_fifo_flush(65536u);
    if (main_ds_fifo_flags_read() & 1u) {
        puts("capture rejected: FIFO drain limit reached (reader cannot catch producer)");
        return 0;
    }

    main_ds_fifo_clear_write(1);
    /* Clear traverses SYS->UC and its status returns UC->SYS. Do not mistake
     * the old sticky overflow for an error in the new record. */
    for (unsigned i = 0; i < 32u; ++i)
        (void)main_ds_fifo_flags_read();
    unsigned polls;
    for (polls = 0; polls < 1024u; ++polls) {
        if (!main_ds_fifo_overflow_read() && !main_ds_fifo_underflow_read())
            break;
    }
    if (polls == 1024u) {
        puts("capture rejected: FIFO error flags did not clear");
        return 0;
    }

    for (unsigned i = 0; i < settle; ++i) {
        iq6_frame_t frame;
        if (!track3_wait_ds_fifo("settle", i, settle)) return 0;
        ds_fifo_read_frame(&frame);
        ce_service_background_budget(4);
    }
    return ds_capture_end();
}

struct uberclock_ds_capture_stats fifo_ds_capture_stats(void) {
    struct uberclock_ds_capture_stats stats;
    stats.drained = ds_capture_drained;
    stats.old_overflow = ds_capture_old_overflow;
    stats.overflow = ds_capture_overflow;
    stats.underflow = ds_capture_underflow;
    return stats;
}

/* ========================================================================
 * Non-blocking DSP pump / software sample history queue.
 * ======================================================================== */

#define DSP_SWQ_LEN 256u

struct dsp_pump_state {
    volatile int enabled;
    volatile uint32_t work_tokens;
    int16_t swq_x[DSP_SWQ_LEN];
    int16_t swq_y[DSP_SWQ_LEN];
    unsigned swq_r;
    unsigned swq_w;
    unsigned swq_count;
};

static struct dsp_pump_state g_dsp_pump;

static inline void dsp_process(int16_t in_x, int16_t in_y, int16_t *out_x, int16_t *out_y) {
    /* Passthrough. */
    *out_x = in_x;
    *out_y = in_y;
}

unsigned uberclock_dsp_pump_step(unsigned max_in, unsigned max_out) {
    (void)max_out; /* no loopback push anymore */

    unsigned popped = 0;

    for (unsigned i = 0; i < max_in; i++) {
        if ((main_ds_fifo_flags_read() & 0x1u) != 0u) {
            iq6_frame_t frame;
            ds_fifo_read_frame(&frame);
            int16_t in_x = frame.x[0];
            int16_t in_y = frame.y[0];

            /* optional processing hook, but no UPS push */
            int16_t out_x = 0, out_y = 0;
            dsp_process(in_x, in_y, &out_x, &out_y);
            (void)out_x;
            (void)out_y;

            /* store for later analysis if wanted */
            g_dsp_pump.swq_x[g_dsp_pump.swq_w] = in_x;
            g_dsp_pump.swq_y[g_dsp_pump.swq_w] = in_y;
            g_dsp_pump.swq_w = (g_dsp_pump.swq_w + 1u) % DSP_SWQ_LEN;
            if (g_dsp_pump.swq_count < DSP_SWQ_LEN) {
                g_dsp_pump.swq_count++;
            } else {
                /* overwrite oldest if buffer full */
                g_dsp_pump.swq_r = (g_dsp_pump.swq_r + 1u) % DSP_SWQ_LEN;
            }

            popped++;
        } else {
            break;
        }
    }

    return popped;
}

void dsp_pump_set_enabled(int enable) {
    g_dsp_pump.enabled = enable ? 1 : 0;
    if (g_dsp_pump.enabled) {
        g_dsp_pump.swq_r = 0;
        g_dsp_pump.swq_w = 0;
        g_dsp_pump.swq_count = 0;
        g_dsp_pump.work_tokens = 0;
        main_ds_fifo_clear_write(1);
    } else {
        g_dsp_pump.work_tokens = 0;
    }
}

int dsp_pump_is_enabled(void) {
    return g_dsp_pump.enabled;
}

void fifo_run_dsp_test(unsigned limit, unsigned *processed_out, unsigned *stall_out) {
    const unsigned STALL_MAX = 1000000u;
    unsigned processed = 0;
    unsigned stall = 0;

    /* Temporarily disable the background pump so this test owns the flow. */
    int prev_enabled = g_dsp_pump.enabled;
    g_dsp_pump.enabled = 0;

    while (!limit || processed < limit) {
        unsigned step = uberclock_dsp_pump_step(64, 64);
        if (step == 0u) {
            if (++stall >= STALL_MAX) break;
            continue;
        }
        stall = 0;
        processed += step;
    }

    g_dsp_pump.enabled = prev_enabled;

    if (processed_out) *processed_out = processed;
    if (stall_out) *stall_out = stall;
}
