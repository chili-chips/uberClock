// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_DMA_H
#define UBERCLOCK_DMA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Map a size token ("bus"/"32"/"16"/"8", or anything else/NULL)
 *  to the UberDDR3 DMA size code (0=bus, 1=32, 2=16, 3=8). */
uint8_t dma_size_code_from_string(const char *s);

/** @brief Start an S2MM (capture-to-DDR) transfer using whatever source
 *  mode (ramp/capture) is currently selected via hw_set_cap_enable().
 *  Prints a one-line status summary before issuing the request. */
void dma_start(uint64_t addr, uint32_t beats, uint8_t size_code);

/** @brief Force ramp-generator source mode, then dma_start(). */
void dma_force_ramp_and_start(uint64_t addr, uint32_t beats, uint8_t size_code);

/** @brief Force live-design-capture source mode, then dma_start(). */
void dma_force_capture_and_start(uint64_t addr, uint32_t beats, uint8_t size_code);

/** @brief Block until the DMA is no longer busy, flush the CPU/L2 caches
 *  so the CPU sees what the DMA wrote, and report any DMA error flag. */
void dma_wait(void);

/** @brief Hexdump @p len bytes of physical memory starting at @p addr. */
void dma_hexdump(uint64_t addr, uint32_t len);

/** @brief Send @p total bytes of physical memory starting at @p addr to
 *  (@p dst_ip, @p dst_port) over UDP, chunked into UBD3-framed packets.
 *  Resolves the destination's MAC via ARP first; prints "No ARP reply."
 *  and returns if that fails. */
void dma_send_udp(uint64_t addr, uint32_t total, uint32_t dst_ip, uint16_t dst_port);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_DMA_H */
