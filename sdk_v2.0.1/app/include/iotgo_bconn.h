#ifndef __IOTGO_BCONN_H__
#define __IOTGO_BCONN_H__

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
#include "addition/Spro.h"

#define IOTGO_DISTRIBUTOR_RESULT_OK             (0x01)
#define IOTGO_DISTRIBUTOR_RESULT_ERR_DNS        (0x02)
#define IOTGO_DISTRIBUTOR_RESULT_ERR_CONN       (0x03)
#define IOTGO_DISTRIBUTOR_RESULT_ERR_RESP       (0x04)
#define IOTGO_DISTRIBUTOR_RESULT_ERR_REQ        (0x05)
#define IOTGO_DISTRIBUTOR_RESULT_ERR_TIMEOUT    (0x06)

void ICACHE_FLASH_ATTR startDistorRequest(void);
void ICACHE_FLASH_ATTR stopDistorRequest(void);
IoTgoHostInfo ICACHE_FLASH_ATTR distorGetServerInfo(void);
void ICACHE_FLASH_ATTR distorSetServerInfo(const char *host, int32 port);

#endif /* #ifndef __IOTGO_BCONN_H__ */
