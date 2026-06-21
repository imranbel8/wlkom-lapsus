#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VICTIM_DISK="$SCRIPT_DIR/victim.qcow2"
ISO_VICTIM="$SCRIPT_DIR/debian_victim.iso"
RAM=2048
CPUS=2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Installation VM VICTIM"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "Ferme la fenêtre QEMU quand l'installation est terminée"
echo ""

[ ! -f "$VICTIM_DISK" ] && echo "❌ Disque introuvable" && exit 1
[ ! -f "$ISO_VICTIM" ] && echo "❌ ISO introuvable" && exit 1

timeout 3600 qemu-system-x86_64 \
    -name "WLKOM-Victim-Install" \
    -m $RAM -smp $CPUS -enable-kvm \
    -drive file=$VICTIM_DISK,format=qcow2,index=0 \
    -drive file=$ISO_VICTIM,media=cdrom,readonly=on,index=1 \
    -boot once=d \
    -netdev user,id=net0 \
    -device virtio-net,netdev=net0 \
    -vga virtio -display gtk || true

echo ""
echo "✓ Installation terminée"
