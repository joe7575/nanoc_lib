/*
 * NanoC source-level debugger (fresh implementation).
 *
 * Uses the core debugger primitives (line table, k_DBG_TRAP_N1 software
 * breakpoints, single step) but is decoupled from any transport.
 *
 * AGPL-3.0-or-later
 */

#pragma once

#include "nanoc_host.h"
#include "nc_int.h"

/* Halt reasons (match doc/protocol.md section 3.4) */
enum {
	NANOC_RSN_NONE = 0,
	NANOC_RSN_BREAKPOINT,
	NANOC_RSN_STEP,
	NANOC_RSN_HALT,
	NANOC_RSN_BREAK_STMT,
	NANOC_RSN_ERROR,
	NANOC_RSN_RESET,
};

int  nanoc_dbg_attach(nanoc_vm_t *h, int allow_write, int user_lines);
void nanoc_dbg_detach(nanoc_vm_t *h);

int  nanoc_dbg_set_breakpoint(nanoc_vm_t *h, int line);
int  nanoc_dbg_clear_breakpoint(nanoc_vm_t *h, int line);
void nanoc_dbg_clear_all(nanoc_vm_t *h);

/* Execute a single VM instruction (stepping over a breakpoint trap). */
int  nanoc_dbg_step(nanoc_vm_t *h);

uint16_t nanoc_dbg_pc(nanoc_vm_t *h);
int  nanoc_dbg_line(nanoc_vm_t *h);
int  nanoc_dbg_reason(nanoc_vm_t *h);

/* Line table access (valid until the next compile). */
const nc_dbg_line_t *nanoc_dbg_lines(uint16_t *num);
const nc_dbg_local_t *nanoc_dbg_locals(uint16_t *num);
