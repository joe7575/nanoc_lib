/*
 * NanoC host layer implementation.
 * AGPL-3.0-or-later
 */

#include "nanoc_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000));
}

/* The NanoC core calls these two global hooks. Because the core keeps its
 * compiler/debugger state globally (single active instance), we track the
 * currently active host instance here. */
static nanoc_vm_t *g_active = NULL;
static int g_initialized = 0;

/* ids of the built-in external functions (assigned during init) */
static uint8_t g_f_ta_signal,
	g_f_ta_recv, g_f_ta_port, g_f_ta_topic, g_f_ta_payload,
	g_f_ta_recv_sig, g_f_ta_sig_port, g_f_ta_sig_val, g_f_key, g_f_sleep,
	g_f_millis;

static int g_num_builtins = 0;

static int host_xfunc(void *user, uint8_t id);

static void ensure_init(void)
{
	if (g_initialized)
		return;
	nc_init();

	uint8_t t_2n[]  = { NB_NUM, NB_NUM };
	uint8_t t_1n[]  = { NB_NUM };
	uint8_t t_0[]   = { NB_NONE };

	/* nc_define_external_function() returns NB_XFUNC + idx; store the idx.
	 * Command/request functions (ta_send_cmd / ta_req_data) are Lua-backed
	 * (see the mod), because they call techage synchronously and return a
	 * status/answer. */
	g_f_ta_signal   = nc_define_external_function("ta_signal", 2, t_2n, NB_NONE) - NB_XFUNC;
	g_f_ta_recv     = nc_define_external_function("ta_recv", 0, t_0, NB_NUM) - NB_XFUNC;
	g_f_ta_port     = nc_define_external_function("ta_port", 0, t_0, NB_NUM) - NB_XFUNC;
	g_f_ta_topic    = nc_define_external_function("ta_topic", 0, t_0, NB_NUM) - NB_XFUNC;
	g_f_ta_payload  = nc_define_external_function("ta_payload", 1, t_1n, NB_NUM) - NB_XFUNC;
	g_f_ta_recv_sig = nc_define_external_function("ta_recv_sig", 0, t_0, NB_NUM) - NB_XFUNC;
	g_f_ta_sig_port = nc_define_external_function("ta_sig_port", 0, t_0, NB_NUM) - NB_XFUNC;
	g_f_ta_sig_val  = nc_define_external_function("ta_sig_val", 0, t_0, NB_NUM) - NB_XFUNC;
	g_f_key         = nc_define_external_function("key", 0, t_0, NB_NUM) - NB_XFUNC;
	g_f_sleep       = nc_define_external_function("sleep", 1, t_1n, NB_NONE) - NB_XFUNC;
	g_f_millis      = nc_define_external_function("millis", 0, t_0, NB_NUM) - NB_XFUNC;

	g_num_builtins = 11;

	g_initialized = 1;
}

/* ---- outgoing queue helpers ---- */

static int post_out_signal(nanoc_vm_t *h, uint16_t port, uint16_t val)
{
	if (h->sig_count >= NANOC_SIGNAL_QUEUE_LEN)
		return -1;
	nanoc_signal_t *s = &h->sig_q[h->sig_tail];
	s->port = port;
	s->signal = val;
	h->sig_tail = (h->sig_tail + 1) % NANOC_SIGNAL_QUEUE_LEN;
	h->sig_count++;
	return 1;
}

/* ---- built-in external function dispatch ---- */

