#ifndef __IOTGO_FLASH_H__
#define __IOTGO_FLASH_H__

#include "sdk_include.h"

#include "iotgo_type.h"

void ICACHE_FLASH_ATTR iotgoFlashLoadParam(IoTgoFlashParam *param);
void ICACHE_FLASH_ATTR iotgoFlashSaveParam(IoTgoFlashParam *param);

bool ICACHE_FLASH_ATTR iotgoFlashCipher(void);
bool ICACHE_FLASH_ATTR iotgoFlashDecipher(void);


#endif /* #ifndef __IOTGO_FLASH_H__ */
