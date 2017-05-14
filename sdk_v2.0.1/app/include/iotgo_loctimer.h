#ifndef _IOTGO_LOCTIMER_H
#define _IOTGO_LOCTIMER_H

#include "iotgo_global.h"

#define IOTGO_TIMER_NUM    (8)
typedef void(*IotgoTimerCallback)(void *ptr);

typedef enum{
    TIMER_NULL = 0,
    TIMER_MCU = 1,
    TIMER_APP = 2,
}TimerManage;
typedef struct{
    uint32_t timer_ver;
    uint8_t timer_cnt;
}iotgoTimerInfo;

void ICACHE_FLASH_ATTR timerInit(IotgoTimerCallback timer_callback);
bool ICACHE_FLASH_ATTR timerProcUpdate(cJSON *cjson_params,TimerManage manager);
bool ICACHE_FLASH_ATTR timerProcQuery(cJSON *cjson_params,TimerManage manager);
void timerFlashErase(void);
iotgoTimerInfo ICACHE_FLASH_ATTR getIotgoTimerInfo(void);

#endif
