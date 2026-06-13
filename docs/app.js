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
let readLoopPromise = null; // Promesse pour suivre la fin de la boucle de lecture
let activeTrackers = {};    // Dictionnaire des émetteurs détectés : { name: { typeLabel, lastApid, packetCount, lastSeen, lastPayloadHex } }
let allReceivedFrames = []; // Historique complet pour export CSV (capé à 5000 trames)

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

// ============================================================================
// Sélection des éléments du DOM
// ============================================================================
const btnConnect = document.getElementById('btn-connect');
const btnDisconnect = document.getElementById('btn-disconnect');
const lblPortName = document.getElementById('lbl-port-name');
const connBadge = document.getElementById('conn-badge');
const selectBaudrate = document.getElementById('baudrate');

// Inputs Configuration (support des anciens ID 'input-*' si index.html est en cache)
const inputFreq = document.getElementById('input-freq');
const selectSf = document.getElementById('select-sf') || document.getElementById('input-sf');
const selectBw = document.getElementById('select-bw') || document.getElementById('input-bw');

// Boutons Configuration
const btnReadCfg = document.getElementById('btn-read-cfg');
const btnWriteCfg = document.getElementById('btn-write-cfg');
const btnSaveCfg = document.getElementById('btn-save-cfg');
const btnResetCfg = document.getElementById('btn-reset-cfg');

