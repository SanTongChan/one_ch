#ifndef __IOTGO_TIMER_H__
#define __IOTGO_TIMER_H__

#include "iotgo_clock.h"

/* ���嵱ǰ��ʱ������ʱʱ������ */
typedef enum {
    IOTGO_TIME_TYPE_INVALID    = 0,
    IOTGO_TIME_TYPE_GMT        = 1,
    IOTGO_TIME_TYPE_CRON       = 2,
	IOTGO_TIME_TYPE_INTERVAL   = 3,          /*ÿ��һ��ʱ��ִ��һ��*/
	IOTGO_TIME_TYPE_INTERVAL_DURATION   = 4, /*ÿ��һ��ʱ�䣬����ִ�ж���һ��*/
	IOTGO_TIME_TYPE_INTERVAL_DURATION_START_END   = 5 /*ÿ��һ��ʱ�䣬��ʼִ�ж���1��Ȼ��ִ�ж���2����ʱ����*/
}IoTgoTimerType;

/* ��¼����ʱ��ֲ��� */
typedef struct {
	int32 minute;
	int8 second;
} IoTgoMinuteDiff;

/*����һ������ָ�����ͣ�����ָ���ض�ʱ��Ļص�����*/
typedef void (*ioTgoTimerCallback)(void *ptr);

/* ����һ����ʱ���ṹ */
typedef struct {
    IoTgoTimerType type;      /* GMT ���� CRON ���� DURATION */ 
    union 
    {
        IoTgoGMTTime gmt;       /* ���ζ�ʱ��ʱ�� */
        IoTgoCron cron;         /* �ظ���ʱ��ʱ�� */
    } timing;                   /* timing to action(call callback with ptr passed) */
	uint32 time_interval;        /* ʱ���� */
	uint32 time_duration;       /* ʱ���� */
    ioTgoTimerCallback tsch;     /* ����ָ�룬ָ��do */	
	ioTgoTimerCallback start_do; /* ����ָ�룬ָ��start_do */
	ioTgoTimerCallback end_do;   /* ����ָ�룬ָ��end_do */
	
	//char ptr[20];                  /* ��do���ݲ��� */
	char *ptr;
	char *p_start_do;			/* ��start_do���ݲ��� */
	char *p_end_do;				/* ��end_do ���ݲ��� */

	bool enabled ;               /* ʹ�ܵ�ǰ��ʱ��*/
    bool used;                  /* ��ǵ�ǰ��ʱ�������Ƿ�ʹ�� */
    os_timer_t interval_timer;
    uint8 second_count ;
    uint32 minute_count;
    uint8 time_compare_flag;
    IoTgoMinuteDiff diff_timer;
}IoTgoTimeObject;


/* ������ʱ����� */
void ICACHE_FLASH_ATTR startTimerMonitor(void);

/* �رն�ʱ����� */
void ICACHE_FLASH_ATTR stopTimerMonitor(void);

/* ���һ��GMT���Ͷ�ʱ�������� */	
int16_t ICACHE_FLASH_ATTR addTimeObjectByGMTString(bool enabled,const char *gmt_str, ioTgoTimerCallback tsch, void *ptr,uint8 ptr_size);

/* ���һ��CRON���͵Ķ�ʱ������ */
int16_t ICACHE_FLASH_ATTR addTimeObjectByCronString(bool enabled,const char *cron_str, ioTgoTimerCallback tsch, void *ptr,uint8 ptr_size);

/* ���һ��INTERVAL���͵Ķ�ʱ������ */
int16_t ICACHE_FLASH_ATTR addTimeObjectInterval(bool enabled,const char *gmt_str,uint32 interval ,ioTgoTimerCallback tsch,void *ptr,uint8 ptr_size);

/* ���һ��INTERVAL_DURATION���͵Ķ�ʱ������ */
int16_t ICACHE_FLASH_ATTR addTimeObjectIntervalDuration(bool enabled,const char *gmt_str,uint32 interval,uint32 duration,ioTgoTimerCallback tsch,void *ptr,uint8 ptr_size);	

/* ���һ��INTERVAL_DURATION_START_END���͵Ķ�ʱ������ */
int16_t ICACHE_FLASH_ATTR addTimeObjectDurationStartEnd(bool enabled,const char *gmt_str,uint32 interval,uint32 duration, ioTgoTimerCallback start_do, 
	void *p_start_do,uint8 p_start_do_size,ioTgoTimerCallback end_do,void *p_end_do,uint8 p_end_do_size);	

/* ɾ�����еĶ�ʱ������ */
bool ICACHE_FLASH_ATTR deleteAllTimeObject(void);

#endif 

