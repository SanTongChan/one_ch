#ifndef __IOTGO_WIFI_H__
#define __IOTGO_WIFI_H__

#include "sdk_include.h"

#include "user_config.h"
#include "iotgo_define.h"
#include "iotgo_global.h"
#include "driver/uart.h"
#include "iotgo_core.h"

void ICACHE_FLASH_ATTR iotgoWifiInit(void);
void ICACHE_FLASH_ATTR iotgoWifiJoin(void);
void ICACHE_FLASH_ATTR iotgoWifiLeave(void);
void ICACHE_FLASH_ATTR iotgoWifiToStationAndApMode(void);
void ICACHE_FLASH_ATTR iotgoWifiToStationMode(void);

#endif /* __IOTGO_WIFI_H__ */
