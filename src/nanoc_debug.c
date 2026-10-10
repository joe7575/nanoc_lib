/*
 * NanoC debugger implementation.
 * AGPL-3.0-or-later
 */

#include "nanoc_debug.h"

#include <string.h>

static nanoc_bp_t *find_bp_pc(nanoc_vm_t *h, uint16_t pc)
{
	for (int i = 0; i < NANOC_MAX_BREAKPOINTS; i++)
		if (h->bps[i].used && h->bps[i].pc == pc)
			return &h->bps[i];
	return NULL;
}

static nanoc_bp_t *find_bp_line(nanoc_vm_t *h, int line)
{
	for (int i = 0; i < NANOC_MAX_BREAKPOINTS; i++)
		if (h->bps[i].used && h->bps[i].line == line)
			return &h->bps[i];
	return NULL;
}

int nanoc_dbg_attach(nanoc_vm_t *h, int allow_write, int user_lines)
{
	h->dbg_attached = 1;
	h->dbg_allow_write = allow_write;
	h->dbg_user_lines = user_lines;
	h->dbg_mode = NANOC_RSN_STEP;
	return 0;
}

void nanoc_dbg_detach(nanoc_vm_t *h)
{
	nanoc_dbg_clear_all(h);
	h->dbg_attached = 0;
	h->dbg_halted = 0;
	h->dbg_reason = NANOC_RSN_NONE;
}

int nanoc_dbg_set_breakpoint(nanoc_vm_t *h, int line)
{
	t_VM *vm = h->vm;
	uint16_t num = 0;
	const nc_dbg_line_t *lt = nc_dbg_get_lines(&num);
	uint16_t pc = 0;
	int found = 0;

	for (uint16_t i = 0; i < num; i++) {
		if (lt[i].line == (uint16_t)line) {
			pc = lt[i].pc;
			found = 1;
			break;
		}
	}
	if (!found)
		return -1;
	if (find_bp_pc(h, pc))
		return line; /* already set */

	for (int i = 0; i < NANOC_MAX_BREAKPOINTS; i++) {
		if (!h->bps[i].used) {
			h->bps[i].pc = pc;
			h->bps[i].line = (uint16_t)line;
			h->bps[i].orig_op = vm->code[pc];
			vm->code[pc] = k_DBG_TRAP_N1;
			h->bps[i].used = 1;
			return line;
		}
	}
	return -1;
}

int nanoc_dbg_clear_breakpoint(nanoc_vm_t *h, int line)
{
	t_VM *vm = h->vm;
	nanoc_bp_t *bp = find_bp_line(h, line);
	if (!bp)
		return -1;
	vm->code[bp->pc] = bp->orig_op;
	bp->used = 0;
	return line;
}

void nanoc_dbg_clear_all(nanoc_vm_t *h)
{
	t_VM *vm = h->vm;
	for (int i = 0; i < NANOC_MAX_BREAKPOINTS; i++) {
		if (h->bps[i].used) {
			vm->code[h->bps[i].pc] = h->bps[i].orig_op;
			h->bps[i].used = 0;
		}
	}
}

int nanoc_dbg_step(nanoc_vm_t *h)
{
	t_VM *vm = h->vm;
	uint16_t pc = vm->pc;
	nanoc_bp_t *bp = find_bp_pc(h, pc);
	uint8_t saved = 0;

	if (bp) {
		saved = vm->code[pc];
		vm->code[pc] = bp->orig_op;
	}
	uint16_t ret = nanoc_host_run(h, 2);
	if (bp)
		vm->code[pc] = saved;

	if (ret == NB_HALTED) {
		h->dbg_halted = 1;
		h->dbg_reason = NANOC_RSN_BREAKPOINT;
	} else {
		h->dbg_halted = 0;
		h->dbg_reason = NANOC_RSN_STEP;
	}
	h->dbg_mode = 2;
	return h->dbg_reason;
}

uint16_t nanoc_dbg_pc(nanoc_vm_t *h)
{
	t_VM *vm = h->vm;
	return vm ? vm->pc : 0;
}

int nanoc_dbg_line(nanoc_vm_t *h)
{
	t_VM *vm = h->vm;
	uint16_t num = 0;
	uint16_t pc = vm ? vm->pc : 0;
	const nc_dbg_line_t *lt = nc_dbg_get_lines(&num);
	int line = 0;

	for (uint16_t i = 0; i < num; i++) {
		if (lt[i].pc <= pc)
			line = lt[i].line;
		else
			break;
	}
	return line;
}

int nanoc_dbg_reason(nanoc_vm_t *h)
{
	return h->dbg_reason;
}

const nc_dbg_line_t *nanoc_dbg_lines(uint16_t *num)
{
	return nc_dbg_get_lines(num);
}

const nc_dbg_local_t *nanoc_dbg_locals(uint16_t *num)
{
	return nc_dbg_get_locals(num);
}
