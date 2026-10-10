# NanoC protocol specification

Version: **1** (`NANOC_PROTOCOL = 1`)

This document is the contract between `nanoc_lib`, `nanoc_mod` and
`nanoc_client`. It is owned by `nanoc_lib`; the other two pin a protocol
version and validate it at start-up.

Three interfaces are defined, from the inside out:

1. **Lua <-> C module API** (`nanoclib`) — used by the Luanti mod.
2. **Techage command queue** — the in-process "Beduino API" that connects a
   NanoC VM to techage / tubelib2 nodes.
3. **TCP wire protocol** — used by the external Python client (debugger and
   terminal), optionally wrapped in TLS.

Conventions:
- All multi-byte integers on the wire are **little-endian**.
- `status` is a signed 32-bit integer unless stated otherwise.
- Node positions use the same 9-byte hash scheme as `vm2`/`nanobasic`
  (`hash_node_position` / `get_position_from_hash`).

---

## 1. Lua <-> C module API

`IE.require("nanoclib")` returns a table. Identifiers are stable; the mod must
check `nanoclib.protocol()` and `nanoclib.version()` before use.

### 1.1 Meta

| Function | Return | Description |
|---|---|---|
| `version()` | string | Library semver (e.g. `"0.1.0"`) |
| `protocol()` | integer | Wire/API protocol version (`1`) |
| `hash_node_position(pos)` | string(9) | Position hash |
| `get_position_from_hash(hash)` | `{x,y,z}` | Inverse of the above |

### 1.2 VM lifecycle

| Function | Return | Description |
|---|---|---|
| `create(pos[, ram_words])` | bool | Allocate a VM instance |
| `destroy(pos)` | bool | Free the instance |
| `is_loaded(pos)` | bool | Instance exists |
| `reset(pos)` | bool | Reset PC/stack, keep program |

### 1.3 Program

| Function | Return | Description |
|---|---|---|
| `load(pos, source)` | status | Compile `source`, replace program |
| `run(pos, cycles)` | status | Execute up to `cycles` instructions |
| `step(pos)` | status | Execute one instruction (debug) |
| `get_state(pos)` | state | See `NANOC_STATE_*` |

`status` values follow `nc_run()`: `END`, `ERROR`, `BUSY`, `RETI`, `XFUNC`,
and the debugger extension `HALTED`.

### 1.4 Debug

| Function | Return | Description |
|---|---|---|
| `debug_attach(pos, {write=bool, user_lines=int})` | state | Attach debugger |
| `debug_detach(pos)` | status | Detach |
| `set_breakpoint(pos, line)` | int | Returns the resolved line (or status) |
| `clear_breakpoint(pos, line)` | bool | |
| `clear_breakpoints(pos)` | bool | |
| `get_backtrace(pos)` | array of `{frame,line}` | |
| `get_variables(pos, scope)` | array of `{name,scope,kind,value}` | scope `0`=global, `1`=local |
| `write_variable(pos, scope, index, value)` | bool | int32 only |
| `read_arr(pos, scope, index, first)` | array | |

The command/event/status vocabulary matches the NanoC debugger and is reused
verbatim on the TCP wire (section 3).

### 1.5 Memory / registers / IO

| Function | Return | Description |
|---|---|---|
| `peek(pos, addr)` / `poke(pos, addr, value)` | int / bool | Word access |
| `read_mem(pos, addr, num)` / `write_mem(pos, addr, tbl)` | array / int | Block word access |
| `read_ascii(pos, addr, num)` / `write_ascii(pos, addr, str)` | string / int | Char access |
| `get_pc(pos)` / `set_pc(pos, addr)` | int / bool | |
| `get_flags(pos)` | int | `ZERO`, `NEG`, `EOS`, `INT`, `SSTEP` |
| `tty_write(pos, addr, data)` | status | Terminal output |
| `tty_read(pos, addr)` | int | `-1` if empty |

