// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
// uberclock_parse.c - generic CLI argument parsing helpers shared by the
// command wrappers in uberclock_commands.c.
#include <stdio.h>
#include <stdlib.h>

#include "uberclock/uberclock_parse.h"
#include "libliteeth/udp.h" /* IPTOINT() */

unsigned uberclock_parse_unsigned(const char *s, unsigned max, const char *what) {
    unsigned v = (unsigned)strtoul(s ? s : "0", NULL, 0);
    if (v >= max)
        printf("Error: %s must be 0..%u\n", what, max - 1);
    return v;
}

int uberclock_parse_signed(const char *s, int minv, int maxv, const char *what) {
    long v = strtol(s ? s : "0", NULL, 0);
    if (v < minv || v > maxv)
        printf("Error: %s must be %d..%d\n", what, minv, maxv);
    return (int)v;
}

int uberclock_parse_ipv4(const char *s, uint32_t *out_ip) {
    unsigned a, b, c, d;
    if (!s)
        return -1;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
        return -1;
    if (a > 255 || b > 255 || c > 255 || d > 255)
        return -1;
    *out_ip = IPTOINT(a, b, c, d);
    return 0;
}
