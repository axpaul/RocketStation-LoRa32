# RocketStation-LoRa32 (Récepteur NectarMC)

**RocketStation-LoRa32** est une station au sol de réception LoRa destinée à capter la télémétrie de fusées expérimentales et de ballons-sondes. Elle repose sur une carte TTGO LoRa32 (ESP32 + SX1276 + OLED) et fonctionne à la fréquence de 869.525 MHz. 

Cette version a été optimisée pour être entièrement compatible avec le logiciel de traitement de télémétrie **NectarMC** en générant des trames binaires série conformes et en prenant en charge plusieurs émetteurs de manière dynamique.

## Fonctionnalités principales

- **Compatibilité NectarMC** : Génère à la volée des trames binaires série structurées (Magic byte 0xEB, Id_mission, calcul du CRC16-CCITT en Little-endian) prêtes à être décodées par NectarMC.
- **Réception LoRa dynamique** : Supporte des longueurs de paquets variables sans avoir besoin de recompiler le récepteur.
- **Validation matérielle** : Utilise le CRC matériel du module SX1276 pour garantir l'intégrité des données reçues dans l'air (les trames corrompues sont rejetées à la source).
- **Journalisation CSV intelligente** : Crée un fichier par session de démarrage (`/log_0.csv`, `/log_1.csv`, etc.) pour éviter les mélanges de données. Enregistre l'horodatage, le RSSI, le SNR, le SSID de l'émetteur, l'APID et la trame brute en hexadécimal.
- **Interface OLED temps réel** : Affiche en continu l'heure de la station (rafraîchie toutes les secondes) ainsi que les informations de la dernière trame valide reçue (SSID, APID, RSSI et SNR).

## Structure des trames LoRa (Émetteurs)

Pour que la station puisse router dynamiquement les trames vers NectarMC, les émetteurs doivent envoyer un paquet LoRa structuré ainsi :

| Position | Type | Rôle | Description |
| :--- | :--- | :--- | :--- |
| **Octet 0** | `uint8_t` | `SSID_NUM` | ID ou numéro de la mission (de 0 à 255) |
| **Octet 1** | `uint8_t` | `APID` | Type d'application / de paquet (de 0 à 63) |
| **Octet 2** | `uint8_t` | `SSID_TYPE` | Type de mission (`0` = FX, `1` = MF, `2` = BALLOON, `3` = OTHER) |
| **Octets 3 à L-1** | `uint8_t[]` | `Payload` | Données brutes des capteurs (taille variable L-3) |

*Note : Aucun CRC logiciel n'est nécessaire dans le paquet LoRa car le SX1276 gère l'intégrité de la liaison radio au niveau matériel.*

## Format de trame série NectarMC (Sortie USB)

Les trames émises par le récepteur vers le PC sur le port série USB ont la structure binaire suivante (taille totale : 4 + N + 2 octets) :

```
┌───────────────────────────────────────────┬──────────────┬─────────────────┐
│                 HEADER                    │   PAYLOAD    │ PACKET CONTROL  │
├───────────────────────────────────────────┼──────────────┼─────────────────┤
│   MAGIC     │  Id_mission  │ payload_size │   N bytes    │     CRC16       │
│   1 Byte    │   2 Bytes    │   1 Byte     │              │    2 Bytes      │
│    0xEB     │ (Little-End) │              │              │  (Little-End)   │
└─────────────┴──────────────┴──────────────┴──────────────┴─────────────────┘
```

- **MAGIC** : `0xEB` (Marqueur de synchronisation).
- **Id_mission** : Fusion du SSID (TYPE sur bits 15-14, NUM sur bits 13-6) et de l'APID (bits 5-0) sur 16 bits.
- **payload_size** : Nombre d'octets de données utiles (sans l'en-tête LoRa).
- **CRC16** : Calculé sur le Header + Payload (polynôme CCITT 0x1021, init 0xFFFF).

## Structure des logs (Carte SD)

Les données sont enregistrées dans un fichier CSV avec la structure suivante :
`Timestamp,RSSI,SNR,SSID,APID,RawFrame`

Exemple de ligne de log :
`00:05:42,-85.00,8.50,FX99,7,EBC7181401020304`

## Compilation et Flashage

Le projet utilise **PlatformIO**. Pour compiler et flasher le récepteur :

1. Ouvrez le projet dans VS Code avec l'extension PlatformIO.
2. Connectez votre carte TTGO LoRa32 v2.1.6 via USB.
3. Lancez la compilation et le téléversement (Upload).

En ligne de commande :
```powershell
pio run -t upload
```

## Auteur

Paul Miailhe - Juin 2023 (Mis à jour en Mai 2026 pour la compatibilité NectarMC).
