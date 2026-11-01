// SPDX-FileCopyrightText: 2026 Ahmed Imamovic
// SPDX-License-Identifier: CC-BY-SA-4.0
#ifndef UBERCLOCK_COMMANDS_H
#define UBERCLOCK_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Register every UberClock console command (and its legacy
 *  per-channel aliases) with the shared console. Call once from
 *  uberclock_init(), before the console starts reading input. */
void uberclock_register_commands(void);

#ifdef __cplusplus
}
#endif

#endif /* UBERCLOCK_COMMANDS_H */
