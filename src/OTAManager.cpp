#include "OTAManager.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_spiffs.h"
#include <string.h>

static const char* TAG = "OTA";

// Maximum file size in ZIP (10MB for firmware, 2MB for SPIFFS)
#define MAX_FIRMWARE_SIZE (10 * 1024 * 1024)
#define MAX_SPIFFS_SIZE (2 * 1024 * 1024)

OTAManager::OTAManager() : progress(0) {
    memset(lastError, 0, sizeof(lastError));
}

OTAManager::~OTAManager() {
}

bool OTAManager::processTarUpdate(const uint8_t* tarData, size_t tarSize) {
    ESP_LOGI(TAG, "Processing TAR update, size: %d bytes", tarSize);
    progress = 0;
    
    if (!tarData || tarSize == 0) {
        snprintf(lastError, sizeof(lastError), "Invalid TAR data");
        return false;
    }
    
    return extractAndFlashTar(tarData, tarSize);
}

bool OTAManager::processTarUpdateFromFile(const char* tarFilePath) {
    ESP_LOGI(TAG, "Processing TAR update from file: %s", tarFilePath);
    progress = 0;
    
    FILE* tarFile = fopen(tarFilePath, "rb");
    if (!tarFile) {
        snprintf(lastError, sizeof(lastError), "Failed to open TAR file");
        return false;
    }
    
    // Get file size
    fseek(tarFile, 0, SEEK_END);
    size_t tarSize = ftell(tarFile);
    fseek(tarFile, 0, SEEK_SET);
    
    ESP_LOGI(TAG, "TAR file size: %d bytes", tarSize);
    
    bool result = extractAndFlashTarFromFile(tarFile, tarSize);
    fclose(tarFile);
    
    return result;
}

size_t OTAManager::parseOctal(const char* str, size_t len) {
    size_t result = 0;
    for (size_t i = 0; i < len && str[i] != '\0' && str[i] != ' '; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            result = result * 8 + (str[i] - '0');
        }
    }
    return result;
}

