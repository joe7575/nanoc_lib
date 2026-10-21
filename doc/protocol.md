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

### 1.7 Built-in external functions (NanoC language)

Available to NanoC programs (registered by the host, not via Lua):

| Function | Description |
|---|---|
| `ta_signal(port, value)` | Queue an outgoing on/off signal |
| `ta_recv(cmd[])` / `ta_recv_sig()` | Read the next incoming command into `cmd` (`[port, topic, p0, …]`) / signal; returns 1/0 |
| `ta_sig_port()` / `ta_sig_val()` | Read the latched incoming signal |
| `key()` | Next key byte, or -1 |
| `sleep(ms)` | Suspend the task for `ms` milliseconds |
| `millis()` | Milliseconds since VM start/reset |

The synchronous Techage command/request functions (`ta_send_cmd`,
`ta_req_data`, `ta_req_data_s`) are **Lua-backed** external
functions registered by the mod (they call techage synchronously and return a
status/answer). Their payload is an `int32[]` array for numeric topics, or a
string for the Beduino string-topic ranges.

`sleep()` makes `run()` return `NANOC_SLEEP` (512). The mod should wait
`nanoclib.sleep_remaining(pos)` ms before running again. Convenience
constants: `nanoclib.END` (0), `nanoclib.ERROR` (1), `nanoclib.BUSY` (3),
`nanoclib.SLEEP` (512).

