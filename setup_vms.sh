#!/bin/bash

# =============================================================================
# WLKOM — Script de configuration automatique des VMs
# =============================================================================

set -e

# =============================================================================
# COULEURS
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
VM_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/vms"
ATTACKER_DISK="$VM_DIR/attacker.qcow2"
VICTIM_DISK="$VM_DIR/victim.qcow2"
ATTACKER_IP="192.168.100.10"
VICTIM_IP="192.168.100.20"
NETWORK_NAME="wlkom"
BRIDGE_NAME="virbr-wlkom"
GATEWAY="192.168.100.1"
DISK_SIZE="20G"
RAM="2048"
CPUS="2"
ISO_URL="https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/debian-12.11.0-amd64-netinst.iso"
ISO_PATH="$VM_DIR/debian.iso"

# =============================================================================
# FONCTIONS UTILITAIRES
# =============================================================================

log_info()    { echo -e "${CYAN}[INFO]${NC}  $1"; }
log_ok()      { echo -e "${GREEN}[OK]${NC}    $1"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }
log_step()    { echo -e "\n${BLUE}========== $1 ==========${NC}"; }

check_root() {
    if [ "$EUID" -eq 0 ]; then
        log_error "Ne pas lancer ce script en root. Utilise sudo uniquement quand nécessaire."
    fi
}

check_kvm() {
    if ! grep -qE 'vmx|svm' /proc/cpuinfo; then
        log_error "KVM non supporté sur ce CPU."
    fi
    log_ok "KVM supporté"
}

# =============================================================================
# ETAPE 1 — DEPENDANCES
# =============================================================================

install_deps() {
    log_step "Installation des dépendances"

    if command -v pacman &>/dev/null; then
        log_info "Détecté : Arch Linux"
        sudo pacman -Sy --noconfirm \
            qemu-full \
            virt-manager \
            libvirt \
            dnsmasq \
            wget \
            openssh
    elif command -v apt &>/dev/null; then
        log_info "Détecté : Debian/Ubuntu"
        sudo apt update -y
        sudo apt install -y \
            qemu-system-x86 \
            qemu-utils \
            virt-manager \
            libvirt-daemon-system \
            libvirt-clients \
            dnsmasq \
            bridge-utils \
            wget \
            openssh-client
    else
        log_error "Gestionnaire de paquets non supporté."
    fi

    log_ok "Dépendances installées"
}

# =============================================================================
# ETAPE 2 — LIBVIRT
# =============================================================================

setup_libvirt() {
    log_step "Configuration de libvirt"

    sudo systemctl enable --now libvirtd
    log_ok "libvirtd démarré"

    # Ajouter l'utilisateur aux groupes
    sudo usermod -aG libvirt "$USER"
    sudo usermod -aG kvm "$USER"
    log_ok "Utilisateur $USER ajouté aux groupes libvirt et kvm"

    log_warn "Les groupes seront effectifs à la prochaine session (newgrp ou relogin)"
}

# =============================================================================
# ETAPE 3 — RESEAU
# =============================================================================

setup_network() {
    log_step "Configuration du réseau WLKOM"

    # Vérifier si le réseau existe déjà
    if sudo virsh net-info "$NETWORK_NAME" &>/dev/null; then
        log_warn "Le réseau '$NETWORK_NAME' existe déjà, on le supprime pour le recréer"
        sudo virsh net-destroy "$NETWORK_NAME" 2>/dev/null || true
        sudo virsh net-undefine "$NETWORK_NAME" 2>/dev/null || true
    fi

    # Créer le fichier XML du réseau
    cat > /tmp/wlkom-net.xml << EOF
<network>
  <name>${NETWORK_NAME}</name>
  <forward mode='none'/>
  <bridge name='${BRIDGE_NAME}' stp='on' delay='0'/>
  <ip address='${GATEWAY}' netmask='255.255.255.0'>
    <dhcp>
      <range start='192.168.100.10' end='192.168.100.50'/>
    </dhcp>
  </ip>
</network>
EOF

    sudo virsh net-define /tmp/wlkom-net.xml
    sudo virsh net-start "$NETWORK_NAME"
    sudo virsh net-autostart "$NETWORK_NAME"

    log_ok "Réseau '$NETWORK_NAME' créé et démarré"
    log_info "  Gateway  : $GATEWAY"
    log_info "  Attacker : $ATTACKER_IP"
    log_info "  Victim   : $VICTIM_IP"

    # Vérifier le bridge
    if ip link show "$BRIDGE_NAME" &>/dev/null; then
        log_ok "Bridge '$BRIDGE_NAME' actif"
    else
        log_warn "Bridge '$BRIDGE_NAME' non détecté, vérifier libvirt"
    fi
}

