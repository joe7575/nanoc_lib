# nanoc_lib/src — file plan

## Copied: portable NanoC core

The portable VM core was copied from the original NanoC sources. All of these
are (C) Joachim Stolberg, MIT, and are relicensed here as AGPLv3:

| File | Purpose |
|---|---|
| `nc.h`, `nc_int.h`, `nc_cfg.h` | VM core headers / limits |
| `nc_scanner.c` | lexer |
| `nc_compiler.c` | compiler |
| `nc_runtime.c` | VM / interpreter |
| `nc_memory.c` | VM memory |
| `nc_kvstore.c/.h` | key/value store |

No contents were changed during the copy.

## Fresh implementation (written, not copied)

The original **host/scripting/debug layer** (`NCS*.c/.h`) was deliberately
**not** copied: its headers are marked "Copyright (C) HMS Technology Center
Ravensburg GmbH, all rights reserved", which is not compatible with this
repository's AGPLv3 license without explicit permission. It was rewritten
fresh (the algorithms are small and portable):

| New file | Replaces | Notes |
|---|---|---|
| `nanoc_host.c/.h` | `NCSmain.c`, `NCSfunc.c`, `NCS.h`, `NCSint.h` | host hooks `nc_print` / `nc_get_code_line`, per-instance state, I/O buffers, xfunc dispatch |
| `nanoc_debug.c/.h` | `NCSdbg.c` | software breakpoints (TRAP opcode) + single step, no ETX/MSG/SER |
| `nanoc_techage.c/.h` | — | techage command/signal queues (model: vm2 `io_cmnd.c`) |
| `../lua/nanoclib.c` | — | Lua binding (model: vm2 `vm2lua.c`) |

## Porting notes

- Replace `nc_get_code_line` / `nc_print` with host hooks driven from Lua
  (source text from Lua, output to a screen buffer).
- Convert fixed globals (`cfg_MAX_INSTANCES`, `CODE_SIZE`, static arrays) to
  per-instance allocation so one Luanti node = one VM.
- Keep the command/event/status vocabulary from `doc/protocol.md` section 3
  so it can be reused verbatim on the TCP wire.
