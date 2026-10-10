/*
 * NanoC host layer
 *
 * Wraps the portable NanoC VM core (nc_*) and provides:
 *  - source loading / compilation from a memory buffer
 *  - the nc_print / nc_get_code_line host hooks
 *  - terminal output and key input buffers
 *  - external function dispatch
 *  - per-instance techage command/signal queues
 *
 * AGPL-3.0-or-later
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nc.h"

#define NANOC_VERSION        "0.1.0"
#define NANOC_PROTOCOL       1

#define NANOC_MAX_BREAKPOINTS 32
#define NANOC_MAX_CMND_WORDS  64   /* topic + up to 63 payload words */
#define NANOC_CMND_QUEUE_LEN  8
#define NANOC_SIGNAL_QUEUE_LEN 16

/* One techage command: [type, length, topic, payload...] */
typedef struct {
	uint16_t type;     /* 1 = table, 2 = string */
	uint16_t length;   /* number of words (topic + payload) */
	uint16_t port;
	uint16_t words[NANOC_MAX_CMND_WORDS];
} nanoc_cmnd_t;

typedef struct {
	uint16_t port;
	uint16_t signal;
} nanoc_signal_t;

typedef struct {
	uint16_t pc;
	uint16_t line;
	uint8_t  orig_op;
	uint8_t  used;
} nanoc_bp_t;

typedef struct {
	/* VM instance (t_VM *) */
	void *vm;

	/* source buffer for nc_get_code_line */
	char  *source;
	size_t source_len;
	size_t line_off;

	/* terminal output */
	char  *out;
	size_t out_len;
	size_t out_cap;

	/* key input */
	char  *in;
	size_t in_len;
	size_t in_cap;

	/* external functions */
	int (*xfunc_cb)(void *user, uint8_t func_id);
	void *xfunc_user;

	/* debugger */
	int dbg_attached;
	int dbg_allow_write;
	int dbg_user_lines;
	int dbg_halted;
	int dbg_reason;      /* last halt reason */
	int dbg_mode;        /* 0=run 1=halted 2=step ... */
	nanoc_bp_t bps[NANOC_MAX_BREAKPOINTS];

	/* techage command queue */
	nanoc_cmnd_t cmnd_q[NANOC_CMND_QUEUE_LEN];
	int cmnd_head, cmnd_tail, cmnd_count;
	/* techage signal queue */
	nanoc_signal_t sig_q[NANOC_SIGNAL_QUEUE_LEN];
	int sig_head, sig_tail, sig_count;
} nanoc_vm_t;

/* ---- lifecycle ---- */
nanoc_vm_t *nanoc_host_create(void);
void nanoc_host_destroy(nanoc_vm_t *h);

/* ---- program ---- */
int nanoc_host_load(nanoc_vm_t *h, const char *source);
uint16_t nanoc_host_run(nanoc_vm_t *h, uint16_t cycles);
void nanoc_host_reset(nanoc_vm_t *h);

void *nanoc_host_vm(nanoc_vm_t *h);

/* ---- terminal I/O ---- */
const char *nanoc_host_output(nanoc_vm_t *h, size_t *len);
void nanoc_host_output_clear(nanoc_vm_t *h);
void nanoc_host_out_write(nanoc_vm_t *h, const char *s, size_t n);
void nanoc_host_push_input(nanoc_vm_t *h, const char *bytes, size_t len);
int  nanoc_host_pop_input(nanoc_vm_t *h);

/* ---- external functions ---- */
uint8_t nanoc_host_define_xfunc(const char *name, uint8_t num_params,
		const uint8_t *types, uint8_t return_type);
void nanoc_host_set_xfunc_cb(nanoc_vm_t *h,
		int (*cb)(void *user, uint8_t func_id), void *user);
