-- NanoC library smoke test.
-- Build first, e.g.:
--   gcc -O2 -fPIC -shared -I/usr/include/lua5.1 -Isrc -o /tmp/nanoclib.so \
--       src/nc_scanner.c src/nc_compiler.c src/nc_runtime.c src/nc_memory.c \
--       src/nc_kvstore.c src/nanoc_debug.c src/nanoc_host.c \
--       src/nanoc_techage.c lua/nanoclib.c
-- Run:
--   LUA_CPATH="/tmp/?.so;;" lua5.1 test/smoke.lua

package.cpath = "/tmp/?.so;" .. package.cpath
local n = require("nanoclib")

local pos = { x = 1, y = 2, z = 3 }
assert(n.protocol() == 1, "protocol")
assert(n.get_position_from_hash(n.hash_node_position(pos)).x == 1)

assert(n.create(pos))
assert(n.is_loaded(pos))

-- Note: NanoC is C-like; declarations with initializer ("int32 a = 3").
local src = "int32 a = 3\n" ..
            "a = a + 1\n" ..
            "printf(\"a=%d\\n\", a)\n"
assert(n.load(pos, src), "compile failed: " .. n.output(pos))

n.debug_attach(pos, 1, 0)
assert(n.set_breakpoint(pos, 2) == 2)
assert(n.run(pos, 10000) == 256)          -- NB_HALTED
assert(n.get_reason(pos) == 1)            -- breakpoint
assert(n.get_line(pos) == 2)

n.step(pos)
assert(n.get_pc(pos) > 0)

n.output_clear(pos)
n.run(pos, 10000)
io.write(n.output(pos))

-- techage queue
assert(n.techage_write_cmnd(pos, 64, 7, { 111, 222 }) == 1)
local port, topic, payload, typ = n.techage_read_cmnd(pos)
assert(port == 64 and topic == 7 and payload[1] == 111 and payload[2] == 222 and typ == 1)

assert(n.techage_write_signal(pos, 65, 1) == 1)
local p, s = n.techage_read_signal(pos)
assert(p == 65 and s == 1)

n.destroy(pos)
assert(not n.is_loaded(pos))
print("smoke test OK")
