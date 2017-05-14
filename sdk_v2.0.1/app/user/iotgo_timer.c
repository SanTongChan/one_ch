#include "iotgo_timer.h"
#include "sdk_include.h"

/* 支持的定时器个数 */
#define IOTGO_DEVICE_TIMER_MAX (10)

#define MONITOR_REFRESH_TIME  (1000)
#define INTERVAL_REFRESH_TIME (1000)

#define TIMER_REPAET    (1)
#define TIMER_NO_REPEAT (0)

/* 默认时间基数是2015年 1 月 1 日 0 时 0 分 0 秒*/
IoTgoGMTTime know_date ={2015,1,1,0,0,0};
#define KNOW_DATE know_date

/* 维护一个定时器任务列表 */
static IoTgoTimeObject time_object_list[IOTGO_DEVICE_TIMER_MAX] = {0};
static uint8 time_object_index = 0;
static uint8 time_object_sum = 0;

static os_timer_t timer_monitor;

static IoTgoMinuteDiff ICACHE_FLASH_ATTR minuteDiffFromKnownYear(IoTgoGMTTime time)
{
	IoTgoMinuteDiff temp;
	uint32 year = 0;
	uint32 count = 0;
	uint32 day_sum = 0;

	if(time.year>KNOW_DATE.year)
	{
		for(year = KNOW_DATE.year ; year<time.year ; year++)
		{
			for(count=1;count<=12;count++)
			{
				day_sum+=daysOfMonth(year,count);
			}
		}

		for(count=1;count<time.month;count++)
		{
			day_sum+=daysOfMonth(year,count);
		}
	}

	if(time.year==KNOW_DATE.year)
	{

		for(year = KNOW_DATE.year ; year<=time.year ; year++)
		{
			for(count=1;count<time.month;count++)
			{
				day_sum+=daysOfMonth(year,count);
			}

		}

	}

	day_sum += (time.day-1);
	temp.minute = day_sum * 24 * 60 + time.hour * 60 + time.minute;
	temp.second = time.second;
	return temp;
}

static IoTgoMinuteDiff ICACHE_FLASH_ATTR minuteDiff(IoTgoGMTTime time1,IoTgoGMTTime time2)
{
    IoTgoMinuteDiff temp1,temp2;
    uint32 val = 0;
    uint32 val_temp1 = 0;
    uint32 val_temp2 = 0;
    temp1 = minuteDiffFromKnownYear(time1);
    temp2 = minuteDiffFromKnownYear(time2);
    val_temp1 = temp1.minute * 60 + temp1.second;
    val_temp2 = temp2.minute * 60 + temp2.second;
    if(val_temp2 > val_temp1)
    {
        temp1.minute = -1;
        temp1.second = -1;
    }
    else
    {
        val = val_temp1 - val_temp2;
        temp1.minute = val / 60;
        temp1.second = val % 60;
    }
    return temp1;
}



static bool ICACHE_FLASH_ATTR deleteOneTimeObject(uint8 time_object_index)
{
    bool ret = false;
         
    if (time_object_index >= IOTGO_DEVICE_TIMER_MAX || time_object_index < 0)
    {
        iotgoWarn("Invalid parameter time_object_index = %d", time_object_index);
        return false;
    }
    
    if(time_object_list[time_object_index].used)
    {
        time_object_list[time_object_index].used = false;
        time_object_list[time_object_index].diff_timer.minute = -1;
        time_object_list[time_object_index].diff_timer.second = -1;
        time_object_list[time_object_index].time_compare_flag = 0;
        os_timer_disarm(&time_object_list[time_object_index].interval_timer);
        if(time_object_list[time_object_index].type != IOTGO_TIME_TYPE_INTERVAL_DURATION_START_END)
        {
            os_free(time_object_list[time_object_index].ptr);
            time_object_list[time_object_index].ptr = NULL;
            iotgoInfo("delete prt now");
        }
        else
        {
            os_free(time_object_list[time_object_index].p_start_do);
            time_object_list[time_object_index].p_start_do= NULL;
            os_free(time_object_list[time_object_index].p_end_do);
            time_object_list[time_object_index].p_end_do= NULL;
            iotgoInfo("delete p_start_do and p_end_do now");
        }
        os_bzero(&time_object_list[time_object_index],sizeof(time_object_list[time_object_index]));
        iotgoInfo("Delete time object index %d ok", time_object_index);
        return true;
    }
}

