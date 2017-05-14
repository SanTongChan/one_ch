#ifndef __UART0_SEND_DATA_QUENE__
#define __UART0_SEND_DATA_QUENE__

#include "sdk_include.h"

typedef void (*tcQueueAction)(void *arg);

typedef enum 
{
    TC_NULL  = 0,  
    TC_SWITCH      = 1 ,
    TC_ERROR0      = 2 ,
    TC_PING        = 3 ,
    TC_UPGRADE     = 4 ,
    TC_SWITCH_VERSION  = 5 ,
    TC_QUERY       = 6 ,
    TC_DATE        = 7 ,
    TC_REGISTER        = 8 ,
    TC_UPDATE_TEMP_HUMI = 9,
    TC_QUERY_TIMER      = 10,//用于APP查询本地定时器
    TC_USER_DEFAULT  = 97 ,
    TC_USER_QUERY    = 98 ,
    TC_USER_UPDATE_RESULT = 99,
    TC_USER_QUERY_RESULT  = 100,
} tcDateType;

typedef struct _dataNode  
{  
    char *data;
    struct _dataNode *next;  
    uint8_t type;
}tcQueueNode;  
  
typedef struct _tcLinkQueue  
{  
    tcQueueNode *head;  
    tcQueueNode *tail;  
    uint8_t length;
}tcLinkQueue;  

void ICACHE_FLASH_ATTR tcCreateLinkQueue(tcLinkQueue *q,tcQueueAction action);
bool ICACHE_FLASH_ATTR tcDeleteNode(tcLinkQueue *q);
bool ICACHE_FLASH_ATTR tcClearQueue(tcLinkQueue *q);
bool ICACHE_FLASH_ATTR tcAddDataToQueue(tcLinkQueue *q,char *data,uint8_t type,bool del_type_flag);

#endif

