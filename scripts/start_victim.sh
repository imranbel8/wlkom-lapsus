#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VICTIM_DISK="$SCRIPT_DIR/victim.qcow2"
RAM=2048
CPUS=2
SOCKET_PORT=12345

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔵 VM VICTIM"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

[ ! -f "$VICTIM_DISK" ] && echo "❌ Disque introuvable : $VICTIM_DISK" && exit 1

qemu-system-x86_64 \
    -name "WLKOM-Victim" \
    -m $RAM -smp $CPUS -enable-kvm \
    -drive file=$VICTIM_DISK,format=qcow2 \
    -netdev user,id=net0,hostfwd=tcp::2223-:22 \
    -device virtio-net,netdev=net0,mac=52:54:00:12:34:20 \
    -netdev socket,id=net1,connect=127.0.0.1:$SOCKET_PORT \
    -device virtio-net,netdev=net1,mac=52:54:00:12:34:21 \
    -vga virtio -display gtk

echo "✓ VM fermée"