VM state (PC, variables, stack) is preserved across `run()` calls, so a
`while (1) { ... sleep(...) }` loop behaves like a task.

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
| `0x20` | `PROG_GET` | request the stored program source |
| `0x21` | `PROG_SET` | `[0]`: length, `[1..]`: source bytes (UTF-8, one per word) |
| `0x22` | `SCAN` | `[0]`: radius around the controller (default 8) |
| `0x23` | `CATALOG` | Beduino topic catalog (read-only) |

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
| `10` | `NOT_LOADED` (controller's mapblock is not loaded; no player nearby) |

### 3.4 Execution modes and halt reasons

Modes: `RUN=0`, `HALTED=1`, `STEP=2`, `STEP_OVER=3`, `STEP_OUT=4`.
Halt reasons: `NONE=0`, `BREAKPOINT=1`, `STEP=2`, `HALT=3`, `BREAK_STMT=4`,
`ERROR=5`, `RESET=6`.
Variable kinds: `INT=1`, `STR=2`, `ARR=3`, `CONST=4`.

### 3.5 Response layout

Every response echoes the command and puts a status in `data[0]`:

| Response | Words |
|---|---|
| `ATTACH` | `status, mode, reason, line, pc` |
| `DETACH` | `status` |
| `STATE` | `status, mode, reason, line, pc` |
| `BP_SET` | `status, actual_line` |
| `BP_CLR` / `BP_CLR_ALL` | `status` |
| `HALT` | `status` |
| `CONTINUE` | `status` |
| `STEP` / `STEP_OVER` / `STEP_OUT` | `status, mode, reason, line, pc` |
| `VAR_INFO` | `status, kind, name_len, name_chars..., value_len, value_chars...` (one byte per word) |
| `VAR_WRITE` | `status` |
| `ARR_READ` | `status, first, count, values...` |
| `BACKTRACE` | `status, count, (line, pc) * count` |
| `PROG_GET` | `status, length, bytes...` (one byte per word) |
| `PROG_SET` | `status` |
| `SCAN` | `status, length, bytes...` (UTF-8 JSON, one byte per word) |
| `CATALOG` | `status, length, bytes...` (UTF-8 JSON, one byte per word) |

Events (unsolicited, sent on the debug channel):

| Event | Words |
|---|---|
| `HALTED` | `0x80, reason, line, pc` |
| `ENDED` | `0x81` |
| `DETACHED` | `0x82` |

`CONTINUE`/`HALT` acknowledge immediately; the subsequent stop is reported by
a `HALTED`/`ENDED` event.

The **terminal channel is bidirectional**: server -> client carries output
bytes (section 4); client -> server carries key input bytes.

### 3.6 Focus (single debugger)

`ATTACH` also claims exclusive control of the controller:

- if no owner: granted (`status = OK`); the client becomes the debug owner.
- if another client owns it: denied (`status = DENIED`); the client stays a
  terminal viewer (it still receives output).

Every control command (`STATE`, `BP_*`, `HALT`, `CONTINUE`, `STEP*`, `VAR_*`,
`ARR_READ`, `BACKTRACE`, `PROG_SET`) from a non-owner is answered with
`DENIED`. Terminal key input is **not** a control command and is accepted from
any authenticated client (like the in-game terminal); it is rate-limited.
`PROG_GET` is allowed for any authenticated client. `DETACH` or disconnect
releases the focus, so several players can watch but only one debugs at a time.

### 3.7 Scan payload (TA4)

`SCAN` returns a JSON snapshot of the TA4 techage network around the controller
(read-only, allowed for any authenticated client):

```json
{
  "center": [x, y, z], "radius": 8,
  "nodes": [
    {"pos": [x, y, z], "name": "techage:ta4_chest", "description": "Chest",
     "number": "0102", "owner": "alice", "param2": 0, "state": "RUNNING",
     "caps": [131, 192],
     "inv": {"main": [{"item": "default:stone", "count": 40}]}}
  ],
  "edges": [
    {"from": "0102", "to": "0110", "dir": 2, "via": "tube"}
  ],
  "flow": [
    {"from": "0102", "to": "0107", "role": "pull"},
    {"from": "0107", "to": "0110", "role": "push"}
  ]
}
```

Only TA4 nodes are reported (TA2 is excluded by design). `state` is one of
`RUNNING`, `BLOCKED`, `STANDBY`, `NOPOWER`, `FAULT`, `STOPPED`, `UNLOADED`,
`INACTIVE` (may be `null`). `edges` come from tubelib2 tube lines
(`via = "tube"`) or direct neighbours (`via = "adjacent"`). `flow` lists
directed edges derived from pushers (`pull_dir`/`push_dir`): source ->
pusher (`role = "pull"`) and pusher -> destination (`role = "push"`).

`caps` lists the Beduino topics the node accepts (send, `< 128`) or answers
(request, `>= 128`). `CATALOG` returns the topic reference
(`topics["<num>"] = {kind, name, note/response}`) and the name-pattern rules
(BEP 005). External mods can extend the catalog via
`nanoc.register_capabilities(names, topics, meta)`.

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
| 3 | u8 | channel: `1`=debug, `2`=terminal, `3`=control, `4`=log |
| 4 | u32 | session id |
| 8 | u16 | payload length (bytes) |
| 10 | u16 | reserved = 0 |

### 5.2 Channels

- **control (3)**: handshake / keepalive.
- **debug (1)**: payload = the word messages of section 3.
- **terminal (2)**: payload = raw bytes of the termlib terminal stream
  (control characters, `ESC` sequences `1..7`, `@` commands).
- **log (4)**: server -> client program log messages (NanoC built-in
  `log()`), a raw UTF-8 string. Shown by debugger front ends (e.g. the VS Code
  Debug Console), never mixed into the terminal screen. Rate-limited.

### 5.3 Handshake

1. Client connects (optionally TLS).
2. Client -> control: `HELLO` payload =
   `u32 client_protocol, u8 token_len, token[token_len]`.
3. Server checks protocol + token (per-controller secret, generated by the mod
   and shown to the player in-game).
4. Server -> control: `HELLO_ACK` payload = `u8 status (0 = ok), u32 server_protocol`.
   Status: `0=OK`, `1=BAD_PROTOCOL`, `2=BAD_TOKEN`, `3=BUSY`, `4=NOT_LOADED`.
   `NOT_LOADED` means the controller's mapblock is currently not loaded (no
   player nearby / no forceload), so debugging/starting is refused. Control
   commands (except `DETACH`/`CATALOG`) also answer `NOT_LOADED` if the area
   becomes inactive during a session.
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