# =============================================================================
# ETAPE 4 — DOSSIER ET DISQUES
# =============================================================================

setup_disks() {
    log_step "Création des disques virtuels"

    mkdir -p "$VM_DIR"
    log_ok "Dossier $VM_DIR créé"

    # Disque attaquant
    if [ -f "$ATTACKER_DISK" ]; then
        log_warn "Disque attaquant déjà existant : $ATTACKER_DISK"
        read -rp "  Le supprimer et recréer ? [y/N] " choice
        if [[ "$choice" =~ ^[Yy]$ ]]; then
            rm -f "$ATTACKER_DISK"
            qemu-img create -f qcow2 "$ATTACKER_DISK" "$DISK_SIZE"
            log_ok "Disque attaquant recréé ($DISK_SIZE)"
        fi
    else
        qemu-img create -f qcow2 "$ATTACKER_DISK" "$DISK_SIZE"
        log_ok "Disque attaquant créé : $ATTACKER_DISK ($DISK_SIZE)"
    fi

    # Disque victime
    if [ -f "$VICTIM_DISK" ]; then
        log_warn "Disque victime déjà existant : $VICTIM_DISK"
        read -rp "  Le supprimer et recréer ? [y/N] " choice
        if [[ "$choice" =~ ^[Yy]$ ]]; then
            rm -f "$VICTIM_DISK"
            qemu-img create -f qcow2 "$VICTIM_DISK" "$DISK_SIZE"
            log_ok "Disque victime recréé ($DISK_SIZE)"
        fi
    else
        qemu-img create -f qcow2 "$VICTIM_DISK" "$DISK_SIZE"
        log_ok "Disque victime créé : $VICTIM_DISK ($DISK_SIZE)"
    fi
}

# =============================================================================
# ETAPE 5 — TELECHARGEMENT ISO
# =============================================================================

download_iso() {
    log_step "Téléchargement de l'ISO Debian"

    if [[ -f "$ISO_PATH" ]]; then
        log_warn "ISO déjà présente : $ISO_PATH"
        read -rp "  La re-télécharger ? [y/N] " choice
        [[ "$choice" != "y" && "$choice" != "Y" ]] && return
    fi

    log_info "Recherche de la dernière version de Debian..."
    
    # Récupère automatiquement le bon nom de fichier
    ISO_FILENAME=$(curl -s https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/ \
        | grep -o 'debian-[0-9.]*-amd64-netinst.iso' \
        | head -1)

    if [[ -z "$ISO_FILENAME" ]]; then
        log_error "Impossible de trouver l'ISO Debian. Vérifie ta connexion."
        exit 1
    fi

    ISO_URL="https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/$ISO_FILENAME"
    log_info "Téléchargement de $ISO_URL ..."

    wget "$ISO_URL" -O "$ISO_PATH"

    if [[ $? -ne 0 ]]; then
        log_error "Échec du téléchargement."
        exit 1
    fi

    log_ok "ISO téléchargée : $ISO_PATH"
}


# =============================================================================
# ETAPE 6 — SCRIPTS DE LANCEMENT
# =============================================================================

