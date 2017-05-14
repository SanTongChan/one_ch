#include "iotgo_loctimer.h"

#define IOTGO_TIMER_MAX_NUM (30)
#define GMT_STRING_MIN_LENGTH (24)

typedef enum {
    IOTGO_TIME_TYPE_INVALID    = 0,
    IOTGO_TIME_TYPE_ONCE        = 1,
    IOTGO_TIME_TYPE_REPEAT       = 2,
	IOTGO_TIME_TYPE_DURATION   = 3,          /*每隔一段时间执行一次*/
}IoTgoTimerType;
typedef struct{
    char start_do[100];
    char end_do[100];
}DurationDo;
typedef struct{
    uint32 time_interval;
    uint32 time_duration;
    uint8_t id;
    bool enabled;
    IoTgoTimerType type;
    union{
        IoTgoGMTTime gmt;
        IoTgoCron cron;
    }timing;
    union{
        char action_do[100];
        DurationDo duration_do;
    }time_action;
    char at[30];
}iotgoTimer;
typedef struct{
    uint32_t timer_num;
    iotgoTimer timer[(IOTGO_TIMER_MAX_NUM - 1) / 2 + 1];
    iotgoTimerInfo timer_info;
    uint8_t timer_id[IOTGO_TIMER_MAX_NUM];
}iotgoTimerFlashParam;
typedef struct{
    uint32_t pad;
    uint8 flag; 
} IoTgoTimerFlashParamFlag;

typedef struct{
    uint8_t timer_flash_save1;
    uint8_t timer_flash_save2;
    uint8_t timer_flash_save3;
    uint8_t timer_flash_save4;
    uint8_t timer_flash_flag;
}iotgoTimerFlashIndex;

static iotgoTimerFlashIndex timer_flash_index = {0x74,0x75,0x76,0x77,0x7b};
static iotgoTimer iotgo_timer[IOTGO_TIMER_NUM] = {0};
static iotgoTimerInfo iotgo_timer_info = {0};
static IotgoTimerCallback iotgo_timer_action = NULL;
static uint8_t iotgo_timer_id[IOTGO_TIMER_NUM] = {0};
static os_timer_t iotgo_timer_couter;
static TimerManage timer_manager = TIMER_NULL;
#define ERROR500   (500)
#define ERROR400   (400)
#define ERROR0     (0)

/*timer compare*/
static bool ICACHE_FLASH_ATTR gmtCompareWithNow(IoTgoGMTTime gmt)
{
    IoTgoGMTTime cur_gmt;
    getGMTTime(&cur_gmt);
    if (gmt.year == cur_gmt.year && gmt.month == cur_gmt.month
            && gmt.day == cur_gmt.day && gmt.hour == cur_gmt.hour
            && gmt.minute == cur_gmt.minute
            && cur_gmt.second == 0)
    {
        return true;
    }
    return false;
}
static bool ICACHE_FLASH_ATTR cronCompareWitchNow(IoTgoCron cron)
{
    int8 week;
    IoTgoGMTTime cur_gmt;
    getGMTTime(&cur_gmt);
    week = getWeek(cur_gmt.year, cur_gmt.month, cur_gmt.day);
    if (cron.weeks[week] == true 
        && (cron.hour == cur_gmt.hour || cron.hour == 24) 
        && cron.minute == cur_gmt.minute 
        && cur_gmt.second == 0
    )
    {
        return true;
    }
    return false;
}
static uint32_t ICACHE_FLASH_ATTR compareTimeToKnowDate(IoTgoGMTTime gmt)
{
    uint32 year = 0;
    uint32 count = 0;
    uint32 day_sum = 0;
    uint32 second_sum = 0;
    IoTgoGMTTime know_date ={2016,1,1,0,0,0};

    if(gmt.year < know_date.year)
    {
        return second_sum;
    }
    else if(gmt.year > know_date.year)
    {
        for(year = know_date.year ; year<gmt.year ; year++)
        {
            for(count=1; count<=12; count++)
            {
                day_sum += daysOfMonth(year,count);
            }
        }

        for(count=1; count < gmt.month; count++)
        {
            day_sum += daysOfMonth(year,count);
        }
    }
    else
    {
        for(count=1; count < gmt.month; count++)
        {
            day_sum += daysOfMonth(year,count);
        }
    }
    day_sum += (gmt.day-1);
    second_sum = (day_sum * 24 * 60 + gmt.hour * 60 + gmt.minute) * 60 + gmt.second;
    return second_sum;
}

static int8_t ICACHE_FLASH_ATTR durationCompareWithNow(IoTgoGMTTime gmt,
                                                    uint32 time_interval,uint32 time_duration)
{
    uint32_t cur_diff_second = 0;
    uint32_t time_diff_second = 0;
    uint32_t diff_val = 0;
    uint32_t diff_second = 0;
    uint32_t diff_minute = 0;
    IoTgoGMTTime cur_gmt;
    getGMTTime(&cur_gmt);
    cur_diff_second = compareTimeToKnowDate(cur_gmt);
    time_diff_second = compareTimeToKnowDate(gmt);
    if(0 == cur_diff_second || 0 == time_diff_second || (time_diff_second > cur_diff_second))
    {
        return -1;
    }
    diff_val = cur_diff_second - time_diff_second;
    diff_minute = diff_val / 60;
    diff_second = diff_val % 60;
    if(time_interval != 0 && (diff_minute % time_interval) == 0 && (diff_second == 0))
    {
        return  0;
    }
    else if(time_duration != 0 && ((diff_minute % time_interval)% time_duration) == 0 && (diff_second == 0))
    {
        return 1;
    }
    else
    {
        return -1;
    }
}



/*Timer result*/
static cJSON* ICACHE_FLASH_ATTR createBaseObject(uint16_t result)
{
    cJSON *json_root = NULL;
    if(NULL == (json_root = cJSON_CreateObject()))
    {
        iotgoError("create cjson object is error");
        return json_root;
    }
    cJSON_AddNumberToObject(json_root,IOTGO_STRING_ERROR, result);
    if(TIMER_APP == timer_manager)
    {
        cJSON_AddStringToObject(json_root,IOTGO_STRING_USERAGENT, IOTGO_STRING_DEVICE);
        cJSON_AddStringToObject(json_root,IOTGO_STRING_APIKEY, iotgo_device.owner_uuid);
        cJSON_AddStringToObject(json_root,IOTGO_STRING_DEVICEID, iotgo_device.deviceid);    
        cJSON_AddStringToObject(json_root,IOTGO_STRING_SEQUENCE, server_sequence_value);
    }
    return json_root;
}

