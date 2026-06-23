# WLKOM — Control Server

TCP control server that listens for rootkit connections, authenticates them, and
exposes an interactive command shell to the operator.

---

## Architecture

```
attacking_program/
├── Makefile
├── include/
│   ├── crypto.h      — password constants, decrypt_caesar, check_password
│   ├── protocol.h    — CMD_* opcodes, send_packet / recv_packet
│   ├── network.h     — client_t, server_init / server_run / server_close
│   └── commands.h    — MAX_ARGS, commands_loop
└── src/
    ├── main.c        — entry point, password gate
    ├── crypto.c      — Caesar cipher, operator password check
    ├── protocol.c    — XOR encryption, packet framing
    ├── network.c     — TCP server, authentication, client handling
    └── commands.c    — interactive dispatch loop
```

The header chain is linear: `commands.h → network.h → protocol.h → crypto.h`.
Each `.c` file only includes its own header, which transitively pulls in everything
it needs. No cross-includes between `.c` files.

---

## Technical choices

### Split from a single `server.c`

The original code lived in one file (`server.c`) that mixed four concerns:
password obfuscation, XOR encryption, packet framing, and TCP socket management.
This made each function hard to locate and violated the single-responsibility
principle. The file was split along its natural seams:

| File | Responsibility |
|---|---|
| `crypto.c` | Caesar-decode the stored password; prompt and validate operator login |
| `protocol.c` | XOR cipher; `send_packet` / `recv_packet` framing |
| `network.c` | TCP socket lifecycle; client acceptance; auth handshake |
| `commands.c` | Interactive loop; command dispatch |

### `client_t` lives in `network.h`

`client_t` is an abstraction of a connected rootkit: it carries the file
descriptor, authentication state, and remote address. Placing it in `network.h`
keeps it close to the code that creates and destroys clients, while letting
`commands.c` use it without knowing anything about sockets.

### `conn_ctx_t` not needed here

Unlike the rootkit (kernel-space), the userspace server can freely pass the
file descriptor as an `int`. The `client_t` struct already bundles `fd` with the
auth flag, so there is no need for an extra context wrapper. Functions stay at
or under 4 parameters throughout.

### Password obfuscation (Caesar shift)

The operator password is stored ROT-3 encoded in `WLKOM_PASSWORD` (in `crypto.h`).
`decrypt_caesar` reverses the shift at runtime before comparison. This prevents
the real password from appearing verbatim in `strings ./wlkom_control`. It is
intentionally lightweight — the goal is to slow down a casual binary inspection,
not to provide cryptographic security.

The decoded password is also what the server sends to the rootkit over the
encrypted channel at authentication time, so both sides share the same secret
without it appearing in plaintext in either binary.

### XOR stream cipher

All packet payloads are XOR-encrypted with a 32-byte key
(`wlk0m_xor_k3y_32bytes_padding__`). The same key and algorithm are used on both
sides. This prevents a passive observer (`tcpdump`, Wireshark) from reading
commands and outputs in cleartext. XOR is symmetric, so the same `xor_crypt`
function is used for both encryption and decryption.

### Packet framing

Each packet is:
```
[1 byte  : opcode]
[4 bytes : payload length, big-endian]
[N bytes : XOR-encrypted payload]
```

The header is sent unencrypted so the receiver can allocate the right buffer
before decryption. `htonl` / `ntohl` ensure byte order is consistent between
architectures.

The `packet_hdr_t` struct uses `__attribute__((packed))` instead of `#pragma pack`
— the former is a GCC attribute and does not require a matching pop directive,
which eliminates the risk of accidentally leaving pack mode active.

### `send_all` / `recv_all`

TCP is a stream protocol: `send()` and `recv()` are not guaranteed to transfer
the requested number of bytes in one call. `send_all` and `recv_all` loop until
all bytes are transferred or an error occurs. Without this, the protocol would
silently corrupt on any partial write.

### `do_auth` and `accept_client` extracted from `handle_client`

`handle_client` was originally 46 lines (over the 40-line norm). Extracting
`do_auth` (sends password, reads response) and `accept_client` (creates and
populates `client_t`) brings each function under the limit and gives each step
a clear name.

### `dispatch` split in `commands.c`

The original `dispatch` function was 89 lines — a flat `if/else` chain repeated
for every command. It was split into:

- `parse_args` — tokenizes the input line with `strtok`, returns 0 on empty input
- `dispatch_hide_file` — handles both `hide_file` and `unhide_file` with a `bool hide` flag
- `dispatch` — now ~20 lines, one `if` per command

### EPITA norm compliance

- No explicit casts except `(struct sockaddr *)` for POSIX socket API
  (`bind`, `accept`) — unavoidable, the API predates `void *` generics in C
- All variable declarations at the top of each block (no declaration after statement)
- No VLAs (`-Wvla`)
- All functions ≤ 40 lines, ≤ 4 parameters

---

## Build

```bash
make          # compile wlkom_control
make clean    # remove build artifacts
make check    # run Criterion test suite (requires libcriterion)
```
