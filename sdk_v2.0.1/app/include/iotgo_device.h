#ifndef __IOTGO_DEVICE_H__
#define __IOTGO_DEVICE_H__

#include "sdk_include.h"

#include "user_config.h"
#include "iotgo_type.h"
#include "iotgo_define.h"
#include "iotgo_global.h"
#include "driver/uart.h"
#include "iotgo_json.h"
#include "iotgo_clock.h"
#include "iotgo_timer.h"

void ICACHE_FLASH_ATTR iotgoRegisterCallbackSet(IoTgoDevice *device);

#endif /* __IOTGO_DEVICE_H__ */
