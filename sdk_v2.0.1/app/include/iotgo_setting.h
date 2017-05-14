#ifndef __IOTGO_SETTING_H__
#define __IOTGO_SETTING_H__

#include "sdk_include.h"

#include "user_config.h"
#include "iotgo_type.h"
#include "iotgo_define.h"
#include "iotgo_global.h"
#include "driver/uart.h"
#include "iotgo_json.h"
#include "iotgo_flash.h"
#include "addition/Spro.h"

void ICACHE_FLASH_ATTR iotgoSettingServerStart(void);
void ICACHE_FLASH_ATTR iotgoSettingServerStop(void);
void ICACHE_FLASH_ATTR iotgoSettingSendDeviceInfoToApp(void);
void ICACHE_FLASH_ATTR iotgoSettingSendRespToApp(uint32 par);
void ICACHE_FLASH_ATTR iotgoSettingTimeoutMonitorStart(void);
void ICACHE_FLASH_ATTR iotgoSettingTimeoutMonitorStop(void);
void ICACHE_FLASH_ATTR iotgoSettingSmartConfigStart(void);
void ICACHE_FLASH_ATTR iotgoSettingSmartConfigStop(void);


#endif /* __IOTGO_SETTING_H__ */