create_launch_scripts() {
    log_step "Création des scripts de lancement"

    # ---- Script VM Attaquante (install) ----
    cat > "$VM_DIR/install_attacker.sh" << EOF
#!/bin/bash
# Lance la VM attaquante en mode INSTALLATION
echo "[WLKOM] Lancement installation VM Attaquante..."
qemu-system-x86_64 \\
    -name "WLKOM-Attacker" \\
    -m ${RAM} \\
    -smp ${CPUS} \\
    -enable-kvm \\
    -drive file=${ATTACKER_DISK},format=qcow2 \\
    -cdrom ${ISO_PATH} \\
    -boot order=dc \\
    -netdev bridge,id=net0,br=${BRIDGE_NAME} \\
    -device virtio-net,netdev=net0 \\
    -vga virtio \\
    -display gtk
EOF

    # ---- Script VM Attaquante (run) ----
    cat > "$VM_DIR/start_attacker.sh" << EOF
#!/bin/bash
# Lance la VM attaquante (après installation)
echo "[WLKOM] Démarrage VM Attaquante (${ATTACKER_IP})..."
qemu-system-x86_64 \\
    -name "WLKOM-Attacker" \\
    -m ${RAM} \\
    -smp ${CPUS} \\
    -enable-kvm \\
    -drive file=${ATTACKER_DISK},format=qcow2 \\
    -netdev bridge,id=net0,br=${BRIDGE_NAME} \\
    -device virtio-net,netdev=net0 \\
    -vga virtio \\
    -display gtk
EOF

    # ---- Script VM Victime (install) ----
    cat > "$VM_DIR/install_victim.sh" << EOF
#!/bin/bash
# Lance la VM victime en mode INSTALLATION
echo "[WLKOM] Lancement installation VM Victime..."
qemu-system-x86_64 \\
    -name "WLKOM-Victim" \\
    -m ${RAM} \\
    -smp ${CPUS} \\
    -enable-kvm \\
    -drive file=${VICTIM_DISK},format=qcow2 \\
    -cdrom ${ISO_PATH} \\
    -boot order=dc \\
    -netdev bridge,id=net0,br=${BRIDGE_NAME} \\
    -device virtio-net,netdev=net0 \\
    -vga virtio \\
    -display gtk
EOF

    # ---- Script VM Victime (run) ----
    cat > "$VM_DIR/start_victim.sh" << EOF
#!/bin/bash
# Lance la VM victime (après installation)
echo "[WLKOM] Démarrage VM Victime (${VICTIM_IP})..."
qemu-system-x86_64 \\
    -name "WLKOM-Victim" \\
    -m ${RAM} \\
    -smp ${CPUS} \\
    -enable-kvm \\
    -drive file=${VICTIM_DISK},format=qcow2 \\
    -netdev bridge,id=net0,br=${BRIDGE_NAME} \\
    -device virtio-net,netdev=net0 \\
    -vga virtio \\
    -display gtk
EOF

    # ---- Script de test réseau ----
    cat > "$VM_DIR/test_network.sh" << EOF
#!/bin/bash
echo "[WLKOM] Test du réseau..."
echo ""
echo "Ping gateway ($GATEWAY)..."
ping -c 3 $GATEWAY && echo "OK" || echo "FAIL"
echo ""
echo "Ping attaquant ($ATTACKER_IP)..."
ping -c 3 $ATTACKER_IP && echo "OK" || echo "FAIL"
echo ""
echo "Ping victime ($VICTIM_IP)..."
ping -c 3 $VICTIM_IP && echo "OK" || echo "FAIL"
EOF

    # Rendre tous les scripts exécutables
    chmod +x \
        "$VM_DIR/install_attacker.sh" \
        "$VM_DIR/start_attacker.sh" \
        "$VM_DIR/install_victim.sh" \
        "$VM_DIR/start_victim.sh" \
        "$VM_DIR/test_network.sh"

    log_ok "Scripts créés dans $VM_DIR"
}

# =============================================================================
# ETAPE 7 — CONFIG POST-INSTALL (fichiers à appliquer dans les VMs)
# =============================================================================

