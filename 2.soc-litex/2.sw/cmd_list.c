#include <stdio.h>
#include "console.h"
#include "cmd_list.h"
#include "uberclock/uberclock_commands.h"
#include "ubddr3.h"

/* Secondary command registry: the DDR/UberDDR3 commands (ubddr3.c) and the
 * cross-cutting "cmdlist" introspection command. The UberClock command
 * table now registers itself (uberclock_register_commands(), called
 * below) from src/uberclock/uberclock_commands.c, next to its handlers,
 * instead of being declared here.
 *
 * arg_spec grammar (see console.h): "none", "int:min:max",
 * "enum:v=label,...", or NULL (GUI falls back to a plain text field - used
 * for commands with unbounded, optional, or multiple arguments, since a
 * wrong guessed numeric range would block legitimate input). */

static void cmd_cmdlist(char *args) {
    (void)args;

    printf("--CMDLIST-BEGIN--\n");
    unsigned ntbls = console_table_count();
    for (unsigned t = 0; t < ntbls; ++t) {
        const struct cmd_entry *tbl = console_table(t);
        unsigned len = console_table_len(t);
        for (unsigned i = 0; i < len; ++i) {
            const char *help = tbl[i].help ? tbl[i].help : "";
            const char *spec = tbl[i].arg_spec ? tbl[i].arg_spec : "";
            printf("%s\t%s\t%s\n", tbl[i].name, help, spec);
        }
    }
    printf("--CMDLIST-END--\n");
}

static const struct cmd_entry cmd_tbl[] = {
    {"cmdlist", cmd_cmdlist, "Dump all commands as name<TAB>help<TAB>arg_spec (machine-parseable)", "none"},

    /* ---- DDR commands (ubddr3.c) ---- */
    {"help_ddr",  cmd_help_ddr,  "DDR/UberDDR3 command list", "none"},
    {"ddrinfo",   cmd_ddrinfo,   "Print DDR base + calib CSR state", "none"},
    {"ddrwait",   cmd_ddrwait,   "Wait for UberDDR3 calibration to complete", "none"},
    {"ddrprobe",  cmd_ddrprobe,  "One 32-bit store/load at base", "none"},
    {"ddrbyte",   cmd_ddrbyte,   "Byte-lane sanity (0..31 at base)", "none"},
    {"ddrtest",   cmd_ddrtest,   "32-bit test, pattern=A5A5^index (default 4 KiB) [KiB]", NULL},
    {"ddrtestb",  cmd_ddrtestb, "Byte test, pattern=A5^index (default 4 KiB) [KiB]", NULL},
    {"ddrmap",    cmd_ddrmap,   "One 256-bit beat lane-map sanity check at base", "none"},
    {"ddrpat",    cmd_ddrpat,   "Pattern test: ddrpat [size] [pattern] [seed] (patterns: a5xor|const|addr|prbs|walk1|walk0)", NULL},
    {"timertest", cmd_timertest,"100 ms sanity (prints source, ticks, us)", "none"},
    {"timeinfo",  cmd_timeinfo, "Show timing source and CLK_HZ", "none"},
};

void cmd_list_register_cmds(void) {
    /* The UberClock command table (name, help, arg_spec) now lives next to
     * its handlers in uberclock_commands.c; this just adds it as another
     * registered table, same as the DDR/cmdlist one below. */
    uberclock_register_commands();
    console_register(cmd_tbl, sizeof(cmd_tbl) / sizeof(cmd_tbl[0]));
}
