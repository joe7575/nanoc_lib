/*
 * nanoclib - Lua binding for the NanoC host/debug/techage layer.
 * AGPL-3.0-or-later
 */

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdint.h>
#include <string.h>

#include "nanoc_host.h"
#include "nanoc_debug.h"
#include "nanoc_techage.h"
#include "nanoc_net.h"

#define NANOC_VM_MT "nanoclib.vm"
#define NANOC_REG   "nanoclib.registry"

/* ---- helpers ---- */

static void read_pos(lua_State *L, int idx, int *x, int *y, int *z)
{
	int a = (idx < 0) ? lua_gettop(L) + idx + 1 : idx;
	luaL_checktype(L, a, LUA_TTABLE);
	lua_getfield(L, a, "x"); *x = (int)luaL_checkinteger(L, -1); lua_pop(L, 1);
	lua_getfield(L, a, "y"); *y = (int)luaL_checkinteger(L, -1); lua_pop(L, 1);
	lua_getfield(L, a, "z"); *z = (int)luaL_checkinteger(L, -1); lua_pop(L, 1);
}

static void pos_hash(int x, int y, int z, char out[10])
{
	int c[3] = { x, y, z };
	int idx = 0;
	for (int i = 0; i < 3; i++) {
		uint32_t v = (uint32_t)(c[i] + 32768);
		for (int k = 0; k < 3; k++) {
			out[idx++] = (char)((v & 0x3F) + 48);
			v >>= 6;
		}
	}
	out[9] = '\0';
}

static nanoc_vm_t *check_vm(lua_State *L, int idx)
{
	int x, y, z;
	char hash[10];
	nanoc_vm_t *h = NULL;

	read_pos(L, idx, &x, &y, &z);
	pos_hash(x, y, z, hash);

	lua_getfield(L, LUA_REGISTRYINDEX, NANOC_REG);
	lua_getfield(L, -1, hash);
	if (lua_isuserdata(L, -1))
		h = *(nanoc_vm_t **)lua_touserdata(L, -1);
	lua_pop(L, 2);

	if (!h)
		luaL_error(L, "nanoclib: no VM at position");
	return h;
}

/* ---- meta ---- */

static int l_version(lua_State *L)
{
	lua_pushstring(L, NANOC_VERSION);
	return 1;
}

static int l_protocol(lua_State *L)
{
	lua_pushinteger(L, NANOC_PROTOCOL);
	return 1;
}

static int l_hash_node_position(lua_State *L)
{
	int x, y, z;
	char hash[10];
	/* read from table in place (create pushes nothing) */
	read_pos(L, 1, &x, &y, &z);
	pos_hash(x, y, z, hash);
	lua_pushstring(L, hash);
	return 1;
}

static int l_get_position_from_hash(lua_State *L)
{
	const char *hash = luaL_checkstring(L, 1);
	int c[3];
	if (strlen(hash) < 9)
		return luaL_error(L, "nanoclib: bad hash");
	for (int i = 0; i < 3; i++) {
		uint32_t v = 0;
		for (int k = 0; k < 3; k++)
			v |= (uint32_t)((unsigned char)hash[i * 3 + k] - 48) << (6 * k);
		c[i] = (int)v - 32768;
	}
	lua_newtable(L);
	lua_pushinteger(L, c[0]); lua_setfield(L, -2, "x");
	lua_pushinteger(L, c[1]); lua_setfield(L, -2, "y");
	lua_pushinteger(L, c[2]); lua_setfield(L, -2, "z");
	return 1;
}

/* ---- lifecycle ---- */

