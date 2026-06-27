import { ESPLoader, Transport } from 'https://cdn.jsdelivr.net/npm/esptool-js@0.6.0/+esm';

// ============================================================================
// Variables Globales
// ============================================================================
let port = null;
let reader = null;
let inputWriter = null;
let isConnected = false;
let rxBuffer = [];
let packetIndex = 0;
let crcErrorsCount = 0;
let readLoopPromise = null; // Promesse pour suivre la fin de la boucle de lecture
let activeTrackers = {};    // Dictionnaire des émetteurs détectés : { name: { typeLabelKey, lastApid, packetCount, lastSeen, lastPayloadHex } }
let allReceivedFrames = []; // Historique complet pour export CSV (capé à 5000 trames)

// Variables pour le téléchargement de logs depuis la carte SD
let isDownloadingSdFile = false;
let sdDownloadFilename = '';
let sdDownloadSize = 0;
let sdDownloadBuffer = [];
let sdDownloadLinesCount = 0;

// Variables pour le calcul de débit et le graphique
let bytesCountThisSecond = 0;
let lastThroughputCalculation = Date.now();
const throughputHistory = Array(30).fill(0); // 30 points pour 30 secondes

// Variables de configuration active de la carte
let currentConfig = {
  frequency: 869.525,
  sf: 8,
  bw: 250.0
};

// Variable globale pour stocker le nom du port et la langue
let currentPortName = '';
let currentLang = 'fr';

// Historique pour les graphiques RSSI/SNR
const maxChartPoints = 30;
const rssiHistory = []; // { value, time }
const snrHistory = [];  // { value, time }

// Variables globales pour la télémétrie WASP et la carte Leaflet
let waspMap = null;
let waspMarker = null;
let waspLastPos = null; // Stockage de la dernière position valide {lat, lon}