bool ICACHE_FLASH_ATTR deleteAllTimeObject(void)
{
    uint8 index;
    for (index = 0; index < IOTGO_DEVICE_TIMER_MAX; index++)
    {   
        deleteOneTimeObject(index);
    }
    iotgoInfo("delete all time object ok");
    return true;
}

static void ICACHE_FLASH_ATTR intervalTimerCallback(void *p_object_temp)
{
    IoTgoTimeObject *p_object = (IoTgoTimeObject*)p_object_temp;
    p_object->second_count++;
    if(p_object->second_count == 60)
    {
        p_object->second_count = 0;
        p_object->minute_count++;
        if(p_object->minute_count == p_object->time_interval)
        {
            p_object->second_count = 0;
            p_object->minute_count = 0;
            if(p_object->tsch)
            {
                p_object->tsch(p_object->ptr);
            }
        }
    } 

};

static void ICACHE_FLASH_ATTR durationTimerCallback(void *p_object_temp)
{
    IoTgoTimeObject *p_object = (IoTgoTimeObject*)p_object_temp;    
    p_object->second_count++;
    if(p_object->second_count == 60)
    {
        p_object->second_count = 0;
        p_object->minute_count++;
        if(p_object->minute_count == p_object->time_duration)
        {
        /* do nothing */

        }
        else if(p_object->minute_count == p_object->time_interval)
        {
            p_object->second_count = 0;
            p_object->minute_count = 0;
            if(p_object->tsch)
            {
                p_object->tsch(p_object->ptr);
            }
        }
    } 
   
};

static void ICACHE_FLASH_ATTR durationStartEndTimerCallback(void *p_object_temp)
{
    IoTgoTimeObject *p_object = (IoTgoTimeObject*)p_object_temp;
    p_object->second_count++;
    if(p_object->second_count == 60)
    {
        p_object->second_count = 0;
        p_object->minute_count++;
        if(p_object->minute_count == p_object->time_duration)
        {
            iotgoInfo("p_object->time_duration == [%d]",p_object->time_duration);
            if(p_object->end_do)
            {
                p_object->end_do(p_object->p_end_do); 
            }
        }
        else if(p_object->minute_count == p_object->time_interval)
        {
            iotgoInfo("p_object->time_interval == [%d]",p_object->time_interval);
            p_object->second_count = 0;
            p_object->minute_count = 0;
            if(p_object->start_do)
            {
                p_object->start_do(p_object->p_start_do);
            }
        }
    } 
    
};

static int16_t ICACHE_FLASH_ATTR addOneTimeObject(IoTgoTimeObject time_object)
{
    int16_t index;
    int16_t ret = -1;

    for (index = 0; index < IOTGO_DEVICE_TIMER_MAX; index++)
    {
        if (!time_object_list[index].used)
        {
            time_object_list[index].diff_timer.minute = -1;
            time_object_list[index].diff_timer.second = -1;
            time_object_list[index] = time_object;
            time_object_list[index].used = true;
            time_object_list[index].time_compare_flag = 0;
            ret = index;
            iotgoInfo("add time object index %d ok\n", ret);
            break;
        }
    }
    
    if (-1 == ret)
    {
        iotgoWarn("add time object err");
    }
    return ret;
}
	
int16_t ICACHE_FLASH_ATTR addTimeObjectByGMTString(bool enabled,const char *gmt_str, ioTgoTimerCallback tsch, void *ptr,uint8 ptr_size)
{
    IoTgoTimeObject time_obj;
    int16_t ret = -1;
    if (!gmt_str || !tsch)
    {
        iotgoWarn("Invalid parameter!");
        return -1;
    }
    
    if (!parseGMTTimeFromString(gmt_str, &time_obj.timing.gmt))
    {
        iotgoWarn("parseGMTTimeFromString err!");
        return -1;
    }
        
    time_obj.type = IOTGO_TIME_TYPE_GMT;
    time_obj.tsch = tsch;
    time_obj.ptr = (char *)os_malloc(ptr_size + 1);
    if(NULL == time_obj.ptr)
    {
        iotgoError("malloc time_obj is error,please check memory is sufficient");
        return -1;
    }
    os_bzero(time_obj.ptr,ptr_size + 1);
    os_memcpy(time_obj.ptr,ptr,ptr_size);
    time_obj.enabled = enabled;   
    iotgoInfo("time_obj.enabled[%d],time_obj.type[%d],time_obj.ptr[%s]",time_obj.enabled,time_obj.type,time_obj.ptr); 
    ret = addOneTimeObject(time_obj);
    if(-1 == ret)
    {
        os_free(time_obj.ptr);
    }
    return ret;

}

