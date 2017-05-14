#ifndef __IOTGO_CLOCK_H__
#define __IOTGO_CLOCK_H__

#include "sdk_include.h"

#include "user_config.h"
#include "driver/uart.h"
#include "iotgo_debug.h"
#include "iotgo_type.h"


int8 ICACHE_FLASH_ATTR getWeek(int32 y, int8 m, int8 d);
void ICACHE_FLASH_ATTR getGMTTime(IoTgoGMTTime *gmt_time);
void ICACHE_FLASH_ATTR setGMTTime(IoTgoGMTTime gmt_time);
void ICACHE_FLASH_ATTR startTimeService(void);
void ICACHE_FLASH_ATTR printGMTTime(IoTgoGMTTime *gmt_time);
void ICACHE_FLASH_ATTR printCron(const IoTgoDeviceCron *cron);
bool ICACHE_FLASH_ATTR parseCronFromString(const char *str, IoTgoDeviceCron *cron);
bool ICACHE_FLASH_ATTR parseGMTTimeFromString(const char *str, IoTgoGMTTime *gmt_time);
int8 ICACHE_FLASH_ATTR daysOfMonth(int32 year, int8 month);


#endif /* #ifndef __IOTGO_CLOCK_H__ */