// Dictionnaire de traduction
const i18n = {
  fr: {
    badge_disconnected: "Déconnecté",
    badge_connected: "Connecté",
    header_title: "NECTAR RX STATION",
    header_subtitle: "Web Control Center v1.6.0",
    conn_title: "🔌 Liaison Série USB",
    maintenance_title: "🛠️ Outils & Maintenance",
    sd_title: "📁 Journaux Carte SD",
    sd_desc: "Listez et téléchargez directement les fichiers CSV de vol enregistrés sur la carte SD.",
    at_helper_title: "📋 Aide-Mémoire AT",
    at_desc_at: "Test de communication avec la station",
    at_desc_help: "Afficher le menu d'aide complet",
    at_desc_info: "Interroger l'identification de la station",
    at_desc_freq_get: "Interroger la fréquence active",
    at_desc_freq_set: "Configurer la fréquence LoRa (en MHz)",
    at_desc_sf_get: "Interroger le Spreading Factor",
    at_desc_sf_set: "Configurer le Spreading Factor (6-12)",
    at_desc_bw_get: "Interroger la bande passante",
    at_desc_bw_set: "Configurer la bande passante (kHz)",
    at_desc_crc_get: "Interroger le statut du CRC",
    at_desc_crc_set: "Configurer le CRC (0=OFF, 1=ON [,mode])",
    at_desc_time_get: "Interroger l'heure RTC de la station",
    at_desc_time_set: "Configurer l'heure RTC (Unix Epoch)",
    at_desc_rssi_get: "RSSI du dernier paquet reçu",
    at_desc_snr_get: "SNR du dernier paquet reçu",
    at_desc_sig_get: "RSSI et SNR du dernier paquet",
    at_desc_cfg: "Obtenir la configuration détaillée",
    at_desc_list: "Lister les fichiers CSV sur la carte SD",
    at_desc_dump: "Afficher/télécharger un fichier CSV",
    at_desc_save: "Sauvegarder la config active en NVS",
    at_desc_reset: "Reset d'usine et redémarrage",
    sd_btn_list: "Lister les fichiers",
    sd_th_name: "Nom",
    sd_th_size: "Taille",
    sd_th_action: "Action",
    sd_files_empty: "Aucun fichier listé",
    sd_status_label: "Téléchargement :",
    sd_download_btn: "Télécharger",
    sd_download_wait: "Téléchargement...",
    log_sd_download_start: "Téléchargement du fichier {file}...",
    log_sd_download_success: "Téléchargement réussi : {file} ({lines} lignes)",
    log_sd_download_error: "Erreur lors du téléchargement : {message}",
    conn_baudrate: "Vitesse de transmission (Baud) :",
    conn_fw_version: "Version Décodeur / Trame :",
    conn_btn_connect: "Connexion",
    conn_btn_disconnect: "Déconnexion",
    conn_status_label: "Statut :",
    conn_no_device: "Aucun appareil connecté",
    conn_port_prefix: "Port : ",
    conn_voice_alerts: "🎙️ Synthèse Vocale (Alertes Tracker)",
    conn_wasp_decoding: "🐝 Activer décodeur WASP (32B)",
    wasp_title: "🚀 Télémétrie Wasp Décryptée",
    wasp_map_title: "🗺️ Position GPS Live",
    config_title: "⚙️ Paramètres Radio",
    config_frequency: "Fréquence (MHz) :",
    config_sf: "Spreading Factor (SF) :",
    config_bw: "Bande Passante (BW) :",
    config_crc: "Contrôle CRC :",
    config_btn_read: "Actualiser",
    config_btn_write: "Appliquer",
    config_btn_save: "Sauver NVS",
    config_btn_reset: "Reset Usine",
    stats_title: "📊 Qualité de Liaison",
    stats_rssi: "RSSI (dBm)",
    stats_snr: "SNR (dB)",
    chart_rssi_title: "Tendance RSSI",
    chart_snr_title: "Tendance SNR",
    stats_count: "Trames (Reçues / KO)",
    stats_crc_errors: "Erreurs CRC",
    flash_title: "⚡ Mise à Jour Firmware",
    flash_desc: "Flashez directement la version <strong>v1.6.0</strong> depuis votre navigateur par port USB.",
    flash_band: "Bande Radio native de la carte :",
    flash_band_868: "868 MHz (Europe)",
    flash_band_433: "433 MHz",
    flash_btn_flash: "Flasher la carte (v1.6.0)",
    flash_status_label: "Statut :",
    flash_status_waiting: "Attente...",
    trackers_title: "🛸 Émetteurs Détectés (Active Trackers)",
    btn_reset: "Réinitialiser",
    th_tracker: "Tracker (SSID)",
    th_mission_type: "Type de Mission",
    th_last_apid: "Dernier APID",
    th_received_frames: "Trames Reçues",
    th_last_activity: "Dernière Activité",
    th_status: "Statut",
    th_payload: "Charge Utile (Hex)",
    th_crc: "CRC",
    th_signal_quality: "Qualité Signal (RSSI / SNR)",
    trackers_empty: "Aucun émetteur détecté pour l'instant. Branchez le récepteur LoRa pour intercepter les signaux.",
    telemetry_title: "📡 Trames reçues en direct (NectarMC)",
    telemetry_btn_export: "Exporter CSV",
    btn_clear: "Effacer",
    th_index: "Index",
    th_timestamp: "Horodatage",
    th_apid: "APID",
    th_size: "Taille",
    th_rssi: "RSSI",
    th_snr: "SNR",
    telemetry_empty: "Aucune trame reçue pour l'instant. Branchez le port série et mettez sous tension vos trackers.",
    terminal_title: "📟 Console & Terminal",
    terminal_placeholder: "Tapez une commande AT (ex: AT, AT+FREQ?, AT+CFG)...",
    btn_send: "Envoyer",
    footer_credit: "Conçu et développé par",
    
    // Dialogues et logs
    alert_browser_unsupported: "Votre navigateur ne supporte pas l'API Web Serial. Veuillez utiliser Google Chrome, Microsoft Edge ou Opera.",
    confirm_factory_reset: "Voulez-vous restaurer les paramètres d'usine ? La carte va redémarrer.",
    alert_no_frames_export: "Aucune trame en mémoire à exporter.",
    alert_monitor_active_disconnect: "La liaison moniteur série est active. Veuillez cliquer sur 'Déconnexion' avant de lancer le flash du firmware.",
    log_flash_port_select: "Sélection du port série pour le flash (choisissez le port de votre carte)...",
    log_port_opening: "Ouverture du port série à {baud} baud...",
    log_conn_success: "Connexion établie avec succès.",
    log_conn_error: "Erreur de connexion : {message}",
    log_disconnected: "Liaison série déconnectée.",
    log_read_error: "Erreur de lecture : {message}",
    log_send_error: "Erreur d'envoi : {message}",
    log_physical_disconnect: "Le port série a été déconnecté physiquement.",
    log_crc_error: "[ERREUR CRC] Trame rejetée : CRC reçu = {rec}, calculé = {calc}",
    log_write_flash_start: "Début de l'écriture de l'application à 0x10000...",
    log_update_complete_reboot: "Mise à jour terminée ! Redémarrage de la carte...",
    log_flash_error: "Erreur lors du flash : {message}",
    flash_status_connecting: "Connexion à l'ESP32...",
    flash_status_syncing: "Synchronisation de la carte...",
    flash_status_chip: "Puce détectée : {chip}",
    log_download_bin: "Téléchargement du firmware depuis {url}...",
    log_download_bin_failed: "Impossible de récupérer le binaire ({status})",
    flash_status_writing: "Écriture en cours (flash)...",
    flash_status_success: "Flash Réussi !",
    flash_status_failed: "ÉCHEC !",
    
    // Télémétrie dynamique
    mission_rocket: "Fusée (FX)",
    mission_minirocket: "Minifusée (MF)",
    mission_balloon: "Ballon (BALLOON)",
    mission_other: "Autre (OTHER)",
    unit_bytes: "octets",
    status_active: "ACTIF",
    status_lost: "PERDU",
    
    // Alertes vocales
    voice_new_tracker: "Nouveau tracker détecté, {name}",
    voice_tracker_back: "Tracker {name} de retour en ligne",
    voice_tracker_lost: "Alerte, tracker {name} perdu"
  },
  en: {
    badge_disconnected: "Disconnected",
    badge_connected: "Connected",
    header_title: "NECTAR RX STATION",
    header_subtitle: "Web Control Center v1.6.0",
    conn_title: "🔌 USB Serial Link",
    maintenance_title: "🛠️ Tools & Maintenance",
    sd_title: "📁 SD Card Logs",
    sd_desc: "List and download flight CSV files recorded on the SD card.",
    at_helper_title: "📋 AT Command Cheatsheet",
    at_desc_at: "Test communication with the station",
    at_desc_help: "Print the complete help menu",
    at_desc_info: "Query station identification",
    at_desc_freq_get: "Query the active frequency",
    at_desc_freq_set: "Set the LoRa frequency (in MHz)",
    at_desc_sf_get: "Query the Spreading Factor",
    at_desc_sf_set: "Set the Spreading Factor (6-12)",
    at_desc_bw_get: "Query the bandwidth",
    at_desc_bw_set: "Set the bandwidth (kHz)",
    at_desc_crc_get: "Query CRC status",
    at_desc_crc_set: "Set CRC (0=OFF, 1=ON [,mode])",
    at_desc_time_get: "Query the station's RTC time",
    at_desc_time_set: "Set the RTC time (Unix Epoch)",
    at_desc_rssi_get: "RSSI of the last received packet",
    at_desc_snr_get: "SNR of the last received packet",
    at_desc_sig_get: "RSSI and SNR of the last packet",
    at_desc_cfg: "Get detailed configuration",
    at_desc_list: "List CSV log files on the SD card",
    at_desc_dump: "Print/download a CSV file",
    at_desc_save: "Save active configuration to NVS",
    at_desc_reset: "Factory reset and reboot",
    sd_btn_list: "List Files",
    sd_th_name: "Name",
    sd_th_size: "Size",
    sd_th_action: "Action",
    sd_files_empty: "No files listed",
    sd_status_label: "Downloading:",
    sd_download_btn: "Download",
    sd_download_wait: "Downloading...",
    log_sd_download_start: "Downloading {file}...",
    log_sd_download_success: "Download successful: {file} ({lines} lines)",
    log_sd_download_error: "Download error: {message}",
    conn_baudrate: "Baud Rate:",
    conn_fw_version: "Decoder / Frame Version:",
    conn_btn_connect: "Connect",
    conn_btn_disconnect: "Disconnect",
    conn_status_label: "Status:",
    conn_no_device: "No device connected",
    conn_port_prefix: "Port: ",
    conn_voice_alerts: "🎙️ Voice Synthesis (Tracker Alerts)",
    conn_wasp_decoding: "🐝 Enable WASP Decoder (32B)",
    wasp_title: "🚀 Decrypted Wasp Telemetry",
    wasp_map_title: "🗺️ Live GPS Position",
    config_title: "⚙️ Radio Settings",
    config_frequency: "Frequency (MHz):",
    config_sf: "Spreading Factor (SF):",
    config_bw: "Bandwidth (BW):",
    config_crc: "CRC Control:",
    config_btn_read: "Refresh",
    config_btn_write: "Apply",
    config_btn_save: "Save NVS",
    config_btn_reset: "Factory Reset",
    stats_title: "📊 Link Quality",
    stats_rssi: "RSSI (dBm)",
    stats_snr: "SNR (dB)",
    chart_rssi_title: "RSSI Trend",
    chart_snr_title: "SNR Trend",
    stats_count: "Frames (Received / KO)",
    stats_crc_errors: "CRC Errors",
    flash_title: "⚡ Firmware Update",
    flash_desc: "Flash version <strong>v1.6.0</strong> directly from your browser via USB port.",
    flash_band: "Board's native Radio Band:",
    flash_band_868: "868 MHz (Europe)",
    flash_band_433: "433 MHz",
    flash_btn_flash: "Flash Board (v1.6.0)",
    flash_status_label: "Status:",
    flash_status_waiting: "Waiting...",
    trackers_title: "🛸 Detected Transmitters (Active Trackers)",
    btn_reset: "Reset",
    th_tracker: "Tracker (SSID)",
    th_mission_type: "Mission Type",
    th_last_apid: "Last APID",
    th_received_frames: "Received Frames",
    th_last_activity: "Last Activity",
    th_status: "Status",
    th_payload: "Payload (Hex)",
    th_crc: "CRC",
    th_signal_quality: "Signal Quality (RSSI / SNR)",
    trackers_empty: "No transmitter detected yet. Connect the LoRa receiver to intercept signals.",
    telemetry_title: "📡 Live received frames (NectarMC)",
    telemetry_btn_export: "Export CSV",
    btn_clear: "Clear",
    th_index: "Index",
    th_timestamp: "Timestamp",
    th_apid: "APID",
    th_size: "Size",
    th_rssi: "RSSI",
    th_snr: "SNR",
    telemetry_empty: "No frames received yet. Connect the serial port and power on your trackers.",
    terminal_title: "📟 Console & Terminal",
    terminal_placeholder: "Type an AT command (e.g. AT, AT+FREQ?, AT+CFG)...",
    btn_send: "Send",
    footer_credit: "Designed and developed by",
    
    // Dialogues and logs
    alert_browser_unsupported: "Your browser does not support the Web Serial API. Please use Google Chrome, Microsoft Edge, or Opera.",
    confirm_factory_reset: "Do you want to restore factory settings? The board will reboot.",
    alert_no_frames_export: "No frames in memory to export.",
    alert_monitor_active_disconnect: "The serial monitor link is active. Please click 'Disconnect' before starting the firmware flash.",
    log_flash_port_select: "Selecting the serial port for flash (choose your board's port)...",
    log_port_opening: "Opening serial port at {baud} baud...",
    log_conn_success: "Connection established successfully.",
    log_conn_error: "Connection error: {message}",
    log_disconnected: "Serial link disconnected.",
    log_read_error: "Read error: {message}",
    log_send_error: "Send error: {message}",
    log_physical_disconnect: "The serial port was physically disconnected.",
    log_crc_error: "[CRC ERROR] Frame discarded: received CRC = {rec}, calculated = {calc}",
    log_write_flash_start: "Starting application write at 0x10000...",
    log_update_complete_reboot: "Update complete! Rebooting board...",
    log_flash_error: "Error during flash: {message}",
    flash_status_connecting: "Connecting to ESP32...",
    flash_status_syncing: "Synchronizing board...",
    flash_status_chip: "Chip detected: {chip}",
    log_download_bin: "Downloading firmware from {url}...",
    log_download_bin_failed: "Could not fetch the binary ({status})",
    flash_status_writing: "Writing in progress (flash)...",
    flash_status_success: "Flash Success!",
    flash_status_failed: "FAILED!",
    
    // Dynamic telemetry
    mission_rocket: "Rocket (FX)",
    mission_minirocket: "Mini-rocket (MF)",
    mission_balloon: "Balloon (BALLOON)",
    mission_other: "Other (OTHER)",
    unit_bytes: "bytes",
    status_active: "ACTIVE",
    status_lost: "LOST",
    
    // Vocal alerts
    voice_new_tracker: "New tracker detected, {name}",
    voice_tracker_back: "Tracker {name} back online",
    voice_tracker_lost: "Alert, tracker {name} lost"
  }
};

