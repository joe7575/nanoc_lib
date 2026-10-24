/*
 * NanoC host layer
 *
 * Wraps the portable NanoC VM core (nc_*) and provides:
 *  - source loading / compilation from a memory buffer
 *  - the nc_print / nc_get_code_line host hooks
 *  - terminal output and key input buffers
 *  - external function dispatch (techage + terminal built-ins)
 *  - per-instance techage command/signal queues
 *
 * Queue directions (from the mod's point of view):
 *  - outgoing (VM -> mod): the NanoC program calls ta_signal();
 *    the mod drains it with nanoclib.techage_read_signal.
 *  - incoming (mod -> VM): the mod pushes with nanoclib.techage_write_cmnd;
 *    the program polls it with ta_recv()/ta_recv_sig().
 *
 * AGPL-3.0-or-later
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nc.h"

#define NANOC_VERSION        "0.2.2"
#define NANOC_PROTOCOL       1

/* Extra return value of nanoc_host_run(): the program called sleep() and is
 * waiting; resume after nanoc_host_sleep_remaining() has elapsed. */
#define NANOC_SLEEP          0x0200

#define NANOC_MAX_BREAKPOINTS 32
#define NANOC_MAX_CMND_WORDS  64   /* topic + up to 63 payload words */
#define NANOC_CMND_QUEUE_LEN  8
#define NANOC_SIGNAL_QUEUE_LEN 16
#define NANOC_OUT_MAX         (32 * 1024)  /* hard cap for the terminal output buffer */

/* Frozen VM image (pack_vm/unpack_vm): magic + format version + struct size.
 * Bump NANOC_VM_FORMAT whenever the t_VM layout, the opcode encoding or the
 * bytecode semantics change in an incompatible way. Old images are then
 * rejected and the controller recompiles its stored program. */
#define NANOC_VM_MAGIC        0x4E43564D  /* "NCVM" */
#define NANOC_VM_FORMAT       2

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
	int dbg_reason;
	int dbg_mode;
	nanoc_bp_t bps[NANOC_MAX_BREAKPOINTS];

	/* outgoing queues (VM -> mod) */
	nanoc_cmnd_t cmnd_q[NANOC_CMND_QUEUE_LEN];
	int cmnd_head, cmnd_tail, cmnd_count;
	nanoc_signal_t sig_q[NANOC_SIGNAL_QUEUE_LEN];
	int sig_head, sig_tail, sig_count;

	/* incoming queues (mod -> VM) */
	nanoc_cmnd_t in_cmnd_q[NANOC_CMND_QUEUE_LEN];
	int in_cmnd_head, in_cmnd_tail, in_cmnd_count;
	nanoc_signal_t in_sig_q[NANOC_SIGNAL_QUEUE_LEN];
	int in_sig_head, in_sig_tail, in_sig_count;

	/* latched incoming item, read via ta_recv()/ta_recv_sig() */
	int latch_cmnd_valid;
	int latch_sig_valid;
	nanoc_cmnd_t latch_cmnd;
	nanoc_signal_t latch_sig;

	/* sleep() state */
	int sleeping;
	uint32_t sleep_until_ms;

	/* millis() time base */
	uint32_t started_ms;
} nanoc_vm_t;

/* ---- lifecycle ---- */
nanoc_vm_t *nanoc_host_create(void);
void nanoc_host_destroy(nanoc_vm_t *h);

/* ---- program ---- */
int nanoc_host_load(nanoc_vm_t *h, const char *source);
uint16_t nanoc_host_run(nanoc_vm_t *h, uint16_t cycles);

/* Run the VM and RETURN to the caller when a Lua-backed external function
 * (id >= nanoc_host_num_builtins()) is called, so Lua can dispatch it.
 * *left is set to the remaining cycle budget. */
uint16_t nanoc_host_run_lua(nanoc_vm_t *h, uint16_t cycles, uint16_t *left);

/* Number of C built-in external functions (everything below this id is
 * handled inside the engine, everything at/above is dispatched by Lua). */
int nanoc_host_num_builtins(void);

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

/* millis since create (used by the millis() built-in) */
void nanoc_host_set_time(nanoc_vm_t *h, uint32_t now_ms);

/* remaining sleep time in ms (0 if not sleeping) */
uint32_t nanoc_host_sleep_remaining(nanoc_vm_t *h);