### 1.6 Persistence

| Function | Return | Description |
|---|---|---|
| `pack_vm(pos)` | string | Serialize VM state |
| `unpack_vm(pos, s)` | bool | Restore VM state |

---

## 2. Techage command queue ("Beduino API")

This mirrors the vm2/yauc2 command interface (`io_cmnd.c`) and the techage
Beduino API (`techage.beduino_send_cmnd` / `beduino_request_data` /
`on_beduino_receive_cmnd`). Keeping the same shape lets the yauc2/beduino I/O
modules be adapted with minimal changes.

### 2.1 Port space

| Address range | Ports | Purpose |
|---|---|---|
| `0x0000-0x003F` | 0-15 | serial / tty |
| `0x0100-0x01FF` | 64-127 | techage command ports |

Command ports and signal ports share the 64-127 namespace; they are addressed
by different API calls.

### 2.2 Command structure

```
[ type, length, topic, payload... ]
```
- `type`: `1` = table command (payload is words), `2` = string command
- `length`: number of words = `1 (topic) + payload_words` (or payload chars)
- `topic`: 16-bit
- `payload`: up to 63 words (table) or 32 chars (string)

### 2.3 Lua API

| Function | Return | Description |
|---|---|---|
| `techage_write_cmnd(pos, port, topic, payload)` | status | Queue outgoing command |
| `techage_read_cmnd(pos)` | `port, topic, payload, type` or nil | Drain next command |
| `techage_write_signal(pos, port, signal)` | status | Queue outgoing signal (16-bit) |
| `techage_read_signal(pos)` | `port, signal` or nil | Drain next signal |

`status`/state values match vm2 semantics:
`1` = success / free, `0` = no data, `-1` = error.

### 2.4 Lua-side bridge (in `nanoc_mod`, not in the lib)

The mod maps ports to techage node numbers and forwards via the techage
Beduino API, exactly like `yauc2/src/tech/io_module.lua`:

- `techage.beduino_send_cmnd(src, number, topic, payload)` for `topic < 128`
- `techage.beduino_request_data(src, number, topic, payload)` otherwise
- responses written back with `techage_write_cmnd`
- node callback `on_beduino_receive_cmnd(pos, src, topic, payload)`

---

## 3. Debug word protocol

Reused from the NanoC debugger. One message is a sequence of 32-bit words.

```
message := u32 cmd, u32 data[N]
```

### 3.1 Commands (client -> server)

| Code | Name | Data |
|---|---|---|
| `0x01` | `ATTACH` | `[0]`: bit0 = allow write, `[1]`: last user line (0 = all) |
| `0x02` | `DETACH` | |
| `0x03` | `STATE` | |
| `0x04` | `BP_SET` | `[0]`: line |
| `0x05` | `BP_CLR` | `[0]`: line |
| `0x06` | `BP_CLR_ALL` | |
| `0x07` | `HALT` | |
| `0x08` | `CONTINUE` | |
| `0x09` | `STEP` | |
| `0x0A` | `STEP_OVER` | |
| `0x0B` | `STEP_OUT` | |
| `0x0C` | `VAR_INFO` | `[0]`: scope, `[1]`: index |
| `0x0D` | `VAR_WRITE` | `[0]`: scope, `[1]`: index, `[2]`: value |
| `0x0E` | `ARR_READ` | `[0]`: scope, `[1]`: index, `[2]`: first element |
| `0x0F` | `BACKTRACE` | |

### 3.2 Events (server -> client)

| Code | Name |
|---|---|
| `0x80` | `HALTED` |
| `0x81` | `ENDED` |
| `0x82` | `DETACHED` |

### 3.3 Status codes

| Code | Name |
|---|---|
| `0` | `OK` |
| `1` | `END` (index beyond last item) |
| `2` | `NOT_ATTACHED` |
| `3` | `NO_SCRIPT` |
| `4` | `PARAM` |
| `5` | `NOT_HALTED` |
| `6` | `FULL` |
| `7` | `NO_CODE` |
| `8` | `DENIED` |
| `9` | `UNKNOWN_CMD` |