function getTranslation(key, replacements = {}) {
  let text = i18n[currentLang]?.[key] || i18n['fr']?.[key] || key;
  for (const [placeholder, value] of Object.entries(replacements)) {
    text = text.replace(`{${placeholder}}`, value);
  }
  return text;
}

function setLanguage(lang) {
  if (lang !== 'fr' && lang !== 'en') {
    lang = 'fr';
  }
  currentLang = lang;
  localStorage.setItem('nectar_lang', lang);
  
  // Mettre à jour les éléments statiques avec data-i18n
  document.querySelectorAll('[data-i18n]').forEach(el => {
    const key = el.getAttribute('data-i18n');
    if (i18n[lang] && i18n[lang][key]) {
      if (i18n[lang][key].includes('<') && i18n[lang][key].includes('>')) {
        el.innerHTML = i18n[lang][key];
      } else {
        el.textContent = i18n[lang][key];
      }
    }
  });

  // Mettre à jour les placeholders
  document.querySelectorAll('[data-i18n-placeholder]').forEach(el => {
    const key = el.getAttribute('data-i18n-placeholder');
    if (i18n[lang] && i18n[lang][key]) {
      el.placeholder = i18n[lang][key];
    }
  });

  // Gérer la classe active sur les boutons
  const btnFr = document.getElementById('btn-lang-fr');
  const btnEn = document.getElementById('btn-lang-en');
  if (btnFr && btnEn) {
    if (lang === 'fr') {
      btnFr.classList.add('active');
      btnEn.classList.remove('active');
    } else {
      btnEn.classList.add('active');
      btnFr.classList.remove('active');
    }
  }

  // Mettre à jour les textes dynamiques de connexion et des tableaux
  updateConnectionUI(isConnected, currentPortName);
  renderTelemetryTable();
  updateTrackersTable();
  renderAtHelperList();
}

// ============================================================================
// Sélection des éléments du DOM
// ============================================================================
const btnConnect = document.getElementById('btn-connect');
const btnDisconnect = document.getElementById('btn-disconnect');
const lblPortName = document.getElementById('lbl-port-name');
const connBadge = document.getElementById('conn-badge');
const selectBaudrate = document.getElementById('baudrate');
const selectFwVersion = document.getElementById('select-fw-version');

// Inputs Configuration (support des anciens ID 'input-*' si index.html est en cache)
const inputFreq = document.getElementById('input-freq');
const selectSf = document.getElementById('select-sf') || document.getElementById('input-sf');
const selectBw = document.getElementById('select-bw') || document.getElementById('input-bw');
const selectCrc = document.getElementById('select-crc');

// Boutons Configuration
const btnReadCfg = document.getElementById('btn-read-cfg');
const btnWriteCfg = document.getElementById('btn-write-cfg');
const btnSaveCfg = document.getElementById('btn-save-cfg');
const btnResetCfg = document.getElementById('btn-reset-cfg');

  // Stats
  const statRssi = document.getElementById('stat-rssi');
  const statSnr = document.getElementById('stat-snr');
  const statCount = document.getElementById('stat-count');
  const statCrcErrors = document.getElementById('stat-crc-errors');
  const lblThroughput = document.getElementById('lbl-throughput');

// Terminal & Log
const terminalLogs = document.getElementById('terminal-logs');
const terminalForm = document.getElementById('terminal-form');
const terminalInput = document.getElementById('terminal-input');
const btnSend = document.getElementById('btn-send');

// Flasher
const btnFlash = document.getElementById('btn-flash');
const selectBand = document.getElementById('select-band');
const flashProgressContainer = document.getElementById('flash-progress-container');
const flashProgressBar = document.getElementById('flash-progress-bar');
const lblFlashStatus = document.getElementById('lbl-flash-status');
const lblFlashPercent = document.getElementById('lbl-flash-percent');

// Table
const tableTelemetryBody = document.querySelector('#table-telemetry tbody');
const rowEmpty = document.getElementById('row-empty');

// Nouveaux boutons de nettoyage, export et option vocale
const btnClearTerminal = document.getElementById('btn-clear-terminal');
const btnClearTelemetry = document.getElementById('btn-clear-telemetry');
const btnExportTelemetry = document.getElementById('btn-export-telemetry');
const btnClearTrackers = document.getElementById('btn-clear-trackers');
const btnListSd = document.getElementById('btn-list-sd');

// Helper sécurisé pour activer/désactiver un élément s'il existe
function setElementDisabled(el, disabled) {
  if (el) {
    el.disabled = disabled;
  }
}

// ============================================================================
// Fonctions d'Affichage & Utilitaires
// ============================================================================
function speak(text) {
  const chkVoiceAlerts = document.getElementById('chk-voice-alerts');
  if (!chkVoiceAlerts || !chkVoiceAlerts.checked) return;
  
  if ('speechSynthesis' in window) {
    const utterance = new SpeechSynthesisUtterance(text);
    utterance.lang = currentLang === 'fr' ? 'fr-FR' : 'en-US';
    window.speechSynthesis.speak(utterance);
  }
}

function logToTerminal(message, type = 'cmd-out') {
  if (!terminalLogs) return;
  const div = document.createElement('div');
  div.className = type;
  div.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
  terminalLogs.appendChild(div);
  
  // Limiter le nombre de lignes à 500 pour éviter de surcharger le DOM
  while (terminalLogs.children.length > 500) {
    terminalLogs.removeChild(terminalLogs.firstChild);
  }
  
  terminalLogs.scrollTop = terminalLogs.scrollHeight;
}

function updateConnectionUI(connected, name = '') {
  isConnected = connected;
  currentPortName = name;
  if (connBadge) {
    connBadge.textContent = connected ? getTranslation('badge_connected') : getTranslation('badge_disconnected');
    connBadge.className = connected ? 'badge connected' : 'badge disconnected';
  }
  if (lblPortName) {
    lblPortName.textContent = connected ? getTranslation('conn_port_prefix') + name : getTranslation('conn_no_device');
  }
  
  setElementDisabled(btnConnect, connected);
  setElementDisabled(btnDisconnect, !connected);
  setElementDisabled(selectBaudrate, connected);

  // Activer/Désactiver les contrôles
  const disabledState = !connected;
  setElementDisabled(inputFreq, disabledState);
  setElementDisabled(selectSf, disabledState);
  setElementDisabled(selectBw, disabledState);
  setElementDisabled(selectCrc, disabledState);
  setElementDisabled(btnReadCfg, disabledState);
  setElementDisabled(btnWriteCfg, disabledState);
  setElementDisabled(btnSaveCfg, disabledState);
  setElementDisabled(btnResetCfg, disabledState);
  setElementDisabled(terminalInput, disabledState);
  setElementDisabled(btnSend, disabledState);
  setElementDisabled(btnListSd, disabledState);
}

// Convertit un tableau d'octets en chaîne hexadécimale continue
function bytesToHex(bytes) {
  return Array.from(bytes)
    .map(b => b.toString(16).toUpperCase().padStart(2, '0'))
    .join('');
}

// ============================================================================
// API Web Serial - Connexion et Lecture
// ============================================================================
async function connectSerial() {
  if ('serial' in navigator) {
    try {
      port = await navigator.serial.requestPort();
      const baud = selectBaudrate ? parseInt(selectBaudrate.value, 10) : 115200;
      
      logToTerminal(getTranslation('log_port_opening', { baud: baud }), 'sys-out');
      await port.open({ baudRate: baud });
      
      const portInfo = port.getInfo();
      const portName = `USB Vendor 0x${(portInfo.usbVendorId || 0).toString(16)} Product 0x${(portInfo.usbProductId || 0).toString(16)}`;
      
      updateConnectionUI(true, portName);
      logToTerminal(getTranslation('log_conn_success'), 'sys-out');

      // Démarrer la boucle de lecture
      readLoopPromise = readSerialLoop();

      // Envoyer la synchronisation temporelle et la demande de configuration initiale après le boot de la carte (6s) de manière séquentielle (évite les verrous)
      setTimeout(async () => {
        const currentEpoch = Math.floor(Date.now() / 1000);
        await sendSerialText(`AT+TIME=${currentEpoch}`);
        await sendSerialText('AT+FREQ?');
        await sendSerialText('AT+SF?');
        await sendSerialText('AT+BW?');
        await sendSerialText('AT+CRC?');
      }, 6000);

    } catch (err) {
      logToTerminal(getTranslation('log_conn_error', { message: err.message }), 'sys-out');
      console.error(err);
    }
  } else {
    alert(getTranslation('alert_browser_unsupported'));
  }
}

