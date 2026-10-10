/*
 * NanoC host layer implementation.
 * AGPL-3.0-or-later
 */

#include "nanoc_host.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The NanoC core calls these two global hooks. Because the core keeps its
 * compiler/debugger state globally (single active instance), we track the
 * currently active host instance here. */
static nanoc_vm_t *g_active = NULL;
static int g_initialized = 0;

static void ensure_init(void)
{
	if (!g_initialized) {
		nc_init();
		g_initialized = 1;
	}
}

/* ---- core host hooks ---- */

void nc_print(const char *format, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);

	if (g_active)
		nanoc_host_out_write(g_active, buf, strlen(buf));
	else
		fputs(buf, stdout);
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

	/* clear breakpoints: the code array was rebuilt */
	for (int i = 0; i < NANOC_MAX_BREAKPOINTS; i++)
		h->bps[i].used = 0;

	return (int)err;
}

void nanoc_host_reset(nanoc_vm_t *h)
{
	g_active = h;
	nc_reset(h->vm);
	g_active = NULL;
	h->dbg_halted = 0;
	h->dbg_reason = 0;
}

uint16_t nanoc_host_run(nanoc_vm_t *h, uint16_t cycles)
{
	g_active = h;

	for (;;) {
		uint16_t c = cycles;
		uint16_t ret = nc_run(h->vm, &c);
		cycles = c;

		if (ret >= NB_XFUNC && ret < NB_HALTED) {
			uint8_t id = (uint8_t)(ret - NB_XFUNC);
			if (h->xfunc_cb)
				h->xfunc_cb(h->xfunc_user, id);
			else
				nc_push_num(h->vm, 0);
			if (cycles <= 1)
				return NB_BUSY;
			continue;
		}
		if (ret == NB_HALTED) {
			h->dbg_halted = 1;
			h->dbg_reason = 1; /* breakpoint */
		}
		return ret;
	}
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