bool OTAManager::extractAndFlashTarFromFile(FILE* tarFile, size_t tarSize) {
    bool firmwareFound = false;
    bool spiffsFound = false;
    bool success = true;
    size_t offset = 0;
    
    // Allocate buffer for reading TAR headers and file data
    const size_t BUFFER_SIZE = 32 * 1024; // 32KB buffer for file data
    uint8_t* buffer = (uint8_t*)malloc(BUFFER_SIZE);
    if (!buffer) {
        snprintf(lastError, sizeof(lastError), "Failed to allocate buffer");
        return false;
    }
    
    TarHeader header;
    
    while (offset < tarSize) {
        // Read TAR header
        if (fread(&header, 1, 512, tarFile) != 512) {
            ESP_LOGE(TAG, "Failed to read TAR header");
            free(buffer);
            return false;
        }
        offset += 512;
        
        // Check for end of archive (zero block)
        bool allZero = true;
        uint8_t* headerBytes = (uint8_t*)&header;
        for (size_t i = 0; i < 512; i++) {
            if (headerBytes[i] != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero) {
            ESP_LOGI(TAG, "Reached end of TAR archive");
            break;
        }
        
        // Verify TAR magic
        if (strncmp(header.magic, "ustar", 5) != 0) {
            ESP_LOGW(TAG, "Invalid TAR magic at offset %d", offset - 512);
            free(buffer);
            return false;
        }
        
        // Parse file size
        size_t fileSize = parseOctal(header.size, 12);
        char filename[256];
        snprintf(filename, sizeof(filename), "%s", header.name);
        
        ESP_LOGI(TAG, "Found file: %s (%u bytes)", filename, fileSize);
        
        // Check if it's a regular file
        if (header.typeflag == '0' || header.typeflag == '\0') {
            if (strcmp(filename, "firmware.bin") == 0) {
                ESP_LOGI(TAG, "Flashing firmware.bin...");
                progress = 25;
                
                // Allocate buffer for firmware (we need entire firmware in RAM for flashing)
                uint8_t* fwBuffer = (uint8_t*)malloc(fileSize);
                if (!fwBuffer) {
                    ESP_LOGE(TAG, "Failed to allocate %d bytes for firmware", fileSize);
                    free(buffer);
                    snprintf(lastError, sizeof(lastError), "Out of memory for firmware");
                    return false;
                }
                
                // Read firmware into buffer
                size_t read = fread(fwBuffer, 1, fileSize, tarFile);
                if (read != fileSize) {
                    ESP_LOGE(TAG, "Failed to read firmware data");
                    free(fwBuffer);
                    free(buffer);
                    return false;
                }
                
                if (flashFirmware(fwBuffer, fileSize)) {
                    firmwareFound = true;
                    progress = 50;
                    ESP_LOGI(TAG, "Firmware flashed successfully");
                } else {
                    success = false;
                }
                
                free(fwBuffer);
                if (!success) break;
                
            } else if (strcmp(filename, "spiffs.bin") == 0) {
                ESP_LOGI(TAG, "Flashing spiffs.bin...");
                progress = 75;
                
                // Allocate buffer for SPIFFS
                uint8_t* spiffsBuffer = (uint8_t*)malloc(fileSize);
                if (!spiffsBuffer) {
                    ESP_LOGE(TAG, "Failed to allocate %d bytes for SPIFFS", fileSize);
                    free(buffer);
                    snprintf(lastError, sizeof(lastError), "Out of memory for SPIFFS");
                    return false;
                }
                
                // Read SPIFFS into buffer
                size_t read = fread(spiffsBuffer, 1, fileSize, tarFile);
                if (read != fileSize) {
                    ESP_LOGE(TAG, "Failed to read SPIFFS data");
                    free(spiffsBuffer);
                    free(buffer);
                    return false;
                }
                
                if (flashSPIFFS(spiffsBuffer, fileSize)) {
                    spiffsFound = true;
                    progress = 100;
                    ESP_LOGI(TAG, "SPIFFS flashed successfully");
                } else {
                    success = false;
                }
                
                free(spiffsBuffer);
                if (!success) break;
                
            } else {
                ESP_LOGW(TAG, "Ignoring file: %s", filename);
                // Skip file data
                fseek(tarFile, fileSize, SEEK_CUR);
            }
        } else {
            // Skip non-regular files
            fseek(tarFile, fileSize, SEEK_CUR);
        }
        
        // Move to next file (data is padded to 512-byte boundary)
        size_t paddedSize = (fileSize + 511) & ~511;
        offset += paddedSize;
        
        // Align file position to 512-byte boundary
        long currentPos = ftell(tarFile);
        long alignedPos = (currentPos + 511) & ~511;
        if (alignedPos != currentPos) {
            fseek(tarFile, alignedPos, SEEK_SET);
        }
    }
    
    free(buffer);
    
    if (!firmwareFound && !spiffsFound) {
        snprintf(lastError, sizeof(lastError), "TAR must contain firmware.bin and/or spiffs.bin");
        return false;
    }
    
    if (success) {
        ESP_LOGI(TAG, "Update successful! Firmware: %s, SPIFFS: %s", 
                 firmwareFound ? "YES" : "NO", 
                 spiffsFound ? "YES" : "NO");
    }
    
    return success;
}

bool OTAManager::extractAndFlashTar(const uint8_t* tarData, size_t tarSize) {
    size_t offset = 0;
    bool firmwareFound = false;
    bool spiffsFound = false;
    bool success = true;
    
    while (offset < tarSize) {
        // Check if we've reached end of archive (two consecutive zero blocks)
        if (offset + 1024 > tarSize) break;
        
        bool allZero = true;
        for (size_t i = 0; i < 512; i++) {
            if (tarData[offset + i] != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero) break; // End of archive
        
        // Read TAR header
        TarHeader* header = (TarHeader*)(tarData + offset);
        
        // Verify TAR magic
        if (strncmp(header->magic, "ustar", 5) != 0) {
            ESP_LOGW(TAG, "Invalid TAR magic at offset %d", offset);
            offset += 512;
            continue;
        }
        
        // Parse file size
        size_t fileSize = parseOctal(header->size, 12);
        
        // Get filename
        char filename[256];
        snprintf(filename, sizeof(filename), "%s", header->name);
        
        ESP_LOGI(TAG, "Found file: %s (%u bytes)", filename, fileSize);
        
        // Move to file data (skip header)
        offset += 512;
        
        if (offset + fileSize > tarSize) {
            snprintf(lastError, sizeof(lastError), "TAR file truncated");
            return false;
        }
        
        // Check if it's a regular file
        if (header->typeflag == '0' || header->typeflag == '\0') {
            const uint8_t* fileData = tarData + offset;
            
            // Check filename and flash accordingly
            if (strcmp(filename, "firmware.bin") == 0) {
                ESP_LOGI(TAG, "Flashing firmware.bin...");
                progress = 25;
                if (flashFirmware(fileData, fileSize)) {
                    firmwareFound = true;
                    progress = 50;
                    ESP_LOGI(TAG, "Firmware flashed successfully");
                } else {
                    success = false;
                    break;
                }
            } 
            else if (strcmp(filename, "spiffs.bin") == 0) {
                ESP_LOGI(TAG, "Flashing spiffs.bin...");
                progress = 75;
                if (flashSPIFFS(fileData, fileSize)) {
                    spiffsFound = true;
                    progress = 100;
                    ESP_LOGI(TAG, "SPIFFS flashed successfully");
                } else {
                    success = false;
                    break;
                }
            }
            else {
                ESP_LOGW(TAG, "Ignoring file: %s", filename);
            }
        }
        
        // Move to next file (data is padded to 512-byte boundary)
        size_t paddedSize = (fileSize + 511) & ~511;
        offset += paddedSize;
    }
    
    if (!firmwareFound && !spiffsFound) {
        snprintf(lastError, sizeof(lastError), "TAR must contain firmware.bin and/or spiffs.bin");
        return false;
    }
    
    if (success) {
        ESP_LOGI(TAG, "Update successful! Firmware: %s, SPIFFS: %s", 
                 firmwareFound ? "YES" : "NO", 
                 spiffsFound ? "YES" : "NO");
    }
    
    return success;
}

bool OTAManager::flashFirmwareDirect(const uint8_t* data, size_t size) {
    return flashFirmware(data, size);
}

bool OTAManager::flashSPIFFSDirect(const uint8_t* data, size_t size) {
    return flashSPIFFS(data, size);
}

bool OTAManager::flashFirmware(const uint8_t* data, size_t size) {
    ESP_LOGI(TAG, "Flashing firmware (%d bytes)...", size);
    
    if (size > MAX_FIRMWARE_SIZE) {
        snprintf(lastError, sizeof(lastError), "Firmware too large: %d bytes", size);
        return false;
    }
    
    // Get running partition
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        snprintf(lastError, sizeof(lastError), "Failed to get running partition");
        return false;
    }
    
    // Get update partition (factory in this case since no OTA partitions)
    const esp_partition_t* update_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    
    if (!update_partition) {
        snprintf(lastError, sizeof(lastError), "No factory partition found");
        return false;
    }
    
    ESP_LOGI(TAG, "Writing to factory partition (offset: 0x%x, size: 0x%x)", 
             update_partition->address, update_partition->size);
    
    // Erase partition
    ESP_LOGI(TAG, "Erasing partition...");
    esp_err_t err = esp_partition_erase_range(update_partition, 0, update_partition->size);
    if (err != ESP_OK) {
        snprintf(lastError, sizeof(lastError), "Failed to erase partition: %s", esp_err_to_name(err));
        return false;
    }
    
    // Write firmware
    ESP_LOGI(TAG, "Writing firmware data...");
    err = esp_partition_write(update_partition, 0, data, size);
    if (err != ESP_OK) {
        snprintf(lastError, sizeof(lastError), "Failed to write firmware: %s", esp_err_to_name(err));
        return false;
    }
    
    // Verify
    if (!verifyPartition("factory")) {
        snprintf(lastError, sizeof(lastError), "Firmware verification failed");
        return false;
    }
    
    ESP_LOGI(TAG, "Firmware flashed successfully");
    return true;
}

bool OTAManager::flashSPIFFS(const uint8_t* data, size_t size) {
    ESP_LOGI(TAG, "Flashing SPIFFS (%d bytes)...", size);
    
    if (size > MAX_SPIFFS_SIZE) {
        snprintf(lastError, sizeof(lastError), "SPIFFS too large: %d bytes", size);
        return false;
    }
    
    // Unmount SPIFFS first
    esp_vfs_spiffs_unregister(NULL);
    
    // Find SPIFFS partition
    const esp_partition_t* spiffs_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    
    if (!spiffs_partition) {
        snprintf(lastError, sizeof(lastError), "No SPIFFS partition found");
        return false;
    }
    
    ESP_LOGI(TAG, "Writing to SPIFFS partition (offset: 0x%x, size: 0x%x)", 
             spiffs_partition->address, spiffs_partition->size);
    
    // Erase partition
    ESP_LOGI(TAG, "Erasing SPIFFS partition...");
    esp_err_t err = esp_partition_erase_range(spiffs_partition, 0, spiffs_partition->size);
    if (err != ESP_OK) {
        snprintf(lastError, sizeof(lastError), "Failed to erase SPIFFS: %s", esp_err_to_name(err));
        return false;
    }
    
    // Write SPIFFS
    ESP_LOGI(TAG, "Writing SPIFFS data...");
    err = esp_partition_write(spiffs_partition, 0, data, size);
    if (err != ESP_OK) {
        snprintf(lastError, sizeof(lastError), "Failed to write SPIFFS: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "SPIFFS flashed successfully");
    
    // Remount SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false
    };
    esp_vfs_spiffs_register(&conf);
    
    return true;
}

bool OTAManager::verifyPartition(const char* partitionLabel) {
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, partitionLabel);
    
    if (!partition) {
        return false;
    }
    
    // Basic verification - check if partition is not all 0xFF (erased)
    uint8_t sample[32];
    esp_partition_read(partition, 0, sample, sizeof(sample));
    
    bool allFF = true;
    for (int i = 0; i < sizeof(sample); i++) {
        if (sample[i] != 0xFF) {
            allFF = false;
            break;
        }
    }
    
    return !allFF; // Should not be all erased
}

bool OTAManager::flashFirmwareStreaming(size_t totalSize, ReadCallback readCallback, void* userData) {
    ESP_LOGI(TAG, "Streaming firmware flash (%d bytes)...", totalSize);
    
    if (totalSize > MAX_FIRMWARE_SIZE) {
        snprintf(lastError, sizeof(lastError), "Firmware too large: %d bytes", totalSize);
        return false;
    }
    
    // Get running partition
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) {
        snprintf(lastError, sizeof(lastError), "Failed to get running partition");
        return false;
    }
    
    ESP_LOGI(TAG, "Running partition: %s at offset 0x%x", running->label, running->address);
    
    // Get next OTA partition to update
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        snprintf(lastError, sizeof(lastError), "No OTA partition found");
        return false;
    }
    
    ESP_LOGI(TAG, "Writing to OTA partition: %s (offset: 0x%x, size: 0x%x)", 
             update_partition->label, update_partition->address, update_partition->size);
    
    // Begin OTA update
    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, totalSize, &ota_handle);
    if (err != ESP_OK) {
        snprintf(lastError, sizeof(lastError), "Failed to begin OTA: %s", esp_err_to_name(err));
        return false;
    }
    
    // Write firmware in chunks
    const size_t CHUNK_SIZE = 4096; // 4KB chunks
    uint8_t* chunk = (uint8_t*)malloc(CHUNK_SIZE);
    if (!chunk) {
        esp_ota_abort(ota_handle);
        snprintf(lastError, sizeof(lastError), "Failed to allocate chunk buffer");
        return false;
    }
    
    size_t written = 0;
    while (written < totalSize) {
        size_t toRead = (totalSize - written > CHUNK_SIZE) ? CHUNK_SIZE : (totalSize - written);
        size_t read = readCallback(chunk, toRead, userData);
        
        if (read == 0) {
            free(chunk);
            esp_ota_abort(ota_handle);
            snprintf(lastError, sizeof(lastError), "Failed to read firmware data");
            return false;
        }
        
        err = esp_ota_write(ota_handle, chunk, read);
        if (err != ESP_OK) {
            free(chunk);
            esp_ota_abort(ota_handle);
            snprintf(lastError, sizeof(lastError), "Failed to write firmware: %s", esp_err_to_name(err));
            return false;
        }
        
        written += read;
        
        if (written % (64 * 1024) == 0 || written == totalSize) {
            ESP_LOGI(TAG, "Firmware flash progress: %d / %d bytes (%.1f%%)", 
                     written, totalSize, (written * 100.0f) / totalSize);
        }
    }
    
    free(chunk);
    
    // Finalize OTA
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        snprintf(lastError, sizeof(lastError), "OTA end failed: %s", esp_err_to_name(err));
        return false;
    }
    
    // Set boot partition
    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        snprintf(lastError, sizeof(lastError), "Failed to set boot partition: %s", esp_err_to_name(err));
        return false;
    }
    
    ESP_LOGI(TAG, "Firmware flashed successfully, boot partition set to %s", update_partition->label);
    return true;
}