async function disconnectSerial() {
  if (!isConnected) return;
  isConnected = false;

  if (reader) {
    try {
      await reader.cancel();
    } catch (err) {}
  }
  
  // Attendre la fin propre de la boucle de lecture pour éviter les verrous
  if (readLoopPromise) {
    try {
      await readLoopPromise;
    } catch (err) {}
    readLoopPromise = null;
  }
  
  if (port) {
    try {
      await port.close();
    } catch (err) {
      console.error("Erreur de fermeture du port:", err);
    }
    port = null;
  }

  updateConnectionUI(false);
  logToTerminal(getTranslation('log_disconnected'), 'sys-out');
}

async function readSerialLoop() {
  try {
    while (port && port.readable && isConnected) {
      reader = port.readable.getReader();
      try {
        while (isConnected) {
          const { value, done } = await reader.read();
          if (done) {
            break;
          }
          if (value) {
            bytesCountThisSecond += value.length;
            // Ajouter les nouveaux octets au buffer
            for (let i = 0; i < value.length; i++) {
              rxBuffer.push(value[i]);
            }
            parseRxBuffer();
          }
        }
      } catch (err) {
        logToTerminal(getTranslation('log_read_error', { message: err.message }), 'sys-out');
        break;
      } finally {
        if (reader) {
          reader.releaseLock();
          reader = null;
        }
      }
    }
  } finally {
    if (isConnected) {
      setTimeout(() => disconnectSerial(), 0);
    }
  }
}

// Envoie du texte brut avec retour chariot (\n) de manière séquentielle sécurisée
async function sendSerialText(text) {
  if (!port || !port.writable) return;
  
  try {
    const encoder = new TextEncoder();
    const data = encoder.encode(text + '\n');
    
    const writer = port.writable.getWriter();
    await writer.write(data);
    writer.releaseLock();
    
    logToTerminal(text, 'cmd-in');
  } catch (err) {
    logToTerminal(getTranslation('log_send_error', { message: err.message }), 'sys-out');
    console.error("Erreur sendSerialText:", err);
  }
}

// ============================================================================
// Analyseur de Buffer (Décodeur de Trames et Textes)
// ============================================================================
function parseRxBuffer() {
  let processing = true;
  
  while (processing && rxBuffer.length > 0) {
    // Si c'est le début d'une trame binaire NectarMC (NECTAR_MAGIC = 0xEB)
    if (rxBuffer[0] === 0xEB) {
      if (rxBuffer.length < 4) {
        processing = false; // Attente d'octets supplémentaires
        break;
      }
      
      const payloadSize = rxBuffer[3]; // Taille brute des données de la payload LoRa
      const fwVersion = selectFwVersion ? selectFwVersion.value : '1.4.0';
      const totalFrameSize = (fwVersion === '1.3.1') ? (4 + payloadSize + 2 + 2 + 1) : (4 + payloadSize + 2 + 4 + 2 + 1);
      
      if (rxBuffer.length < totalFrameSize) {
        processing = false; // La trame n'est pas encore complète
        break;
      }
      
      // Extraction de la trame complète
      const frameBytes = rxBuffer.slice(0, totalFrameSize);
      rxBuffer = rxBuffer.slice(totalFrameSize);
      
      decodeNectarFrame(frameBytes);
    } 
    // Sinon c'est du texte brut (Boot logs, retours de commandes AT)
    else {
      // Trouver la fin de ligne
      const lfIndex = rxBuffer.indexOf(10); // Code ASCII pour '\n'
      const crIndex = rxBuffer.indexOf(13); // Code ASCII pour '\r'
      
      let splitIndex = -1;
      if (lfIndex !== -1 && crIndex !== -1) {
        splitIndex = Math.min(lfIndex, crIndex);
      } else {
        splitIndex = lfIndex !== -1 ? lfIndex : crIndex;
      }
      
      if (splitIndex !== -1) {
        const lineBytes = rxBuffer.slice(0, splitIndex);
        
        // Retirer la ligne décodée et le retour chariot du buffer
        let skipBytes = splitIndex + 1;
        // Si \r\n se suivent, on saute les deux
        if (rxBuffer[splitIndex] === 13 && rxBuffer[splitIndex + 1] === 10) {
          skipBytes = splitIndex + 2;
        }
        rxBuffer = rxBuffer.slice(skipBytes);
        
        const decoder = new TextDecoder();
        const lineText = decoder.decode(new Uint8Array(lineBytes)).trim();
        
        if (lineText.length > 0) {
          if (lineText === "+DUMP: START") {
            isDownloadingSdFile = true;
            sdDownloadBuffer = [];
            sdDownloadLinesCount = 0;
            return;
          }
          if (lineText === "+DUMP: END") {
            isDownloadingSdFile = false;
            finishSdFileDownload();
            return;
          }
          if (isDownloadingSdFile) {
            sdDownloadBuffer.push(lineText);
            sdDownloadLinesCount++;
            
            const approxBytes = sdDownloadLinesCount * 60;
            let percent = Math.min(99, Math.round((approxBytes / sdDownloadSize) * 100));
            if (isNaN(percent) || percent < 0) percent = 50;
            updateSdProgress(percent);
            return;
          }
          
          logToTerminal(lineText, 'cmd-out');
          parseATResponse(lineText);
        }
      } else {
        // Pas de fin de ligne trouvée.
        // Si le buffer de texte devient trop grand (ex: > 1024), on le vide pour éviter le débordement
        if (rxBuffer.length > 1024) {
          rxBuffer = [];
        }
        processing = false;
      }
    }
  }
}

// Rendu dynamique de la liste d'aide-mémoire AT
function renderAtHelperList() {
  const container = document.getElementById('at-helper-list');
  if (!container) return;
  container.innerHTML = '';
  
  const AT_COMMANDS_HELP = [
    { cmd: "AT", descKey: "at_desc_at" },
    { cmd: "AT+HELP", descKey: "at_desc_help" },
    { cmd: "AT+INFO", descKey: "at_desc_info" },
    { cmd: "AT+FREQ?", descKey: "at_desc_freq_get" },
    { cmd: "AT+FREQ=", descKey: "at_desc_freq_set" },
    { cmd: "AT+SF?", descKey: "at_desc_sf_get" },
    { cmd: "AT+SF=", descKey: "at_desc_sf_set" },
    { cmd: "AT+BW?", descKey: "at_desc_bw_get" },
    { cmd: "AT+BW=", descKey: "at_desc_bw_set" },
    { cmd: "AT+CRC?", descKey: "at_desc_crc_get" },
    { cmd: "AT+CRC=", descKey: "at_desc_crc_set" },
    { cmd: "AT+TIME?", descKey: "at_desc_time_get" },
    { cmd: "AT+TIME=", descKey: "at_desc_time_set" },
    { cmd: "AT+RSSI?", descKey: "at_desc_rssi_get" },
    { cmd: "AT+SNR?", descKey: "at_desc_snr_get" },
    { cmd: "AT+SIG?", descKey: "at_desc_sig_get" },
    { cmd: "AT+CFG", descKey: "at_desc_cfg" },
    { cmd: "AT+LIST", descKey: "at_desc_list" },
    { cmd: "AT+DUMP=", descKey: "at_desc_dump" },
    { cmd: "AT+SAVE", descKey: "at_desc_save" },
    { cmd: "AT+RESET", descKey: "at_desc_reset" }
  ];

  AT_COMMANDS_HELP.forEach(item => {
    const el = document.createElement('div');
    el.className = 'at-helper-item';
    el.innerHTML = `
      <span class="at-helper-cmd">${item.cmd}</span>
      <span class="at-helper-desc">${getTranslation(item.descKey)}</span>
    `;
    el.addEventListener('click', () => {
      const input = document.getElementById('terminal-input');
      if (input) {
        input.value = item.cmd;
        input.focus();
      }
    });
    container.appendChild(el);
  });
}