static int host_xfunc(void *user, uint8_t id)
{
	nanoc_vm_t *h = user;
	void *vm = h->vm;

	if (id == g_f_ta_signal) {
		int32_t val = nc_pop_num(vm);
		int32_t port = nc_pop_num(vm);
		post_out_signal(h, (uint16_t)port, (uint16_t)val);
	} else if (id == g_f_ta_recv) {
		if (h->in_cmnd_count == 0) {
			nc_push_num(vm, 0);
		} else {
			h->latch_cmnd = h->in_cmnd_q[h->in_cmnd_head];
			h->latch_cmnd_valid = 1;
			h->in_cmnd_head = (h->in_cmnd_head + 1) % NANOC_CMND_QUEUE_LEN;
			h->in_cmnd_count--;
			nc_push_num(vm, 1);
		}
	} else if (id == g_f_ta_recv_sig) {
		if (h->in_sig_count == 0) {
			nc_push_num(vm, 0);
		} else {
			h->latch_sig = h->in_sig_q[h->in_sig_head];
			h->latch_sig_valid = 1;
			h->in_sig_head = (h->in_sig_head + 1) % NANOC_SIGNAL_QUEUE_LEN;
			h->in_sig_count--;
			nc_push_num(vm, 1);
		}
	} else if (id == g_f_ta_port) {
		nc_push_num(vm, h->latch_cmnd_valid ? h->latch_cmnd.port : 0);
	} else if (id == g_f_ta_topic) {
		nc_push_num(vm, h->latch_cmnd_valid && h->latch_cmnd.length > 0
			? h->latch_cmnd.words[0] : 0);
	} else if (id == g_f_ta_payload) {
		int32_t n = nc_pop_num(vm);   /* 0-based payload index */
		uint16_t v = 0;
		if (h->latch_cmnd_valid && n >= 0 && (uint16_t)(n + 1) < h->latch_cmnd.length)
			v = h->latch_cmnd.words[n + 1];
		nc_push_num(vm, v);
	} else if (id == g_f_ta_sig_port) {
		nc_push_num(vm, h->latch_sig_valid ? h->latch_sig.port : 0);
	} else if (id == g_f_ta_sig_val) {
		nc_push_num(vm, h->latch_sig_valid ? h->latch_sig.signal : 0);
	} else if (id == g_f_key) {
		nc_push_num(vm, nanoc_host_pop_input(h));
	} else if (id == g_f_sleep) {
		int32_t ms = nc_pop_num(vm);
		h->sleeping = 1;
		h->sleep_until_ms = now_ms() + (uint32_t)(ms > 0 ? ms : 0);
		return 1;   /* stop the run loop until the sleep elapses */
	} else if (id == g_f_millis) {
		nc_push_num(vm, (int32_t)(now_ms() - h->started_ms));
	} else {
		nc_push_num(vm, 0);
	}
	return 0;
}

/* ---- core host hooks ---- */

void nc_print(const char *format, ...)
{
	char buf[256];
	va_list ap;
	int n;

	va_start(ap, format);
	n = vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	if (n < 0)
		return;
	if (n > (int)sizeof(buf) - 1)
		n = (int)sizeof(buf) - 1;

	if (g_active)
		nanoc_host_out_write(g_active, buf, (size_t)n);
	else
		fwrite(buf, 1, (size_t)n, stdout);
}

char *nc_get_code_line(void *fp, char *line, int max_line_len)
{
	nanoc_vm_t *h = fp;
	size_t i = 0;

	if (!h || max_line_len <= 0)
		return NULL;
	if (h->line_off >= h->source_len)
		return NULL;

	while (h->line_off < h->source_len && i + 1 < (size_t)max_line_len) {
		char c = h->source[h->line_off++];
		line[i++] = c;
		if (c == '\n')
			break;
	}
	line[i] = '\0';
	return line;
}

/* ---- output buffer ---- */

void nanoc_host_out_write(nanoc_vm_t *h, const char *s, size_t n)
{
	if (h->out_len + n + 1 > h->out_cap) {
		size_t cap = h->out_cap ? h->out_cap : 256;
		while (cap < h->out_len + n + 1)
			cap *= 2;
		char *p = realloc(h->out, cap);
		if (!p)
			return;
		h->out = p;
		h->out_cap = cap;
	}
	memcpy(h->out + h->out_len, s, n);
	h->out_len += n;
	h->out[h->out_len] = '\0';

	/* hard cap: keep only the most recent bytes so the buffer cannot grow
	 * without bound within a single tick */
	if (h->out_len > NANOC_OUT_MAX) {
		size_t drop = h->out_len - NANOC_OUT_MAX;
		memmove(h->out, h->out + drop, h->out_len - drop + 1);
		h->out_len -= drop;
	}
}

const char *nanoc_host_output(nanoc_vm_t *h, size_t *len)
{
	if (len)
		*len = h->out_len;
	return h->out ? h->out : "";
}

void nanoc_host_output_clear(nanoc_vm_t *h)
{
	h->out_len = 0;
	if (h->out)
		h->out[0] = '\0';
}

/* ---- input buffer ---- */

void nanoc_host_push_input(nanoc_vm_t *h, const char *bytes, size_t len)
{
	if (h->in_len + len + 1 > h->in_cap) {
		size_t cap = h->in_cap ? h->in_cap : 64;
		while (cap < h->in_len + len + 1)
			cap *= 2;
		char *p = realloc(h->in, cap);
		if (!p)
			return;
		h->in = p;
		h->in_cap = cap;
	}
	memcpy(h->in + h->in_len, bytes, len);
	h->in_len += len;
	h->in[h->in_len] = '\0';
}

int nanoc_host_pop_input(nanoc_vm_t *h)
{
	if (h->in_len == 0)
		return -1;
	int c = (unsigned char)h->in[0];
	memmove(h->in, h->in + 1, h->in_len - 1);
	h->in_len--;
	h->in[h->in_len] = '\0';
	return c;
}

/* ---- lifecycle ---- */

