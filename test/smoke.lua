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

-- for loops: empty ranges must not run the body
n.output_clear(pos)
local forsrc = "int32 i = 0\n" ..
    "int32 a = 0\n" ..
    "int32 b = 0\n" ..
    "int32 c = 0\n" ..
    "for i = 1 to 0 {\n" ..
    "    a = a + 1\n" ..
    "}\n" ..
    "for i = 1 to 3 {\n" ..
    "    b = b + i\n" ..
    "}\n" ..
    "for i = 3 to 1 step -1 {\n" ..
    "    c = c + i\n" ..
    "}\n" ..
    "printf(\"%d %d %d\\n\", a, b, c)\n"
assert(n.load(pos, forsrc), "compile failed: " .. n.output(pos))
n.run(pos, 10000)
assert(n.output(pos) == "0 6 6\n", "for loops: " .. n.output(pos))

-- empty for-loop with a function-local loop variable
n.output_clear(pos)
assert(n.load(pos,
    "func int32 g() {\n" ..
    "    int32 s = 0\n" ..
    "    for i = 1 to 0 {\n" ..
    "        s = s + 1\n" ..
    "    }\n" ..
    "    return s\n" ..
    "}\n" ..
    "printf(\"%d\\n\", g())\n"), "compile failed: " .. n.output(pos))
n.run(pos, 10000)
assert(n.output(pos) == "0\n", "local for: " .. n.output(pos))

-- user function calls in expressions (including recursion)
n.output_clear(pos)
assert(n.load(pos,
    "func int32 add(int32 a, int32 b) {\n" ..
    "    return a + b\n" ..
    "}\n" ..
    "func int32 fib(int32 n) {\n" ..
    "    if (n <= 1) {\n" ..
    "        return n\n" ..
    "    }\n" ..
    "    return fib(n - 1) + fib(n - 2)\n" ..
    "}\n" ..
    "int32 r = add(2, 3)\n" ..
    "printf(\"%d %d\\n\", r + add(1, 1), fib(6))\n"), "compile failed: " .. n.output(pos))
n.run(pos, 10000)
assert(n.output(pos) == "7 8\n", "expr calls: " .. n.output(pos))

-- string return value used in an expression
n.output_clear(pos)
assert(n.load(pos,
    "func str8 greeting() {\n" ..
    "    return \"hi\"\n" ..
    "}\n" ..
    "printf(\"%s\\n\", greeting())\n"), "compile failed: " .. n.output(pos))
n.run(pos, 10000)
assert(n.output(pos) == "hi\n", "str return: " .. n.output(pos))

-- calls to an already defined function do not consume forward declarations
n.output_clear(pos)
local callsrc = "func int32 f(int32 x) {\n    return x + 1\n}\nint32 y = 0\n"
for i = 1, 40 do
    callsrc = callsrc .. "y = f(y)\n"
end
callsrc = callsrc .. "printf(\"%d\\n\", y)\n"
assert(n.load(pos, callsrc), "compile failed: " .. n.output(pos))
n.run(pos, 10000)
assert(n.output(pos) == "40\n", "many calls: " .. n.output(pos))


-- statement call to a function defined later (forward reference)
n.output_clear(pos)
assert(n.load(pos,
    "int32 v = 5\n" ..
    "doit(v)\n" ..
    "func doit(int32 x) {\n" ..
    "    printf(\"fwd %d\\n\", x)\n" ..
    "}\n"), "compile failed: " .. n.output(pos))
n.run(pos, 10000)
assert(n.output(pos) == "fwd 5\n", "forward call: " .. n.output(pos))

-- reset must clear the nested for-loop counter
n.output_clear(pos)
assert(n.load(pos,
    "int32 z = 0\n" ..
    "for i = 1 to 2 {\n" ..
    "    for j = 1 to 2 {\n" ..
    "        for k = 1 to 2 {\n" ..
    "            for l = 1 to 2 {\n" ..
    "                z = z + 1\n" ..
    "            }\n" ..
    "        }\n" ..
    "    }\n" ..
    "}\n"), "compile failed: " .. n.output(pos))
n.debug_attach(pos, 1, 0)
n.set_breakpoint(pos, 6)
assert(n.run(pos, 10000) == 256)          -- halted inside four loops
n.reset(pos)
n.output_clear(pos)
assert(n.load(pos,
    "int32 a = 0\n" ..
    "for i = 1 to 1 {\n" ..
    "    for j = 1 to 1 {\n" ..
    "        for k = 1 to 1 {\n" ..
    "            for l = 1 to 1 {\n" ..
    "                for m = 1 to 1 {\n" ..
    "                    a = a + 1\n" ..
    "                }\n" ..
    "            }\n" ..
    "        }\n" ..
    "    }\n" ..
    "}\n" ..
    "printf(\"%d\\n\", a)\n"), "compile failed: " .. n.output(pos))
n.run(pos, 10000)
assert(n.output(pos) == "1\n", "for reset: " .. n.output(pos))


n.destroy(pos)
assert(not n.is_loaded(pos))
print("smoke test OK")
