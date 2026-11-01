// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_PARSE_H
#define UBERCLOCK_PARSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Parse an unsigned CLI token, printing "<what> must be 0..<max-1>"
 *  to stdout if it is out of range.
 *  @param s     Token text (NULL treated as "0").
 *  @param max   Exclusive upper bound; valid range is [0, max).
 *  @param what  Name used in the error message.
 *  @return The parsed value regardless of range (caller must still check
 *          it against @p max before using it, matching the original
 *          parse_u() contract). */
unsigned uberclock_parse_unsigned(const char *s, unsigned max, const char *what);

/** @brief Parse a signed CLI token, printing "<what> must be <min>..<max>"
 *  to stdout if it is out of range.
 *  @param s     Token text (NULL treated as "0").
 *  @param minv  Inclusive lower bound.
 *  @param maxv  Inclusive upper bound.
 *  @param what  Name used in the error message.
 *  @return The parsed value regardless of range (caller must still check
 *          it against [minv, maxv] before using it). */
int uberclock_parse_signed(const char *s, int minv, int maxv, const char *what);

/** @brief Parse a dotted-quad IPv4 address string ("a.b.c.d").
 *  @param s        Address text.
 *  @param out_ip   Receives the address packed via IPTOINT() byte order
 *                   on success.
 *  @return 0 on success, -1 if @p s is NULL, malformed, or any octet > 255. */
int uberclock_parse_ipv4(const char *s, uint32_t *out_ip);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_PARSE_H */
