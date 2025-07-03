# RocketStation-LoRa32

**RocketStation-LoRa32** est une station de réception simple permettant de capter les messages LoRa envoyés par une fusée expérimentale. Elle repose sur une carte TTGO LoRa32 (ESP32 + SX1276 + OLED) et fonctionne à 869.525 MHz.

## Objectif

Recevoir les trames LoRa transmises par une fusée en vol, les retransmettre sur le port série, et les enregistrer brutes sur une carte SD pour analyse post-vol.

## Fonctionnalités

- Réception LoRa à 869.525 MHz
- Affichage du statut et du RSSI sur écran OLED
- Enregistrement des trames brutes (non décodées) sur carte microSD
- Transmission des trames complètes (avec CRC) sur le port série
- Vérification CRC8 pour valider l'intégrité des trames (seules les trames valides sont traitées)

## Utilisation

1. Flasher le firmware sur une carte TTGO LoRa32.
2. Insérer une carte microSD formatée en FAT32.
3. Alimenter la carte via USB ou batterie.
4. Les trames LoRa reçues seront :
   - affichées sommairement sur l'écran OLED (ex : RSSI, statut),
   - renvoyées en binaire brut sur le port série (y compris le CRC),
   - enregistrées telles quelles sur la carte SD.

## Dépendances

Le projet utilise les bibliothèques suivantes :

- [RadioLib](https://github.com/jgromes/RadioLib) – gestion du module SX1276
- [U8g2](https://github.com/olikraus/u8g2) – affichage OLED
- `ESP32Time`, `SPI`, `Wire`, `FS`, `SD`, `Arduino`

## Format des trames

- Taille fixe de 36 octets par trame
- Le dernier octet est un CRC8 (polynôme 0x31)
- Aucune interprétation du contenu n'est effectuée dans la station
- Les trames invalides (CRC incorrect) sont ignorées

## Sortie série

- Les trames sont retransmises telles qu'elles ont été reçues (avec CRC), au format binaire
- Cette sortie est destinée à être exploitée par un outil externe (ex : script Python, enregistreur série)

## Auteur

Paul Miailhe, juin 2023

## Licence

