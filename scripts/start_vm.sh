#!/bin/bash
# Start a VM. The attacker opens the socket; the victim connects to it.
# Usage: ./start_vm.sh <attacker|victim>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
source "$ROOT_DIR/.env"
source "$ROOT_DIR/scripts/utils.sh"

VM_TYPE="${1:-}"
[ -z "$VM_TYPE" ] && log_error "Usage: $0 <attacker|victim>"
[ "$VM_TYPE" != "attacker" ] && [ "$VM_TYPE" != "victim" ] \
    && log_error "Unknown VM type '$VM_TYPE'. Expected: attacker or victim"

VM_DIR="$ROOT_DIR/vms"
DISK="$VM_DIR/${VM_TYPE}.qcow2"
SEED_ISO="$VM_DIR/seed_${VM_TYPE}.iso"

[ ! -f "$DISK" ]     && log_error "Disk not found: $DISK — run init.sh first"
[ ! -f "$SEED_ISO" ] && log_error "Seed ISO not found: $SEED_ISO — run init.sh first"

if [ "$VM_TYPE" = "attacker" ]; then
    SSH_PORT="$ATTACKER_SSH_PORT"
    MAC_NAT="52:54:00:12:34:10"
    MAC_PRIV="52:54:00:12:34:11"
    # Attacker opens the socket — victim must start after
    NET1="-netdev socket,id=net1,listen=127.0.0.1:${SOCKET_PORT}"
else
    SSH_PORT="$VICTIM_SSH_PORT"
    MAC_NAT="52:54:00:12:34:20"
    MAC_PRIV="52:54:00:12:34:21"
    # Victim connects to the socket opened by the attacker
    NET1="-netdev socket,id=net1,connect=127.0.0.1:${SOCKET_PORT}"
fi

log_info "Starting $VM_TYPE VM..."
log_info "SSH available on localhost:$SSH_PORT after cloud-init (~2 min on first boot)"

qemu-system-x86_64 \
    -name "WLKOM-${VM_TYPE}" \
    -m "$RAM" -smp "$CPUS" -enable-kvm \
    -drive file="$DISK",format=qcow2 \
    -drive file="$SEED_ISO",media=cdrom,readonly=on \
    -netdev user,id=net0,hostfwd=tcp::"${SSH_PORT}"-:22 \
    -device virtio-net,netdev=net0,mac="$MAC_NAT" \
    $NET1 \
    -device virtio-net,netdev=net1,mac="$MAC_PRIV" \
    -vga virtio -display gtk

log_ok "$VM_TYPE VM stopped."