// Stats
const statRssi = document.getElementById('stat-rssi');
const statSnr = document.getElementById('stat-snr');
const statCount = document.getElementById('stat-count');

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
    utterance.lang = 'fr-FR';
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
  if (connBadge) {
    connBadge.textContent = connected ? 'Connecté' : 'Déconnecté';
    connBadge.className = connected ? 'badge connected' : 'badge disconnected';
  }
  if (lblPortName) {
    lblPortName.textContent = connected ? `Port : ${name}` : 'Aucun appareil connecté';
  }
  
  setElementDisabled(btnConnect, connected);
  setElementDisabled(btnDisconnect, !connected);
  setElementDisabled(selectBaudrate, connected);

  // Activer/Désactiver les contrôles
  const disabledState = !connected;
  setElementDisabled(inputFreq, disabledState);
  setElementDisabled(selectSf, disabledState);
  setElementDisabled(selectBw, disabledState);
  setElementDisabled(btnReadCfg, disabledState);
  setElementDisabled(btnWriteCfg, disabledState);
  setElementDisabled(btnSaveCfg, disabledState);
  setElementDisabled(btnResetCfg, disabledState);
  setElementDisabled(terminalInput, disabledState);
  setElementDisabled(btnSend, disabledState);
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
      
      logToTerminal(`Ouverture du port série à ${baud} baud...`, 'sys-out');
      await port.open({ baudRate: baud });
      
      const portInfo = port.getInfo();
      const portName = `USB Vendor 0x${(portInfo.usbVendorId || 0).toString(16)} Product 0x${(portInfo.usbProductId || 0).toString(16)}`;
      
      updateConnectionUI(true, portName);
      logToTerminal('Connexion établie avec succès.', 'sys-out');

      // Démarrer la boucle de lecture
      readLoopPromise = readSerialLoop();

      // Envoyer une demande de configuration initiale après le boot de la carte (6s) de manière séquentielle (évite les verrous)
      setTimeout(async () => {
        await sendSerialText('AT+FREQ?');
        await sendSerialText('AT+SF?');
        await sendSerialText('AT+BW?');
      }, 6000);

    } catch (err) {
      logToTerminal(`Erreur de connexion : ${err.message}`, 'sys-out');
      console.error(err);
    }
  } else {
    alert("Votre navigateur ne supporte pas l'API Web Serial. Veuillez utiliser Google Chrome, Microsoft Edge ou Opera.");
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
  logToTerminal('Liaison série déconnectée.', 'sys-out');
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
        logToTerminal(`Erreur de lecture : ${err.message}`, 'sys-out');
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
    logToTerminal(`Erreur d'envoi : ${err.message}`, 'sys-out');
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
      
      const payloadSize = rxBuffer[3];
      const totalFrameSize = 4 + payloadSize + 2 + 1; // Header (4) + Payload + CRC (2) + \n (1)
      
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

// Décodage des trames NectarMC
function decodeNectarFrame(frame) {
  packetIndex++;
  
  const idMission = (frame[2] << 8) | frame[1];
  const ssid = idMission >> 6;
  const apid = idMission & 0x3F;
  const ssidType = (ssid >> 8) & 0x03;
  const ssidNum = ssid & 0xFF;
  
  const payloadSize = frame[3];
  const payload = frame.slice(4, 4 + payloadSize);
  const crc = (frame[4 + payloadSize + 1] << 8) | frame[4 + payloadSize];
  
  let ssidPrefix = 'OTHER';
  let missionTypeLabel = 'Autre (OTHER)';
  if (ssidType === 0) {
    ssidPrefix = 'FX';
    missionTypeLabel = 'Fusée (FX)';
  } else if (ssidType === 1) {
    ssidPrefix = 'MF';
    missionTypeLabel = 'Minifusée (MF)';
  } else if (ssidType === 2) {
    ssidPrefix = 'BALLOON';
    missionTypeLabel = 'Ballon (BALLOON)';
  }
  
  const trackerName = `${ssidPrefix}${ssidNum}`;
  const timestamp = new Date().toLocaleTimeString();
  
  // Ajouter à l'historique complet (capé à 5000 trames)
  allReceivedFrames.push({
    index: packetIndex,
    timestamp: timestamp,
    tracker: trackerName,
    apid: apid,
    size: payloadSize,
    payload: bytesToHex(payload)
  });
  if (allReceivedFrames.length > 5000) {
    allReceivedFrames.shift();
  }
  
  // Ajouter au tableau de télémétrie
  if (rowEmpty) {
    rowEmpty.style.display = 'none';
  }
  
  if (tableTelemetryBody) {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${packetIndex}</td>
      <td>${timestamp}</td>
      <td><span class="badge connected">${trackerName}</span></td>
      <td>${apid}</td>
      <td>${payloadSize} octets</td>
      <td>-- <span class="text-secondary">(OLED)</span></td>
      <td>-- <span class="text-secondary">(OLED)</span></td>
      <td>${bytesToHex(payload)}</td>
    `;
    
    // Insérer en haut de la table (trames les plus récentes en premier)
    tableTelemetryBody.insertBefore(tr, tableTelemetryBody.firstChild);
    
    // Limiter le nombre de lignes à 50 pour la performance DOM
    if (tableTelemetryBody.children.length > 50) {
      tableTelemetryBody.removeChild(tableTelemetryBody.lastChild);
    }
  }

  // Mettre à jour la classification des trackers
  const isNew = !activeTrackers[trackerName];
  if (isNew) {
    activeTrackers[trackerName] = {
      name: trackerName,
      typeLabel: missionTypeLabel,
      lastApid: apid,
      packetCount: 0,
      lastSeen: Date.now(),
      lastPayloadHex: bytesToHex(payload),
      isLost: false
    };
    speak(`Nouveau tracker détecté, ${trackerName.split('').join(' ')}`);
  } else {
    if (activeTrackers[trackerName].isLost) {
      activeTrackers[trackerName].isLost = false;
      speak(`Tracker ${trackerName.split('').join(' ')} de retour en ligne`);
    }
  }
  
  activeTrackers[trackerName].lastApid = apid;
  activeTrackers[trackerName].packetCount++;
  activeTrackers[trackerName].lastSeen = Date.now();
  activeTrackers[trackerName].lastPayloadHex = bytesToHex(payload);
  
  updateTrackersTable();

  // Mettre à jour les indicateurs
  if (statCount) {
    statCount.textContent = packetIndex;
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
      speak(`Alerte, tracker ${name.split('').join(' ')} perdu`);
    }
    
    const statusText = isLost ? 'PERDU' : 'ACTIF';
    const statusClass = isLost ? 'badge disconnected' : 'badge connected';
    
    const timeString = new Date(tracker.lastSeen).toLocaleTimeString();

    if (!row) {
      row = document.createElement('tr');
      row.id = `tracker-row-${name}`;
      tableBody.appendChild(row);
    }
    
    row.innerHTML = `
      <td><span class="badge connected">${name}</span></td>
      <td>${tracker.typeLabel}</td>
      <td>${tracker.lastApid}</td>
      <td>${tracker.packetCount}</td>
      <td>${timeString}</td>
      <td><span class="${statusClass}">${statusText}</span></td>
      <td style="font-family: var(--font-mono); color: var(--color-cyan); word-break: break-all;">${tracker.lastPayloadHex}</td>
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
  // Ligne de boot : "[CONFIG] Loaded from NVS: Freq=869.525 MHz, SF=8, BW=250.0 kHz"
  if (line.includes('Loaded from NVS:')) {
    const match = line.match(/Freq=([\d.]+)\s*MHz,\s*SF=(\d+),\s*BW=([\d.]+)\s*kHz/i);
    if (match) {
      const freq = parseFloat(match[1]);
      const sf = parseInt(match[2], 10);
      const bw = parseFloat(match[3]);
      
      currentConfig.frequency = freq;
      currentConfig.sf = sf;
      currentConfig.bw = bw;
      
      if (inputFreq) inputFreq.value = freq.toFixed(3);
      if (selectSf) selectSf.value = sf.toString();
      if (selectBw) selectBw.value = bw.toString();
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
    
    // Décaler l'historique
    throughputHistory.shift();
    throughputHistory.push(dataRate);
    
    // Mettre à jour l'indicateur de débit s'il y a des paquets
    if (dataRate > 0) {
      if (statRssi) statRssi.textContent = "ACTIF";
      if (statSnr) statSnr.textContent = `${dataRate} B/s`;
    } else {
      if (statRssi) statRssi.textContent = "--";
      if (statSnr) statSnr.textContent = "--";
    }
    
    // Redessiner le graphique SVG
    drawSvgChart();
    
    // Vérifier les timeouts des trackers
    checkTrackersTimeout();
  }
}

function drawSvgChart() {
  const chartLine = document.getElementById('chart-line');
  const chartFill = document.getElementById('chart-fill');
  if (!chartLine || !chartFill) return;
  
  const width = 300;
  const height = 100;
  const maxVal = Math.max(...throughputHistory, 50); // Echelle auto avec minimum à 50 B/s
  const pointsCount = throughputHistory.length;
  
  let dLine = '';
  
  for (let i = 0; i < pointsCount; i++) {
    const x = (i / (pointsCount - 1)) * width;
    const y = height - (throughputHistory[i] / maxVal) * (height - 10) - 5;
    
    if (i === 0) {
      dLine += `M ${x} ${y}`;
    } else {
      dLine += ` L ${x} ${y}`;
    }
  }
  
  const dFill = `${dLine} L ${width} ${height} L 0 ${height} Z`;
  
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
    await sendSerialText('AT+FREQ?');
    await sendSerialText('AT+SF?');
    await sendSerialText('AT+BW?');
  });
}

if (btnWriteCfg) {
  btnWriteCfg.addEventListener('click', async () => {
    const freq = inputFreq ? parseFloat(inputFreq.value) : NaN;
    const sf = selectSf ? parseInt(selectSf.value, 10) : NaN;
    const bw = selectBw ? parseFloat(selectBw.value) : NaN;
    
    if (!isNaN(freq)) await sendSerialText(`AT+FREQ=${freq.toFixed(3)}`);
    if (!isNaN(sf)) await sendSerialText(`AT+SF=${sf}`);
    if (!isNaN(bw)) await sendSerialText(`AT+BW=${bw.toFixed(1)}`);
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
    alert("Aucune trame en mémoire à exporter.");
    return;
  }
  
  let csvRows = ["Index,Horodatage,Tracker,APID,Taille(octets),ChargeUtileHex"];
  allReceivedFrames.forEach(f => {
    csvRows.push(`${f.index},${f.timestamp},${f.tracker},${f.apid},${f.size},${f.payload}`);
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
    alert("La liaison moniteur série est active. Veuillez cliquer sur 'Déconnexion' avant de lancer le flash du firmware.");
    return;
  }
  
  setElementDisabled(btnFlash, true);
  if (flashProgressContainer) flashProgressContainer.classList.remove('hidden');
  if (lblFlashStatus) lblFlashStatus.textContent = "Connexion à l'ESP32...";
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
    logToTerminal("Sélection du port série pour le flash (choisissez le port de votre carte)...", "sys-out");
    const flashPort = await navigator.serial.requestPort();
    
    transport = new Transport(flashPort, true);
    
    esploader = new ESPLoader({
      transport: transport,
      terminal: customTerminal,
      baudrate: 115200 // vitesse de synchronisation bootloader
    });
    
    if (lblFlashStatus) lblFlashStatus.textContent = "Synchronisation de la carte...";
    await esploader.main();
    
    if (lblFlashStatus) lblFlashStatus.textContent = `Puce détectée : ${esploader.chipName}`;
    logToTerminal(`Téléchargement du firmware depuis ${binUrl}...`, "sys-out");
    
    const response = await fetch(binUrl);
    if (!response.ok) {
      throw new Error(`Impossible de récupérer le binaire (${response.statusText})`);
    }
    
    const arrayBuffer = await response.arrayBuffer();
    const firmwareData = new Uint8Array(arrayBuffer);
    
    if (lblFlashStatus) lblFlashStatus.textContent = "Écriture en cours (flash)...";
    logToTerminal("Début de l'écriture de l'application à 0x10000...", "sys-out");
    
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
    
    if (lblFlashStatus) lblFlashStatus.textContent = "Flash Réussi !";
    logToTerminal("Mise à jour terminée ! Redémarrage de la carte...", "sys-out");
    
    // Redémarrer la carte matériellement
    await transport.setDTR(false);
    await new Promise(resolve => setTimeout(resolve, 100));
    await transport.setDTR(true);
    
  } catch (err) {
    if (lblFlashStatus) lblFlashStatus.textContent = "ÉCHEC !";
    logToTerminal(`Erreur lors du flash : ${err.message}`, 'sys-out');
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
    allReceivedFrames = [];
    if (statCount) statCount.textContent = '0';
    if (tableTelemetryBody) {
      tableTelemetryBody.innerHTML = '';
      if (rowEmpty) {
        rowEmpty.style.display = 'table-row';
        tableTelemetryBody.appendChild(rowEmpty);
      }
    }
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
    logToTerminal("Le port série a été déconnecté physiquement.", "sys-out");
    disconnectSerial();
  }
});
