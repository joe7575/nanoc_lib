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

-- techage outgoing (VM -> mod): program calls ta_signal
n.output_clear(pos)
assert(n.load(pos, "ta_signal(65, 1)\n"))
n.run(pos, 10000)
local p, s = n.techage_read_signal(pos)
assert(p == 65 and s == 1)

-- techage incoming (mod -> VM): program reads via ta_recv(cmd[])
assert(n.techage_write_cmnd(pos, 70, 9, { 5, 6 }) == 1)
n.output_clear(pos)
assert(n.load(pos, "int32 cmd[4]\nint32 r = ta_recv(cmd)\nprintf(\"%d %d %d\\n\", r, cmd[0], cmd[1])\n"))
n.run(pos, 10000)
assert(n.output(pos) == "1 70 9\n", "incoming: " .. n.output(pos))

n.destroy(pos)
assert(not n.is_loaded(pos))
print("smoke test OK")
