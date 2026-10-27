# nanoc_lib

C library + Lua module for **NanoC**, a small C-like language and VM with a
source-level debugger, intended for Luanti/Minetest computer mods.

This repository is the **single source of truth** for the contract between the
three NanoC repositories:

| Repo | Artifact | Distribution |
|---|---|---|
| [`nanoc_lib`](https://github.com/joe7575/nanoc_lib) (this) | `nanoclib.so` (Lua C module) | LuaRocks |
| [`nanoc_mod`](https://github.com/joe7575/nanoc_mod) | Luanti mod (pure Lua) | ContentDB / GitHub |
| [`nanoc_client`](https://github.com/joe7575/nanoc_client) | Python client (debugger / terminal) | pip / pipx |

The wire and API contract lives in [`doc/protocol.md`](doc/protocol.md).

## Status

Working first cut:

- portable VM core (`nc_*`) copied from the original NanoC sources
- `nanoc_host.c` — source loading, `nc_print`/`nc_get_code_line` hooks,
  terminal output/key input, external function dispatch
- `nanoc_debug.c` — software breakpoints, single step, PC/line (fresh rewrite,
  no ETX/MSG/SER, no HMS code)
- `nanoc_techage.c` — techage signal queue (`ta_signal`) and incoming
  command/signal queues
- `lua/nanoclib.c` — Lua binding (see [`doc/protocol.md`](doc/protocol.md))

## Build

```bash
# plain TCP bridge (no external build dependencies)
luarocks make --lua-version 5.1

# with TLS for the external client bridge (needs libssl-dev/openssl-devel)
luarocks make tls/nanoc-0.1.0-2.rockspec --lua-version 5.1
```

The TLS build enables `NANOC_TLS` and links `ssl`/`crypto`. If OpenSSL is in a
non-standard location, pass `OPENSSL_DIR=/path` (or `OPENSSL_INCDIR` and
`OPENSSL_LIBDIR`) to `luarocks`.

Quick smoke test without installing:

```bash
gcc -O2 -fPIC -shared -I/usr/include/lua5.1 -Isrc -o /tmp/nanoclib.so \
    src/nc_scanner.c src/nc_compiler.c src/nc_runtime.c src/nc_memory.c \
    src/nc_kvstore.c src/nanoc_debug.c src/nanoc_host.c \
    src/nanoc_techage.c src/nanoc_net.c lua/nanoclib.c
lua5.1 test/smoke.lua
```

The module is loaded by the mod through the insecure environment:

```lua
local IE = minetest.request_insecure_environment()
local nanoclib = IE.require("nanoclib")
```

## License

AGPLv3 or later. See [`LICENSE.txt`](LICENSE.txt).
The NanoC core originates from MIT-licensed code by the same author and is
relicensed here as AGPLv3; see `NOTICE`.
