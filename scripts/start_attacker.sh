#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ATTACKER_DISK="$SCRIPT_DIR/attacker.qcow2"
RAM=2048
CPUS=2
SOCKET_PORT=12345

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🔴 VM ATTACKER"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

[ ! -f "$ATTACKER_DISK" ] && echo "❌ Disque introuvable : $ATTACKER_DISK" && exit 1

qemu-system-x86_64 \
    -name "WLKOM-Attacker" \
    -m $RAM -smp $CPUS -enable-kvm \
    -drive file=$ATTACKER_DISK,format=qcow2 \
    -netdev user,id=net0,hostfwd=tcp::2222-:22 \
    -device virtio-net,netdev=net0,mac=52:54:00:12:34:10 \
    -netdev socket,id=net1,listen=127.0.0.1:$SOCKET_PORT \
    -device virtio-net,netdev=net1,mac=52:54:00:12:34:11 \
    -vga virtio -display gtk

echo "✓ VM fermée"
