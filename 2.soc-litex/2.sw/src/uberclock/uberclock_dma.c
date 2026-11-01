// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_dma.c - UberDDR3 S2MM (capture-to-DDR) DMA control and the
// UBD3-framed UDP streamer used to pull a captured region off the board.
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <irq.h>
#include <generated/csr.h>
#include "libliteeth/udp.h" /* LiteEth UDP stack */
#include "ubddr3.h"

#include "uberclock/uberclock_dma.h"
#include "uberclock/uberclock_hw.h"

static inline void dma_cache_sync(void) {
    flush_cpu_dcache();
    flush_l2_cache();
}

uint8_t dma_size_code_from_string(const char *s) {
    if (!s) return 0;             // 00 = bus width
    if (!strcmp(s, "bus")) return 0;
    if (!strcmp(s, "32"))  return 1;
    if (!strcmp(s, "16"))  return 2;
    if (!strcmp(s, "8"))   return 3;
    return 0;
}

/* Issues the actual S2MM request; dma_start()/dma_force_*_and_start() are
 * the public entry points so the mode/beats bookkeeping around it always
 * happens the same way. */
static void dma_issue_request(uint64_t addr, uint8_t size_code) {
    ubddr3_dma_inc_write(1);
    ubddr3_dma_size_write(size_code);
    ubddr3_dma_addr0_write((uint32_t)(addr & 0xffffffffu));
    ubddr3_dma_addr1_write((uint32_t)(addr >> 32));
    ubddr3_dma_req_write(1);
}

void dma_start(uint64_t addr, uint32_t beats, uint8_t size_code) {
    unsigned mode = hw_get_cap_enable();

    printf("S2MM start: mode=%s addr=0x%08lx_%08lx beats=%u size=%s\n",
           mode ? "CAPTURE" : "RAMP",
           (unsigned long)(addr >> 32), (unsigned long)(addr & 0xffffffffu),
           (unsigned)beats,
           (size_code == 0) ? "bus" : (size_code == 1) ? "32" : (size_code == 2) ? "16" : "8");

    hw_set_cap_beats(beats);
    uberclock_commit_config();
    dma_issue_request(addr, size_code);
}

void dma_force_ramp_and_start(uint64_t addr, uint32_t beats, uint8_t size_code) {
    hw_set_cap_enable(0);
    uberclock_commit_config();
    dma_start(addr, beats, size_code);
}

void dma_force_capture_and_start(uint64_t addr, uint32_t beats, uint8_t size_code) {
    hw_set_cap_enable(1);
    uberclock_commit_config();
    dma_start(addr, beats, size_code);
}

void dma_wait(void) {
    printf("Waiting for DMA ... ");
    fflush(stdout);
    while (ubddr3_dma_busy_read()) ;
    dma_cache_sync();
    puts("done.");
    if (ubddr3_dma_err_read())
        puts("DMA error flag is set!");
}

void dma_hexdump(uint64_t addr, uint32_t len) {
    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)addr;

    for (uint32_t i = 0; i < len; i++) {
        if ((i & 0x0f) == 0)
            printf("\n%08lx: ", (unsigned long)((addr + i) & 0xffffffffu));
        printf("%02x ", p[i]);
    }
    puts("");
}

/* ========================================================================
 * UDP DDR streamer (FAST).
 * ======================================================================== */

#define UBD3_MAGIC 0x55424433u /* "UBD3" */

struct __attribute__((packed)) ubd3_hdr {
    uint32_t magic;
    uint32_t seq;
    uint32_t offset;
    uint32_t total;
};

/* Board's own IP for the UDP source stack (change if you want). */
#define UBD3_BOARD_IP IPTOINT(192, 168, 0, 123)

/* Keep payload below MTU. 1400 is safe; 1472 may work with MTU1500. */
#define UBD3_PAYLOAD_MAX 1400u

/* Call udp_service() once every N packets (power-of-two recommended) */
#define UBD3_SERVICE_EVERY 1u

/* Small pacing delay to reduce host-side RX overruns on long bursts. */
#define UBD3_PACE_WAIT_MS 1u

/* 0 disables progress completely */
#define UBD3_PROGRESS_EVERY 0u

static inline void u32le_store(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 0);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void dma_send_udp(uint64_t addr, uint32_t total, uint32_t dst_ip, uint16_t dst_port) {
    uint16_t src_port = dst_port;

    if (total == 0) {
        puts("Error: bytes must be > 0");
        return;
    }

    static const unsigned char board_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0xAB};

    /* eth_init() resets the PHY (ethphy_crg_reset_write) - a real link
     * down/up. Doing that on every send forces the link to renegotiate
     * each time, which many switches/routers answer with a few seconds of
     * blocked forwarding (STP/port re-learning) - long enough to blow past
     * the host's capture timeout on repeated captures. The PHY link stays
     * valid once trained, so only reset it on the first call;
     * udp_set_mac/udp_set_ip/udp_start are cheap software-only resets and
     * stay per-call. */
    static int eth_phy_ready = 0;
    if (!eth_phy_ready) {
        eth_init();
        eth_phy_ready = 1;
    }
    udp_set_mac(board_mac);
    udp_set_ip(UBD3_BOARD_IP);
    udp_start(board_mac, UBD3_BOARD_IP);

    /* ARP resolve (minimal) */
    int ok = 0;
    for (unsigned i = 0; i < 200000; i++) {
        udp_service();
        if (udp_arp_resolve(dst_ip) != 0) { ok = 1; break; }
    }
    if (!ok) {
        puts("No ARP reply.");
        return;
    }

    volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)addr;

    const uint32_t hdr_sz = 16u;
    const uint32_t max_data = (UBD3_PAYLOAD_MAX > hdr_sz) ? (UBD3_PAYLOAD_MAX - hdr_sz) : 0u;
    if (max_data < 64u) {
        puts("Error: UBD3_PAYLOAD_MAX too small");
        return;
    }

    uint32_t sent = 0;
    uint32_t seq = 0;

    /* If SERVICE_EVERY is not power-of-two, fallback to modulo logic */
    const uint32_t service_mask = (UBD3_SERVICE_EVERY && ((UBD3_SERVICE_EVERY & (UBD3_SERVICE_EVERY - 1u)) == 0u))
        ? (UBD3_SERVICE_EVERY - 1u)
        : 0u;

    while (sent < total) {
        /* keep the network stack alive (not every packet) */
        if (UBD3_SERVICE_EVERY) {
            if (service_mask) {
                if ((seq & service_mask) == 0u) udp_service();
            } else {
                if ((seq % UBD3_SERVICE_EVERY) == 0u) udp_service();
            }
        }

        uint32_t chunk = total - sent;
        if (chunk > max_data) chunk = max_data;

        uint8_t *tx = (uint8_t *)udp_get_tx_buffer();
        if (!tx) return;

        /* Write header directly (little-endian) */
        u32le_store(tx + 0, UBD3_MAGIC);
        u32le_store(tx + 4, seq);
        u32le_store(tx + 8, sent);
        u32le_store(tx + 12, total);

        memcpy(tx + hdr_sz, (const void *)(p + sent), chunk);
        (void)udp_send(src_port, dst_port, (unsigned)(hdr_sz + chunk));
        if (UBD3_PACE_WAIT_MS)
            busy_wait(UBD3_PACE_WAIT_MS);

        sent += chunk;
        seq++;

        if (UBD3_PROGRESS_EVERY && ((seq % UBD3_PROGRESS_EVERY) == 0u)) {
            printf("sent %lu / %lu\n", (unsigned long)sent, (unsigned long)total);
        }
    }
}
