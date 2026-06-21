#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ATTACKER_DISK="$SCRIPT_DIR/attacker.qcow2"
ISO_ATTACKER="$SCRIPT_DIR/debian_attacker.iso"
RAM=2048
CPUS=2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Installation VM ATTACKER"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "Ferme la fenêtre QEMU quand l'installation est terminée"
echo ""

[ ! -f "$ATTACKER_DISK" ] && echo "❌ Disque introuvable" && exit 1
[ ! -f "$ISO_ATTACKER" ] && echo "❌ ISO introuvable" && exit 1

timeout 3600 qemu-system-x86_64 \
    -name "WLKOM-Attacker-Install" \
    -m $RAM -smp $CPUS -enable-kvm \
    -drive file=$ATTACKER_DISK,format=qcow2,index=0 \
    -drive file=$ISO_ATTACKER,media=cdrom,readonly=on,index=1 \
    -boot once=d \
    -netdev user,id=net0 \
    -device virtio-net,netdev=net0 \
    -vga virtio -display gtk || true

echo ""
echo "✓ Installation terminée"