int16_t ICACHE_FLASH_ATTR addTimeObjectByCronString(bool enabled,const char *cron_str, ioTgoTimerCallback tsch, void *ptr,uint8 ptr_size)
{
    IoTgoTimeObject time_obj;
    int16_t ret = -1;
    if (!cron_str || !tsch)
    {
        iotgoWarn("Invalid parameter!");
        return -1;
    }
    
    if (!parseCronFromString(cron_str, &time_obj.timing.cron))
    {
        iotgoWarn("parseCronFromString err!");
        return -1;
    }
        
    time_obj.type = IOTGO_TIME_TYPE_CRON;
    time_obj.tsch = tsch;
    time_obj.ptr = (char *)os_malloc(ptr_size + 1);
    if(NULL == time_obj.ptr)
    {
        iotgoError("malloc time_obj is error,please check memory is sufficient");
        return -1;
    }
    os_bzero(time_obj.ptr,ptr_size + 1);
    os_memcpy(time_obj.ptr,ptr,ptr_size);
    time_obj.enabled = enabled;    

    ret = addOneTimeObject(time_obj);
    if(-1 == ret)
    {
        os_free(time_obj.ptr);
    }
    return ret;

}

int16_t ICACHE_FLASH_ATTR addTimeObjectInterval(bool enabled,const char *gmt_str,uint32 interval ,ioTgoTimerCallback tsch,void *ptr,uint8 ptr_size)
{
    IoTgoTimeObject time_obj;
    int16_t ret = -1;
    if (!gmt_str || !tsch || (interval < 0))
    {
        iotgoWarn("Invalid parameter!");
        return -1;
    }
    
    if (!parseGMTTimeFromString(gmt_str, &time_obj.timing.gmt))
    {
        iotgoWarn("parseGMTTimeFromString err!");
        return -1;
    }
        
    time_obj.type = IOTGO_TIME_TYPE_INTERVAL;
    time_obj.time_interval = interval; 
    time_obj.tsch = tsch;
    time_obj.ptr = (char *)os_malloc(ptr_size + 1);
    if(NULL == time_obj.ptr)
    {
        iotgoError("malloc time_obj is error,please check memory is sufficient");
        return -1;
    }
    os_bzero(time_obj.ptr,ptr_size + 1);
    os_memcpy(time_obj.ptr,ptr,ptr_size);
    time_obj.enabled = enabled;
    time_obj.second_count = 0;
    time_obj.minute_count = 0;
      
    ret = addOneTimeObject(time_obj);
    if(-1 == ret)
    {
        os_free(time_obj.ptr);
    }
    return ret;
}

int16_t ICACHE_FLASH_ATTR addTimeObjectIntervalDuration(bool enabled,const char *gmt_str,uint32 interval,uint32 duration,ioTgoTimerCallback tsch,void *ptr,uint8 ptr_size)	
{
    IoTgoTimeObject time_obj;
    int16_t ret = -1;
    if (!gmt_str || !tsch|| (interval <= duration))
    {
        iotgoWarn("Invalid parameter!");
        return -1;
    }
    
    if (!parseGMTTimeFromString(gmt_str, &time_obj.timing.gmt))
    {
        iotgoWarn("parseGMTTimeFromString err!");
        return -1;
    }
        
    time_obj.type = IOTGO_TIME_TYPE_INTERVAL_DURATION;
    time_obj.time_interval = interval;
    time_obj.time_duration = duration;
    time_obj.tsch = tsch;
    time_obj.ptr = (char *)os_malloc(ptr_size + 1);
    if(NULL == time_obj.ptr)
    {
        iotgoError("malloc time_obj is error,please check memory is sufficient");
        return -1;
    }
    os_bzero(time_obj.ptr,ptr_size + 1);
    os_memcpy(time_obj.ptr,ptr,ptr_size);
    time_obj.enabled = enabled;
    time_obj.second_count = 0;
    time_obj.minute_count = 0;
        
    ret = addOneTimeObject(time_obj);
    if(-1 == ret)
    {
        os_free(time_obj.ptr);
    }
    return ret;

}