nanoc_vm_t *nanoc_host_create(void)
{
	ensure_init();

	nanoc_vm_t *h = calloc(1, sizeof(nanoc_vm_t));
	if (!h)
		return NULL;
	h->vm = nc_create();
	if (!h->vm) {
		free(h);
		return NULL;
	}
	h->dbg_mode = 1;
	h->xfunc_cb = host_xfunc;
	h->xfunc_user = h;
	h->started_ms = now_ms();
	return h;
}

void nanoc_host_destroy(nanoc_vm_t *h)
{
	if (!h)
		return;
	if (h->vm)
		nc_destroy(h->vm);
	free(h->source);
	free(h->out);
	free(h->in);
	free(h);
}

void *nanoc_host_vm(nanoc_vm_t *h)
{
	return h ? h->vm : NULL;
}

/* ---- program ---- */

int nanoc_host_load(nanoc_vm_t *h, const char *source)
{
	size_t len = strlen(source);
	char *copy = malloc(len + 1);
	if (!copy)
		return -1;
	memcpy(copy, source, len + 1);

	free(h->source);
	h->source = copy;
	h->source_len = len;
	h->line_off = 0;

	nanoc_host_output_clear(h);

	g_active = h;
	uint16_t err = nc_compile(h->vm, h);
	g_active = NULL;

	for (int i = 0; i < NANOC_MAX_BREAKPOINTS; i++)
		h->bps[i].used = 0;

	nc_reset(h->vm);
	return (int)err;
}

void nanoc_host_reset(nanoc_vm_t *h)
{
	g_active = h;
	nc_reset(h->vm);
	g_active = NULL;
	h->dbg_halted = 0;
	h->dbg_reason = 0;
	h->started_ms = now_ms();
}

uint16_t nanoc_host_run(nanoc_vm_t *h, uint16_t cycles)
{
	g_active = h;

	if (h->sleeping) {
		if (now_ms() < h->sleep_until_ms)
			return NANOC_SLEEP;
		h->sleeping = 0;
	}

	for (;;) {
		uint16_t c = cycles;
		uint16_t ret = nc_run(h->vm, &c);
		cycles = c;

		if (ret >= NB_XFUNC && ret < NB_HALTED) {
			uint8_t id = (uint8_t)(ret - NB_XFUNC);
			if (h->xfunc_cb) {
				if (h->xfunc_cb(h->xfunc_user, id) != 0)
					return NANOC_SLEEP;
			} else {
				nc_push_num(h->vm, 0);
			}
			if (cycles <= 1)
				return NB_BUSY;
			continue;
		}
		if (ret == NB_HALTED) {
			h->dbg_halted = 1;
			h->dbg_reason = 1;
		}
		return ret;
	}
}

int nanoc_host_num_builtins(void)
{
	return g_num_builtins;
}

uint16_t nanoc_host_run_lua(nanoc_vm_t *h, uint16_t cycles, uint16_t *left)
{
	g_active = h;

	if (h->sleeping) {
		if (now_ms() < h->sleep_until_ms) {
			if (left) *left = cycles;
			return NANOC_SLEEP;
		}
		h->sleeping = 0;
	}

	for (;;) {
		uint16_t c = cycles;
		uint16_t ret = nc_run(h->vm, &c);
		cycles = c;

		if (ret >= NB_XFUNC && ret < NB_HALTED) {
			uint8_t id = (uint8_t)(ret - NB_XFUNC);
			if (id < g_num_builtins) {
				if (h->xfunc_cb) {
					if (h->xfunc_cb(h->xfunc_user, id) != 0) {
						if (left) *left = cycles;
						return NANOC_SLEEP;
					}
				} else {
					nc_push_num(h->vm, 0);
				}
				if (cycles <= 1) {
					if (left) *left = cycles;
					return NB_BUSY;
				}
				continue;
			}
			/* Lua-backed external function: return to the caller */
			if (left) *left = cycles;
			return ret;
		}
		if (ret == NB_HALTED) {
			h->dbg_halted = 1;
			h->dbg_reason = 1;
		}
		if (left) *left = cycles;
		return ret;
	}
}

uint32_t nanoc_host_sleep_remaining(nanoc_vm_t *h)
{
	if (!h->sleeping)
		return 0;
	uint32_t now = now_ms();
	return (now < h->sleep_until_ms) ? (h->sleep_until_ms - now) : 0;
}

/* ---- external functions ---- */

uint8_t nanoc_host_define_xfunc(const char *name, uint8_t num_params,
		const uint8_t *types, uint8_t return_type)
{
	ensure_init();
	return nc_define_external_function((char *)name, num_params,
			(uint8_t *)types, return_type);
}

void nanoc_host_set_xfunc_cb(nanoc_vm_t *h,
		int (*cb)(void *user, uint8_t func_id), void *user)
{
	h->xfunc_cb = cb;
	h->xfunc_user = user;
}

void nanoc_host_set_time(nanoc_vm_t *h, uint32_t now_ms)
{
	h->started_ms = now_ms;
}