create_postinstall_configs() {
    log_step "Création des configs post-installation"

    mkdir -p "$VM_DIR/configs"

    # Config réseau VM attaquante
    cat > "$VM_DIR/configs/attacker_interfaces" << EOF
# /etc/network/interfaces — VM Attaquante WLKOM
auto lo
iface lo inet loopback

auto ens3
iface ens3 inet static
    address ${ATTACKER_IP}
    netmask 255.255.255.0
    gateway ${GATEWAY}
    dns-nameservers 8.8.8.8
EOF

    # Config réseau VM victime
    cat > "$VM_DIR/configs/victim_interfaces" << EOF
# /etc/network/interfaces — VM Victime WLKOM
auto lo
iface lo inet loopback

auto ens3
iface ens3 inet static
    address ${VICTIM_IP}
    netmask 255.255.255.0
    gateway ${GATEWAY}
    dns-nameservers 8.8.8.8
EOF

    # Script post-install attaquante
    cat > "$VM_DIR/configs/setup_attacker_vm.sh" << 'EOF'
#!/bin/bash
# A lancer DANS la VM attaquante après installation
set -e
echo "[WLKOM] Configuration VM Attaquante..."

# Copier la config réseau
cp /tmp/attacker_interfaces /etc/network/interfaces

# Installer les paquets nécessaires
apt update -y
apt install -y build-essential gcc make gcc netcat-openbsd openssh-server

# Activer SSH
systemctl enable --now ssh

# Appliquer la config réseau
systemctl restart networking

echo "[WLKOM] VM Attaquante configurée !"
echo "IP : 192.168.100.10"
ip a show ens3
EOF

    # Script post-install victime
    cat > "$VM_DIR/configs/setup_victim_vm.sh" << 'EOF'
#!/bin/bash
# A lancer DANS la VM victime après installation
set -e
echo "[WLKOM] Configuration VM Victime..."

# Copier la config réseau
cp /tmp/victim_interfaces /etc/network/interfaces

# Installer les paquets nécessaires
apt update -y
apt install -y \
    build-essential \
    gcc \
    make \
    linux-headers-$(uname -r) \
    openssh-server \
    kmod

# Activer SSH
systemctl enable --now ssh

# Appliquer la config réseau
systemctl restart networking

echo "[WLKOM] VM Victime configurée !"
echo "IP : 192.168.100.20"
ip a show ens3
EOF

    chmod +x \
        "$VM_DIR/configs/setup_attacker_vm.sh" \
        "$VM_DIR/configs/setup_victim_vm.sh"

    log_ok "Configs post-install créées dans $VM_DIR/configs/"
}

# =============================================================================
# RESUME FINAL
# =============================================================================

print_summary() {
    log_step "RÉSUMÉ"

    echo -e "${GREEN}"
    echo "  Tout est prêt !"
    echo -e "${NC}"
    echo -e "  ${CYAN}Dossier VMs${NC}        : $VM_DIR"
    echo -e "  ${CYAN}Disque attaquant${NC}   : $ATTACKER_DISK"
    echo -e "  ${CYAN}Disque victime${NC}     : $VICTIM_DISK"
    echo -e "  ${CYAN}ISO Debian${NC}         : $ISO_PATH"
    echo -e "  ${CYAN}Réseau${NC}             : $NETWORK_NAME ($GATEWAY/24)"
    echo ""
    echo -e "  ${YELLOW}Prochaines étapes :${NC}"
    echo ""
    echo -e "  1️⃣  Installer la VM attaquante :"
    echo -e "     ${GREEN}$VM_DIR/install_attacker.sh${NC}"
    echo ""
    echo -e "  2️⃣  Installer la VM victime :"
    echo -e "     ${GREEN}$VM_DIR/install_victim.sh${NC}"
    echo ""
    echo -e "  3️⃣  Après installation, configurer chaque VM :"
    echo -e "     Copier et lancer ${GREEN}configs/setup_attacker_vm.sh${NC} dans la VM attaquante"
    echo -e "     Copier et lancer ${GREEN}configs/setup_victim_vm.sh${NC} dans la VM victime"
    echo ""
    echo -e "  4️⃣  Lancer les VMs :"
    echo -e "     ${GREEN}$VM_DIR/start_attacker.sh${NC}"
    echo -e "     ${GREEN}$VM_DIR/start_victim.sh${NC}"
    echo ""
    echo -e "  5️⃣  Tester le réseau :"
    echo -e "     ${GREEN}$VM_DIR/test_network.sh${NC}"
    echo ""
    echo -e "  ${YELLOW}IPs :${NC}"
    echo -e "     Gateway  : ${GREEN}$GATEWAY${NC}"
    echo -e "     Attacker : ${GREEN}$ATTACKER_IP${NC}"
    echo -e "     Victim   : ${GREEN}$VICTIM_IP${NC}"
    echo ""
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
    echo "  VM Setup Script"
    echo ""

    check_root
    check_kvm
    install_deps
    setup_libvirt
    setup_network
    setup_disks
    download_iso
    create_launch_scripts
    create_postinstall_configs
    print_summary
}

main