static int l_create(lua_State *L)
{
	int x, y, z;
	char hash[10];
	read_pos(L, 1, &x, &y, &z);
	pos_hash(x, y, z, hash);

	lua_getfield(L, LUA_REGISTRYINDEX, NANOC_REG);
	lua_getfield(L, -1, hash);
	if (lua_isuserdata(L, -1)) {
		lua_pushboolean(L, 1);
		return 1;
	}
	lua_pop(L, 1);

	nanoc_vm_t *h = nanoc_host_create();
	if (!h) {
		lua_pushboolean(L, 0);
		return 1;
	}
	nanoc_vm_t **ud = (nanoc_vm_t **)lua_newuserdata(L, sizeof(nanoc_vm_t *));
	*ud = h;
	luaL_getmetatable(L, NANOC_VM_MT);
	lua_setmetatable(L, -2);
	lua_setfield(L, -2, hash); /* reg[hash] = ud */
	lua_pushboolean(L, 1);
	return 1;
}

static int l_destroy(lua_State *L)
{
	int x, y, z;
	char hash[10];
	read_pos(L, 1, &x, &y, &z);
	pos_hash(x, y, z, hash);

	lua_getfield(L, LUA_REGISTRYINDEX, NANOC_REG);
	lua_getfield(L, -1, hash);
	if (lua_isuserdata(L, -1)) {
		nanoc_vm_t **ud = (nanoc_vm_t **)lua_touserdata(L, -1);
		if (*ud) {
			nanoc_host_destroy(*ud);
			*ud = NULL;
		}
	}
	lua_pop(L, 1);
	lua_pushnil(L);
	lua_setfield(L, -2, hash);
	return 0;
}

static int l_is_loaded(lua_State *L)
{
	int x, y, z;
	char hash[10];
	int loaded = 0;
	read_pos(L, 1, &x, &y, &z);
	pos_hash(x, y, z, hash);

	lua_getfield(L, LUA_REGISTRYINDEX, NANOC_REG);
	lua_getfield(L, -1, hash);
	if (lua_isuserdata(L, -1))
		loaded = (*(nanoc_vm_t **)lua_touserdata(L, -1)) != NULL;
	lua_pushboolean(L, loaded);
	return 1;
}

/* ---- program ---- */

static int l_load(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	const char *src = luaL_checkstring(L, 2);
	int err = nanoc_host_load(h, src);
	lua_pushboolean(L, err == 0);
	return 1;
}

static int l_run(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	lua_Integer cycles = luaL_optinteger(L, 2, 10000);
	lua_pushinteger(L, (lua_Integer)nanoc_host_run(h, (uint16_t)cycles));
	return 1;
}

static int l_reset(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	nanoc_host_reset(h);
	return 0;
}

static int l_sleep_remaining(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	lua_pushinteger(L, (lua_Integer)nanoc_host_sleep_remaining(h));
	return 1;
}

static int l_pack_vm(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	t_VM *vm = (t_VM *)nanoc_host_vm(h);
	if (!vm) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushlstring(L, (const char *)vm, sizeof(t_VM));
	return 1;
}

static int l_unpack_vm(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	size_t len = 0;
	const char *s = luaL_checklstring(L, 2, &len);
	t_VM *vm = (t_VM *)nanoc_host_vm(h);
	if (!vm || len != sizeof(t_VM)) {
		lua_pushboolean(L, 0);
		return 1;
	}
	memcpy(vm, s, sizeof(t_VM));
	h->sleeping = 0;
	lua_pushboolean(L, 1);
	return 1;
}

/* ---- terminal I/O ---- */

static int l_output(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	size_t n;
	const char *s = nanoc_host_output(h, &n);
	lua_pushlstring(L, s, n);
	return 1;
}

static int l_output_clear(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	nanoc_host_output_clear(h);
	return 0;
}

static int l_push_key(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	size_t n;
	const char *s = luaL_checklstring(L, 2, &n);
	nanoc_host_push_input(h, s, n);
	return 0;
}

static int l_pop_key(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	int c = nanoc_host_pop_input(h);
	if (c < 0)
		lua_pushnil(L);
	else
		lua_pushinteger(L, c);
	return 1;
}

/* ---- debug ---- */

static int l_debug_attach(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	int write = (int)luaL_optinteger(L, 2, 1);
	int user_lines = (int)luaL_optinteger(L, 3, 0);
	nanoc_dbg_attach(h, write, user_lines);
	return 0;
}

static int l_debug_detach(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	nanoc_dbg_detach(h);
	return 0;
}

