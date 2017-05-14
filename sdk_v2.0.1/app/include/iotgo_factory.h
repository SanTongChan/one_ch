#ifndef __IOTGO_FACTORY_H__
#define __IOTGO_FACTORY_H__

#include "sdk_include.h"

#include "iotgo_global.h"
#include "user_config.h"
#include "driver/uart.h"
#include "iotgo_json.h"
#include "iotgo_flash.h"
#include "iotgo_factory.h"


void ICACHE_FLASH_ATTR startFlashDataMode(void);
void ICACHE_FLASH_ATTR stopFlashDataMode(void);

#endif /* __IOTGO_FACTORY_H__ */
