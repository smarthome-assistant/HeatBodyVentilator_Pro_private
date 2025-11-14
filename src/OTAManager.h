#ifndef OTAMANAGER_H
#define OTAMANAGER_H

#include <stdint.h>
#include <stddef.h>

class OTAManager {
public:
    OTAManager();
    ~OTAManager();
    
    /**
     * Process TAR file containing firmware.bin and/or spiffs.bin
     * @param tarData Pointer to TAR file data
     * @param tarSize Size of TAR file
     * @return true if successful
     */
    bool processTarUpdate(const uint8_t* tarData, size_t tarSize);
    
    /**
     * Flash firmware.bin directly
     * @param data Pointer to firmware data
     * @param size Size of firmware
     * @return true if successful
     */
    bool flashFirmwareDirect(const uint8_t* data, size_t size);
    
    /**
     * Flash spiffs.bin directly  
     * @param data Pointer to SPIFFS data
     * @param size Size of SPIFFS image
     * @return true if successful
     */
    bool flashSPIFFSDirect(const uint8_t* data, size_t size);
    
    /**
     * Get last error message
     */
    const char* getLastError() const { return lastError; }
    
    /**
     * Get update progress (0-100)
     */
    uint8_t getProgress() const { return progress; }

private:
    char lastError[256];
    uint8_t progress;
    
    // TAR parsing structures
    struct TarHeader {
        char name[100];
        char mode[8];
        char uid[8];
        char gid[8];
        char size[12];
        char mtime[12];
        char checksum[8];
        char typeflag;
        char linkname[100];
        char magic[6];
        char version[2];
        char uname[32];
        char gname[32];
        char devmajor[8];
        char devminor[8];
        char prefix[155];
        char padding[12];
    };
    
    size_t parseOctal(const char* str, size_t len);
    bool extractAndFlashTar(const uint8_t* tarData, size_t tarSize);
    bool flashFirmware(const uint8_t* data, size_t size);
    bool flashSPIFFS(const uint8_t* data, size_t size);
    bool verifyPartition(const char* partitionLabel);
};

#endif // OTAMANAGER_H
