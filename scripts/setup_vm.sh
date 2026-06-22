#!/bin/bash

# Setup one VM: creates its disk and cloud-init seed ISO.
# Usage: ./setup_vm.sh <attacker|victim>

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
CLOUD_BASE="$VM_DIR/debian-12-cloud-base.qcow2"
DISK="$VM_DIR/${VM_TYPE}.qcow2"
SEED_ISO="$VM_DIR/seed_${VM_TYPE}.iso"
CI_DIR="$ROOT_DIR/scripts/cloud-init/${VM_TYPE}"
WORK_DIR="$VM_DIR/cloud-init-${VM_TYPE}"

# Resolve IP from .env based on VM type
[ "$VM_TYPE" = "attacker" ] && VM_IP="$ATTACKER_IP" || VM_IP="$VICTIM_IP"

create_disk() {
    log_step "$VM_TYPE — creating disk"

    [ ! -f "$CLOUD_BASE" ] \
        && log_error "Cloud base image not found: $CLOUD_BASE — run init.sh first"

    if [ -f "$DISK" ]; then
        log_warn "Disk already exists: $DISK"
        read -rp "  Delete and recreate? [y/N] " choice
        [[ "$choice" =~ ^[Yy]$ ]] || return
        rm -f "$DISK"
    fi

    # Copy-on-write: the base image is never modified
    qemu-img create -f qcow2 -b "$CLOUD_BASE" -F qcow2 "$DISK" "$DISK_SIZE"
    log_ok "Disk created: $DISK ($DISK_SIZE, backed by cloud base)"
}

build_seed_iso() {
    log_step "$VM_TYPE — building cloud-init seed ISO"

    rm -rf "$WORK_DIR" && mkdir -p "$WORK_DIR"

    sed \
        -e "s/__VM_USER__/$VM_USER/g" \
        -e "s/__VM_PASS__/$VM_PASS/g" \
        "$CI_DIR/user-data" > "$WORK_DIR/user-data"

    sed \
        -e "s/__VM_IP__/$VM_IP/g" \
        "$CI_DIR/network-config" > "$WORK_DIR/network-config"

    # meta-data has no placeholders
    cp "$CI_DIR/meta-data" "$WORK_DIR/meta-data"

    # Volume label must be exactly "cidata" for cloud-init to detect it
    genisoimage \
        -output "$SEED_ISO" \
        -volid cidata \
        -joliet -rock \
        "$WORK_DIR/user-data" \
        "$WORK_DIR/meta-data" \
        "$WORK_DIR/network-config" \
        2>/dev/null

    rm -rf "$WORK_DIR"
    log_ok "Seed ISO built: $SEED_ISO"
}

main() {
    create_disk
    build_seed_iso
}

main