int16_t ICACHE_FLASH_ATTR addTimeObjectDurationStartEnd(bool enabled,const char *gmt_str,uint32 interval,uint32 duration, ioTgoTimerCallback start_do, 
	void *p_start_do,uint8 p_start_do_size,ioTgoTimerCallback end_do,void *p_end_do,uint8 p_end_do_size)

{
        IoTgoTimeObject time_obj;
        int16_t ret = -1;
        if (!gmt_str || !start_do || !end_do || (interval <= duration))
        {
            iotgoWarn("Invalid parameter!");
            return -1;
        }
        
        if (!parseGMTTimeFromString(gmt_str, &time_obj.timing.gmt))
        {
            iotgoWarn("parseGMTTimeFromString err!");
            return -1;
        }
            
        time_obj.type = IOTGO_TIME_TYPE_INTERVAL_DURATION_START_END;
        time_obj.time_interval = interval;
        time_obj.time_duration = duration;
        time_obj.start_do = start_do;
        time_obj.p_start_do = (char *)os_malloc(p_start_do_size + 1);
        if(NULL == time_obj.p_start_do)
        {
            iotgoError("malloc time_obj is error,please check memory is sufficient");
            return -1;
        }
        os_bzero(time_obj.p_start_do, p_start_do_size + 1);
        os_memcpy(time_obj.p_start_do,p_start_do,p_start_do_size);
        time_obj.end_do = end_do;
        time_obj.p_end_do = (char *)os_malloc(p_end_do_size + 1);
        if(NULL == time_obj.p_end_do)
        {
            iotgoError("malloc time_obj is error,please check memory is sufficient");
            os_free(time_obj.p_start_do);
            return -1;
        }
        os_bzero(time_obj.p_end_do, p_end_do_size + 1);        
        os_memcpy(time_obj.p_end_do,p_end_do,p_end_do_size);
        time_obj.enabled = enabled;
        time_obj.second_count = 0;
        time_obj.minute_count = 0;   
        
        ret = addOneTimeObject(time_obj);
        if(-1 == ret)
        {
            os_free(time_obj.p_start_do);
            os_free(time_obj.p_end_do);
        }
        return ret;
}