### 3.4 Execution modes and halt reasons

Modes: `RUN=0`, `HALTED=1`, `STEP=2`, `STEP_OVER=3`, `STEP_OUT=4`.
Halt reasons: `NONE=0`, `BREAKPOINT=1`, `STEP=2`, `HALT=3`, `BREAK_STMT=4`,
`ERROR=5`, `RESET=6`.
Variable kinds: `INT=1`, `STR=2`, `ARR=3`, `CONST=4`.

---

## 4. Terminal control protocol

A minimal ECMA-48 subset, used both by the in-game screen renderer and the
Python client. Print-able UTF-8 bytes are written at the cursor.

| Sequence | Meaning |
|---|---|
| `\n` | line feed (scroll at bottom) |
| `\r` | carriage return |
| `\t` | tab to next multiple of 8 |
| `\x1b[H` | cursor home |
| `\x1b[<row>;<col>H` | cursor position (1-based) |
| `\x1b[2J` | clear screen + home |
| `\x1b[K` / `\x1b[2K` | clear line (to end / whole line) |
| `\x1b[0m` | SGR reset |
| `\x1b[30..37m` / `39` | fg color / default |
| `\x1b[40..47m` / `49` | bg color / default |
| `\x1b[7m` / `27` | reverse video on / off |

Unsupported sequences are ignored. There is no scrolled-back buffer.

---

## 5. TCP wire protocol

The socket carries a sequence of frames. If TLS is used (recommended), the
framing sits on top of the TLS record layer.

### 5.1 Frame header (12 bytes)

| Offset | Size | Field |
|---|---|---|
| 0 | u8 | magic0 = `0x4E` (`'N'`) |
| 1 | u8 | magic1 = `0x43` (`'C'`) |
| 2 | u8 | protocol version |
| 3 | u8 | channel: `1`=debug, `2`=terminal, `3`=control |
| 4 | u32 | session id |
| 8 | u16 | payload length (bytes) |
| 10 | u16 | reserved = 0 |

### 5.2 Channels

- **control (3)**: handshake / keepalive.
- **debug (1)**: payload = the word messages of section 3.
- **terminal (2)**: payload = raw bytes of section 4.

### 5.3 Handshake

1. Client connects (optionally TLS).
2. Client -> control: `HELLO` payload =
   `u32 client_protocol, u8 token_len, token[token_len]`.
3. Server checks protocol + token (per-node/per-session secret, generated by
   the mod and shown to the player in-game).
4. Server -> control: `HELLO_ACK` payload = `u8 status (0 = ok), u32 server_protocol`.
5. Debug/terminal frames may flow. The client must not send debug frames
   before a successful `HELLO_ACK`.

Keepalive: a `PING` control frame must be answered with `PONG`; the server may
detach an idle debug client (default 30 s).

### 5.4 Encryption

- Preferred: TLS (server: `luasec`; client: Python `ssl`). Self-signed cert or
  a deployment-provided cert.
- Minimal deployment: bind to `127.0.0.1` and rely on the session token.
- Do not implement custom crypto.

---

## 6. Versioning

- `NANOC_PROTOCOL` changes on any incompatible wire/API change.
- `nanoclib.version()` is the library semver and is independent of the
  protocol number.
- `nanoc_mod` refuses to start if `nanoclib.protocol() ~= 1` or the library
  version is below the required minimum, logging a clear error.
- `nanoc_client` sends its protocol in `HELLO` and refuses mismatches.

---

## 7. Open points

- Exact `status` return values of the techage queue functions (align with
  vm2 once the port starts).
- Signal vs. command port collision handling.
- Whether terminal and debug run on one port (channels) or two ports.
- Token lifetime / rotation.
