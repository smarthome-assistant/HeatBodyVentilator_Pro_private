# 🏠 HeatBodyVentilator Pro

Der HeatBodyVentilator Pro - die professionelle Heizkörper-Lüfter Einheit mit vollständiger Home Assistant Integration, erweiterter Temperaturüberwachung und intelligenter Lüftersteuerung.

[![License](https://img.shields.io/badge/License-Custom-red.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-ESP32-green.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Framework](https://img.shields.io/badge/Framework-Arduino-blue.svg)](https://www.arduino.cc/)

## ✨ Features

### 🌐 Web Interface
- **Responsive Design**: Mobile-optimierte Benutzeroberfläche
- **Sicherer Login**: Token-basierte Authentifizierung
- **Echtzeit-Dashboard**: Live-Anzeige aller Sensordaten
- **Einstellungsseite**: Konfiguration von WiFi, MQTT und Geräteparametern
- **Deutschsprachig**: Vollständig lokalisierte Benutzeroberfläche

### 📡 Home Assistant Integration
- **MQTT Auto-Discovery**: Automatische Erkennung in Home Assistant
- **Sensoren**:
  - 🌡️ Temperatur (KMeter-ISO Thermoelement)
  - 💨 Lüfter-Geschwindigkeit in %
  - 📶 WiFi Signal-Stärke
  - ⏱️ Uptime
- **Steuerung**:
  - 💡 LED An/Aus
  - 🔄 Neustart-Button
- **Status**: Online/Offline Überwachung

### 🌡️ Temperaturüberwachung
- **KMeter-ISO Sensor**: Professionelle K-Type Thermoelement-Unterstützung
- **Temperatureinheiten**: Celsius und Fahrenheit
- **Interne Temperatur**: Zusätzlicher interner Sensor
- **Fehlerüberwachung**: Sensor-Statusmeldungen

### 💨 Intelligente Lüftersteuerung
- **PWM-Steuerung**: Präzise Geschwindigkeitsregelung (0-100%)
- **Auto-Modus**: Temperaturbasierte automatische Regelung
- **Manueller Modus**: Direkte Geschwindigkeitseinstellung
- **Temperatur-Mapping**: Konfigurierbare Start- und Maximaltemperatur

### 🔐 Sicherheit
- **Passwortschutz**: Sicherer Web-Zugang
- **Session-Management**: Token-basierte Sitzungen
- **Passwort-Änderung**: Über Web-Interface
- **Factory Reset**: Zurücksetzen auf Werkseinstellungen

### 📶 WiFi Management
- **Station Mode**: Verbindung zu bestehendem WiFi
- **Access Point Mode**: Eigener Hotspot für Setup
- **Dual Mode**: Gleichzeitige AP und STA Funktion
- **WiFi Scanner**: Verfügbare Netzwerke anzeigen
- **Verbindungsstatus**: Live-Überwachung

## 🔧 Hardware-Anforderungen

### Minimal
- **ESP32**: M5Stack Atom oder kompatibles Board
- **Stromversorgung**: USB 5V

### Empfohlen
- **ESP32**: M5Stack Atom Lite
- **Temperatursensor**: M5Stack KMeter-ISO Unit (K-Type Thermoelement)
- **Lüfter**: 12V PWM-Lüfter (Pin 22)
- **LED**: FastLED-kompatible RGB LED (integriert bei M5Stack Atom Lite)

## 📦 Installation

### Voraussetzungen
- [VS Code](https://code.visualstudio.com/)
- [PlatformIO Extension](https://platformio.org/install/ide?install=vscode)
- USB-Treiber für ESP32

### Schritt 1: Repository klonen
```bash
git clone https://github.com/smarthome-assistant/HeatBodyVentilator_Pro.git
cd HeatBodyVentilator_Pro
```

### Schritt 2: Projekt in VS Code öffnen
```bash
code .
```

### Schritt 3: Build und Upload
1. Öffnen Sie PlatformIO Home (Alien-Icon in der Sidebar)
2. Klicken Sie auf "Build" (✓)
3. Verbinden Sie Ihr ESP32 via USB
4. Klicken Sie auf "Upload" (→)

### Schritt 4: Erste Konfiguration
1. ESP32 startet einen Access Point: `SmartHome-Assistant-AP`
2. Verbinden Sie sich mit diesem WiFi (Passwort: `smarthome-assistant.info`)
3. Öffnen Sie im Browser: `http://192.168.4.1`
4. Login mit Standard-Passwort: `smarthome-assistant.info`
5. Konfigurieren Sie WiFi und MQTT

## ⚙️ Konfiguration

### WiFi Einstellungen
- **SSID**: Ihr WiFi-Netzwerkname
- **Passwort**: Ihr WiFi-Passwort
- **Access Point**: Optional zusätzlicher AP-Modus

### MQTT Einstellungen
- **Server**: MQTT Broker IP/Hostname
- **Port**: Standard 1883
- **Username**: Optional
- **Password**: Optional
- **Topic**: Base-Topic für Nachrichten

### Lüftersteuerung
- **Start-Temperatur**: Temperatur ab der der Lüfter startet
- **Max-Temperatur**: Temperatur bei voller Geschwindigkeit

## 🏗️ Projektstruktur

```
HeatBodyVentilator_Pro/
├── data/                      # Web-Dateien (HTML, CSS, Bilder)
│   ├── index.html            # Login-Seite
│   ├── login.html            # Login-Formular
│   ├── main.html             # Haupt-Dashboard
│   ├── setting.html          # Einstellungen
│   ├── style.css             # Einheitliche Styles
│   └── img/
│       └── logo.png          # Logo
├── src/                       # Quellcode
│   ├── main.cpp              # Hauptprogramm
│   ├── Config.*              # Konfigurationsverwaltung
│   ├── ServerManager.*       # Webserver-Management
│   ├── WiFiManager.*         # WiFi-Verbindungsverwaltung
│   ├── MQTTManager.*         # MQTT-Funktionalität
│   ├── LEDManager.*          # LED-Steuerung
│   └── KMeterManager.*       # Temperatursensor-Management
├── platformio.ini            # PlatformIO-Konfiguration
└── README.md                 # Diese Datei
```

## 📚 Libraries

- **FastLED** 3.9.20 - LED-Steuerung
- **ArduinoJson** 6.21.5 - JSON-Verarbeitung
- **PubSubClient** 2.8.0 - MQTT-Client
- **M5Unit-KMeterISO** 1.0.1 - Temperatursensor
- **Preferences** - ESP32 NVS Storage
- **WiFi** - ESP32 WiFi
- **WebServer** - ESP32 Webserver
- **Update** - OTA Updates

## 🐛 Troubleshooting

### ESP32 startet nicht
- Drücken Sie den Reset-Button
- Überprüfen Sie die Stromversorgung
- Versuchen Sie einen Factory Reset

### Keine WiFi-Verbindung
- Überprüfen Sie SSID und Passwort
- Stellen Sie sicher, dass 2.4GHz WiFi verwendet wird
- Aktivieren Sie den AP-Modus für Neukonfiguration

### MQTT verbindet nicht
- Überprüfen Sie Broker-IP und Port
- Testen Sie die Verbindung mit MQTT Explorer
- Überprüfen Sie Username/Password (falls verwendet)

### Sensor zeigt keine Daten
- Überprüfen Sie die I2C-Verbindung
- Stellen Sie sicher, dass KMeter-ISO korrekt angeschlossen ist
- Prüfen Sie die Sensor-Adresse (Standard: 0x66)

## 🚀 Erweiterte Funktionen

### OTA Updates
- Firmware-Updates über Web-Interface

### API Endpoints
Das Gerät bietet REST-APIs für externe Integration:
- `GET /api/wifi-status` - WiFi-Status
- `GET /api/mqtt-status` - MQTT-Status
- `POST /api/pwm-control` - Lüftersteuerung
- `GET /api/kmeter-status` - Temperaturdaten

## 📄 Lizenz

### Eigener Code (Custom Source-Available License)

Der eigene Programmcode dieses Projekts (alle Dateien in `/src` und `/data`) unterliegt einer **Custom Source-Available License**:

- ✅ **Erlaubt**: Private und nicht-kommerzielle Nutzung, Bildungszwecke, Studium des Codes
- ❌ **Nicht erlaubt ohne Genehmigung**: Kommerzielle Nutzung, Verkauf, Veröffentlichung modifizierter Versionen
- 📄 **Details**: Siehe [LICENSE](LICENSE) Datei

**Für kommerzielle Nutzung** kontaktieren Sie bitte:
- GitHub: [@smarthome-assistant](https://github.com/smarthome-assistant)
- Email: markus.reza.tain@smarthome-assistant.info

### Verwendete Open-Source Bibliotheken

Dieses Projekt nutzt folgende **MIT-lizenzierte** Open-Source-Bibliotheken, die kommerzielle Nutzung erlauben:

| Bibliothek | Lizenz | Zweck |
|------------|--------|-------|
| [FastLED](https://github.com/FastLED/FastLED) | MIT | LED-Steuerung |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | MIT | JSON-Verarbeitung |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MIT | MQTT-Client |
| [M5Unit-KMeterISO](https://github.com/m5stack/M5Unit-KMeterISO) | MIT | Temperatursensor |

📋 **Vollständige Lizenztexte**: Siehe [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)

---

**Hinweis**: Die restriktive Lizenz gilt NUR für den eigenen Code. Die verwendeten Bibliotheken bleiben unter ihren permissiven MIT-Lizenzen verfügbar.

## 👤 Autor

**SmartHome-Assistant.info (Markus Reza Tain)**
- GitHub: [@smarthome-assistant](https://github.com/smarthome-assistant)
- Repository: [HeatBodyVentilator Pro](https://github.com/smarthome-assistant/HeatBodyVentilator_Pro)

**Kontakt für Lizenzanfragen:**
- Kommerzielle Nutzung
- Veröffentlichung modifizierter Versionen
- Sondervereinbarungen

## 🙏 Danksagungen

- M5Stack für die hervorragende Hardware
- Home Assistant Community
- PlatformIO Team

## 📝 Changelog

### Version 0.0.1 (2025-10-21)
- ✨ Initiale Entwicklungs-Version
- 🌡️ KMeter-ISO Temperatursensor-Integration
- 💨 Intelligente Lüftersteuerung mit Auto-Modus
- 📡 Vollständige Home Assistant MQTT Integration
- 🌐 Responsive Web-Interface
- 🔐 Sicherheitsfunktionen implementiert

---

**Hinweis**: Dies ist ein Community-Projekt. Beiträge sind willkommen!
