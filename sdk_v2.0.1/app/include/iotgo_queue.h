#ifndef __IOTGO_QUEUE_H__
#define __IOTGO_QUEUE_H__

#include "sdk_include.h"
#include "iotgo_debug.h"

typedef enum 
{
    IOTGO_NULL  = 0,  
    IOTGO_SWITCH      = 1 ,
    IOTGO_ERROR0      = 2 ,
    IOTGO_PING        = 3 ,
    IOTGO_UPGRADE     = 4 ,
    IOTGO_SWITCH_VERSION  = 5 ,
    IOTGO_QUERY       = 6 ,
    IOTGO_DATE        = 7 ,
    IOTGO_REGISTER        = 8 ,
    IOTGO_UPDATE_TEMP_HUMI = 9,
    IOTGO_QUERY_TIMER      = 10, //用于定时器合并
    IOTGO_UPDATE_TIMER     = 11,
    IOTGO_USER_DEFAULT  = 97 ,
    IOTGO_USER_QUERY    = 98 ,
    IOTGO_USER_UPDATE_RESULT = 99,
    IOTGO_USER_QUERY_RESULT  = 100,
} IoTgoDateType;

typedef struct dataNode  
{  
    char *data;
    struct dataNode *next;  
    uint8_t type;
}qNode;  
  
typedef struct linkQueue  
{  
    qNode *head;  
    qNode *tail;  
    uint8_t length;
}linkQueue;  

const char* ICACHE_FLASH_ATTR iotgoQueueHeadData(void);
uint8_t ICACHE_FLASH_ATTR iotgoQueueHeadType(void);
uint8_t ICACHE_FLASH_ATTR iotgoQueueLength(void);
bool ICACHE_FLASH_ATTR iotgoQueueDeleteHead(void);
bool ICACHE_FLASH_ATTR iotgoQueueDeleteAll(void);
bool ICACHE_FLASH_ATTR iotgoQueueAdd(char *data,uint8_t type,bool del_type_flag);

#endif