static cJSON* ICACHE_FLASH_ATTR createTimerInfoObject(iotgoTimerInfo *timer_info)
{
    cJSON *json_timerinfo = NULL;
    if(NULL == (json_timerinfo = cJSON_CreateObject()))
    {
        iotgoError("create cjson object is error");
        return json_timerinfo;
    }
    cJSON_AddNumberToObject(json_timerinfo,IOTGO_STRING_TIMERCNT,timer_info->timer_cnt);
    cJSON_AddNumberToObject(json_timerinfo,IOTGO_STRING_TIMERVER,timer_info->timer_ver);  
    return json_timerinfo;
}
static cJSON* ICACHE_FLASH_ATTR createTimerObjcet(iotgoTimer *timer)
{
    cJSON *json_timer = NULL;
    if(NULL == (json_timer = cJSON_CreateObject()))
    {
        iotgoError("create cjson object is error");
        return json_timer;
    }
    cJSON_AddNumberToObject(json_timer,IOTGO_STRING_ID, timer->id);
    cJSON_AddNumberToObject(json_timer,IOTGO_STRING_ENABLED, (uint8_t)timer->enabled);
    cJSON_AddStringToObject(json_timer,IOTGO_STRING_AT, timer->at);
    if(IOTGO_TIME_TYPE_ONCE == timer->type)
    {
        cJSON_AddStringToObject(json_timer,IOTGO_STRING_TYPE, IOTGO_STRING_ONCE);
    }
    else if(IOTGO_TIME_TYPE_REPEAT == timer->type)
    {
        cJSON_AddStringToObject(json_timer,IOTGO_STRING_TYPE, IOTGO_STRING_REPEAT);
    }
    else if(IOTGO_TIME_TYPE_DURATION == timer->type)
    {
        cJSON_AddStringToObject(json_timer,IOTGO_STRING_TYPE, IOTGO_STRING_DURATION);
    }
    else
    {
        iotgoError("timer object is error");
        cJSON_Delete(json_timer);
        json_timer = NULL;
        return json_timer;
    }
    if(0 != os_strlen(timer->time_action.action_do))
    {
        cJSON *action_do = NULL;
        if(NULL == (action_do = cJSON_Parse(timer->time_action.action_do)))
        {
            iotgoError("timer object is error");
            cJSON_Delete(json_timer);
            json_timer = NULL;
            return json_timer;
        }
        cJSON_AddItemToObject(json_timer,IOTGO_STRING_DO, action_do);
    }
    else if( 0 != os_strlen(timer->time_action.duration_do.start_do) 
        && 0 != os_strlen(timer->time_action.duration_do.end_do))
    {
        cJSON *action_start_do = NULL;
        cJSON *action_end_do = NULL;
        if(NULL == (action_start_do = cJSON_Parse(timer->time_action.duration_do.start_do))
            || NULL == (action_end_do = cJSON_Parse(timer->time_action.duration_do.end_do)))
        {
            iotgoError("timer object is error");
            cJSON_Delete(json_timer);
            if(action_end_do)
            {
                cJSON_Delete(action_end_do);
            }
            if(action_start_do)
            {
                cJSON_Delete(action_start_do);
            }
            json_timer = NULL;
            return json_timer;
        }
        cJSON_AddStringToObject(json_timer,IOTGO_STRING_START_DO, timer->time_action.duration_do.start_do);
        cJSON_AddStringToObject(json_timer,IOTGO_STRING_END_DO, timer->time_action.duration_do.end_do);
    }
    else
    {
        iotgoError("timer object is error");
        cJSON_Delete(json_timer);
        json_timer = NULL;
        return json_timer;
    }
    return json_timer;
}
static cJSON* ICACHE_FLASH_ATTR createIdArray(uint8_t *id,uint8_t num)
{
    cJSON *cjson_id = NULL;
    cjson_id = cJSON_CreateIntArray((int *)id,num);
    return cjson_id;
}
static bool ICACHE_FLASH_ATTR replyUpdate(uint16_t result)
{
    
    cJSON *cjson_root = NULL;
    char *out = NULL;
    if(NULL == (cjson_root = createBaseObject(result)))
    {
        iotgoError("serious error");
        return false;
    }
    out = cJSON_PrintUnformatted(cjson_root);
    cJSON_Delete(cjson_root);
    if(TIMER_APP == timer_manager)
    {
        iotgoQueueAdd(out,IOTGO_ERROR0,false);
    }
    else if(TIMER_MCU == timer_manager)
    {
        cmdSendRetToMcu(out,os_strlen(out));
    }
    else
    {
        /*do nothing*/
    }
    os_free(out);
    return true;
}
static bool ICACHE_FLASH_ATTR replyUpdateWithId(uint16_t result,uint8_t *id,uint8_t num)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_id = NULL;
    char *out = NULL;
    if(NULL == (cjson_root = createBaseObject(result)))
    {
        replyUpdate(ERROR500);
        return false;
    }
    if(NULL == (cjson_id = createIdArray(id,num)))
    {
        replyUpdate(ERROR500);
        cJSON_Delete(cjson_root);
        return false;
    }
    cJSON_AddItemToObject(cjson_root,IOTGO_STRING_TIMERID,cjson_id);
    out = cJSON_PrintUnformatted(cjson_root);
    cJSON_Delete(cjson_root);
    if(TIMER_APP == timer_manager)
    {
        iotgoQueueAdd(out,IOTGO_ERROR0,false);
    }
    else if(TIMER_MCU == timer_manager)
    {
        cmdSendRetToMcu(out,os_strlen(out));
    }
    else
    {
        /*do nothing*/
    }
    os_free(out);
    return true;
}
static bool ICACHE_FLASH_ATTR replyQueryTimerInfo(uint16_t result,iotgoTimerInfo *timer_info)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_params = NULL;
    cJSON *cjson_timer_info = NULL;
    char *out = NULL;
    if(ERROR500 == result)
    {
        return replyUpdate(result);
    }
    if(NULL == (cjson_root = createBaseObject(result)))
    {
        replyUpdate(ERROR500);
        return false;
    }
    cJSON_AddItemToObject(cjson_root, IOTGO_STRING_PARAMS, cjson_params = cJSON_CreateObject());
    if(NULL == (cjson_timer_info = createTimerInfoObject(timer_info)))
    {
        replyUpdate(ERROR500);
        cJSON_Delete(cjson_root);
        return false;
    }
    cJSON_AddItemToObject(cjson_params, IOTGO_STRING_TIMERINFO,cjson_timer_info);
    out = cJSON_PrintUnformatted(cjson_root);
    cJSON_Delete(cjson_root);
    if(TIMER_APP == timer_manager)
    {
        iotgoQueueAdd(out,IOTGO_QUERY_TIMER,false);
    }
    else if(TIMER_MCU == timer_manager)
    {
        cmdSendRetToMcu(out,os_strlen(out));
    }
    else
    {
        /*do nothing*/
    }
    os_free(out);
    return true; 
}
static bool ICACHE_FLASH_ATTR replyQueryTimer(uint16_t result,iotgoTimerInfo *timer_info,iotgoTimer *timer,uint8_t num)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_params = NULL;
    cJSON *cjson_timer_info = NULL;
    cJSON *cjson_timer = NULL;
    cJSON *cjson_timer_array = NULL;
    char *out = NULL;
    uint8_t i = 0;
    if(ERROR500 == result)
    {
        return replyUpdate(result);
    }
    if(NULL == (cjson_root = createBaseObject(result)))
    {
        replyUpdate(ERROR500);
        return false;
    }
    if(NULL == (cjson_params = cJSON_CreateObject()))
    {
        replyUpdate(ERROR500);
        cJSON_Delete(cjson_root);
        return false;
    }
    cJSON_AddItemToObject(cjson_root, IOTGO_STRING_PARAMS, cjson_params);
    if(NULL == (cjson_timer_info = createTimerInfoObject(timer_info)))
    {
        replyUpdate(ERROR500);
        cJSON_Delete(cjson_root);
        return false;
    }
    cJSON_AddItemToObject(cjson_params, IOTGO_STRING_TIMERINFO,cjson_timer_info);
    
    if(NULL == (cjson_timer_array = cJSON_CreateArray()))
    {
        replyUpdate(ERROR500);
        cJSON_Delete(cjson_root);
        return false;
    }
    cJSON_AddItemToObject(cjson_params, IOTGO_STRING_TIMER,cjson_timer_array);
    
    for(i = 0; i < num; i++)
    {
        if(NULL == (cjson_timer = createTimerObjcet(&timer[i])))
        {
            replyUpdate(ERROR500);
            cJSON_Delete(cjson_root);
            return false;
        }
        cJSON_AddItemToArray(cjson_timer_array,cjson_timer);
    }
    out = cJSON_PrintUnformatted(cjson_root);
    cJSON_Delete(cjson_root);
    if(TIMER_APP == timer_manager)
    {
        iotgoQueueAdd(out,IOTGO_QUERY_TIMER,false);
    }
    else if(TIMER_MCU == timer_manager)
    {
        cmdSendRetToMcu(out,os_strlen(out));
    }
    else
    {
        /*do nothing*/
    }
    os_free(out);
    return true;
}



