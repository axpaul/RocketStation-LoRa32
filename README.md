# RocketStation-LoRa32 (Récepteur NectarMC)

**RocketStation-LoRa32** est une station au sol de réception LoRa destinée à capter la télémétrie de fusées expérimentales et de ballons-sondes. Elle repose sur la carte de développement [LilyGO TTGO T3 V1.6.1 (LoRa32 V2.1.6)](https://lilygo.cc/en-us/products/lora3) équipée d'un microcontrôleur ESP32, d'un module radio SX1276 et d'un écran OLED intégré. Elle fonctionne à la fréquence de 869.525 MHz (868 MHz ou 433 Mhz Bande ICM en fonction de la version).

Le but principal de cette station est d'assurer l'interface de réception physique pour le logiciel de visualisation et de traitement de télémétrie [NectarMC](https://github.com/mlavardin/NectarMC).

<p align="center">
  <img src="Image/DSC03016.JPG" alt="Station sol TTGO LoRa32 affichant NECTAR" width="800" />
</p>

Cette version du logiciel est optimisée pour être compatible avec le logiciel [NectarMC](https://github.com/mlavardin/NectarMC) en générant des trames binaires série conformes, en gérant dynamiquement les trackers et en enregistrant l'historique sur carte SD.

---

## Aperçu du Matériel

Voici les vues de la carte de développement ainsi que son brochage (Pinout) et ses dimensions :

<p align="center">
  <img src="Image/PIN-DIAGRAM_4162b444-51eb-42ec-9e20-7ce3fedd2dc1_600x600.jpg" alt="Pinout Diagram" width="500" />
</p>
<p align="center">
  <img src="Image/P-SIZE_600x600.jpg" alt="Board Dimensions" width="500" />
</p>

**[Télécharger la Fiche Technique et le Schéma PDF Officiel de la TTGO T3 V1.6.1](T3_V1.6.1.pdf)**

---

## Fonctionnalités principales

*   **Compatibilité NectarMC** : Génère à la volée des trames binaires série structurées (Magic byte `0xEB`, `Id_mission` codé sur 16 bits en Little-Endian, calcul du `CRC16-CCITT` en Little-Endian) prêtes à être décodées et affichées en temps réel par le logiciel [NectarMC](https://github.com/mlavardin/NectarMC).
*   **Réception LoRa dynamique** : Supporte des longueurs de paquets LoRa variables de manière totalement transparente.
*   **Robustesse radio** : Utilise le CRC matériel du module SX1276 pour garantir l'intégrité de la liaison RF (les trames corrompues en vol sont directement jetées par le silicium).
*   **Journalisation CSV incrémentale** : Crée un fichier par session de démarrage (`/log_0.csv`, `/log_1.csv`, etc.) pour éviter d'écraser vos données de vol précédentes.
*   **Suivi de l'alimentation** : Mesure la tension de la batterie en temps réel (via le pin ADC GPIO 35) et détecte si la station est alimentée en USB.

---

## Description de l'Affichage OLED et des Menus

L'écran OLED (128x64 pixels) affiche des informations complètes sur l'état de fonctionnement du récepteur. Au démarrage, il affiche une **animation de pylône radio émetteur** avec des ondes électromagnétiques clignotantes, puis affiche un retour visuel sur l'état de la carte SD (un **icône de coche de validation** en cas de succès, ou un **triangle d'alerte clignotant** si la carte est absente/défectueuse).

Pendant le fonctionnement, l'écran est structuré en deux parties :

### 1. En-tête Persistant (Ligne 1 - Toujours visible)
*   **Gauche** : Compteur de paquets reçus (et d'erreurs éventuelles), ex: `RX: 12` ou `RX:12 E:2`.
*   **Milieu** : Statut de l'alimentation. Affiche `USB` avec une icône de batterie pleine s'il est alimenté par câble USB, ou affiche la tension de la batterie en volts (ex: `3.9V`) avec une icône de jauge proportionnelle au niveau de charge.
*   **Droite** : Horloge temps réel (RTC) comptant le temps écoulé depuis le démarrage (`HH:MM:SS`).

### 2. Rotation des Écrans Principaux (Alternance toutes les 4 secondes)

Les informations détaillées s'affichent sous forme de deux écrans alternant automatiquement toutes les 4 secondes :

| Écran 1 : Infos de la dernière trame | Écran 2 : Configuration & Stats réseau |
| :---: | :---: |
| <img src="Image/IMG_1197.jpg" width="300" /> | <img src="Image/IMG_1198.jpg" width="300" /> |
| **Dernier paquet reçu** : Affiche le SSID décodé, l'APID de l'émetteur, ainsi que le RSSI (dBm) et le SNR (dB) physiques du signal capté. | **Configuration & Débit** : Affiche la fréquence active, le Spreading Factor (SF), la bande passante (BW), le nombre de trackers actifs uniques et le débit de données instantané. |

---

## Structure de la trame radio LoRa (Émetteurs)

Les trames radio LoRa émises par les trackers/émetteurs vers la station au sol doivent respecter la structure binaire suivante en fonction du mode de contrôle d'intégrité de la liaison choisi :

### Option A : Avec CRC matériel (Recommandé & Par défaut)
C'est le mode par défaut de cette station de réception. Le contrôle d'intégrité est pris en charge directement au niveau silicium par le module radio SX1276.
*   Taille totale de la trame LoRa : $3 + N$ octets.

```
┌───────────────────────────────────────────────────────────┬───────────────────┐
│                          HEADER                           │      PAYLOAD      │
├───────────────────────────────────────────────────────────┼───────────────────┤
│       SSID_NUM       │     APID      │     SSID_TYPE      │      N data       │
│        1 Byte        │    1 Byte     │       1 Byte       │       bytes       │
│       (0-255)        │    (0-63)     │       (0-3)        │     (N bytes)     │
└──────────────────────┴───────────────┴────────────────────┴───────────────────┘
```

### Option B : Avec CRC logiciel (Si le CRC matériel est désactivé)
Si le CRC matériel de la puce LoRa n'est pas utilisé, l'émetteur doit calculer et ajouter une somme de contrôle logicielle CRC16 de 2 octets immédiatement à la suite de la charge utile de données.
*   Taille totale de la trame LoRa : $5 + N$ octets.

```
┌───────────────────────────────────────────────────────────┬───────────────────┬───────────────┐
│                          HEADER                           │      PAYLOAD      │    CONTROL    │
├───────────────────────────────────────────────────────────┼───────────────────┼───────────────┤
│       SSID_NUM       │     APID      │     SSID_TYPE      │      N data       │     CRC16     │
│        1 Byte        │    1 Byte     │       1 Byte       │       bytes       │    2 Bytes    │
│       (0-255)        │    (0-63)     │       (0-3)        │     (N bytes)     │  (Software)   │
└──────────────────────┴───────────────┴────────────────────┴───────────────────┴───────────────┘
```

### Description des octets de la trame radio

| Position | Type | Nom du Champ | Description |
| :--- | :--- | :--- | :--- |
| **Octet 0** | `uint8_t` | `SSID_NUM` | ID ou numéro du tracker (de 0 à 255). |
| **Octet 1** | `uint8_t` | `APID` | Identifiant du processus applicatif ou type de paquet (de 0 à 63). |
| **Octet 2** | `uint8_t` | `SSID_TYPE` | Type de mission (`0` = FX, `1` = MF, `2` = BALLOON, `3` = OTHER). |
| **Octets 3 à 2+N** | `uint8_t[]` | `Payload` | Charge utile contenant les données brutes des capteurs ($N$ octets). |
| **Octets 3+N à 4+N** | `uint16_t` | `CRC16` | *(Option B uniquement)* Somme de contrôle logicielle de 2 octets en Little-Endian. |

---

## Contrôle d'Intégrité (CRC) et de Liaison

Pour garantir la fiabilité de la transmission des données de la fusée jusqu'à votre écran, deux niveaux de contrôle d'intégrité (CRC) sont appliqués :
1. **Liaison Radio LoRa (Tracker ➔ Station Sol)** : CRC matériel géré directement par le silicium de la puce radio SX1276 (Option A, par défaut), ou CRC logiciel inséré dans la payload LoRa si le mode matériel est désactivé (Option B).
2. **Liaison Série & Bluetooth (Station Sol ➔ PC)** : CRC logiciel calculé par l'ESP32 et vérifié à la réception par le PC (NectarMC ou Dashboard Web).

Pour une explication détaillée de ces deux niveaux de sécurité et un guide pas-à-pas idéal pour les débutants :
👉 **[Consulter le Guide complet sur les CRC](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/CRC_GUIDE.md)**

---

## Structure de la trame série NectarMC (Sortie USB / Bluetooth)

Les trames émises par la station sol vers le PC sur le port série USB et la liaison Bluetooth sont lues par le logiciel pour affichage et traitement. Elles ont la structure binaire suivante (taille totale : $13 + N$ octets) :

```
┌───────────────────────────────────────────┬───────────────────┬───────────────────────────────────────┬───────────────┐
│                 HEADER                    │      PAYLOAD      │               METADATA                │     CONTROL   │
├───────────────────────────────────────────┼───────────────────┼───────────────────────────────────────┼───────────────┤
│   MAGIC     │  Id_mission  │ payload_size │      N data       │  RSSI   │   SNR   │     Timestamp     │     CRC16     │
│   1 Byte    │   2 Bytes    │   1 Byte     │      bytes        │ 1 Byte  │ 1 Byte  │      4 Bytes      │    2 Bytes    │
│    0xEB     │ (Little-End) │   (N bytes)  │                   │(int8_t) │(int8_t) │ (uint32_t Little-E)│ (Little-End)  │
└─────────────┴──────────────┴──────────────┴───────────────────┴─────────┴─────────┴───────────────────┴───────────────┘
```

### Description des octets de la trame série

| Position | Type | Nom du Champ | Description |
| :--- | :--- | :--- | :--- |
| **Octet 0** | `uint8_t` | `MAGIC` | Marqueur de synchronisation de début de trame. Toujours égal à `0xEB`. |
| **Octets 1 à 2** | `uint16_t` | `Id_mission` | Identifiant unique de mission codé en Little-Endian. Regroupe sur 16 bits :<br>- Le type de tracker (`SSID_TYPE`, 2 bits de poids fort, bits 15-14)<br>- Le numéro du tracker (`SSID_NUM`, 8 bits, bits 13-6)<br>- L'identifiant de paquet (`APID`, 6 bits de poids faible, bits 5-0) |
| **Octet 3** | `uint8_t` | `payload_size` | Longueur $N$ de la charge utile LoRa brute en octets. |
| **Octets 4 à 3+N** | `uint8_t[]` | `Payload` | Données utiles brutes provenant directement du tracker LoRa ($N$ octets). |
| **Octet 4+N** | `int8_t` | `RSSI` | Niveau de puissance du signal reçu en dBm. Entier signé (ex: `-85` dBm). |
| **Octet 5+N** | `int8_t` | `SNR` | Rapport signal/bruit. Multiplié par 4 à l'émission pour encoder une précision de 0.25 dB (ex: `38` pour 9.5 dB, plage utile : -32 dB à +31.75 dB). |
| **Octets 6+N à 9+N** | `uint32_t` | `Timestamp` | Horodatage de réception absolu (Epoch Unix en secondes) codé en Little-Endian. |
| **Octets 10+N à 11+N** | `uint16_t` | `CRC16` | Somme de contrôle de validation (polynôme CCITT 0x1021, init 0xFFFF, Little-Endian) calculée sur l'ensemble Header, Payload et Métadonnées (de l'octet 0 à $9+N$ inclus). |
| **Octet 12+N** | `char` | `Newline` | Caractère retour à la ligne `\n` (`0x0A`) facilitant la détection de fin et la journalisation brute. |

> [!TIP]
> **Gestion intelligente du temps :**
> L'ajout du **Timestamp** permet de dater précisément chaque trame sans dépendre de l'horloge système du PC au moment du traitement. Si la station sol n'a pas encore été synchronisée (ex. fonctionnement autonome), l'horloge RTC démarre par défaut à l'époque 0 (`1er janvier 1970`). L'application Web Ground Station synchronise automatiquement la RTC de la station à la connexion en lui transmettant l'époque Unix courante de l'ordinateur.

---

## Commandes de Configuration AT (Série / Bluetooth)

La station sol dispose d'un décodeur de commandes AT standard permettant de configurer la radio à chaud (en USB à **115200 bauds** ou sans fil via liaison **Bluetooth Classic (SPP)** avec l'appareil **`Nectar-RxStation-XXXX`**).

Chaque commande doit se terminer par un retour chariot (`\n` ou `\r`). Les réponses sont renvoyées sur le même canal que celui d'où provient la commande.

> [!IMPORTANT]
> **Sécurité Anti-Conflit :**
> Toutes les commandes doivent obligatoirement commencer par le préfixe **`AT`**. Tout flux série ou Bluetooth ne débutant pas par ces deux lettres est silencieusement ignoré. Cela évite tout conflit avec des trames de données binaires entrantes ou du bruit sur le port.

### Liste des commandes AT disponibles

| Commande | Rôle | Format de Réponse & Exemples |
| :--- | :--- | :--- |
| **`AT`** | Teste la communication avec la station | `OK` |
| **`AT+FREQ=<val>`** | Modifie la fréquence LoRa active (en MHz) | Ex: `AT+FREQ=869.525`. Renvoie `OK` ou `ERROR`. |
| **`AT+FREQ?`** | Interroge la fréquence active | Renvoie `+FREQ: 869.525` suivi de `OK` |
| **`AT+SF=<val>`** | Modifie le Spreading Factor LoRa | De `6` à `12`. Ex: `AT+SF=8`. Renvoie `OK` ou `ERROR`. |
| **`AT+SF?`** | Interroge le Spreading Factor actif | Renvoie `+SF: 8` suivi de `OK` |
| **`AT+BW=<val>`** | Modifie la bande passante LoRa (en kHz) | Valeur $> 0$. Ex: `AT+BW=250.0`. Renvoie `OK` ou `ERROR`. |
| **`AT+BW?`** | Interroge la bande passante active | Renvoie `+BW: 250.0` suivi de `OK` |
| **`AT+CRC=<0/1>[,0/1]`** | Active (`1`) ou désactive (`0`) le CRC matériel du SX1276.<br>Paramètre facultatif de mode : `0` = CCITT (par défaut), `1` = IBM (mode FSK). | Ex: `AT+CRC=1,0`. Renvoie `OK` ou `ERROR`. |
| **`AT+CRC?`** | Interroge l'état et le mode du CRC matériel actif | Renvoie `+CRC: <activé>,<mode>` suivi de `OK` (ex: `+CRC: 1,0`) |
| **`AT+TIME=<epoch>`** | Configure l'heure RTC de la station (Epoch Unix en secondes) | Ex: `AT+TIME=1781290382`. Renvoie `OK`. |
| **`AT+TIME?`** | Interroge l'horloge RTC de la station (Epoch Unix en secondes) | Renvoie `+TIME: 1781290382` suivi de `OK`. |
| **`AT+RSSI?`** | Interroge le RSSI du dernier paquet reçu (en dBm) | Renvoie `+RSSI: -85.0` suivi de `OK`. |
| **`AT+SNR?`** | Interroge le SNR du dernier paquet reçu (en dB) | Renvoie `+SNR: 9.5` suivi de `OK`. |
| **`AT+SIG?`** | Interroge à la fois le RSSI et le SNR du dernier paquet reçu | Renvoie `+SIG: RSSI=-85.0, SNR=9.5` suivi de `OK`. |
| **`AT+CFG`** ou **`AT+STATUS`** | Affiche le rapport complet de la configuration | Affiche la version, la bande native, les limites, les réglages actifs, l'état de la SD et du Bluetooth, suivi de `OK`. |
| **`AT+SAVE`** | Persiste la configuration active dans la Flash (NVS) | Renvoie `OK`. Elle sera rechargée automatiquement au boot. |
| **`AT+RESET`** | Efface la configuration personnalisée et redémarre | Renvoie `OK`, puis réinitialise la carte aux paramètres d'usine. |

### Retours d'erreurs et statuts

* **Succès général** :
  * `OK`
* **Erreur de limites de fréquence (Bandes ISM natives protégées)** :
  * Si hors de la bande configurée :
    `ERROR: Out of limits [863.0 - 870.0] MHz`
* **Erreur de paramètre invalide** :
  * Si la valeur du paramètre est incorrecte (ex. `AT+SF=13`) :
    `ERROR: SF must be between 6 and 12`
  * Si la bande passante demandée est négative ou nulle :
    `ERROR: Bandwidth must be greater than 0`
* **Erreur de commande inconnue** :
  * Si la commande AT est incorrecte ou non supportée :
    `ERROR: Unknown AT command '<votre_saisie>'`

---

## Structure des logs (Carte SD)

Les données sont enregistrées dans un fichier CSV avec la structure suivante :
`Timestamp,RSSI,SNR,SSID,APID,RawFrame`

Exemple de ligne de log :
`00:05:42,-85.00,8.50,FX99,7,EBC7181401020304`

---

## Architecture Logicielle

Le micrologiciel du récepteur est conçu avec une structure modulaire en C++ afin de séparer les responsabilités (entrées/sorties, affichage, stockage, communication sans fil) et d'assurer une exécution robuste et sans blocage des tâches critiques de réception radio.

```mermaid
graph TD
    Main[main.cpp <br/> Orchestrateur] -->|Initialise et cadence| Radio[radio.cpp <br/> Gestion Radio & OLED]
    Main -->|Charge/Sauvegarde Config| Func[function.cpp <br/> Mémoire NVS, SD & IHM]
    Radio -->|Achemine les paquets| Serial[serial.cpp <br/> Calcul CRC16 & Format NectarMC]
    Radio -->|Rafraîchit| OLED[Ecran OLED <br/> Statuts & Écrans de Télémétrie]
    Main -->|Enregistre les données| SD[function.cpp <br/> Journalisation CSV sur SD]
    Serial -->|Transmet les trames| Output[USB Série & Bluetooth SerialBT]
```

### Description des Modules

*   **[main.cpp](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/src/main.cpp) (Orchestrateur)** : Point d'entrée principal. Il initialise les composants système dans `setup()` (port USB, Bluetooth Classic, configuration radio, carte SD) et gère l'exécution des tâches dans `loop()` (lecture périodique des commandes AT entrantes et mise à jour de l'affichage OLED toutes les secondes).
*   **[radio.cpp](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/src/radio.cpp) (Gestion Radio & OLED)** : Configure le module radio SX1276 (via RadioLib), traite la réception asynchrone des trames LoRa (sécurisée par interruption matérielle via `setFlag()`) et met à jour l'affichage OLED (via U8g2). Il gère également le calcul dynamique des métriques réseau (débit instantané en B/s et liste des émetteurs actifs filtrée par un timeout de 10 secondes).
*   **[serial.cpp](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/src/serial.cpp) (Sérialisation & Bluetooth Mirror)** : Implémente le calcul de somme de contrôle CRC16-CCITT et encapsule les payloads LoRa décodées dans le format de trame binaire officiel de NectarMC. Il s'occupe de dupliquer la trame finalisée sur le port série USB et sur le flux série Bluetooth Classic (`SerialBT`) lorsqu'un client est connecté.
*   **[function.cpp](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/src/function.cpp) (Mémoire NVS, SD & Interface Graphique)** : Regroupe les fonctions utilitaires système. Il gère le stockage non-volatile (NVS via `<Preferences.h>`) pour sauvegarder/charger les configurations LoRa à chaud, effectue la détection et les tests de capacité de la carte SD, et écrit les logs au format CSV (`/log_X.csv`). Il pilote également les animations graphiques OLED (animation de démarrage du pylône radio et icônes visuelles d'état d'insertion de carte SD).
*   **[header.h](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/include/header.h) (Configuration & Pinout)** : Fichier d'en-tête central. Il déclare les variables globales partagées, configure les constantes matérielles (mapping des broches GPIO pour l'écran I2C, le bus SPI de la radio, le bus SPI de la carte SD et le pin ADC de la batterie), et définit les structures de configuration (`LoRaConfig`) ainsi que les limites de fréquence ISM physiques autorisées par environnement de compilation.

---

## Compilation et Flashage

Le projet utilise **PlatformIO**. Pour compiler et flasher le récepteur :

1. Ouvrez le projet dans VS Code avec l'extension PlatformIO.
2. Connectez votre carte TTGO LoRa32 v2.1.6 via USB.
3. Lancez la compilation et le téléversement (Upload).

En ligne de commande :
```powershell
pio run -t upload
```

---

## Outils de l'Écosystème NectarMC

> [!IMPORTANT]
> Pour exploiter pleinement votre station sol RocketStation-LoRa32, vous pouvez utiliser les deux solutions logicielles officielles :
> 
> ### 1. Console Web de Contrôle & Flasheur en Ligne
> Une interface web moderne et statique est disponible sans aucune installation requise. Elle communique en direct avec votre récepteur en USB :
> 👉 **[Ouvrir la Nectar Rx Station Web Console (Live)](https://axpaul.github.io/RocketStation-LoRa32/)**
> 
> Cette console web vous permet de :
> *   **Piloter la station par port COM USB** : Connectez votre récepteur LoRa32 en un clic et configurez-le dynamiquement (fréquence, Spreading Factor, Bande Passante) à l'aide de boutons simples ou de la console AT interactive.
> *   **Suivre les Trackers Actifs en temps réel** : La page liste automatiquement tous les émetteurs détectés (fusées, minifusées, ballons...) avec leurs types de mission, APID, nombre de trames et charges utiles. Elle détecte et marque automatiquement comme `PERDU` les trackers inactifs pendant plus de 15 secondes.
> *   **Tracer le débit de données** : Un graphique SVG en temps réel affiche le flux instantané de données reçues.
> *   **Flasher le firmware en ligne** : Mettez à jour le micrologiciel de votre carte TTGO avec la version **v1.3.1** native (en 868 ou 433 MHz) directement en un clic depuis le navigateur grâce à `esptool-js`.
> 
> ### 2. Logiciel de Traitement & Visualisation 3D : NectarMC
> La station sol est entièrement configurée pour transmettre les données de vol en temps réel vers le logiciel principal de visualisation de la télémétrie :
> 👉 **[Découvrir NectarMC sur GitHub](https://github.com/mlavardin/NectarMC)**

---

## Auteur

*   **Paul Miailhe ([axpaul](https://github.com/axpaul))** :
    *   **Concepteur principal** du projet matériel et logiciel de la station de réception sol RocketStation-LoRa32.
    *   **Version originale (MSE)** : Juin 2023.
    *   **Refonte & Améliorations** : Mai/Juin 2026 (Intégration du protocole binaire NectarMC, animation graphique live de pylône OLED, décodeur et gestionnaire de trackers dynamiques et interface Web de contrôle/flashage en ligne par Web Serial API).