static int l_set_breakpoint(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	int line = (int)luaL_checkinteger(L, 2);
	lua_pushinteger(L, nanoc_dbg_set_breakpoint(h, line));
	return 1;
}

static int l_clear_breakpoint(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	int line = (int)luaL_checkinteger(L, 2);
	lua_pushinteger(L, nanoc_dbg_clear_breakpoint(h, line));
	return 1;
}

static int l_clear_breakpoints(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	nanoc_dbg_clear_all(h);
	return 0;
}

static int l_step(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	lua_pushinteger(L, nanoc_dbg_step(h));
	return 1;
}

static int l_get_pc(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	lua_pushinteger(L, nanoc_dbg_pc(h));
	return 1;
}

static int l_get_line(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	lua_pushinteger(L, nanoc_dbg_line(h));
	return 1;
}

static int l_get_reason(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	lua_pushinteger(L, nanoc_dbg_reason(h));
	return 1;
}

/* ---- techage ---- */

static int l_techage_write_cmnd(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	uint16_t port = (uint16_t)luaL_checkinteger(L, 2);
	uint16_t topic = (uint16_t)luaL_checkinteger(L, 3);
	uint16_t words[NANOC_MAX_CMND_WORDS];
	uint16_t n = 0;
	uint16_t type = 1;

	if (lua_type(L, 4) == LUA_TSTRING) {
		size_t len;
		const char *s = lua_tolstring(L, 4, &len);
		type = 2;
		if (len > NANOC_MAX_CMND_WORDS)
			len = NANOC_MAX_CMND_WORDS;
		for (size_t i = 0; i < len; i++)
			words[n++] = (uint16_t)(unsigned char)s[i];
	} else if (lua_istable(L, 4)) {
		int len = (int)lua_objlen(L, 4);
		if (len > NANOC_MAX_CMND_WORDS - 1)
			len = NANOC_MAX_CMND_WORDS - 1;
		for (int i = 0; i < len; i++) {
			lua_rawgeti(L, 4, i + 1);
			words[n++] = (uint16_t)lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
	}
	if (!lua_isnoneornil(L, 5))
		type = (uint16_t)luaL_checkinteger(L, 5);

	lua_pushinteger(L, nanoc_techage_write_cmnd(h, port, topic, words, n, type));
	return 1;
}

static int l_techage_read_cmnd(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	uint16_t port = 0, topic = 0, type = 0, n = 0;
	uint16_t words[NANOC_MAX_CMND_WORDS];

	int sts = nanoc_techage_read_cmnd(h, &port, &topic, words, &n, &type);
	if (sts != 1) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, port);
	lua_pushinteger(L, topic);
	lua_newtable(L);
	for (uint16_t i = 0; i < n; i++) {
		lua_pushinteger(L, words[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_pushinteger(L, type);
	return 4;
}

static int l_techage_write_signal(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	uint16_t port = (uint16_t)luaL_checkinteger(L, 2);
	uint16_t signal = (uint16_t)luaL_checkinteger(L, 3);
	lua_pushinteger(L, nanoc_techage_write_signal(h, port, signal));
	return 1;
}

static int l_techage_read_signal(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	uint16_t port = 0, signal = 0;
	int sts = nanoc_techage_read_signal(h, &port, &signal);
	if (sts != 1) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, port);
	lua_pushinteger(L, signal);
	return 2;
}

/* ---- debug introspection ---- */

static int line_for_pc(const nc_dbg_line_t *lt, uint16_t num, uint16_t pc)
{
	int line = 0;
	for (uint16_t i = 0; i < num; i++) {
		if (lt[i].pc <= pc)
			line = lt[i].line;
		else
			break;
	}
	return line;
}

static int kind_of(uint8_t type)
{
	if (type == ID)
		return 1;      /* INT */
	if (type == STRID)
		return 2;      /* STR */
	if (type == ARR)
		return 3;      /* ARR */
	return 0;
}

static int l_get_variables(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	int scope = (int)luaL_optinteger(L, 2, 0);
	t_VM *vm = (t_VM *)nanoc_host_vm(h);
	int n = 0;

	lua_newtable(L);

	if (scope == 0) {
		uint16_t start = 0;
		sym_t *sym = nc_get_symbol_table(&start);
		for (uint16_t i = start; i < cfg_MAX_NUM_SYM; i++) {
			uint8_t t = sym[i].type;
			int kind = kind_of(t);
			if (sym[i].name[0] == '\0' || sym[i].is_local || kind == 0)
				continue;
			lua_newtable(L);
			lua_pushstring(L, sym[i].name);
			lua_setfield(L, -2, "name");
			lua_pushinteger(L, kind);
			lua_setfield(L, -2, "kind");
			if (kind == 1)
				lua_pushinteger(L, nc_get_number(vm, (uint8_t)sym[i].value));
			else if (kind == 2) {
				char *s = nc_get_string(vm, (uint8_t)sym[i].value);
				lua_pushstring(L, s ? s : "");
			} else
				lua_pushinteger(L, (lua_Integer)sym[i].value);
			lua_setfield(L, -2, "value");
			lua_rawseti(L, -2, ++n);
		}
	} else {
		uint16_t num = 0;
		const nc_dbg_local_t *loc = nc_dbg_get_locals(&num);
		uint16_t pc = vm->pc;
		for (uint16_t i = 0; i < num; i++) {
			if (pc < loc[i].start_pc || pc >= loc[i].end_pc)
				continue;
			int kind = kind_of(loc[i].type);
			if (kind == 0)
				continue;
			lua_newtable(L);
			lua_pushstring(L, loc[i].name);
			lua_setfield(L, -2, "name");
			lua_pushinteger(L, kind);
			lua_setfield(L, -2, "kind");
			if (loc[i].offs < cfg_STACK_SIZE)
				lua_pushinteger(L, vm->stack[vm->fp + loc[i].offs]);
			else
				lua_pushinteger(L, 0);
			lua_setfield(L, -2, "value");
			lua_rawseti(L, -2, ++n);
		}
	}
	return 1;
}

static int l_get_backtrace(lua_State *L)
{
	nanoc_vm_t *h = check_vm(L, 1);
	t_VM *vm = (t_VM *)nanoc_host_vm(h);
	uint16_t num = 0;
	const nc_dbg_line_t *lt = nc_dbg_get_lines(&num);
	uint16_t fp = vm->fp;
	uint16_t pc = vm->pc;
	int n = 0;

	lua_newtable(L);
	for (int f = 0; f < 16; f++) {
		lua_newtable(L);
		lua_pushinteger(L, line_for_pc(lt, num, pc));
		lua_setfield(L, -2, "line");
		lua_pushinteger(L, pc);
		lua_setfield(L, -2, "pc");
		lua_rawseti(L, -2, ++n);

		if (fp < 2 || fp > cfg_STACK_SIZE)
			break;
		uint16_t ret = (uint16_t)vm->stack[fp - 2];
		uint16_t prev_fp = (uint16_t)vm->stack[fp - 1];
		if (ret == 0 || ret >= vm->code_size)
			break;
		pc = ret;
		fp = prev_fp;
	}
	return 1;
}

/* ---- non-blocking TCP sockets ---- */

static int l_net_listen(lua_State *L)
{
	const char *host = luaL_checkstring(L, 1);
	int port = (int)luaL_checkinteger(L, 2);
	int id = nanoc_net_listen(host, port);
	if (id < 0) {
		lua_pushnil(L);
		lua_pushstring(L, "bind failed");
		return 2;
	}
	lua_pushinteger(L, id);
	return 1;
}

static int l_net_accept(lua_State *L)
{
	int id = nanoc_net_accept((int)luaL_checkinteger(L, 1));
	if (id < 0) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, id);
	return 1;
}

static int l_net_recv(lua_State *L)
{
	char buf[4096];
	int id = (int)luaL_checkinteger(L, 1);
	int n = nanoc_net_recv(id, buf, sizeof(buf));
	if (n > 0) {
		lua_pushlstring(L, buf, (size_t)n);
		return 1;
	}
	lua_pushnil(L);
	lua_pushstring(L, n == 0 ? "timeout" : "closed");
	return 2;
}

static int l_net_send(lua_State *L)
{
	int id = (int)luaL_checkinteger(L, 1);
	size_t len = 0;
	const char *data = luaL_checklstring(L, 2, &len);
	if (nanoc_net_send(id, data, (int)len) < 0) {
		lua_pushboolean(L, 0);
		return 1;
	}
	nanoc_net_flush(id);
	lua_pushboolean(L, 1);
	return 1;
}

static int l_net_flush(lua_State *L)
{
	int id = (int)luaL_checkinteger(L, 1);
	lua_pushboolean(L, nanoc_net_flush(id) == 0);
	return 1;
}

static int l_net_close(lua_State *L)
{
	nanoc_net_close((int)luaL_checkinteger(L, 1));
	return 0;
}

/* ---- registration ---- */

static const luaL_Reg nanoc_funcs[] = {
	{ "version",            l_version },
	{ "protocol",           l_protocol },
	{ "hash_node_position", l_hash_node_position },
	{ "get_position_from_hash", l_get_position_from_hash },
	{ "create",             l_create },
	{ "destroy",            l_destroy },
	{ "is_loaded",          l_is_loaded },
	{ "load",               l_load },
	{ "run",                l_run },
	{ "reset",              l_reset },
	{ "sleep_remaining",    l_sleep_remaining },
	{ "pack_vm",            l_pack_vm },
	{ "unpack_vm",          l_unpack_vm },
	{ "output",             l_output },
	{ "output_clear",       l_output_clear },
	{ "push_key",           l_push_key },
	{ "pop_key",            l_pop_key },
	{ "debug_attach",       l_debug_attach },
	{ "debug_detach",       l_debug_detach },
	{ "set_breakpoint",     l_set_breakpoint },
	{ "clear_breakpoint",   l_clear_breakpoint },
	{ "clear_breakpoints",  l_clear_breakpoints },
	{ "step",               l_step },
	{ "get_pc",             l_get_pc },
	{ "get_line",           l_get_line },
	{ "get_reason",         l_get_reason },
	{ "get_variables",      l_get_variables },
	{ "get_backtrace",      l_get_backtrace },
	{ "techage_write_cmnd", l_techage_write_cmnd },
	{ "techage_read_cmnd",  l_techage_read_cmnd },
	{ "techage_write_signal", l_techage_write_signal },
	{ "techage_read_signal", l_techage_read_signal },
	{ "net_listen",         l_net_listen },
	{ "net_accept",         l_net_accept },
	{ "net_recv",           l_net_recv },
	{ "net_send",           l_net_send },
	{ "net_flush",          l_net_flush },
	{ "net_close",          l_net_close },
	{ NULL, NULL }
};

static int gc_vm(lua_State *L)
{
	nanoc_vm_t **ud = (nanoc_vm_t **)luaL_checkudata(L, 1, NANOC_VM_MT);
	if (*ud) {
		nanoc_host_destroy(*ud);
		*ud = NULL;
	}
	return 0;
}

int luaopen_nanoclib(lua_State *L)
{
	/* userdata metatable */
	luaL_newmetatable(L, NANOC_VM_MT);
	lua_pushcfunction(L, gc_vm);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	/* registry table */
	lua_newtable(L);
	lua_setfield(L, LUA_REGISTRYINDEX, NANOC_REG);

	/* module table */
	luaL_register(L, "nanoclib", nanoc_funcs);
	lua_pushinteger(L, NB_END);      lua_setfield(L, -2, "END");
	lua_pushinteger(L, NB_ERROR);    lua_setfield(L, -2, "ERROR");
	lua_pushinteger(L, NB_BUSY);     lua_setfield(L, -2, "BUSY");
	lua_pushinteger(L, NANOC_SLEEP); lua_setfield(L, -2, "SLEEP");
	return 1;
}