/*Timer executor*/
static void ICACHE_FLASH_ATTR timerSaveToFlash(void)
{
    uint8_t temp = 0;
    uint8_t temp1 = 0;
    uint8_t temp2 = 0;
    IoTgoTimerFlashParamFlag flag;
    iotgoTimerFlashParam *timer_flash_param = NULL;
    if(NULL == (timer_flash_param = (iotgoTimerFlashParam *)os_malloc(sizeof(iotgoTimerFlashParam))))
    {
        iotgoError("memory error");
        return;
    }
    os_memset(timer_flash_param,0,sizeof(iotgoTimerFlashParam));
    timer_flash_param->timer_num = IOTGO_TIMER_NUM;
    timer_flash_param->timer_info = iotgo_timer_info;
    os_memcpy(timer_flash_param->timer_id,iotgo_timer_id,sizeof(iotgo_timer_id));
    spi_flash_read((timer_flash_index.timer_flash_flag) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)&flag, sizeof(IoTgoTimerFlashParamFlag));
    temp = (IOTGO_TIMER_MAX_NUM - 1) / 2 + 1;
    temp1 = (IOTGO_TIMER_NUM > temp)?(temp):(IOTGO_TIMER_NUM);
    temp2 = (IOTGO_TIMER_NUM > temp)?(IOTGO_TIMER_NUM - temp):(0);
    if(0 == flag.flag)
    {
        spi_flash_erase_sector(timer_flash_index.timer_flash_save1);
        spi_flash_erase_sector(timer_flash_index.timer_flash_save3);
        os_memcpy(timer_flash_param->timer,&iotgo_timer[0],sizeof(iotgoTimer) * temp1);
        spi_flash_write((timer_flash_index.timer_flash_save1) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
        os_memset(timer_flash_param->timer,0,sizeof(timer_flash_param->timer));
        if(0 != temp2)
        {
            os_memcpy(timer_flash_param->timer,&iotgo_timer[temp],sizeof(iotgoTimer) * temp2);
        }
        spi_flash_write((timer_flash_index.timer_flash_save3) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
        flag.flag = 1;
        spi_flash_erase_sector(timer_flash_index.timer_flash_flag);
        spi_flash_write((timer_flash_index.timer_flash_flag) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)&flag, sizeof(IoTgoTimerFlashParamFlag));
    }
    else
    {
        spi_flash_erase_sector(timer_flash_index.timer_flash_save2);
        spi_flash_erase_sector(timer_flash_index.timer_flash_save4);
        os_memcpy(timer_flash_param->timer,&iotgo_timer[0],sizeof(iotgoTimer) * temp1);
        spi_flash_write((timer_flash_index.timer_flash_save2) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
        os_memset(timer_flash_param->timer,0,sizeof(timer_flash_param->timer));
        if(0 != temp2)
        {
            os_memcpy(timer_flash_param->timer,&iotgo_timer[temp],sizeof(iotgoTimer) * temp2);
        }
        spi_flash_write((timer_flash_index.timer_flash_save4) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
        flag.flag = 0;
        spi_flash_erase_sector(timer_flash_index.timer_flash_flag);
        spi_flash_write((timer_flash_index.timer_flash_flag) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)&flag, sizeof(IoTgoTimerFlashParamFlag));
    }
    os_free(timer_flash_param);
    timer_flash_param = NULL;
}

static void ICACHE_FLASH_ATTR timerLoadFromFlash(void)
{
    IoTgoTimerFlashParamFlag flag;
    iotgoTimerFlashParam *timer_flash_param = NULL;
    uint8_t temp = 0;
    uint8_t temp1 = 0;
    uint8_t temp2 = 0;
    if(NULL == (timer_flash_param = (iotgoTimerFlashParam *)os_malloc(sizeof(iotgoTimerFlashParam))))
    {
        iotgoError("memory error");
        return;
    }
    os_memset(timer_flash_param,0,sizeof(iotgoTimerFlashParam));
    temp = (IOTGO_TIMER_MAX_NUM - 1) / 2 + 1;
    temp1 = (IOTGO_TIMER_NUM > temp)?(temp):(IOTGO_TIMER_NUM);
    temp2 = (IOTGO_TIMER_NUM > temp)?(IOTGO_TIMER_NUM - temp):(0);
    spi_flash_read((timer_flash_index.timer_flash_flag) * SPI_FLASH_SEC_SIZE,
               (uint32 *)&flag, sizeof(IoTgoTimerFlashParamFlag));
    if(0 == flag.flag)
    {
        spi_flash_read((timer_flash_index.timer_flash_save2) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
        if(timer_flash_param->timer_num > IOTGO_TIMER_NUM)
        {
            timerFlashErase();
        }
        else
        {
            iotgo_timer_info = timer_flash_param->timer_info;
            os_memcpy(&iotgo_timer_id,timer_flash_param->timer_id,sizeof(iotgo_timer_id));
            os_memcpy(&iotgo_timer,timer_flash_param->timer,sizeof(iotgoTimer) * temp1);
            if(temp2 > 0)
            {
                os_memset(timer_flash_param->timer,0,sizeof(timer_flash_param->timer));
                spi_flash_read((timer_flash_index.timer_flash_save4) * SPI_FLASH_SEC_SIZE,
                           (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
                os_memcpy(&iotgo_timer[temp],timer_flash_param->timer,sizeof(iotgoTimer) * temp2);
            }
        }
    }
    else
    {
        spi_flash_read((timer_flash_index.timer_flash_save1) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));            
        if(timer_flash_param->timer_num > IOTGO_TIMER_NUM)
        {
            timerFlashErase();
        }
        else
        {
            iotgo_timer_info = timer_flash_param->timer_info;
            os_memcpy(&iotgo_timer_id,timer_flash_param->timer_id,sizeof(iotgo_timer_id));
            os_memcpy(&iotgo_timer,timer_flash_param->timer,sizeof(iotgoTimer) * temp1);
            if(temp2 > 0)
            {
                os_memset(timer_flash_param->timer,0,sizeof(timer_flash_param->timer));
                spi_flash_read((timer_flash_index.timer_flash_save3) * SPI_FLASH_SEC_SIZE,
                           (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
                os_memcpy(&iotgo_timer[temp],timer_flash_param->timer,sizeof(iotgoTimer) * temp2);
            }
        }
    }
    os_free(timer_flash_param);
    timer_flash_param = NULL;
}


static uint8_t ICACHE_FLASH_ATTR timerFindTimerByIndex(uint8_t index)
{
    uint8_t index_temp = 0;
    uint8_t i = 0;
    uint8_t ret = 0;
    if(index >= IOTGO_TIMER_NUM || index >= iotgo_timer_info.timer_cnt)
    {
        return ret;
    }
    for(i = 0; i < IOTGO_TIMER_NUM; i++)
    {
        if(0 != iotgo_timer[i].id)
        {
            if(index_temp == index)
            {
                ret = iotgo_timer[i].id;  
                break;
            }
            index_temp++;
        }
    }
    return ret;
}
static bool replyError(void)
{
    return replyUpdate(ERROR500);
}

static uint8_t ICACHE_FLASH_ATTR timerProductId(void)
{
    uint8_t id_temp = 0;
    uint8_t i = 0;
    if(iotgo_timer_info.timer_cnt >= IOTGO_TIMER_NUM)
    {
        return id_temp;
    }
    for(i = 0; i < IOTGO_TIMER_NUM; i++)
    {
        if(0 == iotgo_timer_id[i])
        {
            iotgo_timer_id[i] = i + 1;
            id_temp = iotgo_timer_id[i];
            break;
        }
    }
    return id_temp;
}
static bool ICACHE_FLASH_ATTR timerDeleteId(uint8_t id)
{
    uint8_t i = 0;
    if(id <= 0)
    {
        return false;
    }
    if(id == iotgo_timer_id[id -1])
    {
        iotgo_timer_id[id - 1] = 0;
        return true;
    }
    return false;
}

static bool ICACHE_FLASH_ATTR timerNew(iotgoTimer *timers,uint8_t num)
{
    uint8_t i = 0;
    uint8_t id[IOTGO_TIMER_NUM] = {0};
    if(IOTGO_TIMER_NUM < num + iotgo_timer_info.timer_cnt || NULL == timers || 0 == num)
    {
        replyUpdate(ERROR500);
        return false;
    }
    for(i = 0; i < num; i++)
    {
        if(0 == (id[i] = timerProductId()))
        {
            timerLoadFromFlash();
            replyUpdate(ERROR500);
            return false;
        }
        os_memcpy(&iotgo_timer[id[i] - 1],&timers[i],sizeof(iotgoTimer));
        if(iotgo_timer[id[i] - 1].id != 0)
        {
            timerLoadFromFlash();
            replyUpdate(ERROR400);
            return false;
        }
        iotgo_timer[id[i] - 1].id = id[i];
        iotgo_timer_info.timer_cnt++;
    }
    iotgo_timer_info.timer_ver++;
    timerSaveToFlash();
    return replyUpdateWithId(ERROR0,id,num);
}
static bool ICACHE_FLASH_ATTR timerEdit(iotgoTimer *timers,uint8_t num)
{
    uint8_t i = 0;
    if(iotgo_timer_info.timer_cnt < num || NULL == timers || 0 == num)
    {
        replyUpdate(ERROR500);
        return false;
    }
    for(i = 0; i < num; i++)
    {
        uint8_t id = timers[i].id;
        if(id != iotgo_timer_id[id - 1] || 0 == id)
        {
            timerLoadFromFlash();
            replyUpdate(ERROR500);
            return false;
        }
        os_memcpy(&iotgo_timer[id - 1],&timers[i],sizeof(iotgoTimer));
    }
    iotgo_timer_info.timer_ver++;
    timerSaveToFlash();
    return replyUpdate(ERROR0);
}
static bool ICACHE_FLASH_ATTR timerDel(uint8_t *id,uint8_t num)
{
    uint8_t i = 0;
    if(NULL == id || 0 == num)
    {
        replyUpdate(ERROR500);
        return false;
    }
    for(i = 0; i < num; i++)
    {
        if(!timerDeleteId(id[i]) || 0 == iotgo_timer_info.timer_cnt)
        {
            timerLoadFromFlash();
            replyUpdate(ERROR500);
            return false;
        }
        os_memset(&iotgo_timer[id[i] - 1],0,sizeof(iotgoTimer));
        iotgo_timer_info.timer_cnt--;
    }
    iotgo_timer_info.timer_ver++;
    timerSaveToFlash();
    return replyUpdate(ERROR0);
}
static bool ICACHE_FLASH_ATTR timerEnable(uint8_t *id,uint8_t num)
{
    uint8_t i = 0;
    if(NULL == id || 0 == num || num > iotgo_timer_info.timer_cnt)
    {
        replyUpdate(ERROR500);
        return false;
    }
    for(i = 0; i < num; i++)
    {
        if(id[i] != iotgo_timer_id[id[i] - 1] || 0 == id[i])
        {
            timerLoadFromFlash();
            replyUpdate(ERROR500);
            return false;
        }
        iotgo_timer[id[i] - 1].enabled = true;
    }
    iotgo_timer_info.timer_ver++;
    timerSaveToFlash();
    return replyUpdate(ERROR0);
}
static bool ICACHE_FLASH_ATTR timerDisable(uint8_t *id,uint8_t num)
{
    uint8_t i = 0;
    if(NULL == id || 0 == num || num > iotgo_timer_info.timer_cnt)
    {
        replyUpdate(ERROR500);
        return false;
    }
    for(i = 0; i < num; i++)
    {
        if(id[i] != iotgo_timer_id[id[i] - 1] || 0 == id[i])
        {
            timerLoadFromFlash();
            replyUpdate(ERROR500);
            return false;
        }
        iotgo_timer[id[i] - 1].enabled = false;
    }
    iotgo_timer_info.timer_ver++;
    timerSaveToFlash();
    return replyUpdate(ERROR0);
}
static bool ICACHE_FLASH_ATTR timerDelAll(void)
{
    uint8_t i = 0;
    os_memset(iotgo_timer,0,sizeof(iotgo_timer));
    os_memset(iotgo_timer_id,0,sizeof(iotgo_timer_id));
    iotgo_timer_info.timer_cnt = 0;
    iotgo_timer_info.timer_ver++;
    timerSaveToFlash();
    return replyUpdate(ERROR0);
}
static bool ICACHE_FLASH_ATTR timerEnableAll(void)
{
    uint8_t i = 0;

    for(i = 0; i < IOTGO_TIMER_NUM; i++)
    {
        if(iotgo_timer[i].id != 0)
        {
            iotgo_timer[i].enabled = true;
        }
    }
    iotgo_timer_info.timer_ver++;
    timerSaveToFlash();
    return replyUpdate(ERROR0);
}
static bool ICACHE_FLASH_ATTR timerDisableAll(void)
{
    uint8_t i = 0;

    for(i = 0; i < IOTGO_TIMER_NUM; i++)
    {
        if(iotgo_timer[i].id != 0)
        {
            iotgo_timer[i].enabled = false;
        }
    }
    iotgo_timer_info.timer_ver++;
    timerSaveToFlash();
    return replyUpdate(ERROR0);;
}
static bool ICACHE_FLASH_ATTR timerQueryTimerInfo(void)
{
    return replyQueryTimerInfo(ERROR0,&iotgo_timer_info);
}
static bool ICACHE_FLASH_ATTR timerQueryTimerByIndex(uint8_t start_index,uint8_t end_index)
{
    iotgoTimer timer[IOTGO_TIMER_NUM] = {0};
    uint8_t num = 0;
    uint8_t i = 0;
    uint8_t id = 0;
    num = end_index - start_index + 1;
    if((end_index < start_index) || (iotgo_timer_info.timer_cnt < num))
    {
        replyError();
        return false;
    }
    for(i = start_index; i <= end_index; i++)
    {
        if(0 == (id = timerFindTimerByIndex(i)))
        {
            replyError();
            return false;
        }
        os_memcpy(&timer[i],&iotgo_timer[id - 1],sizeof(iotgoTimer));
    }
    return replyQueryTimer(ERROR0,&iotgo_timer_info,timer,num);
}
static bool ICACHE_FLASH_ATTR timerQueryTimerById(uint8_t *id,uint8_t num)
{
    uint8_t i = 0;
    iotgoTimer timer[IOTGO_TIMER_NUM] = {0};
    if(num > iotgo_timer_info.timer_cnt || NULL == id)
    {
        replyError();
        return false;
    }
    for(i = 0; i < num; i++)
    {
        if(id[i] != iotgo_timer_id[id[i] - 1] || id[i] == 0)
        {
            replyError();
            return false;
        }
        os_memcpy(&timer[i],&iotgo_timer[id[i] - 1],sizeof(iotgoTimer));
    }
    return replyQueryTimer(ERROR0,&iotgo_timer_info,timer,num);
}
static void ICACHE_FLASH_ATTR timerDealTime(void *arg)
{
    uint8_t i = 0;
    int8 week;
    IoTgoGMTTime gmt;
    IoTgoCron cron;
    IoTgoGMTTime cur_gmt;
    for(i = 0; i < IOTGO_TIMER_NUM;i++)
    {
        if(0 == iotgo_timer[i].id || false == iotgo_timer[i].enabled)
        {
            continue;
        }
        switch(iotgo_timer[i].type)
        {
            case IOTGO_TIME_TYPE_ONCE:
            {
                if(gmtCompareWithNow(iotgo_timer[i].timing.gmt))
                {
                    if(NULL != iotgo_timer_action)
                    {
                        iotgo_timer_action(iotgo_timer[i].time_action.action_do);
                    }
                    iotgo_timer[i].enabled = false;
                }
            }break;
            case IOTGO_TIME_TYPE_REPEAT:
            {
                if(cronCompareWitchNow(iotgo_timer[i].timing.cron))
                {
                    if(NULL != iotgo_timer_action)
                    {
                        iotgo_timer_action(iotgo_timer[i].time_action.action_do);
                    }
                }
            }break;
            case IOTGO_TIME_TYPE_DURATION:
            {
                int8_t ret = -1;
                ret = durationCompareWithNow(iotgo_timer[i].timing.gmt,
                             iotgo_timer[i].time_interval,iotgo_timer[i].time_duration);
                if(0 == ret)
                {
                    if(0 != os_strlen(iotgo_timer[i].time_action.action_do))
                    {
                        if(NULL != iotgo_timer_action)
                        {
                            iotgo_timer_action(iotgo_timer[i].time_action.action_do);
                        }
                    }
                    else
                    {
                        if(NULL != iotgo_timer_action)
                        {
                            iotgo_timer_action(iotgo_timer[i].time_action.duration_do.start_do);
                        }
                    }
                }
                else if(1 == ret)
                {
                    if(NULL != iotgo_timer_action)
                    {
                        iotgo_timer_action(iotgo_timer[i].time_action.duration_do.end_do);
                    }
                }
                
            }break;
            default:
            {
                iotgoError("please check code,this type don't exit");
            };
        }
    }
}



/*Timer proc*/
static void ICACHE_FLASH_ATTR procError(void)
{
    if(!replyError())
    {
        iotgoError("proc error go wrong");
    }
}
static bool ICACHE_FLASH_ATTR getTimerId(iotgoTimer *timer,cJSON *cjson_timer)
{
    cJSON *cjson_id = NULL;
    if(NULL == cjson_timer
        || NULL == (cjson_id = cJSON_GetObjectItem(cjson_timer,IOTGO_STRING_ID))
        || cJSON_Number != cjson_id->type)
    {
        return false;
    }
    timer->id = cjson_id->valueint;
	return true;
}
static bool ICACHE_FLASH_ATTR getTimerEnabled(iotgoTimer *timer,cJSON *cjson_timer)
{
    cJSON *cjson_enabled = NULL;
    if(NULL == cjson_timer
        || NULL == (cjson_enabled = cJSON_GetObjectItem(cjson_timer,IOTGO_STRING_ENABLED))
        || cJSON_Number != cjson_enabled->type)
    {
        return false;
    }
    timer->enabled= cjson_enabled->valueint;
	return true;
}
static bool ICACHE_FLASH_ATTR getTimerType(iotgoTimer *timer,cJSON *cjson_timer)
{
    cJSON *cjson_type = NULL;
    if(NULL == cjson_timer
        || NULL == (cjson_type = cJSON_GetObjectItem(cjson_timer,IOTGO_STRING_TYPE))
        || cJSON_String != cjson_type->type)
    {
        return false;
    }
    if(0 == os_strcmp(cjson_type->valuestring,IOTGO_STRING_ONCE))
    {
        timer->type = IOTGO_TIME_TYPE_ONCE;
    }
    else if(0 == os_strcmp(cjson_type->valuestring,IOTGO_STRING_REPEAT))
    {
        timer->type = IOTGO_TIME_TYPE_REPEAT;
    }
    else if(0 == os_strcmp(cjson_type->valuestring,IOTGO_STRING_DURATION))
    {
        timer->type = IOTGO_TIME_TYPE_DURATION;
    }
    else
    {
        timer->type = IOTGO_TIME_TYPE_INVALID;
        return false;
    }  
    return true;
}
static bool ICACHE_FLASH_ATTR parseFromATString(const char*at_str,char*gmt,uint32* interval,uint32* duration)
{
    uint8 str_len;
    int32 field_len;
    char *p = (char*)at_str;
    char *temp ;
    char buffer[30];
    if(NULL == at_str || NULL == gmt)
    {
        iotgoError("[IOTGO Debug:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);
        return false;
    }
    
    str_len = os_strlen(at_str);
    if(str_len <= GMT_STRING_MIN_LENGTH)
    {
        iotgoInfo("at_str string too short \n");
        return false;
    }
    
    /* 解析第一个空格 */
    temp = (char*)os_strstr(p," ");
    if(!temp || (temp-p) <= 0)
    {
        iotgoInfo("[IOTGO Debug:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);
        return false;
    }
    field_len = temp -p;
    os_bzero(buffer,sizeof(buffer));
    os_strncpy(buffer,p,field_len);
    buffer[field_len]='\0';
    os_strcpy(gmt,buffer);
    
    /* 解析interval */
    temp++;
    p = temp;
    temp = (char*)os_strstr(p," ");

    /*只有interval */
    if(temp == NULL)
    {
        field_len = str_len-(p -at_str);
        os_bzero(buffer,sizeof(buffer));
        os_strncpy(buffer,p,field_len);
        buffer[field_len] = '\0';
        *interval = atoi(buffer);
        if(*interval == 0)
        {
            return false;
        }
        *duration = 0;
        iotgoInfo("[IOTGO Debug:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);
        return true; ;
    }

    field_len = temp -p;
    os_bzero(buffer,sizeof(buffer));
    os_strncpy(buffer,p,field_len);
    buffer[field_len] = '\0';
    *interval = atoi(buffer);
    if(*interval == 0)
    {
        return false;
    }
    /*解析duartion*/
    temp++;
    p = temp;
    field_len = str_len-(p -at_str);
    os_bzero(buffer,sizeof(buffer));
    os_strncpy(buffer,p,field_len);
    buffer[field_len] = '\0';
    *duration = atoi(buffer);
    iotgoInfo("[IOTGO Debug:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);
    return true;
}

static bool ICACHE_FLASH_ATTR getTImerAt(iotgoTimer *timer,cJSON *cjson_timer)
{
    cJSON *cjson_at = NULL;
    bool ret = false;
    if(NULL == cjson_timer
        || NULL == (cjson_at = cJSON_GetObjectItem(cjson_timer,IOTGO_STRING_AT))
        || cJSON_String != cjson_at->type)
    {
        return false;
    }
    os_memset(timer->at,0,sizeof(timer->at));
    os_strcpy(timer->at,cjson_at->valuestring);
    switch(timer->type)
    {
        case IOTGO_TIME_TYPE_ONCE:
        {
            if (parseGMTTimeFromString(cjson_at->valuestring, &timer->timing.gmt))
            {
                ret = true;
            }
        }break;
        case IOTGO_TIME_TYPE_REPEAT:
        {
            if (parseCronFromString(cjson_at->valuestring, &timer->timing.cron))
            {
                ret = true;
            }
        }break;
        case IOTGO_TIME_TYPE_DURATION:
        {
            char timer_gmt[30] = {0};
            if(parseFromATString(cjson_at->valuestring,timer_gmt,&timer->time_interval,&timer->time_duration))
            {
                if (parseGMTTimeFromString(timer_gmt, &timer->timing.gmt))
                {
                    ret = true;
                }
            }
        }break;
        default:
        {
            ret = false;
        }
    }
    return ret;
}
static bool ICACHE_FLASH_ATTR getTimerAction(iotgoTimer *timer,cJSON *cjson_timer)
{
    cJSON *cjson_do = NULL;
    cJSON *cjson_start_do = NULL;
    cJSON *cjson_end_do = NULL;
    char *action_do = NULL;
    char *action_start_do = NULL;
    char *action_end_do = NULL;
    bool ret = false;
    if(NULL == cjson_timer)
    {
        return false;
    }
    if(NULL != (cjson_do = cJSON_GetObjectItem(cjson_timer,"do")))
	{
        if(cjson_do->type != cJSON_Object)
        {
            return false;
        }
        action_do = cJSON_PrintUnformatted(cjson_do);
        os_memset(timer->time_action.action_do,0,sizeof(timer->time_action.action_do));
        os_strcpy(timer->time_action.action_do,action_do);
        os_free(action_do);
	} 
	else if(NULL != (cjson_start_do = cJSON_GetObjectItem(cjson_timer,"startDo")) 
	        && (NULL != (cjson_end_do = cJSON_GetObjectItem(cjson_timer,"endDo"))))
	{
        if(cjson_start_do->type != cJSON_Object || cjson_end_do->type != cJSON_Object)
        {
            return false;
        }
        action_start_do = cJSON_PrintUnformatted(cjson_start_do);
        action_end_do = cJSON_PrintUnformatted(cjson_end_do);
        os_memset(timer->time_action.duration_do.start_do,0,sizeof(timer->time_action.duration_do.start_do));
        os_strcpy(timer->time_action.duration_do.start_do,action_start_do);
        os_free(action_start_do);

        os_memset(timer->time_action.duration_do.end_do,0,sizeof(timer->time_action.duration_do.end_do));
        os_strcpy(timer->time_action.duration_do.end_do,action_end_do);
        os_free(action_end_do);
	}
	else
	{
        return false;
	}
	return true;
}

static bool ICACHE_FLASH_ATTR generateTimer(iotgoTimer *timer,cJSON *cjson_timer)
{
    if(!getTimerId(timer,cjson_timer))
    {
        return false;
    }
    if(!getTimerEnabled(timer,cjson_timer))
    {
        return false;
    }
    if(!getTimerType(timer,cjson_timer))
    {
        return false;
    }
    if(!getTImerAt(timer,cjson_timer))
    {
        return false;
    }
    if(!getTimerAction(timer,cjson_timer))
    {
        return false;
    }
    return true;
}
static bool ICACHE_FLASH_ATTR procNew(cJSON *cjson_root)
{
    cJSON *cjson_timer = NULL;
    uint8_t timer_num = 0;
    uint8_t i = 0;
    iotgoTimer timers[IOTGO_TIMER_NUM] = {0};
    if(NULL == cjson_root || cJSON_Array != cjson_root->type)
    {
        procError();
        return false;
    }
    if(IOTGO_TIMER_NUM < (timer_num = cJSON_GetArraySize(cjson_root)))
    {
        procError();
        return false;
    }
    for(i = 0; i < timer_num; i++)
    {
        cjson_timer = cJSON_GetArrayItem(cjson_root,i);
        if(NULL == cjson_timer || cJSON_Object != cjson_timer->type || !generateTimer(&timers[i],cjson_timer))
        {
            procError();
            return false;
        }
    }
    return timerNew(timers,timer_num);
}
static bool ICACHE_FLASH_ATTR procEdit(cJSON *cjson_root)
{
    cJSON *cjson_timer = NULL;
    uint8_t timer_num = 0;
    uint8_t i = 0;
    iotgoTimer timers[IOTGO_TIMER_NUM] = {0};
    if(NULL == cjson_root || cJSON_Array != cjson_root->type)
    {
        procError();
        return false;
    }
    if(IOTGO_TIMER_NUM < (timer_num = cJSON_GetArraySize(cjson_root)))
    {
        procError();
        return false;
    }
    for(i = 0; i < timer_num; i++)
    {
        cjson_timer = cJSON_GetArrayItem(cjson_root,i);
        if(NULL == cjson_timer || cJSON_Object != cjson_timer->type || !generateTimer(&timers[i],cjson_timer))
        {
            procError();
            return false;
        }
    }
    return timerEdit(timers,timer_num);
}
static bool ICACHE_FLASH_ATTR procDel(cJSON *cjson_tiemrid)
{
    cJSON *cjson_id = NULL;
    uint8_t timerid_num = 0;
    uint8_t i = 0;
    char id[IOTGO_TIMER_NUM] = {0};
    if(NULL == cjson_tiemrid || cJSON_Array != cjson_tiemrid->type )
    {
        procError();
        return false;
    }
    timerid_num = cJSON_GetArraySize(cjson_tiemrid);
    for(i = 0; i < timerid_num; i++)
    {
        if(NULL == (cjson_id = cJSON_GetArrayItem(cjson_tiemrid,i)))
        {
            procError();
            return false;
        }
        id[i] = cjson_id->valueint;
    }
    return timerDel(id,timerid_num);
}
static bool ICACHE_FLASH_ATTR procEnable(cJSON *cjson_tiemrid)
{
    cJSON *cjson_id = NULL;
    uint8_t timerid_num = 0;
    uint8_t i = 0;
    char id[IOTGO_TIMER_NUM] = {0};
    if(NULL == cjson_tiemrid || cJSON_Array != cjson_tiemrid->type )
    {
        procError();
        return false;
    }
    timerid_num = cJSON_GetArraySize(cjson_tiemrid);
    for(i = 0; i < timerid_num; i++)
    {
        if(NULL == (cjson_id = cJSON_GetArrayItem(cjson_tiemrid,i)))
        {
            procError();
            return false;
        }
        id[i] = cjson_id->valueint;
    }
    return timerEnable(id,timerid_num);
}
static bool ICACHE_FLASH_ATTR procDisable(cJSON *cjson_tiemrid)
{
    cJSON *cjson_id = NULL;
    uint8_t timerid_num = 0;
    uint8_t i = 0;
    char id[IOTGO_TIMER_NUM] = {0};
    if(NULL == cjson_tiemrid || cJSON_Array != cjson_tiemrid->type )
    {
        procError();
        return false;
    }
    timerid_num = cJSON_GetArraySize(cjson_tiemrid);
    for(i = 0; i < timerid_num; i++)
    {
        if(NULL == (cjson_id = cJSON_GetArrayItem(cjson_tiemrid,i)))
        {
            procError();
            return false;
        }
        id[i] = cjson_id->valueint;
    }
    return timerDisable(id,timerid_num);
}
static bool ICACHE_FLASH_ATTR procDelAll(void)
{
    return timerDelAll();
}
static bool ICACHE_FLASH_ATTR procEnableAll(void)
{
    return timerEnableAll();
}
static bool ICACHE_FLASH_ATTR procDisableAll(void)
{
    return timerDisableAll();
}
static bool ICACHE_FLASH_ATTR procQueryTimerInfo(cJSON *cjson_params)
{
    if(NULL == cjson_params || cJSON_Array != cjson_params->type)
    {
        procError();
        return false;
    }
    return timerQueryTimerInfo();
}
static bool ICACHE_FLASH_ATTR procQueryTimerByIndex(cJSON *cjson_params)
{
    uint8_t array_num = 0;
    uint8_t start_index = 0;
    uint8_t end_index = 0;
    cJSON *cjson_start_index = NULL;
    cJSON *cjson_end_index = NULL;
    if((NULL == cjson_params) 
        || (cJSON_Array != cjson_params->type)
        || (3 < (array_num = cJSON_GetArraySize(cjson_params))))
    {
        procError();
        return false;
    }
    if((NULL == (cjson_start_index = cJSON_GetArrayItem(cjson_params,1))) 
        || (NULL == (cjson_end_index = cJSON_GetArrayItem(cjson_params,2))))
    {
        procError();
        return false;
    }
    start_index = cjson_start_index->valueint;
    end_index = cjson_end_index->valueint;
    return timerQueryTimerByIndex(start_index,end_index);
}
static bool ICACHE_FLASH_ATTR procQueryTimerById(cJSON *cjson_params)
{
    uint8_t array_num = 0;
    uint8_t id[IOTGO_TIMER_NUM] = {0};
    cJSON *cjson_id = NULL;
    uint8_t i = 0;
    if((NULL == cjson_params) 
        || (cJSON_Array != cjson_params->type)
        || ( 2 < (array_num = cJSON_GetArraySize(cjson_params))))
    {
        procError();
        return false;
    }
    cjson_id = cJSON_GetArrayItem(cjson_params,1);
    if((NULL == cjson_id) 
        || (cJSON_Array != cjson_id->type)
        || (iotgo_timer_info.timer_cnt < (array_num = cJSON_GetArraySize(cjson_id))))
    {
        procError();
        return false;
    }
    for(i = 0; i < array_num; i++)
    {
        cJSON *cjson_temp = NULL;
        if(NULL == (cjson_temp = cJSON_GetArrayItem(cjson_id,i)))
        {
            procError();
            return false;
        }
        id[i] = cjson_temp->valueint;
    }
    return timerQueryTimerById(id,array_num);
    
}


/*Timer manager*/
bool ICACHE_FLASH_ATTR timerProcQuery(cJSON *cjson_params,TimerManage manager)
{
    cJSON *cjson_query_type = NULL;
    timer_manager = manager;
    if(NULL == cjson_params || cJSON_Array != cjson_params->type
        || NULL == (cjson_query_type = cJSON_GetArrayItem(cjson_params,0)))
    {
        procError();
        return false;
    }
    if(0 == os_strcmp(IOTGO_STRING_TIMERINFO,cjson_query_type->valuestring))
    {
        return procQueryTimerInfo(cjson_params);
    }
    else if(0 == os_strcmp(IOTGO_STRING_TIMERINDEX,cjson_query_type->valuestring))
    {
        return procQueryTimerByIndex(cjson_params);
    }
    else if(0 == os_strcmp(IOTGO_STRING_TIMERID,cjson_query_type->valuestring))
    {
        return procQueryTimerById(cjson_params);
    }
    else
    {
        procError();
        return false;
    }
}

bool ICACHE_FLASH_ATTR timerProcUpdate(cJSON *cjson_params,TimerManage manager)
{
    cJSON *cjson_timer_act = NULL;
    cJSON *cjson_timer_id = NULL;
    cJSON *cjson_timer = NULL;
    timer_manager = manager;
    if(NULL == cjson_params)
    {
        procError();
        return false;
    }
    if(NULL == (cjson_timer_act = cJSON_GetObjectItem(cjson_params,IOTGO_STRING_TIMERACT)))
    {
        procError();
        return false;
    }
    if(0 == os_strcmp(IOTGO_STRING_DEL,cjson_timer_act->valuestring))
    {
        cjson_timer_id = cJSON_GetObjectItem(cjson_params,IOTGO_STRING_TIMERID);
        if(NULL == cjson_timer_id)
        {
            procError();
            return false;
        }
        return procDel(cjson_timer_id);
    }
    else if(0 == os_strcmp(IOTGO_STRING_NEW,cjson_timer_act->valuestring))
    {
        cjson_timer = cJSON_GetObjectItem(cjson_params,IOTGO_STRING_TIMER);
        if(NULL == cjson_timer)
        {
            procError();
            return false;
        }
        return procNew(cjson_timer);
    }
    else if(0 == os_strcmp(IOTGO_STRING_EDIT,cjson_timer_act->valuestring))
    {
        cjson_timer = cJSON_GetObjectItem(cjson_params,IOTGO_STRING_TIMER);
        if(NULL == cjson_timer)
        {
            procError();
            return false;
        }
        return procEdit(cjson_timer);
    }
    else if(0 == os_strcmp(IOTGO_STRING_ENABLE,cjson_timer_act->valuestring))
    {
        cjson_timer_id = cJSON_GetObjectItem(cjson_params,IOTGO_STRING_TIMERID);
        if(NULL == cjson_timer_id)
        {
            procError();
            return false;
        }
        return procEnable(cjson_timer_id);
    }
    else if(0 == os_strcmp(IOTGO_STRING_DISABLE,cjson_timer_act->valuestring))
    {
        cjson_timer_id = cJSON_GetObjectItem(cjson_params,IOTGO_STRING_TIMERID);
        if(NULL == cjson_timer_id)
        {
            procError();
            return false;
        }
        return procDisable(cjson_timer_id);
    }
    else if(0 == os_strcmp(IOTGO_STRING_DELALL,cjson_timer_act->valuestring))
    {
        return procDelAll();
    }
    else if(0 == os_strcmp(IOTGO_STRING_ENABLEALL,cjson_timer_act->valuestring))
    {
        return procEnableAll();
    }
    else if(0 == os_strcmp(IOTGO_STRING_DISABLEALL,cjson_timer_act->valuestring))
    {
        return procDisableAll();
    }
    else
    {
        procError();
        return false;
    }
}

void ICACHE_FLASH_ATTR timerFlashErase(void)
{
    iotgoTimerFlashParam *timer_flash_param = NULL;
    if(NULL == (timer_flash_param = (iotgoTimerFlashParam *)os_malloc(sizeof(iotgoTimerFlashParam))))
    {
        iotgoError("memory error");
        return;
    }
    os_memset(timer_flash_param,0,sizeof(iotgoTimerFlashParam));
    timer_flash_param->timer_num = IOTGO_TIMER_NUM;
    spi_flash_erase_sector(timer_flash_index.timer_flash_save1);
    spi_flash_erase_sector(timer_flash_index.timer_flash_save2);
    spi_flash_erase_sector(timer_flash_index.timer_flash_save3);
    spi_flash_erase_sector(timer_flash_index.timer_flash_save4);
    spi_flash_write((timer_flash_index.timer_flash_save1) * SPI_FLASH_SEC_SIZE,
        (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
    spi_flash_write((timer_flash_index.timer_flash_save2) * SPI_FLASH_SEC_SIZE,
        (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
    spi_flash_write((timer_flash_index.timer_flash_save3) * SPI_FLASH_SEC_SIZE,
        (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
    spi_flash_write((timer_flash_index.timer_flash_save4) * SPI_FLASH_SEC_SIZE,
        (uint32 *)timer_flash_param, sizeof(iotgoTimerFlashParam));
    os_free(timer_flash_param);
    timer_flash_param = NULL;
    os_memset(iotgo_timer_info,0,sizeof(iotgo_timer_info));
    os_memset(iotgo_timer_id,0,sizeof(iotgo_timer_id));
    os_memset(iotgo_timer,0,sizeof(iotgo_timer));
}
void ICACHE_FLASH_ATTR timerInit(IotgoTimerCallback timer_callback)
{
    iotgo_timer_action = timer_callback;
    timerLoadFromFlash();
    os_timer_disarm(&iotgo_timer_couter);
    os_timer_setfn(&iotgo_timer_couter, (os_timer_func_t *)timerDealTime, NULL);
    os_timer_arm(&iotgo_timer_couter,1000, 1);
}
iotgoTimerInfo ICACHE_FLASH_ATTR getIotgoTimerInfo(void)
{
    return iotgo_timer_info;
}

