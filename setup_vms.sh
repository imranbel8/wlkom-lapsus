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
ISO_PATH="$VM_DIR/debian.iso"
ISO_ATTACKER="$VM_DIR/debian_attacker.iso"
ISO_VICTIM="$VM_DIR/debian_victim.iso"
VM_USER="wlkom"
VM_PASS="wlkom1234"

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
        sudo pacman -S --needed --noconfirm \
            qemu-full \
            libvirt \
            dnsmasq \
            wget \
            python \
            openssh \
            xorriso
    elif command -v apt &>/dev/null; then
        log_info "Détecté : Debian/Ubuntu"
        sudo apt update -y
        sudo apt install -y \
            qemu-system-x86 \
            qemu-utils \
            libvirt-daemon-system \
            libvirt-clients \
            dnsmasq \
            bridge-utils \
            wget \
            python3 \
            openssh-client \
            xorriso
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

    if sudo virsh net-info "$NETWORK_NAME" &>/dev/null; then
        log_warn "Le réseau '$NETWORK_NAME' existe déjà, on le supprime pour le recréer"
        sudo virsh net-destroy "$NETWORK_NAME" 2>/dev/null || true
        sudo virsh net-undefine "$NETWORK_NAME" 2>/dev/null || true
    fi

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

    for DISK_PATH in "$ATTACKER_DISK" "$VICTIM_DISK"; do
        DISK_LABEL=$([ "$DISK_PATH" = "$ATTACKER_DISK" ] && echo "attaquant" || echo "victime")
        if [ -f "$DISK_PATH" ]; then
            log_warn "Disque $DISK_LABEL déjà existant : $DISK_PATH"
            read -rp "  Le supprimer et recréer ? [y/N] " choice
            if [[ "$choice" =~ ^[Yy]$ ]]; then
                rm -f "$DISK_PATH"
                qemu-img create -f qcow2 "$DISK_PATH" "$DISK_SIZE"
                log_ok "Disque $DISK_LABEL recréé ($DISK_SIZE)"
            fi
        else
            qemu-img create -f qcow2 "$DISK_PATH" "$DISK_SIZE"
            log_ok "Disque $DISK_LABEL créé : $DISK_PATH ($DISK_SIZE)"
        fi
    done
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

    ISO_FILENAME=$(curl -s https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/ \
        | grep -o 'debian-[0-9.]*-amd64-netinst.iso' \
        | head -1)

    if [[ -z "$ISO_FILENAME" ]]; then
        log_error "Impossible de trouver l'ISO Debian. Vérifie ta connexion."
    fi

    ISO_URL="https://cdimage.debian.org/debian-cd/current/amd64/iso-cd/$ISO_FILENAME"
    log_info "Téléchargement de $ISO_URL ..."

    wget "$ISO_URL" -O "$ISO_PATH" || log_error "Échec du téléchargement."

    log_ok "ISO téléchargée : $ISO_PATH"
}

# =============================================================================
# ETAPE 6 — PRESEED FILES
# =============================================================================

