#ifndef __IOTGO_TIMER_H__
#define __IOTGO_TIMER_H__

#include "iotgo_clock.h"

/* 定义当前定时器，定时时间类型 */
typedef enum {
    IOTGO_TIME_TYPE_INVALID    = 0,
    IOTGO_TIME_TYPE_GMT        = 1,
    IOTGO_TIME_TYPE_CRON       = 2,
	IOTGO_TIME_TYPE_INTERVAL   = 3,          /*每隔一段时间执行一次*/
	IOTGO_TIME_TYPE_INTERVAL_DURATION   = 4, /*每隔一段时间，持续执行动作一次*/
	IOTGO_TIME_TYPE_INTERVAL_DURATION_START_END   = 5 /*每隔一段时间，开始执行动作1，然后执行动作2，有时间间隔*/
}IoTgoTimerType;

/* 记录两个时间分差，秒差 */
typedef struct {
	int32 minute;
	int8 second;
} IoTgoMinuteDiff;

/*定义一个函数指针类型，用来指向特定时间的回调函数*/
typedef void (*ioTgoTimerCallback)(void *ptr);

/* 描述一个定时器结构 */
typedef struct {
    IoTgoTimerType type;      /* GMT 或者 CRON 或者 DURATION */ 
    union 
    {
        IoTgoGMTTime gmt;       /* 单次定时，时间 */
        IoTgoCron cron;         /* 重复定时，时间 */
    } timing;                   /* timing to action(call callback with ptr passed) */
	uint32 time_interval;        /* 时间间隔 */
	uint32 time_duration;       /* 时间宽度 */
    ioTgoTimerCallback tsch;     /* 函数指针，指向do */	
	ioTgoTimerCallback start_do; /* 函数指针，指向start_do */
	ioTgoTimerCallback end_do;   /* 函数指针，指向end_do */
	
	//char ptr[20];                  /* 给do传递参数 */
	char *ptr;
	char *p_start_do;			/* 给start_do传递参数 */
	char *p_end_do;				/* 给end_do 传递参数 */

	bool enabled ;               /* 使能当前定时器*/
    bool used;                  /* 标记当前定时器任务是否被使用 */
    os_timer_t interval_timer;
    uint8 second_count ;
    uint32 minute_count;
    uint8 time_compare_flag;
    IoTgoMinuteDiff diff_timer;
}IoTgoTimeObject;


/* 启动定时器监控 */
void ICACHE_FLASH_ATTR startTimerMonitor(void);

/* 关闭定时器监控 */
void ICACHE_FLASH_ATTR stopTimerMonitor(void);

/* 添加一个GMT类型定时器的任务 */	
int16_t ICACHE_FLASH_ATTR addTimeObjectByGMTString(bool enabled,const char *gmt_str, ioTgoTimerCallback tsch, void *ptr,uint8 ptr_size);

/* 添加一个CRON类型的定时器任务 */
int16_t ICACHE_FLASH_ATTR addTimeObjectByCronString(bool enabled,const char *cron_str, ioTgoTimerCallback tsch, void *ptr,uint8 ptr_size);

/* 添加一个INTERVAL类型的定时器任务 */
int16_t ICACHE_FLASH_ATTR addTimeObjectInterval(bool enabled,const char *gmt_str,uint32 interval ,ioTgoTimerCallback tsch,void *ptr,uint8 ptr_size);

/* 添加一个INTERVAL_DURATION类型的定时器任务 */
int16_t ICACHE_FLASH_ATTR addTimeObjectIntervalDuration(bool enabled,const char *gmt_str,uint32 interval,uint32 duration,ioTgoTimerCallback tsch,void *ptr,uint8 ptr_size);	

/* 添加一个INTERVAL_DURATION_START_END类型的定时器任务 */
int16_t ICACHE_FLASH_ATTR addTimeObjectDurationStartEnd(bool enabled,const char *gmt_str,uint32 interval,uint32 duration, ioTgoTimerCallback start_do, 
	void *p_start_do,uint8 p_start_do_size,ioTgoTimerCallback end_do,void *p_end_do,uint8 p_end_do_size);	

/* 删除所有的定时器对象 */
bool ICACHE_FLASH_ATTR deleteAllTimeObject(void);

#endif 

