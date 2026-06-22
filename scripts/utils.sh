#!/bin/bash
# Shared functions sourced by all setup and deploy scripts.
# Requires ROOT_DIR and .env to be loaded before sourcing this file.

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()  { echo -e "${CYAN}[INFO]${NC}  $1"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }
log_step()  { echo -e "\n${BLUE}=== $1 ===${NC}"; }

# SSH options used by deploy.sh and any script that SSHes into VMs
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=3"

ssh_attacker() { ssh $SSH_OPTS -p "$ATTACKER_SSH_PORT" "$VM_USER@localhost" "$@"; }
ssh_victim()   { ssh $SSH_OPTS -p "$VICTIM_SSH_PORT"   "$VM_USER@localhost" "$@"; }

scp_to_attacker() { scp $SSH_OPTS -P "$ATTACKER_SSH_PORT" "$@"; }
scp_to_victim()   { scp $SSH_OPTS -P "$VICTIM_SSH_PORT"   "$@"; }

# Wait up to 60s for SSH to become reachable on a given port
wait_ssh() {
    local port="$1"
    local label="$2"
    log_info "Waiting for SSH on $label (port $port)..."
    for _ in $(seq 1 20); do
        ssh $SSH_OPTS -p "$port" "$VM_USER@localhost" exit 2>/dev/null \
            && log_ok "$label reachable" && return 0
        sleep 3
    done
    log_error "$label unreachable after 60s"
}