create_preseed_files() {
    log_step "Création des fichiers preseed"

    mkdir -p "$VM_DIR"

    # ------------------ ATTACKER ------------------
    cat > "$VM_DIR/preseed_attacker.cfg" << EOF
# Locale
d-i debian-installer/locale string fr_FR.UTF-8
d-i keyboard-configuration/xkb-keymap select fr

# Réseau
d-i netcfg/choose_interface select auto
d-i netcfg/get_hostname string wlkom-attacker
d-i netcfg/get_domain string wlkom.local
d-i netcfg/wireless_wep string

# Miroir
d-i mirror/country string FR
d-i mirror/http/mirror select deb.debian.org
d-i mirror/http/directory string /debian
d-i mirror/http/proxy string

# Horloge
d-i clock-setup/utc boolean true
d-i time/zone string Europe/Paris
d-i clock-setup/ntp boolean true

# Partitionnement
d-i partman-auto/method string regular
d-i partman-lvm/device_remove_lvm boolean true
d-i partman-auto/choose_recipe select atomic
d-i partman-partitioning/confirm_write_new_label boolean true
d-i partman/choose_partition select finish
d-i partman/confirm boolean true
d-i partman/confirm_nooverwrite boolean true

# Utilisateur
d-i passwd/root-login boolean false
d-i passwd/user-fullname string WLKOM Attacker
d-i passwd/username string ${VM_USER}
d-i passwd/user-password password ${VM_PASS}
d-i passwd/user-password-again password ${VM_PASS}

# Packages
tasksel tasksel/first multiselect ssh-server
d-i pkgsel/include string build-essential gcc make netcat-openbsd git openssh-server sudo curl wget python3

# Sudo sans mot de passe
d-i preseed/late_command string \\
    echo '${VM_USER} ALL=(ALL) NOPASSWD:ALL' >> /target/etc/sudoers; \\
    sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin no/' /target/etc/ssh/sshd_config

# Bootloader
d-i grub-installer/only_debian boolean true
d-i grub-installer/bootdev string default

# Fin
d-i finish-install/reboot_in_progress note
EOF

    # ------------------ VICTIM ------------------
    cat > "$VM_DIR/preseed_victim.cfg" << EOF
# Locale
d-i debian-installer/locale string fr_FR.UTF-8
d-i keyboard-configuration/xkb-keymap select fr

# Réseau
d-i netcfg/choose_interface select auto
d-i netcfg/get_hostname string wlkom-victim
d-i netcfg/get_domain string wlkom.local
d-i netcfg/wireless_wep string

# Miroir
d-i mirror/country string FR
d-i mirror/http/mirror select deb.debian.org
d-i mirror/http/directory string /debian
d-i mirror/http/proxy string

# Horloge
d-i clock-setup/utc boolean true
d-i time/zone string Europe/Paris
d-i clock-setup/ntp boolean true

# Partitionnement
d-i partman-auto/method string regular
d-i partman-lvm/device_remove_lvm boolean true
d-i partman-auto/choose_recipe select atomic
d-i partman-partitioning/confirm_write_new_label boolean true
d-i partman/choose_partition select finish
d-i partman/confirm boolean true
d-i partman/confirm_nooverwrite boolean true

# Utilisateur
d-i passwd/root-login boolean false
d-i passwd/user-fullname string WLKOM Victim
d-i passwd/username string ${VM_USER}
d-i passwd/user-password password ${VM_PASS}
d-i passwd/user-password-again password ${VM_PASS}

# Packages
tasksel tasksel/first multiselect ssh-server
d-i pkgsel/include string build-essential gcc make linux-headers-amd64 git openssh-server sudo kmod curl wget

# Sudo sans mot de passe
d-i preseed/late_command string \\
    echo '${VM_USER} ALL=(ALL) NOPASSWD:ALL' >> /target/etc/sudoers; \\
    sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin no/' /target/etc/ssh/sshd_config

# Bootloader
d-i grub-installer/only_debian boolean true
d-i grub-installer/bootdev string default

# Fin
d-i finish-install/reboot_in_progress note
EOF

    log_ok "Preseed attaquant : $VM_DIR/preseed_attacker.cfg"
    log_ok "Preseed victime   : $VM_DIR/preseed_victim.cfg"
    log_info "Credentials VMs  : user=${VM_USER} / pass=${VM_PASS}"
}

# =============================================================================
# ETAPE 6.5 — ISO CUSTOM AVEC PRESEED INTEGRE
# =============================================================================

create_custom_isos() {
    log_step "Création des ISOs personnalisées avec preseed intégré"

    local WORK_DIR="$VM_DIR/iso_work"

    for VM_TYPE in attacker victim; do
        local PRESEED_FILE="$VM_DIR/preseed_${VM_TYPE}.cfg"
        local OUT_ISO
        local HOSTNAME_VAL

        if [ "$VM_TYPE" = "attacker" ]; then
            OUT_ISO="$ISO_ATTACKER"
            HOSTNAME_VAL="wlkom-attacker"
        else
            OUT_ISO="$ISO_VICTIM"
            HOSTNAME_VAL="wlkom-victim"
        fi

        if [ -f "$OUT_ISO" ]; then
            log_warn "ISO $VM_TYPE déjà existante : $OUT_ISO"
            read -rp "  La recréer ? [y/N] " choice
            [[ ! "$choice" =~ ^[Yy]$ ]] && continue
        fi

        log_info "Construction ISO $VM_TYPE..."

        rm -rf "$WORK_DIR"
        mkdir -p "$WORK_DIR"

        # Extraire l'ISO de base
        xorriso -osirrox on \
            -indev "$ISO_PATH" \
            -extract / "$WORK_DIR" 2>/dev/null

        chmod -R +w "$WORK_DIR"

        # Copier le preseed
        cp "$PRESEED_FILE" "$WORK_DIR/preseed.cfg"

        # Patcher isolinux (boot BIOS)
        if [ -f "$WORK_DIR/isolinux/isolinux.cfg" ]; then
            cat > "$WORK_DIR/isolinux/isolinux.cfg" << ISOLINUX
default auto
timeout 10
label auto
    menu label ^Install WLKOM Auto
    kernel /install.amd/vmlinuz
    append initrd=/install.amd/initrd.gz auto=true priority=critical preseed/file=/cdrom/preseed.cfg hostname=${HOSTNAME_VAL} domain=wlkom.local quiet ---
ISOLINUX
        fi

        # Patcher GRUB (boot UEFI)
        if [ -f "$WORK_DIR/boot/grub/grub.cfg" ]; then
            cat > "$WORK_DIR/boot/grub/grub.cfg" << GRUBCFG
set default=0
set timeout=5

menuentry "Install WLKOM Auto" {
    linux   /install.amd/vmlinuz \\
            auto=true \\
            priority=critical \\
            preseed/file=/cdrom/preseed.cfg \\
            hostname=${HOSTNAME_VAL} \\
            domain=wlkom.local \\
            quiet ---
    initrd  /install.amd/initrd.gz
}
GRUBCFG
        fi

        # Reconstruire l'ISO
        xorriso -as mkisofs \
            -r -J -joliet-long \
            -b isolinux/isolinux.bin \
            -c isolinux/boot.cat \
            -no-emul-boot \
            -boot-load-size 4 \
            -boot-info-table \
            -eltorito-alt-boot \
            -e boot/grub/efi.img \
            -no-emul-boot \
            -isohybrid-gpt-basdat \
            -o "$OUT_ISO" \
            "$WORK_DIR" 2>/dev/null

        rm -rf "$WORK_DIR"
        log_ok "ISO $VM_TYPE créée : $OUT_ISO"
    done
}


