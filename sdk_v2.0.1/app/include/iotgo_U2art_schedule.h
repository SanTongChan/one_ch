#ifndef __IOTGO_U2ART_SCHEDULE_H__
#define __IOTGO_U2ART_SCHEDULE_H__

#include "sdk_include.h"

#define TIMEROUT_TIME  (50)

typedef enum{
    U2ART_NULL              = 0,
    U2ART_SUCCESS           = 1,
    U2ART_NO_SUPPORT        = 100,
    U2ART_FORMAT_ERROR      = 101,
    U2ART_INSIDE_ERROR      = 102,
    U2ART_INVALID_PARAMS    = 103,
    U2ART_ILLEGAL_OPERATION = 104,
    U2ART_CRC32_ERROR       = 105,
    U2ART_BUSY_ERROR        = 106,
    U2ART_GMT_ERROR         = 201,
    U2ART_NETWORK_ERROR     = 202,
}U2artError;

typedef U2artError (*cmdPressorAction)(void *arg);
typedef struct{
    char cmd[20];
    cmdPressorAction action;
}CMD;
void cmdProcessorStart(CMD *cmd,uint8_t length);
bool cmdSendRetToMcu(char *data,uint32_t length);
bool cmdSendDataToMcu(char *data,uint32_t length);
#endif


