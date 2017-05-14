#include "iotgo_flash.h"

#define IOTGO_PARAM_START_SEC		(0x78)
#define IOTGO_PARAM_SAVE_0          (0x00)
#define IOTGO_PARAM_SAVE_1          (0x01)
#define IOTGO_PARAM_FLAG            (0x02)

void ICACHE_FLASH_ATTR
iotgoFlashLoadParam(IoTgoFlashParam *param)
{
    IoTgoFlashParamFlag flag;

    spi_flash_read((IOTGO_PARAM_START_SEC + IOTGO_PARAM_FLAG) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)&flag, sizeof(IoTgoFlashParamFlag));

    if (flag.flag == 0) {
        spi_flash_read((IOTGO_PARAM_START_SEC + IOTGO_PARAM_SAVE_0) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)param, sizeof(IoTgoFlashParam));
    } else {
        spi_flash_read((IOTGO_PARAM_START_SEC + IOTGO_PARAM_SAVE_1) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)param, sizeof(IoTgoFlashParam));
    }
}

void ICACHE_FLASH_ATTR
iotgoFlashSaveParam(IoTgoFlashParam *param)
{
    IoTgoFlashParamFlag flag;

    spi_flash_read((IOTGO_PARAM_START_SEC + IOTGO_PARAM_FLAG) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)&flag, sizeof(IoTgoFlashParamFlag));

    if (flag.flag == 0) {
        spi_flash_erase_sector(IOTGO_PARAM_START_SEC + IOTGO_PARAM_SAVE_1);
        spi_flash_write((IOTGO_PARAM_START_SEC + IOTGO_PARAM_SAVE_1) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)param, sizeof(IoTgoFlashParam));
        flag.flag = 1;
        spi_flash_erase_sector(IOTGO_PARAM_START_SEC + IOTGO_PARAM_FLAG);
        spi_flash_write((IOTGO_PARAM_START_SEC + IOTGO_PARAM_FLAG) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)&flag, sizeof(IoTgoFlashParamFlag));
    } else {
        spi_flash_erase_sector(IOTGO_PARAM_START_SEC + IOTGO_PARAM_SAVE_0);
        spi_flash_write((IOTGO_PARAM_START_SEC + IOTGO_PARAM_SAVE_0) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)param, sizeof(IoTgoFlashParam));
        flag.flag = 0;
        spi_flash_erase_sector(IOTGO_PARAM_START_SEC + IOTGO_PARAM_FLAG);
        spi_flash_write((IOTGO_PARAM_START_SEC + IOTGO_PARAM_FLAG) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)&flag, sizeof(IoTgoFlashParamFlag));
    }
}


#define IOTGO_FLASH_CIPHER_SECTOR   (0xF8)


static void ICACHE_FLASH_ATTR
userLoadIoTgoFlashCipher(uint8 *param)
{
    spi_flash_read((IOTGO_FLASH_CIPHER_SECTOR) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)param, SPI_FLASH_SEC_SIZE);
}

static void ICACHE_FLASH_ATTR
userStoreIoTgoFlashCipher(uint8 *param)
{
    spi_flash_erase_sector(IOTGO_FLASH_CIPHER_SECTOR);
    spi_flash_write((IOTGO_FLASH_CIPHER_SECTOR) * SPI_FLASH_SEC_SIZE,
                    (uint32 *)param, SPI_FLASH_SEC_SIZE);
}

bool ICACHE_FLASH_ATTR iotgoFlashCipher(void)
{
    uint32 esp8266_id;
    uint32 flash_id;
    uint32 cipher;
    uint16 i;
    uint8 *psector = (uint8 *)os_zalloc(SPI_FLASH_SEC_SIZE);
    if (NULL == psector)
    {
        return false;
    }
    
    esp8266_id = system_get_chip_id();
    flash_id = spi_flash_get_id();
    cipher = esp8266_id ^ flash_id;

    for (i = 0; i < SPI_FLASH_SEC_SIZE; i++)
    {
        if (7 == i)
        {
            psector[i] = (cipher >> 24) & 0xFF;
        }
        else if (170 == i)
        {
            psector[i] = (cipher >> 16) & 0xFF;
        }
        else if (1700 == i)
        {
            psector[i] = (cipher >> 8) & 0xFF;
        }
        else if (1709 == i)
        {
            psector[i] = (cipher >> 0) & 0xFF;
        }
        else
        {
            psector[i] = rand() % 0x100;
        }
    }

    userStoreIoTgoFlashCipher(psector);
    
    os_free(psector);
    return true;
}

bool ICACHE_FLASH_ATTR iotgoFlashDecipher(void)
{
    uint32 esp8266_id;
    uint32 flash_id;
    uint32 cipher = 0;
    bool ret = false;
    
    uint8 *psector = (uint8 *)os_zalloc(SPI_FLASH_SEC_SIZE);
    if (NULL == psector)
    {
        return ret;
    }
    
    userLoadIoTgoFlashCipher(psector);
    cipher = (psector[7] << 24) 
        | (psector[170] << 16) | (psector[1700] << 8) | (psector[1709] << 0);

    esp8266_id = system_get_chip_id();
    flash_id = spi_flash_get_id();

    if ((esp8266_id == (cipher ^ flash_id))
        && (flash_id == (cipher ^ esp8266_id)))
    {
        ret = true;
    }
    
    os_free(psector);
    return ret;
}