static void ICACHE_FLASH_ATTR timerMonitorCallback(void *arg)
{
    uint8 timer_count ;
    int8 week;
    IoTgoGMTTime gmt;
    IoTgoCron cron;
    IoTgoGMTTime cur_gmt;


    /* Ask every timer object in timer list */
    for(timer_count = 0;timer_count < IOTGO_DEVICE_TIMER_MAX;timer_count++)
    {
        if((!time_object_list[timer_count].used)||(!time_object_list[timer_count].enabled))
        {
            continue;
        }          
        switch(time_object_list[timer_count].type)
        {       
            case IOTGO_TIME_TYPE_GMT:
            {
                getGMTTime(&cur_gmt);
                gmt = time_object_list[timer_count].timing.gmt;
                if (gmt.year == cur_gmt.year && gmt.month == cur_gmt.month
                && gmt.day == cur_gmt.day && gmt.hour == cur_gmt.hour
                && gmt.minute == cur_gmt.minute
                && cur_gmt.second == 0)
                {
                    iotgoInfo("time_object_list[%d].ptr == %s",timer_count,time_object_list[timer_count].ptr);
                    time_object_list[timer_count].tsch(time_object_list[timer_count].ptr);

                    /* once timing, auto delete it! */
                    if(deleteOneTimeObject(timer_count))
                    {
                        iotgoInfo("delete time object [%u] ok",timer_count);    
                    }
                    else
                    {
                        iotgoInfo("delete time object [%u] failed",timer_count); 

                    }
                }
            }           
            break;
                    
            case IOTGO_TIME_TYPE_CRON:
            {
                getGMTTime(&cur_gmt);
                week = getWeek(cur_gmt.year, cur_gmt.month, cur_gmt.day);
                cron = time_object_list[timer_count].timing.cron;
                if (cron.weeks[week] == true 
                && (cron.hour == cur_gmt.hour || cron.hour == 24) 
                && cron.minute == cur_gmt.minute 
                && cur_gmt.second == 0
                )
                {
                    time_object_list[timer_count].tsch(time_object_list[timer_count].ptr); 
                }
            }
            break;
                    
            case IOTGO_TIME_TYPE_INTERVAL:
            {
                getGMTTime(&cur_gmt);
                gmt = time_object_list[timer_count].timing.gmt;
                time_object_list[timer_count].diff_timer = minuteDiff(cur_gmt,gmt);
                iotgoInfo("minutes = %d,sendcond = %d, interval = %d",
                time_object_list[timer_count].diff_timer.minute,
                time_object_list[timer_count].diff_timer.second,
                time_object_list[timer_count].time_interval);
                if((time_object_list[timer_count].diff_timer.minute%time_object_list[timer_count].time_interval) == 0 
                    && (time_object_list[timer_count].diff_timer.second == 0))
                {
                    time_object_list[timer_count].tsch(time_object_list[timer_count].ptr); 
                }
            #if 0
                getGMTTime(&cur_gmt);
                gmt = time_object_list[timer_count].timing.gmt;
                if (gmt.year == cur_gmt.year && gmt.month == cur_gmt.month
                && gmt.day == cur_gmt.day && gmt.hour == cur_gmt.hour
                && gmt.minute == cur_gmt.minute
                && gmt.second == cur_gmt.second)
                {   
                    /* 先执行当前的命令函数，然后开始计时 interval */
                    time_object_list[timer_count].tsch(time_object_list[timer_count].ptr); 
                    os_timer_disarm(&time_object_list[timer_count].interval_timer);
                    os_timer_setfn(&time_object_list[timer_count].interval_timer,(os_timer_func_t *)intervalTimerCallback,(void*)(&time_object_list[timer_count]));
                    os_timer_arm(&time_object_list[timer_count].interval_timer, INTERVAL_REFRESH_TIME, TIMER_REPAET);     
                }
            #endif
            }
            break;
                    
            case IOTGO_TIME_TYPE_INTERVAL_DURATION:
            {
                getGMTTime(&cur_gmt);
                gmt = time_object_list[timer_count].timing.gmt;
                time_object_list[timer_count].diff_timer = minuteDiff(cur_gmt,gmt);
                iotgoInfo("minutes = %d,sendcond = %d, interval = %d",
                time_object_list[timer_count].diff_timer.minute,
                time_object_list[timer_count].diff_timer.second,
                time_object_list[timer_count].time_interval);
                if((time_object_list[timer_count].diff_timer.minute%time_object_list[timer_count].time_interval) == 0 
                    && (time_object_list[timer_count].diff_timer.second == 0))
                {
                    time_object_list[timer_count].tsch(time_object_list[timer_count].ptr); 
                }
                else if(((time_object_list[timer_count].diff_timer.minute 
                 % time_object_list[timer_count].time_interval)
                 % time_object_list[timer_count].time_duration) == 0 
                 && (time_object_list[timer_count].diff_timer.second == 0))
                {
                    /*do nothing*/
                }
                #if 0
                getGMTTime(&cur_gmt);
                gmt = time_object_list[timer_count].timing.gmt;
                if (gmt.year == cur_gmt.year && gmt.month == cur_gmt.month
                && gmt.day == cur_gmt.day && gmt.hour == cur_gmt.hour
                && gmt.minute == cur_gmt.minute
                && gmt.second == cur_gmt.second)
                {   
                    /* 先执行当前的命令函数，然后开始计时 interval */
                    time_object_list[timer_count].tsch(time_object_list[timer_count].ptr); 
                    os_timer_disarm(&time_object_list[timer_count].interval_timer);
                    os_timer_setfn(&time_object_list[timer_count].interval_timer,(os_timer_func_t *)durationTimerCallback,(void*)(&time_object_list[timer_count]));
                    os_timer_arm(&time_object_list[timer_count].interval_timer, INTERVAL_REFRESH_TIME, TIMER_REPAET);     
                }
                #endif
            }break;
            case IOTGO_TIME_TYPE_INTERVAL_DURATION_START_END:
            {
                getGMTTime(&cur_gmt);
                gmt = time_object_list[timer_count].timing.gmt;
                time_object_list[timer_count].diff_timer = minuteDiff(cur_gmt,gmt);
                iotgoInfo("minutes = %d,sendcond = %d, interval = %d",
                time_object_list[timer_count].diff_timer.minute,
                time_object_list[timer_count].diff_timer.second,
                time_object_list[timer_count].time_interval);
                if((time_object_list[timer_count].diff_timer.minute%time_object_list[timer_count].time_interval) == 0 
                    && (time_object_list[timer_count].diff_timer.second == 0))
                {
                    time_object_list[timer_count].start_do(time_object_list[timer_count].p_start_do);
                }
                else if(((time_object_list[timer_count].diff_timer.minute 
                 % time_object_list[timer_count].time_interval)
                 % time_object_list[timer_count].time_duration) == 0 
                 && (time_object_list[timer_count].diff_timer.second == 0))
                {
                    time_object_list[timer_count].end_do(time_object_list[timer_count].p_end_do); 
                }
            #if 0
                getGMTTime(&cur_gmt);
                gmt = time_object_list[timer_count].timing.gmt;
                if(!time_object_list[timer_count].time_compare_flag)
				{
					temp = minuteDiff(cur_gmt,gmt,&result);
					if(result == 1)
					{
						if((temp.minute%time_object_list[timer_count].time_interval) == 0 && (temp.second == 0))
						{
							time_object_list[timer_count].second_count = temp.second;
							time_object_list[timer_count].minute_count = 0;
							time_object_list[timer_count].start_do(time_object_list[timer_count].p_start_do);                                 
						}

						else if(((temp.minute%time_object_list[timer_count].time_interval)%time_object_list[timer_count].time_duration) == 0 && (temp.second == 0))
						{
							time_object_list[timer_count].second_count = temp.second;
							time_object_list[timer_count].minute_count = time_object_list[timer_count].time_duration;
							time_object_list[timer_count].end_do(time_object_list[timer_count].p_end_do); 
						}

						else 
						{
							time_object_list[timer_count].second_count = temp.second;
							time_object_list[timer_count].minute_count = temp.minute%time_object_list[timer_count].time_interval;
						}       
						iotgoInfo("time_object_list[%d].minute_count==%d",timer_count,time_object_list[timer_count].minute_count);
						iotgoInfo("time_object_list[%d].second_count==%d",timer_count,time_object_list[timer_count].second_count);
						os_timer_disarm(&time_object_list[timer_count].interval_timer);
						os_timer_setfn(&time_object_list[timer_count].interval_timer,(os_timer_func_t *)durationStartEndTimerCallback,(void*)(&time_object_list[timer_count]));
						os_timer_arm(&time_object_list[timer_count].interval_timer, INTERVAL_REFRESH_TIME, TIMER_REPAET);      
						time_object_list[timer_count].time_compare_flag = 1;	
						continue;
					}	
					else
					{
					    time_object_list[timer_count].time_compare_flag = 1;
						/* do nothing */
					}	
											
				}						
				if (gmt.year == cur_gmt.year && gmt.month == cur_gmt.month
                && gmt.day == cur_gmt.day && gmt.hour == cur_gmt.hour
                && gmt.minute == cur_gmt.minute
                && cur_gmt.second == 0)
                {   
                    /* 先执行当前的命令函数，然后开始计时 interval */
                    time_object_list[timer_count].start_do(time_object_list[timer_count].p_start_do); 
                    os_timer_disarm(&time_object_list[timer_count].interval_timer);
                    os_timer_setfn(&time_object_list[timer_count].interval_timer,(os_timer_func_t *)durationStartEndTimerCallback,(void*)(&time_object_list[timer_count]));
                    os_timer_arm(&time_object_list[timer_count].interval_timer, INTERVAL_REFRESH_TIME, TIMER_REPAET);     
                }  
		    #endif		
            }break;
            default:
                break;              
        }

    }
  
}

void ICACHE_FLASH_ATTR startTimerMonitor(void)
{
    os_timer_disarm(&timer_monitor);
    os_timer_setfn(&timer_monitor, (os_timer_func_t *)timerMonitorCallback, NULL);
    os_timer_arm(&timer_monitor, MONITOR_REFRESH_TIME, TIMER_REPAET);
    iotgoInfo("startTimeMonitor done");
}

/*
 * Stop time schedule service. 
 */
void ICACHE_FLASH_ATTR stopTimerMonitor(void)
{
    os_timer_disarm(&timer_monitor);
    deleteAllTimeObject();
    iotgoInfo("stopTimeSch done");
}

