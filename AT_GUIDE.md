# Guide Complet des Commandes AT — Nectar RX Station v1.6.0

Ce document détaille toutes les commandes AT disponibles sur le récepteur Nectar RX Station. 

Les commandes peuvent être envoyées via la **liaison série USB** (115200 baud) ou en sans-fil via la **liaison Bluetooth SPP** (nommé `Nectar-RxStation-LoRa32`).

---

## 🔒 Sécurité et Format

> [!IMPORTANT]
> **Sécurité Anti-Conflit :**
> Toutes les commandes doivent obligatoirement commencer par le préfixe **`AT`**. Tout flux série ou Bluetooth ne débutant pas par ces deux lettres est silencieusement ignoré. Cela évite tout conflit avec des trames de données binaires de télémétrie entrantes ou du bruit de fond.

* Les commandes sont insensibles à la casse (ex: `at+freq?` et `AT+FREQ?` sont identiques).
* Chaque commande se termine par un retour chariot (`\r` ou `\n`).
* La longueur maximale d'une commande est de **63 caractères**. Au-delà, un message `ERROR: Command too long` est retourné pour des raisons de sécurité de débordement mémoire.

---

## 📋 Table des Matières

1. [Système et Diagnostic](#1-système-et-diagnostic)
2. [Configuration Physique de la Radio](#2-configuration-physique-de-la-radio)
3. [Résilience et Stockage](#3-résilience-et-stockage)
4. [Mesures de Signal](#4-mesures-de-signal)
5. [Gestion des Erreurs](#5-gestion-des-erreurs)

---

## 1. Système et Diagnostic

### `AT`
*   **Rôle** : Vérifie la communication avec le récepteur.
*   **Format de réponse** : `OK`

### `AT+HELP` ou `AT?`
*   **Rôle** : Renvoie la liste complète d'aide de toutes les commandes supportées.
*   **Format de réponse** : Une liste textuelle des commandes, terminée par `OK`.

### `AT+INFO` ou `AT+VER`
*   **Rôle** : Interroge l'identification de la station et sa version de firmware.
*   **Format de réponse** : `+INFO: NECTAR RX STATION,FW=1.6.0,Band=<bande>` (où bande = `868` ou `433`).

### `AT+CFG` ou `AT+STATUS`
*   **Rôle** : Affiche un rapport complet de diagnostic de l'état actuel de la station.
*   **Informations affichées** : 
    *   Version du firmware et bande native.
    *   Paramètres LoRa actifs (Fréquence, SF, BW, CRC).
    *   Statut matériel de la carte SD et du Bluetooth.
    *   Taille de pile libre (**Stack High Water Mark**) de chaque tâche FreeRTOS (`vRadioRxTask`, `vPeripheralTask`, `vIOProcessingTask`).
*   **Format de réponse** : Un récapitulatif multiligne, terminé par `OK`.

---

## 2. Configuration Physique de la Radio

### `AT+FREQ?`
*   **Rôle** : Interroge la fréquence de réception active (en MHz).
*   **Format de réponse** : `+FREQ: <frequence>` (ex: `+FREQ: 869.525`) suivi de `OK`.

### `AT+FREQ=<mhz>`
*   **Rôle** : Modifie la fréquence active (ex: `AT+FREQ=869.525`).
*   **Contraintes** : La valeur doit être située dans la bande native configurée par le matériel :
    *   **Bande 868 MHz** : Limites de `863.0` à `870.0` MHz.
    *   **Bande 433 MHz** : Limites de `433.05` à `434.79` MHz.
*   **Format de réponse** : `OK` ou `ERROR: Out of limits`.

### `AT+SF?`
*   **Rôle** : Interroge le Spreading Factor (facteur d'étalement) actif.
*   **Format de réponse** : `+SF: <valeur>` (ex: `+SF: 7`) suivi de `OK`.

### `AT+SF=<val>`
*   **Rôle** : Modifie le Spreading Factor.
*   **Contraintes** : Valeur entière comprise entre **6** et **12**.
*   **Format de réponse** : `OK` ou `ERROR: SF must be between 6 and 12`.

### `AT+BW?`
*   **Rôle** : Interroge la bande passante (Bandwidth) active (en kHz).
*   **Format de réponse** : `+BW: <valeur>` (ex: `+BW: 250.0`) suivi de `OK`.

### `AT+BW=<val>`
*   **Rôle** : Modifie la bande passante (en kHz).
*   **Contraintes** : Valeur positive supérieure à **0** (généralement 125.0, 250.0, ou 500.0).
*   **Format de réponse** : `OK` ou `ERROR: Bandwidth must be greater than 0`.

### `AT+CRC?`
*   **Rôle** : Interroge le statut du CRC matériel LoRa.
*   **Format de réponse** : `+CRC: <status>,<mode>` (ex: `+CRC: 1,0`) suivi de `OK`.

### `AT+CRC=<0|1>[,0|1]`
*   **Rôle** : Active/désactive le CRC matériel du SX1276 et définit le mode algorithmique.
*   **Arguments** :
    *   `paramètre 1` (requis) : `0` = CRC désactivé, `1` = CRC activé.
    *   `paramètre 2` (optionnel) : `0` = mode CCITT (par défaut), `1` = mode IBM.
*   **Format de réponse** : `OK` ou `ERROR`.

---

## 3. Résilience et Stockage

### `AT+TIME?`
*   **Rôle** : Interroge l'heure RTC de la station sous forme de Timestamp Unix Epoch.
*   **Format de réponse** : `+TIME: <timestamp>` (ex: `+TIME: 1781290382`) suivi de `OK`.

### `AT+TIME=<epoch>`
*   **Rôle** : Met à jour l'heure RTC de la station avec le Timestamp Epoch fourni (en secondes).
*   **Format de réponse** : `OK`.

### `AT+LIST`
*   **Rôle** : Liste tous les fichiers CSV de log présents sur la carte SD.
*   **Format de réponse** :
    ```
    +LIST: /log_0.csv,45023
    +LIST: /log_1.csv,102832
    OK
    ```

### `AT+DUMP=<file>`
*   **Rôle** : Transmet l'intégralité du contenu d'un fichier CSV de la carte SD via la liaison série/Bluetooth active.
*   **Format de réponse** :
    ```
    +DUMP: START
    Timestamp,RSSI,SNR,SSID,APID,RawFrame
    00:05:42,-85.00,8.50,FX99,7,EBC7181401020304
    ...
    +DUMP: END
    OK
    ```
*   **Remarque** : Si le fichier n'existe pas, retourne `ERROR: File not found`.

### `AT+SAVE`
*   **Rôle** : Persiste la configuration LoRa active (Freq, SF, BW, CRC) dans la mémoire non-volatile NVS de l'ESP32. Elle sera rechargée automatiquement à chaque démarrage.
*   **Format de réponse** : `OK`.

### `AT+RESET`
*   **Rôle** : Réinitialise tous les paramètres aux valeurs d'usine par défaut, efface la mémoire NVS et redémarre la station.
*   **Format de réponse** : `OK` (puis redémarrage physique).

---

## 4. Mesures de Signal

### `AT+RSSI?`
*   **Rôle** : Renvoie l'indicateur de force de signal reçu (RSSI) en dBm du tout dernier paquet LoRa reçu.
*   **Format de réponse** : `+RSSI: <rssi>` (ex: `+RSSI: -82.0`) suivi de `OK`.

### `AT+SNR?`
*   **Rôle** : Renvoie le rapport signal/bruit (SNR) en dB du tout dernier paquet LoRa reçu.
*   **Format de réponse** : `+SNR: <snr>` (ex: `+SNR: 9.7`) suivi de `OK`.

### `AT+SIG?`
*   **Rôle** : Renvoie simultanément le RSSI et le SNR du dernier paquet.
*   **Format de réponse** : `+SIG: RSSI=<rssi>,SNR=<snr>` suivi de `OK`.

---

## 5. Gestion des Erreurs

Lorsqu'une commande n'a pas pu être traitée, le récepteur retourne un message structuré commençant par `ERROR: ` :

| Code d'Erreur | Signification |
| :--- | :--- |
| `ERROR: Unknown AT command '<saisie>'` | La commande saisie n'existe pas ou contient une faute de frappe. |
| `ERROR: Out of limits [<min> - <max>] MHz` | La fréquence demandée sort de la bande native de l'ESP32. |
| `ERROR: SF must be between 6 and 12` | Le Spreading Factor fourni est hors de la plage physique [6-12]. |
| `ERROR: Bandwidth must be greater than 0` | La bande passante doit être strictement positive. |
| `ERROR: Command too long (max 63 chars)` | La ligne d'entrée dépasse la capacité sécurisée du buffer de traitement. |
| `ERROR: File not found` | Le fichier demandé via `AT+DUMP` n'existe pas sur la carte SD. |
| `ERROR: SD card not initialized` | Impossible d'accéder à la liste ou au fichier car la carte SD est absente ou défectueuse. |