# =============================================================================
# ETAPE 7 — SCRIPTS DE LANCEMENT
# =============================================================================

create_launch_scripts() {
    log_step "Création des scripts de lancement"

    # ---- install_attacker.sh ----
    cat > "$VM_DIR/install_attacker.sh" << SCRIPT
#!/bin/bash
echo "[WLKOM] Lancement installation VM Attaquante (automatique)..."
qemu-system-x86_64 \\
    -name "WLKOM-Attacker-Install" \\
    -m ${RAM} \\
    -smp ${CPUS} \\
    -enable-kvm \\
    -drive file=${ATTACKER_DISK},format=qcow2,index=0 \\
    -drive file=${ISO_ATTACKER},media=cdrom,readonly=on,index=1 \\
    -boot once=d \\
    -netdev user,id=net0,net=192.168.100.0/24,host=192.168.100.1,dhcpstart=192.168.100.10,hostfwd=tcp::2222-:22 \\
    -device virtio-net,netdev=net0 \\
    -vga virtio \\
    -display gtk
echo "[WLKOM] Installation terminée. Lance start_attacker.sh"
SCRIPT

    # ---- install_victim.sh ----
    cat > "$VM_DIR/install_victim.sh" << SCRIPT
#!/bin/bash
echo "[WLKOM] Lancement installation VM Victime (automatique)..."
qemu-system-x86_64 \\
    -name "WLKOM-Victim-Install" \\
    -m ${RAM} \\
    -smp ${CPUS} \\
    -enable-kvm \\
    -drive file=${VICTIM_DISK},format=qcow2,index=0 \\
    -drive file=${ISO_VICTIM},media=cdrom,readonly=on,index=1 \\
    -boot once=d \\
    -netdev user,id=net0,net=192.168.100.0/24,host=192.168.100.1,dhcpstart=192.168.100.20,hostfwd=tcp::2223-:22 \\
    -device virtio-net,netdev=net0 \\
    -vga virtio \\
    -display gtk
echo "[WLKOM] Installation terminée. Lance start_victim.sh"
SCRIPT

    # ---- start_attacker.sh ----
    cat > "$VM_DIR/start_attacker.sh" << SCRIPT
#!/bin/bash
echo "[WLKOM] Démarrage VM Attaquante (${ATTACKER_IP})..."
echo "[WLKOM] SSH dispo sur : ssh ${VM_USER}@localhost -p 2222"
qemu-system-x86_64 \\
    -name "WLKOM-Attacker" \\
    -m ${RAM} \\
    -smp ${CPUS} \\
    -enable-kvm \\
    -drive file=${ATTACKER_DISK},format=qcow2 \\
    -netdev user,id=net0,net=192.168.100.0/24,host=192.168.100.1,dhcpstart=192.168.100.10,hostfwd=tcp::2222-:22 \\
    -device virtio-net,netdev=net0 \\
    -vga virtio \\
    -display gtk
SCRIPT

    # ---- start_victim.sh ----
    cat > "$VM_DIR/start_victim.sh" << SCRIPT
#!/bin/bash
echo "[WLKOM] Démarrage VM Victime (${VICTIM_IP})..."
echo "[WLKOM] SSH dispo sur : ssh ${VM_USER}@localhost -p 2223"
qemu-system-x86_64 \\
    -name "WLKOM-Victim" \\
    -m ${RAM} \\
    -smp ${CPUS} \\
    -enable-kvm \\
    -drive file=${VICTIM_DISK},format=qcow2 \\
    -netdev user,id=net0,net=192.168.100.0/24,host=192.168.100.1,dhcpstart=192.168.100.20,hostfwd=tcp::2223-:22 \\
    -device virtio-net,netdev=net0 \\
    -vga virtio \\
    -display gtk
SCRIPT

    # ---- test_network.sh ----
    cat > "$VM_DIR/test_network.sh" << SCRIPT
#!/bin/bash
echo "[WLKOM] Test SSH vers les VMs..."
echo ""
echo "Test SSH attaquante (port 2222)..."
ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no ${VM_USER}@localhost -p 2222 "hostname && ip a show ens3" \
    && echo "[OK] Attaquante joignable" \
    || echo "[FAIL] Attaquante non joignable (VM démarrée ?)"
echo ""
echo "Test SSH victime (port 2223)..."
ssh -o ConnectTimeout=5 -o StrictHostKeyChecking=no ${VM_USER}@localhost -p 2223 "hostname && ip a show ens3" \
    && echo "[OK] Victime joignable" \
    || echo "[FAIL] Victime non joignable (VM démarrée ?)"
SCRIPT

    chmod +x \
        "$VM_DIR/install_attacker.sh" \
        "$VM_DIR/install_victim.sh" \
        "$VM_DIR/start_attacker.sh" \
        "$VM_DIR/start_victim.sh" \
        "$VM_DIR/test_network.sh"

    log_ok "Scripts créés dans $VM_DIR"
}

