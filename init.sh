#!/bin/bash
# Main entry point. Run this once on the host before anything else.
#
# What it does:
# 1. Checks the user is not root and that KVM is available.
# 2. Installs host dependencies (QEMU, genisoimage, wget).
# 3. Downloads the Debian 11 cloud base image (~300 MB).
# 4. Calls setup_vm.sh for each VM (disk + cloud-init seed ISO).
# 5. Copies launch scripts to vms/ and prints next steps.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/.env"
source "$ROOT_DIR/scripts/utils.sh"

VM_DIR="$ROOT_DIR/vms"
TEMP_DIR="$ROOT_DIR/temp"
SCRIPTS_DIR="$ROOT_DIR/scripts"
CLOUD_BASE="$TEMP_DIR/debian-cloud-base.qcow2"

check_root() {
    if [ "$EUID" -eq 0 ]; then
        log_error "Do not run as root. sudo is used internally where needed."
    fi
}

check_kvm() {
    if ! grep -qE 'vmx|svm' /proc/cpuinfo; then
        log_error "KVM not supported on this CPU."
    fi
    log_ok "KVM supported"
}

install_deps() {
    log_step "Installing host dependencies"

    if command -v pacman &>/dev/null; then
        log_info "Detected: Arch Linux"
        sudo pacman -S --needed --noconfirm qemu-full cdrtools wget openssh
    elif command -v apt &>/dev/null; then
        log_info "Detected: Debian/Ubuntu"
        sudo apt update -y
        sudo apt install -y qemu-system-x86 qemu-utils genisoimage wget openssh-client
    else
        log_error "Unsupported package manager (expected pacman or apt)."
    fi

    sudo usermod -aG kvm "$USER" 2>/dev/null || true
    log_ok "Dependencies installed — re-login if kvm group was just added"
}

download_cloud_image() {
    log_step "Downloading Debian 12 cloud base image"

    mkdir -p "$TEMP_DIR"

    if [ -f "$CLOUD_BASE" ]; then
        log_ok "Cloud image already present: $CLOUD_BASE (skipping)"
    else
        log_info "Downloading $DEBIAN_CLOUD_URL ..."
        wget -q --show-progress "$DEBIAN_CLOUD_URL" -O "$CLOUD_BASE"
        log_ok "Cloud image downloaded: $CLOUD_BASE"
    fi
}

generate_ssh_key() {
    local key="$VM_DIR/wlkom_key"

    mkdir -p "$VM_DIR"

    if [ ! -f "$key" ]; then
        log_step "Generating SSH key pair for VM access"
        ssh-keygen -t ed25519 -f "$key" -N "" -C "wlkom-vm-access" -q
        log_ok "SSH key: $key"
    fi
}

copy_scripts() {
    log_step "Copying launch scripts to vms/"

    for SCRIPT in start_vm.sh deploy.sh; do
        SRC="$SCRIPTS_DIR/$SCRIPT"
        DST="$VM_DIR/$SCRIPT"
        [ ! -f "$SRC" ] && log_error "Script not found: $SRC"
        cp "$SRC" "$DST"
        chmod +x "$DST"
        log_ok "Copied $SCRIPT"
    done
}

print_next_steps() {
    log_step "Init complete — ready to deploy"
    echo ""
    echo "  1. Run deployment (starts VMs, waits for cloud-init, compiles + loads rootkit):"
    echo "     $VM_DIR/deploy.sh"
    echo ""
    echo "  2. After deploy completes, SSH to attacker and start the control server:"
    echo "     ssh -i $VM_DIR/wlkom_key $VM_USER@localhost -p $ATTACKER_SSH_PORT"
    echo "     cd attacking_program && ./wlkom_control $CONTROL_PORT"
    echo "     (Password: wlk0m_s3cr3t)"
    echo ""
    echo "  SSH shortcuts:"
    echo "    Attacker: ssh -i $VM_DIR/wlkom_key $VM_USER@localhost -p $ATTACKER_SSH_PORT"
    echo "    Victim:   ssh -i $VM_DIR/wlkom_key $VM_USER@localhost -p $VICTIM_SSH_PORT"
    echo "    Creds:    $VM_USER / $VM_PASS"
    echo ""
}

main() {
    check_root
    check_kvm
    install_deps
    download_cloud_image
    generate_ssh_key
    bash "$ROOT_DIR/scripts/setup_vm.sh" attacker
    bash "$ROOT_DIR/scripts/setup_vm.sh" victim
    copy_scripts
    print_next_steps
}

main
