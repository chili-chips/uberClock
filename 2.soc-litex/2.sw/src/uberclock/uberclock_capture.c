// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_capture.c - the low-speed on-chip debug capture buffer
// (main_cap_arm/main_cap_done/main_cap_idx/main_cap_data; 2048 entries).
// Distinct from the DDR/S2MM "capture" path in uberclock_dma.c, which
// uses different registers (main_cap_enable/main_cap_beats) entirely.
#include <stdio.h>

#include <generated/csr.h>

#include "uberclock/uberclock_capture.h"
#include "uberclock/uberclock_hw.h"

void capture_arm_pulse(void) {
    main_cap_arm_write(0);
    uberclock_commit_config();

    main_cap_arm_write(1);
    uberclock_commit_config();

    main_cap_arm_write(0);
    uberclock_commit_config();
}

unsigned capture_is_done(void) {
    return main_cap_done_read() & 1u;
}

int16_t capture_read_sample(unsigned idx) {
    main_cap_idx_write(idx);
    uberclock_commit_config();

    /* Dummy read to allow CDC/update latency before the real read. */
    (void)main_cap_data_read();
    return (int16_t)(main_cap_data_read() & 0xffffu);
}

void capture_dump(void) {
    if (!capture_is_done()) {
        puts("Capture not done yet. Use 'cap_status' or wait.");
        return;
    }

    puts("#idx,value");
    for (unsigned i = 0; i < 2048u; ++i)
        printf("%u,%d\n", i, (int)capture_read_sample(i));
}