// Rendu dynamique du tableau de télémétrie (gère le changement de langue)
function renderTelemetryTable() {
  if (!tableTelemetryBody) return;
  
  // Vider le corps du tableau
  tableTelemetryBody.innerHTML = '';
  
  if (allReceivedFrames.length === 0) {
    if (rowEmpty) {
      rowEmpty.style.display = 'table-row';
      tableTelemetryBody.appendChild(rowEmpty);
    }
    return;
  }
  
  if (rowEmpty) {
    rowEmpty.style.display = 'none';
  }
  
  // Afficher les 50 dernières trames reçues (les plus récentes en premier)
  const framesToShow = allReceivedFrames.slice(-50).reverse();
  framesToShow.forEach(f => {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${f.index}</td>
      <td>${f.timestamp}</td>
      <td><span class="badge connected">${f.tracker}</span></td>
      <td>${f.apid}</td>
      <td>${f.size} ${getTranslation('unit_bytes')}</td>
      <td>${f.rssi} dBm</td>
      <td>${f.snr} dB</td>
      <td><span style="font-family: var(--font-mono); color: var(--color-success); font-weight: 600; white-space: nowrap;">✔ ${f.crcHex}</span></td>
      <td style="font-family: var(--font-mono); color: var(--color-cyan); word-break: break-all;">${f.payload}</td>
    `;
    tableTelemetryBody.appendChild(tr);
  });
}

// Calcule le CRC16-CCITT (polynôme 0x1021, valeur initiale 0xFFFF)
function calculateCRC16(data) {
  let crc = 0xFFFF;
  for (let i = 0; i < data.length; i++) {
    crc ^= (data[i] << 8);
    for (let j = 0; j < 8; j++) {
      if (crc & 0x8000) {
        crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
      } else {
        crc = (crc << 1) & 0xFFFF;
      }
    }
  }
  return crc;
}

// Décodage des trames NectarMC
function decodeNectarFrame(frame) {
  const payloadSize = frame[3]; // Taille brute de la payload LoRa
  const fwVersion = selectFwVersion ? selectFwVersion.value : '1.4.0';
  let epoch = 0;
  let crc = 0;
  let calculatedCrc = 0;

  if (fwVersion === '1.3.1') {
    crc = (frame[4 + payloadSize + 3] << 8) | frame[4 + payloadSize + 2];
    calculatedCrc = calculateCRC16(frame.slice(0, 4 + payloadSize + 2));
  } else {
    // Le Timestamp (Unix Epoch) est après le SNR (4 octets, uint32_t Little-Endian)
    const tsOffset = 4 + payloadSize + 2;
    epoch = (frame[tsOffset + 3] << 24 >>> 0) +
            (frame[tsOffset + 2] << 16) +
            (frame[tsOffset + 1] << 8) +
            frame[tsOffset];
    crc = (frame[4 + payloadSize + 7] << 8) | frame[4 + payloadSize + 6];
    calculatedCrc = calculateCRC16(frame.slice(0, 4 + payloadSize + 6));
  }

  // Vérification du CRC
  if (crc !== calculatedCrc) {
    crcErrorsCount++;
    if (statCrcErrors) {
      statCrcErrors.textContent = crcErrorsCount.toString();
    }
    const hexCrcRec = '0x' + crc.toString(16).toUpperCase().padStart(4, '0');
    const hexCrcCalc = '0x' + calculatedCrc.toString(16).toUpperCase().padStart(4, '0');
    logToTerminal(getTranslation('log_crc_error', { rec: hexCrcRec, calc: hexCrcCalc }), 'sys-out');
    return; // Rejeter la trame corrompue
  }

  packetIndex++;
  
  const idMission = (frame[2] << 8) | frame[1];
  const ssid = idMission >> 6;
  const apid = idMission & 0x3F;
  const ssidType = (ssid >> 8) & 0x03;
  const ssidNum = ssid & 0xFF;
  
  const payload = frame.slice(4, 4 + payloadSize); // Les données utiles LoRa brutes
  
  // RSSI et SNR sont après la payload (de taille payloadSize)
  const rawRssi = frame[4 + payloadSize];
  const rawSnr = frame[4 + payloadSize + 1];
  const rssi = rawRssi >= 128 ? rawRssi - 256 : rawRssi;
  
  // Le SNR est multiplié par 4 à l'envoi pour coder au 0.25 dB de précision
  const signedSnr = rawSnr >= 128 ? rawSnr - 256 : rawSnr;
  const snr = signedSnr / 4.0;

  // ... (ssidPrefix extraction code is identical) ...
  let ssidPrefix = 'OTHER';
  let missionTypeLabelKey = 'mission_other';
  if (ssidType === 0) {
    ssidPrefix = 'FX';
    missionTypeLabelKey = 'mission_rocket';
  } else if (ssidType === 1) {
    ssidPrefix = 'MF';
    missionTypeLabelKey = 'mission_minirocket';
  } else if (ssidType === 2) {
    ssidPrefix = 'BALLOON';
    missionTypeLabelKey = 'mission_balloon';
  }
  
  const trackerName = `${ssidPrefix}${ssidNum}`;
  
  // Formater l'horodatage à partir de l'Epoch reçu de l'ESP32
  let timestamp;
  if (epoch > 100000000) {
    timestamp = new Date(epoch * 1000).toLocaleTimeString();
  } else {
    // Fallback à l'heure du navigateur si la RTC n'a pas été initialisée (Epoch 0 ou valeur invalide)
    timestamp = new Date().toLocaleTimeString();
  }
  
  const crcHex = '0x' + crc.toString(16).toUpperCase().padStart(4, '0');

  // Ajouter à l'historique complet (capé à 5000 trames) avec la taille brute LoRa
  allReceivedFrames.push({
    index: packetIndex,
    timestamp: timestamp,
    tracker: trackerName,
    apid: apid,
    size: payloadSize, // Taille réelle des données utiles LoRa
    payload: bytesToHex(payload),
    rssi: rssi,
    snr: snr,
    crcHex: crcHex
  });
  if (allReceivedFrames.length > 5000) {
    allReceivedFrames.shift();
  }
  
  // Ajouter aux graphiques de signal en direct
  rssiHistory.push({ value: rssi, time: timestamp });
  if (rssiHistory.length > maxChartPoints) rssiHistory.shift();
  
  snrHistory.push({ value: snr, time: timestamp });
  if (snrHistory.length > maxChartPoints) snrHistory.shift();
  
  drawSignalCharts();
  
  renderTelemetryTable();

  // Mettre à jour la classification des trackers
  const isNew = !activeTrackers[trackerName];
  if (isNew) {
    activeTrackers[trackerName] = {
      name: trackerName,
      typeLabelKey: missionTypeLabelKey,
      lastApid: apid,
      packetCount: 0,
      lastSeen: Date.now(),
      lastPayloadHex: bytesToHex(payload),
      lastRssi: rssi,
      lastSnr: snr,
      isLost: false
    };
    speak(getTranslation('voice_new_tracker', { name: trackerName.split('').join(' ') }));
  } else {
    if (activeTrackers[trackerName].isLost) {
      activeTrackers[trackerName].isLost = false;
      speak(getTranslation('voice_tracker_back', { name: trackerName.split('').join(' ') }));
    }
  }
  
  activeTrackers[trackerName].lastApid = apid;
  activeTrackers[trackerName].packetCount++;
  activeTrackers[trackerName].lastSeen = Date.now();
  activeTrackers[trackerName].lastPayloadHex = bytesToHex(payload);
  activeTrackers[trackerName].lastRssi = rssi;
  activeTrackers[trackerName].lastSnr = snr;
  
  updateTrackersTable();

  // Décodage optionnel de la charge utile WASP (29 octets + 3 octets de header NectarMC = 32 octets au total)
  const chkWaspDecoding = document.getElementById('chk-wasp-decoding');
  if (chkWaspDecoding && chkWaspDecoding.checked && payloadSize === 29) {
    try {
      const buffer = new ArrayBuffer(29);
      const view = new DataView(buffer);
      for (let i = 0; i < 29; i++) {
        view.setUint8(i, payload[i]);
      }
      
      // Les 3 octets de header d'identification (id, apid, type) sont déjà lus dans le header NectarMC
      const waspId = ssidNum;
      const waspApid = apid;
      const waspType = ssidType;
      
      // Lecture des 29 octets restants
      const waspUtc = view.getUint32(0, true);
      const waspLat = view.getFloat32(4, true);
      const waspLon = view.getFloat32(8, true);
      const waspAlt = view.getFloat32(12, true);
      const waspSpd = view.getFloat32(16, true);
      const waspCog = view.getFloat32(20, true);
      const waspVbat = view.getUint16(24, true);
      const waspTemp = view.getInt16(26, true);
      const waspStatus = view.getUint8(28);
      
      const gpsFix = (waspStatus & 0x80) !== 0;
      const numSats = waspStatus & 0x1F;
      
      // Mettre à jour les widgets
      const txtAlt = document.getElementById('wasp-alt');
      const txtSpd = document.getElementById('wasp-spd');
      const txtSats = document.getElementById('wasp-sats');
      const txtTemp = document.getElementById('wasp-temp');
      const txtVbat = document.getElementById('wasp-vbat');
      const txtSignal = document.getElementById('wasp-signal');
      
      if (txtAlt) txtAlt.textContent = waspAlt.toFixed(1) + ' m';
      if (txtSpd) txtSpd.textContent = waspSpd.toFixed(1) + ' km/h';
      if (txtSats) txtSats.textContent = (gpsFix ? '🟢 ' : '🔴 ') + numSats;
      if (txtTemp) txtTemp.textContent = (waspTemp / 100).toFixed(2) + ' °C';
      if (txtVbat) txtVbat.textContent = (waspVbat / 1000).toFixed(2) + ' V';
      if (txtSignal) txtSignal.textContent = `${rssi} / ${snr}`;
      
      // Si la position GPS est valide, on l'affiche sur la carte Leaflet
      if (waspLat !== 0 && waspLon !== 0 && Math.abs(waspLat) <= 90 && Math.abs(waspLon) <= 180) {
        waspLastPos = { lat: waspLat, lon: waspLon };
        if (waspMap && waspMarker) {
          const pos = [waspLat, waspLon];
          waspMarker.setLatLng(pos);
          
          const timeStr = waspUtc > 0 ? new Date(waspUtc * 1000).toLocaleTimeString() : 'Inconnue';
          
          waspMarker.setPopupContent(`
            <b>Tracker WASP [ID: ${waspId}]</b><br>
            Altitude: ${waspAlt.toFixed(1)} m<br>
            Vitesse: ${waspSpd.toFixed(1)} km/h<br>
            Cap (COG): ${waspCog.toFixed(1)}°<br>
            GPS Fix: ${gpsFix ? 'Fix valide' : 'Pas de fix'}<br>
            Heure GPS: ${timeStr}
          `);
          
          // Suivre la position sur la carte
          waspMap.setView(pos, waspMap.getZoom() < 10 ? 14 : waspMap.getZoom());
        }
      }
    } catch (e) {
      console.error("Erreur de décodage de la charge utile WASP:", e);
    }
  }

  // Mettre à jour les indicateurs
  if (statCount) {
    statCount.textContent = packetIndex;
  }
  if (statRssi) {
    statRssi.textContent = `${rssi}`;
  }
  if (statSnr) {
    statSnr.textContent = `${snr}`;
  }
}

// Met à jour la table des trackers actifs
function updateTrackersTable() {
  const tableBody = document.querySelector('#table-trackers tbody');
  const rowEmptyTrackers = document.getElementById('row-empty-trackers');
  if (!tableBody) return;

  const names = Object.keys(activeTrackers).sort();
  
  if (names.length > 0 && rowEmptyTrackers) {
    rowEmptyTrackers.style.display = 'none';
  }

  const now = Date.now();

  names.forEach(name => {
    const tracker = activeTrackers[name];
    let row = document.getElementById(`tracker-row-${name}`);
    
    // Si pas de signal depuis 15 secondes, le tracker est marqué PERDU
    const isLost = (now - tracker.lastSeen) > 15000;
    
    if (isLost && !tracker.isLost) {
      tracker.isLost = true;
      speak(getTranslation('voice_tracker_lost', { name: name.split('').join(' ') }));
    }
    
    const statusText = isLost ? getTranslation('status_lost') : getTranslation('status_active');
    const statusClass = isLost ? 'badge disconnected' : 'badge connected';
    
    const timeString = new Date(tracker.lastSeen).toLocaleTimeString();

    if (!row) {
      row = document.createElement('tr');
      row.id = `tracker-row-${name}`;
      tableBody.appendChild(row);
    }
    
    row.innerHTML = `
      <td><span class="badge connected">${name}</span></td>
      <td>${getTranslation(tracker.typeLabelKey)}</td>
      <td>${tracker.lastApid}</td>
      <td>${tracker.packetCount}</td>
      <td>${timeString}</td>
      <td><span class="${statusClass}">${statusText}</span></td>
      <td style="font-family: var(--font-mono); font-weight: bold; color: var(--color-cyan);">${tracker.lastRssi} dBm / ${tracker.lastSnr} dB</td>
    `;
  });
}

// Vérifie si des trackers sont hors ligne
function checkTrackersTimeout() {
  if (Object.keys(activeTrackers).length === 0) return;
  updateTrackersTable();
}

// Analyse des réponses textuelles AT
function parseATResponse(line) {
  if (line.startsWith('+LIST:')) {
    const val = line.substring(6).trim();
    const parts = val.split(',');
    if (parts.length === 2) {
      const filename = parts[0];
      const size = parseInt(parts[1], 10);
      addSdFileToList(filename, size);
    }
    return;
  }

  // Ligne de boot : "[CONFIG] Loaded from NVS: Freq=869.525 MHz, SF=8, BW=250.0 kHz, CRC=ON (Mode=CCITT)"
  if (line.includes('Loaded from NVS:')) {
    const match = line.match(/Freq=([\d.]+)\s*MHz,\s*SF=(\d+),\s*BW=([\d.]+)\s*kHz,\s*CRC=(ON|OFF)(?:\s*\(Mode=(CCITT|IBM)\))?/i);
    if (match) {
      const freq = parseFloat(match[1]);
      const sf = parseInt(match[2], 10);
      const bw = parseFloat(match[3]);
      const crcEnabled = match[4].toUpperCase() === 'ON';
      const crcMode = match[5] ? match[5].toUpperCase() : 'CCITT';
      
      currentConfig.frequency = freq;
      currentConfig.sf = sf;
      currentConfig.bw = bw;
      
      if (inputFreq) inputFreq.value = freq.toFixed(3);
      if (selectSf) selectSf.value = sf.toString();
      if (selectBw) selectBw.value = bw.toString();
      if (selectCrc) {
        if (!crcEnabled) selectCrc.value = "0";
        else if (crcMode === 'IBM') selectCrc.value = "1,1";
        else selectCrc.value = "1,0";
      }
      return;
    }
  }

  // Fréquence : "+FREQ: <valeur>" ou rapport "Frequency (Active): <valeur> MHz"
  if (line.startsWith('+FREQ:')) {
    const val = parseFloat(line.split(':')[1]);
    currentConfig.frequency = val;
    if (inputFreq) inputFreq.value = val.toFixed(3);
  } else if (line.includes('Frequency (Active):')) {
    const val = parseFloat(line.split(':')[1]);
    currentConfig.frequency = val;
    if (inputFreq) inputFreq.value = val.toFixed(3);
  }
  // Spreading Factor : "+SF: <valeur>" ou rapport "Spreading Factor  : <valeur>"
  else if (line.startsWith('+SF:')) {
    const val = parseInt(line.split(':')[1], 10);
    currentConfig.sf = val;
    if (selectSf) selectSf.value = val.toString();
  } else if (line.includes('Spreading Factor')) {
    const val = parseInt(line.split(':')[1], 10);
    currentConfig.sf = val;
    if (selectSf) selectSf.value = val.toString();
  }
  // Bande Passante : "+BW: <valeur>" ou rapport "Bandwidth         : <valeur> kHz"
  else if (line.startsWith('+BW:')) {
    const val = parseFloat(line.split(':')[1]);
    currentConfig.bw = val;
    if (selectBw) {
      selectBw.value = val.toString();
    }
  } else if (line.includes('Bandwidth')) {
    const val = parseFloat(line.split(':')[1]);
    currentConfig.bw = val;
    if (selectBw) {
      selectBw.value = val.toString();
    }
  }
  // CRC : "+CRC: <valeur>" ou rapport "Hardware CRC      : <valeur>"
  else if (line.startsWith('+CRC:')) {
    const val = line.split(':')[1].trim();
    if (selectCrc) {
      if (val.startsWith('0')) {
        selectCrc.value = "0";
      } else if (val === '1,1') {
        selectCrc.value = "1,1";
      } else {
        selectCrc.value = "1,0";
      }
    }
  } else if (line.includes('Hardware CRC')) {
    const val = line.split(':')[1].trim();
    if (selectCrc) {
      if (val.includes('OFF')) {
        selectCrc.value = "0";
      } else if (val.includes('IBM')) {
        selectCrc.value = "1,1";
      } else {
        selectCrc.value = "1,0";
      }
    }
  }
}

// ============================================================================
// Dessin du graphique temps réel (SVG)
// ============================================================================
function updateThroughputChart() {
  const now = Date.now();
  const elapsed = now - lastThroughputCalculation;
  
  if (elapsed >= 1000) {
    const dataRate = Math.round((bytesCountThisSecond * 1000) / elapsed);
    bytesCountThisSecond = 0;
    lastThroughputCalculation = now;
    
    // Mettre à jour l'indicateur de débit
    if (lblThroughput) {
      lblThroughput.textContent = `${dataRate} B/s`;
    }
    
    // Vérifier les timeouts des trackers
    checkTrackersTimeout();
  }
}

function drawSignalCharts() {
  drawSingleChart('rssi-chart-line', 'rssi-chart-fill', rssiHistory, -120, 0);
  drawSingleChart('snr-chart-line', 'snr-chart-fill', snrHistory, -20, 20);
  
  // Mettre à jour les indicateurs de temps
  const lblRssiTime = document.getElementById('chart-rssi-time');
  if (lblRssiTime) {
    if (rssiHistory.length > 0) {
      lblRssiTime.textContent = rssiHistory.length === 1 
        ? rssiHistory[0].time 
        : `${rssiHistory[0].time} ➔ ${rssiHistory[rssiHistory.length - 1].time}`;
    } else {
      lblRssiTime.textContent = '--:--:--';
    }
  }

  const lblSnrTime = document.getElementById('chart-snr-time');
  if (lblSnrTime) {
    if (snrHistory.length > 0) {
      lblSnrTime.textContent = snrHistory.length === 1 
        ? snrHistory[0].time 
        : `${snrHistory[0].time} ➔ ${snrHistory[snrHistory.length - 1].time}`;
    } else {
      lblSnrTime.textContent = '--:--:--';
    }
  }
}

function drawSingleChart(lineId, fillId, history, minVal, maxVal) {
  const chartLine = document.getElementById(lineId);
  const chartFill = document.getElementById(fillId);
  if (!chartLine || !chartFill) return;
  
  const width = 300;
  const height = 100;
  const pointsCount = history.length;
  
  if (pointsCount === 0) {
    chartLine.setAttribute('d', '');
    chartFill.setAttribute('d', 'M 0 100 L 300 100 Z');
    return;
  }
  
  let dLine = '';
  
  for (let i = 0; i < pointsCount; i++) {
    const x = (i / (maxChartPoints - 1)) * width;
    
    const val = history[i].value;
    const clampedVal = Math.max(minVal, Math.min(maxVal, val));
    const y = height - ((clampedVal - minVal) / (maxVal - minVal)) * (height - 10) - 5;
    
    if (i === 0) {
      dLine += `M ${x} ${y}`;
    } else {
      dLine += ` L ${x} ${y}`;
    }
  }
  
  const firstX = 0;
  const lastX = ((pointsCount - 1) / (maxChartPoints - 1)) * width;
  const dFill = `${dLine} L ${lastX} ${height} L ${firstX} ${height} Z`;
  
  chartLine.setAttribute('d', dLine);
  chartFill.setAttribute('d', dFill);
}

// Démarrer le ticker de statistiques chaque seconde
setInterval(updateThroughputChart, 1000);

// ============================================================================
// Événements des boutons de Configuration
// ============================================================================
if (btnReadCfg) {
  btnReadCfg.addEventListener('click', async () => {
    const currentEpoch = Math.floor(Date.now() / 1000);
    await sendSerialText(`AT+TIME=${currentEpoch}`);
    await sendSerialText('AT+FREQ?');
    await sendSerialText('AT+SF?');
    await sendSerialText('AT+BW?');
    await sendSerialText('AT+CRC?');
  });
}

if (btnWriteCfg) {
  btnWriteCfg.addEventListener('click', async () => {
    const freq = inputFreq ? parseFloat(inputFreq.value) : NaN;
    const sf = selectSf ? parseInt(selectSf.value, 10) : NaN;
    const bw = selectBw ? parseFloat(selectBw.value) : NaN;
    const crcVal = selectCrc ? selectCrc.value : null;
    
    if (!isNaN(freq)) await sendSerialText(`AT+FREQ=${freq.toFixed(3)}`);
    if (!isNaN(sf)) await sendSerialText(`AT+SF=${sf}`);
    if (!isNaN(bw)) await sendSerialText(`AT+BW=${bw.toFixed(1)}`);
    if (crcVal) await sendSerialText(`AT+CRC=${crcVal}`);
  });
}

if (btnSaveCfg) {
  btnSaveCfg.addEventListener('click', () => {
    sendSerialText('AT+SAVE');
  });
}

if (btnResetCfg) {
  btnResetCfg.addEventListener('click', () => {
    if (confirm("Voulez-vous restaurer les paramètres d'usine ? La carte va redémarrer.")) {
      sendSerialText('AT+RESET');
    }
  });
}

if (terminalForm) {
  terminalForm.addEventListener('submit', (e) => {
    e.preventDefault();
    if (terminalInput) {
      const cmd = terminalInput.value.trim();
      if (cmd) {
        sendSerialText(cmd);
        terminalInput.value = '';
      }
    }
  });
}

// Exporte les trames enregistrées en CSV
function exportTelemetryToCSV() {
  if (allReceivedFrames.length === 0) {
    alert(getTranslation('alert_no_frames_export'));
    return;
  }
  
  let csvRows = ["Index,Horodatage,Tracker,APID,Taille(octets),RSSI(dBm),SNR(dB),ChargeUtileHex"];
  allReceivedFrames.forEach(f => {
    csvRows.push(`${f.index},${f.timestamp},${f.tracker},${f.apid},${f.size},${f.rssi},${f.snr},${f.payload}`);
  });
  
  const csvString = csvRows.join("\n");
  const blob = new Blob([csvString], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  
  const link = document.createElement("a");
  link.setAttribute("href", url);
  
  const dateStr = new Date().toISOString().slice(0, 10);
  const timeStr = new Date().toTimeString().slice(0, 8).replace(/:/g, '-');
  link.setAttribute("download", `nectar_telemetry_${dateStr}_${timeStr}.csv`);
  
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
}

// ============================================================================
// Flasheur de Firmware Web (ESPTool)
// ============================================================================
async function flashFirmware() {
  const band = selectBand ? selectBand.value : '868';
  const binUrl = `binaries/firmware_bluetooth_${band}.bin`;
  
  if (isConnected) {
    alert(getTranslation('alert_monitor_active_disconnect'));
    return;
  }
  
  setElementDisabled(btnFlash, true);
  if (flashProgressContainer) flashProgressContainer.classList.remove('hidden');
  if (lblFlashStatus) lblFlashStatus.textContent = getTranslation('flash_status_connecting');
  if (lblFlashPercent) lblFlashPercent.textContent = "0%";
  if (flashProgressBar) flashProgressBar.style.width = "0%";
  
  let esploader = null;
  let transport = null;
  
  const customTerminal = {
    clean() {
      if (terminalLogs) terminalLogs.innerHTML = '';
    },
    writeLine(data) {
      logToTerminal(data, 'sys-out');
    },
    write(data) {
      logToTerminal(data, 'sys-out');
    }
  };

  try {
    logToTerminal(getTranslation('log_flash_port_select'), "sys-out");
    const flashPort = await navigator.serial.requestPort();
    
    transport = new Transport(flashPort, true);
    
    esploader = new ESPLoader({
      transport: transport,
      terminal: customTerminal,
      baudrate: 115200 // vitesse de synchronisation bootloader
    });
    
    if (lblFlashStatus) lblFlashStatus.textContent = getTranslation('flash_status_syncing');
    await esploader.main();
    
    if (lblFlashStatus) lblFlashStatus.textContent = getTranslation('flash_status_chip', { chip: esploader.chipName });
    logToTerminal(getTranslation('log_download_bin', { url: binUrl }), "sys-out");
    
    const response = await fetch(binUrl);
    if (!response.ok) {
      throw new Error(getTranslation('log_download_bin_failed', { status: response.statusText }) || `Impossible de récupérer le binaire (${response.statusText})`);
    }
    
    const arrayBuffer = await response.arrayBuffer();
    const firmwareData = new Uint8Array(arrayBuffer);
    
    if (lblFlashStatus) lblFlashStatus.textContent = getTranslation('flash_status_writing');
    logToTerminal(getTranslation('log_write_flash_start'), "sys-out");
    
    const fileArray = [
      { data: firmwareData, address: 0x10000 }
    ];
    
    await esploader.writeFlash({
      fileArray: fileArray,
      flashSize: 'keep',
      flashMode: 'keep',
      flashFreq: 'keep',
      eraseAll: false,
      compress: true,
      reportProgress: (fileIndex, written, total) => {
        const percent = Math.round((written / total) * 100);
        if (lblFlashPercent) lblFlashPercent.textContent = `${percent}%`;
        if (flashProgressBar) flashProgressBar.style.width = `${percent}%`;
      }
    });
    
    if (lblFlashStatus) lblFlashStatus.textContent = getTranslation('flash_status_success');
    logToTerminal(getTranslation('log_update_complete_reboot'), "sys-out");
    
    // Redémarrer la carte matériellement
    await transport.setDTR(false);
    await new Promise(resolve => setTimeout(resolve, 100));
    await transport.setDTR(true);
    
  } catch (err) {
    if (lblFlashStatus) lblFlashStatus.textContent = getTranslation('flash_status_failed');
    logToTerminal(getTranslation('log_flash_error', { message: err.message }), 'sys-out');
    console.error(err);
  } finally {
    if (transport) {
      try {
        await transport.disconnect();
      } catch (err) {}
    }
    setElementDisabled(btnFlash, false);
  }
}

// ============================================================================
// Événements d'Initialisation
// ============================================================================
if (btnConnect) btnConnect.addEventListener('click', connectSerial);
if (btnDisconnect) btnDisconnect.addEventListener('click', disconnectSerial);
if (btnFlash) btnFlash.addEventListener('click', flashFirmware);

// Événements de la carte SD
if (btnListSd) {
  btnListSd.addEventListener('click', async () => {
    // Vider la liste précédente
    const tableBody = document.querySelector('#table-sd-files tbody');
    if (tableBody) {
      tableBody.innerHTML = '';
      const rowEmpty = document.createElement('tr');
      rowEmpty.id = 'row-empty-sd-files';
      rowEmpty.innerHTML = `<td colspan="3" class="text-center text-secondary" style="padding: 1rem 0; text-align: center;">${getTranslation('sd_files_empty')}</td>`;
      tableBody.appendChild(rowEmpty);
    }
    await sendSerialText('AT+LIST');
  });
}

function addSdFileToList(filename, size) {
  const tableBody = document.querySelector('#table-sd-files tbody');
  const rowEmpty = document.getElementById('row-empty-sd-files');
  if (rowEmpty) {
    rowEmpty.style.display = 'none';
  }
  
  const rowId = `sd-file-row-${filename.replace(/\//g, '_').replace(/\./g, '_')}`;
  let row = document.getElementById(rowId);
  if (!row) {
    row = document.createElement('tr');
    row.id = rowId;
    row.style.borderBottom = '1px solid rgba(255, 255, 255, 0.05)';
    tableBody.appendChild(row);
  }
  
  let sizeStr = `${size} B`;
  if (size > 1024 * 1024) {
    sizeStr = `${(size / (1024 * 1024)).toFixed(2)} MB`;
  } else if (size > 1024) {
    sizeStr = `${(size / 1024).toFixed(1)} KB`;
  }
  
  const cleanName = filename.startsWith('/') ? filename.substring(1) : filename;
  
  row.innerHTML = `
    <td style="padding: 0.5rem; font-family: var(--font-mono);">${cleanName}</td>
    <td style="padding: 0.5rem;">${sizeStr}</td>
    <td style="padding: 0.5rem; text-align: right;">
      <button class="btn btn-accent btn-sm btn-download-sd" data-filename="${filename}" style="padding: 0.2rem 0.5rem; font-size: 0.75rem; border-radius: 4px;">
        ${getTranslation('sd_download_btn')}
      </button>
    </td>
  `;
  
  const btn = row.querySelector('.btn-download-sd');
  btn.addEventListener('click', () => {
    startSdFileDownload(filename, size);
  });
}

async function startSdFileDownload(filename, size) {
  if (isDownloadingSdFile) return;
  
  isDownloadingSdFile = true;
  sdDownloadFilename = filename;
  sdDownloadSize = size;
  sdDownloadBuffer = [];
  sdDownloadLinesCount = 0;
  
  const container = document.getElementById('sd-download-progress-container');
  if (container) container.classList.remove('hidden');
  updateSdProgress(0);
  
  logToTerminal(getTranslation('log_sd_download_start', { file: filename }), 'sys-out');
  await sendSerialText(`AT+DUMP=${filename}`);
}

function finishSdFileDownload() {
  updateSdProgress(100);
  logToTerminal(getTranslation('log_sd_download_success', { file: sdDownloadFilename, lines: sdDownloadLinesCount }), 'sys-out');
  
  setTimeout(() => {
    const container = document.getElementById('sd-download-progress-container');
    if (container) container.classList.add('hidden');
  }, 2000);
  
  const csvContent = sdDownloadBuffer.join('\n');
  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  
  const link = document.createElement("a");
  link.setAttribute("href", url);
  
  const cleanName = sdDownloadFilename.startsWith('/') ? sdDownloadFilename.substring(1) : sdDownloadFilename;
  link.setAttribute("download", `downloaded_${cleanName}`);
  
  document.body.appendChild(link);
  link.click();
  document.body.removeChild(link);
  URL.revokeObjectURL(url);
  
  isDownloadingSdFile = false;
}

function updateSdProgress(percent) {
  const bar = document.getElementById('sd-download-progress-bar');
  const label = document.getElementById('lbl-sd-download-status');
  if (bar) bar.style.width = `${percent}%`;
  if (label) label.textContent = `${percent}%`;
}

// Nettoyage console
if (btnClearTerminal) {
  btnClearTerminal.addEventListener('click', () => {
    if (terminalLogs) terminalLogs.innerHTML = '';
  });
}

// Nettoyage télémétrie
if (btnClearTelemetry) {
  btnClearTelemetry.addEventListener('click', () => {
    packetIndex = 0;
    crcErrorsCount = 0;
    allReceivedFrames = [];
    if (statCount) statCount.textContent = '0';
    if (statCrcErrors) statCrcErrors.textContent = '0';
    if (tableTelemetryBody) {
      tableTelemetryBody.innerHTML = '';
      if (rowEmpty) {
        rowEmpty.style.display = 'table-row';
        tableTelemetryBody.appendChild(rowEmpty);
      }
    }
    rssiHistory.length = 0;
    snrHistory.length = 0;
    drawSignalCharts();
  });
}

// Export télémétrie CSV
if (btnExportTelemetry) {
  btnExportTelemetry.addEventListener('click', exportTelemetryToCSV);
}

// Réinitialisation des trackers
if (btnClearTrackers) {
  btnClearTrackers.addEventListener('click', () => {
    activeTrackers = {};
    const tableBody = document.querySelector('#table-trackers tbody');
    const rowEmptyTrackers = document.getElementById('row-empty-trackers');
    if (tableBody) {
      tableBody.innerHTML = '';
      if (rowEmptyTrackers) {
        rowEmptyTrackers.style.display = 'table-row';
        tableBody.appendChild(rowEmptyTrackers);
      }
    }
  });
}

// Détecter si le port a été déconnecté matériellement (câble arraché)
navigator.serial?.addEventListener('disconnect', (event) => {
  if (port && event.target === port) {
    logToTerminal(getTranslation('log_physical_disconnect'), "sys-out");
    disconnectSerial();
  }
});

// Événements du sélecteur de langue
const btnLangFr = document.getElementById('btn-lang-fr');
const btnLangEn = document.getElementById('btn-lang-en');
if (btnLangFr) btnLangFr.addEventListener('click', () => setLanguage('fr'));
if (btnLangEn) btnLangEn.addEventListener('click', () => setLanguage('en'));

// Initialisation de la carte Leaflet WASP
function initWaspMap() {
  if (waspMap) return; // Déjà initialisée
  
  // Coordonnées par défaut au centre de la France
  const defaultLat = 46.2276;
  const defaultLon = 2.2137;
  
  waspMap = L.map('wasp-map').setView([defaultLat, defaultLon], 5);
  
  // Utilisation de la tuile Dark Matter de CartoDB pour correspondre au design sombre
  L.tileLayer('https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png', {
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors &copy; <a href="https://carto.com/attributions">CARTO</a>',
    maxZoom: 20
  }).addTo(waspMap);
  
  // Création du marqueur avec une popup descriptive
  waspMarker = L.marker([defaultLat, defaultLon]).addTo(waspMap);
  waspMarker.bindPopup("<b>Position Wasp</b><br>Attente de données GPS...").openPopup();
}

// Écouteur d'état pour le mode décodeur WASP
const chkWaspDecoding = document.getElementById('chk-wasp-decoding');
const waspSection = document.getElementById('wasp-section');

if (chkWaspDecoding) {
  chkWaspDecoding.addEventListener('change', () => {
    if (chkWaspDecoding.checked) {
      if (waspSection) {
        waspSection.classList.remove('hidden');
        // Initialise la carte si ce n'est pas déjà fait
        initWaspMap();
        // Permet à Leaflet de recalculer sa taille après que le conteneur soit affiché
        setTimeout(() => {
          if (waspMap) {
            waspMap.invalidateSize();
            // Centrer sur la dernière position si elle existe
            if (waspLastPos) {
              waspMap.setView([waspLastPos.lat, waspLastPos.lon], 13);
            }
          }
        }, 150);
        
        // Scroll fluide vers la section Wasp
        waspSection.scrollIntoView({ behavior: 'smooth' });
      }
    } else {
      if (waspSection) {
        waspSection.classList.add('hidden');
      }
    }
  });
}

// Initialisation de la langue au chargement de la page
const savedLang = localStorage.getItem('nectar_lang');
if (savedLang) {
  setLanguage(savedLang);
} else {
  const browserLang = navigator.language || navigator.userLanguage;
  if (browserLang && browserLang.startsWith('en')) {
    setLanguage('en');
  } else {
    setLanguage('fr');
  }
}
