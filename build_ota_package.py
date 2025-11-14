#!/usr/bin/env python3
"""
Build Script für HeatBodyVentilator OTA Update Package
Erstellt eine update.tar Datei mit firmware.bin und spiffs.bin
"""

import os
import tarfile
from pathlib import Path

# Pfade
PROJECT_ROOT = Path(__file__).parent
BUILD_DIR = PROJECT_ROOT / ".pio" / "build" / "m5stack-atom"
FIRMWARE_BIN = BUILD_DIR / "firmware.bin"
SPIFFS_BIN = BUILD_DIR / "spiffs.bin"
OUTPUT_TAR = PROJECT_ROOT / "update.tar"

def create_ota_package():
    """Erstellt das OTA Update Package"""
    
    print("=" * 60)
    print("HeatBodyVentilator OTA Package Builder (TAR)")
    print("=" * 60)
    
    # Prüfe ob Dateien existieren
    files_to_package = []
    
    if FIRMWARE_BIN.exists():
        size_mb = FIRMWARE_BIN.stat().st_size / (1024 * 1024)
        print(f"✓ firmware.bin gefunden ({size_mb:.2f} MB)")
        files_to_package.append(("firmware.bin", FIRMWARE_BIN))
    else:
        print(f"✗ firmware.bin nicht gefunden in {FIRMWARE_BIN}")
        print("  -> Führen Sie erst 'pio run' aus, um die Firmware zu kompilieren")
    
    if SPIFFS_BIN.exists():
        size_mb = SPIFFS_BIN.stat().st_size / (1024 * 1024)
        print(f"✓ spiffs.bin gefunden ({size_mb:.2f} MB)")
        files_to_package.append(("spiffs.bin", SPIFFS_BIN))
    else:
        print(f"✗ spiffs.bin nicht gefunden in {SPIFFS_BIN}")
        print("  -> Führen Sie erst 'pio run --target buildfs' aus")
    
    if not files_to_package:
        print("\n❌ Keine Dateien zum Paketieren gefunden!")
        print("Führen Sie folgende Befehle aus:")
        print("  pio run                  # Kompiliert Firmware")
        print("  pio run --target buildfs # Erstellt SPIFFS Image")
        return False
    
    # Erstelle TAR
    print(f"\nErstelle {OUTPUT_TAR}...")
    
    # Lösche alte Version
    if OUTPUT_TAR.exists():
        OUTPUT_TAR.unlink()
    
    with tarfile.open(OUTPUT_TAR, 'w') as tar:
        for arcname, filepath in files_to_package:
            print(f"  Adding {arcname}...")
            tar.add(filepath, arcname=arcname)
    
    # Zeige Ergebnis
    tar_size_mb = OUTPUT_TAR.stat().st_size / (1024 * 1024)
    print(f"\n✅ Update Package erstellt: {OUTPUT_TAR}")
    print(f"   Größe: {tar_size_mb:.2f} MB")
    print(f"   Enthält: {', '.join([f[0] for f in files_to_package])}")
    
    print("\n" + "=" * 60)
    print("Nächste Schritte:")
    print("1. Öffnen Sie die Gerät-Einstellungen im Webbrowser")
    print("2. Klicken Sie auf 'Firmware Hochladen'")
    print(f"3. Wählen Sie {OUTPUT_TAR.name} aus")
    print("4. Starten Sie das Update")
    print("=" * 60)
    
    return True

if __name__ == "__main__":
    success = create_ota_package()
    exit(0 if success else 1)
