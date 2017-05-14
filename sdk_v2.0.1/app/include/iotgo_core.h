#ifndef __IOTGO_CORE_H__
#define __IOTGO_CORE_H__

#include "sdk_include.h"

#include "user_config.h"
#include "driver/uart.h"

#include "iotgo_define.h"
#include "iotgo_global.h"
#include "iotgo_flash.h"
#include "iotgo_factory.h"
#include "iotgo_setting.h"
#include "iotgo_wifi.h"
#include "iotgo_sled.h"
#include "iotgo_core.h"
#include "iotgo_device.h"
#include "iotgo_upgrade.h"
#include "iotgo_bconn.h"
#include "addition/Spro.h"

void ICACHE_FLASH_ATTR iotgoCoreRun(void);
void ICACHE_FLASH_ATTR iotgoCoreRefreshLastPkgTime(void);
uint32 ICACHE_FLASH_ATTR iotgoCoreLastPkgTime(void);
void ICACHE_FLASH_ATTR iotgoCoreHeartbeatSetMaxOffset(uint32 delay_second);
void ICACHE_FLASH_ATTR iotgoCoreHeartbeatStart(void);
void ICACHE_FLASH_ATTR iotgoCoreHeartbeatStop(void);
uint32 ICACHE_FLASH_ATTR iotgoCoreRegInterval(void);
void ICACHE_FLASH_ATTR iotgoCoreSetRegInterval(uint32 regi);

#endif /* __IOTGO_CORE_H__ */
