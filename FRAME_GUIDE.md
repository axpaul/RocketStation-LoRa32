# Guide des Formats de Trames (Radio LoRa & Série NectarMC)

Ce guide décrit en détail les formats et la structure binaire des trames utilisées par la station **RocketStation-LoRa32** pour la communication radio et la transmission série vers le PC.

---

## 📡 1. Format des Trames Radio LoRa (Air)

Les trames émises par les trackers/émetteurs dans les airs vers la station sol respectent le format suivant. La présence ou non du CRC en queue dépend du mode de contrôle configuré (voir le [Guide sur les CRC](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/CRC_GUIDE.md)) :

### Option A : Format avec CRC matériel (Recommandé & Par défaut)
Le contrôle d'intégrité est pris en charge directement par le silicium de la puce SX1276. Le paquet LoRa physique se compose uniquement du Header Applicatif et des Données Utiles.
* **Taille totale** : $3 + N$ octets (où $N$ est la taille des données utiles).

```
┌───────────────────────────────────────────────────────────┬───────────────────┐
│                          HEADER                           │      PAYLOAD      │
├───────────────────────────────────────────────────────────┼───────────────────┤
│       SSID_NUM       │     APID      │     SSID_TYPE      │      N data       │
│        1 Byte        │    1 Byte     │       1 Byte       │       bytes       │
│       (0-255)        │    (0-63)     │       (0-3)        │     (N bytes)     │
└──────────────────────┴───────────────┴────────────────────┴───────────────────┘
```

### Option B : Format avec CRC logiciel (Si le CRC matériel est désactivé)
Si le CRC matériel est désactivé (`AT+CRC=0`), la station s'attend à ce que l'émetteur calcule un CRC16 logiciel et l'ajoute à la fin de la charge utile LoRa. L'ESP32 de la station sol vérifiera ce CRC logiciel avant de valider le paquet (voir [radio.cpp](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/src/radio.cpp#L295-L318)).
* **Taille totale** : $5 + N$ octets.

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
| **Octet 0** | `uint8_t` | `SSID_NUM` | ID ou numéro unique du tracker (de 0 à 255). |
| **Octet 1** | `uint8_t` | `APID` | Identifiant du processus applicatif / type de paquet (de 0 à 63). |
| **Octet 2** | `uint8_t` | `SSID_TYPE` | Type de mission (`0` = FX, `1` = MF, `2` = BALLOON, `3` = OTHER). |
| **Octets 3 à 2+N** | `uint8_t[]` | `Payload` | Charge utile contenant les données brutes des capteurs ($N$ octets). |
| **Octets 3+N à 4+N** | `uint16_t` | `CRC16` | *(Option B uniquement)* Somme de contrôle logicielle de 2 octets en Little-Endian calculée sur les octets 0 à `2+N` inclus. |

---

## 💻 2. Format de la Trame Série NectarMC (Série USB & Bluetooth)

Lorsque la station sol a validé une trame radio (Option A ou B), elle l'encapsule dans une trame binaire conforme au protocole NectarMC pour l'envoyer au PC sur le port série USB ou Bluetooth.
* **Taille totale** : $13 + N$ octets.

> [!WARNING]
> **Évolution importante du format en fonction des versions :**
> La structure de la trame série transmise au PC (USB/Bluetooth) a évolué.
> * À partir de la version **v1.4.0**, la trame série fait **$13 + N$ octets** car elle inclut un horodatage absolu de 4 octets (`Timestamp` Epoch Unix) inséré juste après le bit de `SNR` et avant le `CRC16`.
> * Sur les versions antérieures (**v1.3.1 et inférieures**), la trame faisait **$9 + N$ octets** et ne comportait aucun horodatage (les octets après le `SNR` étaient directement les 2 octets du `CRC16`).

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
| **Octets 1 à 2** | `uint16_t` | `Id_mission` | Identifiant de mission codé en Little-Endian. Regroupe :<br>- Le type de tracker (`SSID_TYPE`, bits 15-14)<br>- Le numéro du tracker (`SSID_NUM`, bits 13-6)<br>- L'identifiant de paquet (`APID`, bits 5-0) |
| **Octet 3** | `uint8_t` | `payload_size` | Longueur $N$ de la charge utile LoRa brute en octets (exclut les CRC radio). |
| **Octets 4 à 3+N** | `uint8_t[]` | `Payload` | Données utiles brutes provenant du tracker LoRa ($N$ octets). |
| **Octet 4+N** | `int8_t` | `RSSI` | Niveau de puissance du signal reçu en dBm. Entier signé (ex: `-85` dBm). |
| **Octet 5+N** | `int8_t` | `SNR` | Rapport signal/bruit multiplié par 4 pour conserver une résolution de 0.25 dB (ex: `38` pour 9.5 dB). |
| **Octets 6+N à 9+N** | `uint32_t` | `Timestamp` | Horodatage Unix Epoch (secondes) codé en Little-Endian. Récupéré depuis l'horloge RTC de la station. |
| **Octets 10+N à 11+N** | `uint16_t` | `CRC16` | Somme de contrôle logicielle de validation (CCITT 0x1021, initialisé à 0xFFFF, Little-Endian) calculée sur l'ensemble de la trame série (du Magic `0xEB` jusqu'au Timestamp inclus). |
| **Octet 12+N** | `char` | `Newline` | Caractère retour à la ligne `\n` (`0x0A`) facilitant la détection de fin et la journalisation. |

---

## 📈 Historique et Évolution des Versions

Pour s'assurer que vos parseurs et décodeurs côté PC (sur NectarMC ou votre propre Dashboard) fonctionnent correctement, voici le récapitulatif des versions de la station et l'impact sur le format des trames :

| Version | Taille Trame Série | Format de Trame Série | Nouveautés Majeures |
| :---: | :---: | :--- | :--- |
| **v1.4.0** <br>*(Courante)* | **$13 + N$ octets** | `MAGIC` (1B) + `Id_mission` (2B) + `Size` (1B) + `Payload` (NB) + `RSSI` (1B) + `SNR` (1B) + **`Timestamp` (4B)** + `CRC16` (2B) + `\n` (1B) | - Intégration du **Timestamp RTC** (Epoch Unix) de 4 octets.<br>- Ajout des commandes `AT+TIME` et `AT+TIME?` pour synchroniser l'horloge RTC.<br>- Commande `AT+CRC=<enable>[,mode]` pour configurer le type de CRC (CCITT/IBM).<br>- Vérification automatique du CRC logiciel par l'ESP32 en mode `AT+CRC=0`. |
| **v1.3.1** | **$9 + N$ octets** | `MAGIC` (1B) + `Id_mission` (2B) + `Size` (1B) + `Payload` (NB) + `RSSI` (1B) + `SNR` (1B) + `CRC16` (2B) + `\n` (1B) | - Version originale compatible NectarMC.<br>- CRC matériel obligatoire par défaut sur la liaison radio LoRa.<br>- Pas de timestamp de réception (l'heure était extrapolée sur le PC). |

---

## Liens Utiles :
* **[README.md](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/README.md)** : Retourner à la page principale.
* **[CRC_GUIDE.md](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/CRC_GUIDE.md)** : Guide explicatif des deux niveaux de CRC (Liaison Radio et Liaison Série).
* **[src/serial.cpp](file:///c:/Users/paulm/OneDrive/Documents/PlatformIO/Projects/RocketStation-LoRa32/src/serial.cpp)** : Code source de formatage et d'envoi de la trame série vers le PC.
