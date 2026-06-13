#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Script de simulation de trames série NectarMC pour RocketStation-LoRa32.
Ce script génère des trames valides (CRC OK) et corrompues (CRC KO) sur un port série virtuel
pour tester et valider le comportement du site web Ground Station sans nécessiter de matériel réel.

Dépendance : pip install pyserial
Usage : python test_serial_generator.py COM4 (remplacez par votre port)
"""

import sys
import time
import random
import struct

try:
    import serial
except ImportError:
    print("Erreur: Le module 'pyserial' est requis pour exécuter ce script.")
    print("Veuillez l'installer avec : pip install pyserial")
    sys.exit(1)

NECTAR_MAGIC = 0xEB

def calculate_crc16_ccitt(data: bytes) -> int:
    """
    Calcule le CRC16-CCITT (polynôme 0x1021, valeur initiale 0xFFFF)
    identique à l'implémentation C++ et JavaScript de RocketStation.
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def create_nectar_frame(ssid_type: int, ssid_num: int, apid: int, payload: bytes, rssi: int, snr: float, corrupt_crc: bool = False) -> bytes:
    """
    Construit une trame binaire NectarMC complète avec Timestamp et CRC.
    """
    # 1. Calcul de l'ID Mission (SSID + APID)
    # SSID (10 bits) = (ssid_type << 8) | ssid_num
    ssid = ((ssid_type & 0x03) << 8) | (ssid_num & 0xFF)
    id_mission = ((ssid & 0x03FF) << 6) | (apid & 0x3F)
    
    # 2. Header (4 octets) : Magic (1B), Mission (2B, Little-Endian), Payload Size (1B)
    payload_len = len(payload)
    header = struct.pack("<BHB", NECTAR_MAGIC, id_mission, payload_len)
    
    # 3. Métriques physiques : RSSI (1B, signed char), SNR * 4 (1B, signed char)
    rssi_byte = struct.pack("<b", rssi)
    snr_byte = struct.pack("<b", int(snr * 4.0))
    
    # 4. Horodatage Unix Epoch (4 octets, uint32, Little-Endian)
    current_time = int(time.time())
    time_bytes = struct.pack("<I", current_time)
    
    # Assemblage du bloc de données à protéger par le CRC
    data_to_protect = header + payload + rssi_byte + snr_byte + time_bytes
    
    # 5. Calcul du CRC16
    crc = calculate_crc16_ccitt(data_to_protect)
    
    if corrupt_crc:
        # Altérer le CRC pour simuler une trame corrompue
        crc = (crc ^ 0x5555) & 0xFFFF
        
    crc_bytes = struct.pack("<H", crc)
    
    # Trame complète finale avec le saut de ligne de fin (\n)
    full_frame = data_to_protect + crc_bytes + b'\n'
    return full_frame

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else 'COM4'
    baud = 115200
    
    print("==========================================================")
    print("      Simulateur de Trames Série RocketStation-LoRa32")
    print("==========================================================")
    print(f"Connexion au port série : {port} à {baud} baud...")
    
    try:
        ser = serial.Serial(port, baud, timeout=1)
        print("Connexion RÉUSSIE. Démarrage de la simulation...")
    except Exception as e:
        print(f"Erreur d'ouverture du port série: {e}")
        print("Assurez-vous que le port existe, qu'il n'est pas utilisé par un autre logiciel (comme PlatformIO ou le dashboard web) et que vos pilotes sont à jour.")
        print("\nNote: Sous Windows, vous pouvez créer des ports COM virtuels liés (ex: COM4 <-> COM5) à l'aide de 'com0com'.")
        sys.exit(1)
        
    print("\nCommandes:")
    print("- Envoi automatique d'une trame valide toutes les 2 secondes.")
    print("- Envoi automatique d'une trame corrompue (CRC invalide) toutes les 10 secondes.")
    print("Appuyez sur Ctrl+C pour quitter.")
    print("----------------------------------------------------------")

    packet_index = 0
    
    try:
        while True:
            packet_index += 1
            # Toutes les 5 trames (10 secondes), envoyer une trame corrompue
            corrupt = (packet_index % 5 == 0)
            
            # Paramètres de simulation aléatoires
            # Types de tracker : 0 = FX (Fusée), 1 = MF (Minifusée), 2 = BALLOON, 3 = OTHER
            tracker_type = random.choice([0, 1, 2])
            tracker_num = random.randint(1, 99)
            apid = random.randint(1, 10)
            rssi = random.randint(-110, -50)
            snr = round(random.uniform(-10.0, 12.0), 2)
            
            # Payload GPS fictive : Latitude, Longitude, Altitude (ex: 45.7597, 4.8422, 1200)
            lat = int(random.uniform(45.0, 46.0) * 10000)
            lon = int(random.uniform(4.0, 5.0) * 10000)
            alt = random.randint(100, 3500)
            payload = struct.pack("<iii", lat, lon, alt)
            
            # Génération de la trame
            frame = create_nectar_frame(tracker_type, tracker_num, apid, payload, rssi, snr, corrupt_crc=corrupt)
            
            # Envoi sur le port série
            ser.write(frame)
            
            # Affichage console de debug
            tracker_name = f"{['FX', 'MF', 'BALLOON', 'OTHER'][tracker_type]}{tracker_num}"
            status_str = "❌ CRC CORROMPU" if corrupt else "✅ VALIDE"
            print(f"[{time.strftime('%H:%M:%S')}] Envoyé #{packet_index} | Tracker: {tracker_name} | APID: {apid} | RSSI: {rssi} dBm | SNR: {snr} dB | Statut: {status_str}")
            print(f"  Trame (Hex): {frame[:-1].hex().upper()}")
            
            time.sleep(2.0)
            
    except KeyboardInterrupt:
        print("\nSimulation arrêtée par l'utilisateur.")
    finally:
        ser.close()
        print("Port série fermé. Au revoir !")

if __name__ == '__main__':
    main()
