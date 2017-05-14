#ifndef __IOTGO_PCONN_H__
#define __IOTGO_PCONN_H__

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

void ICACHE_FLASH_ATTR iotgoPconnInit(void);
void ICACHE_FLASH_ATTR iotgoPconnDisconnect(void);
void ICACHE_FLASH_ATTR iotgoPconnConnect(IoTgoHostInfo host_info);
void ICACHE_FLASH_ATTR iotgoPconnSwitchToWebSocket(void);
void ICACHE_FLASH_ATTR iotgoPconnDeviceRegister(void);
void ICACHE_FLASH_ATTR iotgoPconnDeviceDate(void);
void ICACHE_FLASH_ATTR iotgoPconnSendJson(void);
void ICACHE_FLASH_ATTR iotgoPconnSendJsonAgain(void);
void ICACHE_FLASH_ATTR iotgoPconnSendNextJson(uint32 error_code);
void ICACHE_FLASH_ATTR iotgoPconnRespondErrorCode(int32_t err_code, const char *seq);

//add by czp
int8 ICACHE_FLASH_ATTR sendIoTgoPkg(const char *data_to_send);

#endif /* #ifndef __IOTGO_PCONN_H__ */