# =============================================================================
# ETAPE 8 — CONFIG POST-INSTALL
# =============================================================================

create_postinstall_configs() {
    log_step "Création des configs post-installation"

    mkdir -p "$VM_DIR/configs"

    cat > "$VM_DIR/configs/setup_attacker_vm.sh" << 'EOF'
#!/bin/bash
# Lancer DANS la VM attaquante
set -e
echo "[WLKOM] Configuration VM Attaquante..."
apt update -y
apt install -y build-essential gcc make netcat-openbsd openssh-server curl wget python3
systemctl enable --now ssh
echo "[WLKOM] Done. IP actuelle :"
ip a show ens3
EOF

    cat > "$VM_DIR/configs/setup_victim_vm.sh" << 'EOF'
#!/bin/bash
# Lancer DANS la VM victime
set -e
echo "[WLKOM] Configuration VM Victime..."
apt update -y
apt install -y build-essential gcc make linux-headers-$(uname -r) openssh-server kmod curl wget
systemctl enable --now ssh
echo "[WLKOM] Done. IP actuelle :"
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
    echo -e "  ${CYAN}Credentials VMs${NC}    : ${VM_USER} / ${VM_PASS}"
    echo ""
    echo -e "  ${YELLOW}Prochaines étapes :${NC}"
    echo ""
    echo -e "  1️⃣  Installer la VM attaquante :"
    echo -e "     ${GREEN}$VM_DIR/install_attacker.sh${NC}"
    echo -e "     └─ Installtion automatique"
    echo ""
    echo -e "  2️⃣  Installer la VM victime :"
    echo -e "     ${GREEN}$VM_DIR/install_victim.sh${NC}"
    echo -e "     └─ Installtion automatique"
    echo ""
    echo -e "  3️⃣  Démarrer les VMs après install :"
    echo -e "     ${GREEN}$VM_DIR/start_attacker.sh${NC}"
    echo -e "     ${GREEN}$VM_DIR/start_victim.sh${NC}"
    echo ""
    echo -e "  4️⃣  SSH depuis l'hôte :"
    echo -e "     ${GREEN}ssh ${VM_USER}@localhost -p 2222${NC}  (attaquante)"
    echo -e "     ${GREEN}ssh ${VM_USER}@localhost -p 2223${NC}  (victime)"
    echo ""
    echo -e "  5️⃣  Tester la connexion :"
    echo -e "     ${GREEN}$VM_DIR/test_network.sh${NC}"
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
    create_preseed_files
    create_custom_isos
    create_launch_scripts
    create_postinstall_configs
    print_summary
}

main