bool OTAManager::flashSPIFFSStreaming(size_t totalSize, ReadCallback readCallback, void* userData) {
    ESP_LOGI(TAG, "Streaming SPIFFS flash (%d bytes)...", totalSize);
    
    if (totalSize > MAX_SPIFFS_SIZE) {
        snprintf(lastError, sizeof(lastError), "SPIFFS too large: %d bytes", totalSize);
        return false;
    }
    
    // Unmount SPIFFS first
    esp_vfs_spiffs_unregister(NULL);
    
    // Find SPIFFS partition
    const esp_partition_t* spiffs_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
    
    if (!spiffs_partition) {
        snprintf(lastError, sizeof(lastError), "No SPIFFS partition found");
        return false;
    }
    
    ESP_LOGI(TAG, "Writing to SPIFFS partition (offset: 0x%x, size: 0x%x)", 
             spiffs_partition->address, spiffs_partition->size);
    
    // Erase partition
    ESP_LOGI(TAG, "Erasing SPIFFS partition...");
    esp_err_t err = esp_partition_erase_range(spiffs_partition, 0, spiffs_partition->size);
    if (err != ESP_OK) {
        snprintf(lastError, sizeof(lastError), "Failed to erase SPIFFS: %s", esp_err_to_name(err));
        return false;
    }
    
    // Write SPIFFS in chunks
    const size_t CHUNK_SIZE = 4096; // 4KB chunks
    uint8_t* chunk = (uint8_t*)malloc(CHUNK_SIZE);
    if (!chunk) {
        snprintf(lastError, sizeof(lastError), "Failed to allocate chunk buffer");
        return false;
    }
    
    size_t written = 0;
    while (written < totalSize) {
        size_t toRead = (totalSize - written > CHUNK_SIZE) ? CHUNK_SIZE : (totalSize - written);
        size_t read = readCallback(chunk, toRead, userData);
        
        if (read == 0) {
            free(chunk);
            snprintf(lastError, sizeof(lastError), "Failed to read SPIFFS data");
            return false;
        }
        
        err = esp_partition_write(spiffs_partition, written, chunk, read);
        if (err != ESP_OK) {
            free(chunk);
            snprintf(lastError, sizeof(lastError), "Failed to write SPIFFS: %s", esp_err_to_name(err));
            return false;
        }
        
        written += read;
        
        if (written % (64 * 1024) == 0 || written == totalSize) {
            ESP_LOGI(TAG, "SPIFFS flash progress: %d / %d bytes (%.1f%%)", 
                     written, totalSize, (written * 100.0f) / totalSize);
        }
    }
    
    free(chunk);
    
    ESP_LOGI(TAG, "SPIFFS flashed successfully");
    
    // Remount SPIFFS
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false
    };
    esp_vfs_spiffs_register(&conf);
    
    return true;
}
