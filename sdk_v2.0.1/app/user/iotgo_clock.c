#include "iotgo_clock.h"

static os_timer_t local_rtc_timer;
static uint32 count_down = 1;
static bool started = false;

static uint32 rtc_last_counter = 0;
static uint32 rtc_counter = 0;
static uint32 rtc_counter_diff = 0;
static uint32 rtc_time_diff = 0;

static uint32 rtc_period = 0;
static uint32 rtc_period_pint = 0;
static uint32 rtc_period_pdec = 0;

static uint32 micro = 0;

static IoTgoGMTTime current_time = {1970, 1, 1, 0, 0, 0};

static bool ICACHE_FLASH_ATTR verifyGMTTime(IoTgoGMTTime *gmt_time)
{
    if (!gmt_time)
    {
        return false;
    }
    
    if (gmt_time->year > 2000
        && (gmt_time->month >= 1 && gmt_time->month <= 12)
        && (gmt_time->day >= 1 && gmt_time->day <= 31)
        && (gmt_time->hour >= 0 && gmt_time->hour <= 23)
        && (gmt_time->minute >= 0 && gmt_time->minute <= 59)
        && (gmt_time->second >= 0 && gmt_time->second <= 59)
        )
    {
        return true;
    }
    return false;
}

void ICACHE_FLASH_ATTR printGMTTime(IoTgoGMTTime *gmt_time)
{
    iotgoInfo("[%04u-%02u-%02u %02u:%02u:%02u]", 
        gmt_time->year, gmt_time->month, gmt_time->day, 
        gmt_time->hour, gmt_time->minute, gmt_time->second);
}



/* 
2015-02-27T06:52:53.325Z

year:0-3
::4
month: 5-6
::7
day:8-9
T:10
hour:11-12
::13
minute:14-15
::16
second:17-18
Z:23
*/
bool ICACHE_FLASH_ATTR parseGMTTimeFromString(const char *str, IoTgoGMTTime *gmt_time)
{
    char temp[24] = {0};
    if (gmt_time && str)
    {
        if (os_strlen(str) == 24
            && str[10] == 'T'
            && str[23] == 'Z' )
        {
            os_bzero(temp, sizeof(temp));
            os_strncpy(temp, str + 0, 4);
            gmt_time->year = atoi(temp);
            
            os_bzero(temp, sizeof(temp));
            os_strncpy(temp, str + 5, 2);
            gmt_time->month = atoi(temp);
            
            os_bzero(temp, sizeof(temp));
            os_strncpy(temp, str + 8, 2);
            gmt_time->day = atoi(temp);
            
            os_bzero(temp, sizeof(temp));
            os_strncpy(temp, str + 11, 2);
            gmt_time->hour = atoi(temp);
            
            os_bzero(temp, sizeof(temp));
            os_strncpy(temp, str + 14, 2);
            gmt_time->minute = atoi(temp);
            
            os_bzero(temp, sizeof(temp));
            os_strncpy(temp, str + 17, 2);
            gmt_time->second = atoi(temp);
            
            if (verifyGMTTime(gmt_time))
            {
                //printGMTTime(gmt_time);
                return true;
            }
            else
            {
                iotgoError("Bad IoTgoGMTTime");
                return false;
            }
        }
    }
    
    return false;
}

/* 
 * 蔡勒公式:
 * 计算所得的数值对应的星期：0-星期日；1-星期一；2-星期二；3-星期三；4-星期四；5-星期五；6-星期六
 */
int8 ICACHE_FLASH_ATTR getWeek(int32 y, int8 m, int8 d)
{
    int8 c;
    int32 w;
    
    if (m < 3) {
        y -= 1;
        m += 12;
    }
    c = y/100;
    y %= 100;
    
    w = y + y/4 + c/4 - 2*c + ((26*(m+1))/10 + d - 1);
    w = ( w % 7 + 7 ) % 7;
    return (int8)w;
}

static bool ICACHE_FLASH_ATTR isLeapYear(int32 year)
{
    if (year < 0)
        return false;
    return ((year%4 == 0 && year%100 != 0) || year%400 == 0) ? true : false;
}

int8 ICACHE_FLASH_ATTR daysOfMonth(int32 year, int8 month)
{
    int8 days;
    switch(month) 
    {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            days = 31;
            break;
        case 4:
        case 6:
        case 9:
        case 11:
            days = 30;
            break;
        case 2:
            if (isLeapYear(year))
                days = 29;
            else
                days = 28;
            break;
        default:
            iotgoError("Invalid month = %d", month);
    }
    return days;
}

static void ICACHE_FLASH_ATTR localRTC(void *arg) 
{
    static uint32 rtc_divider = 1000000;
    
    if (count_down > 0) {
        count_down--;
        if (count_down == 0) {
            iotgoInfo("started\n", count_down);        
            started = true;
            rtc_last_counter = system_get_rtc_time();
        } else {
            iotgoInfo("start after %u seconds\n", count_down);
        }
    }
    if (started) {
        
        rtc_period = system_rtc_clock_cali_proc();
        rtc_counter = system_get_rtc_time();
        
        rtc_period_pint = rtc_period >> 12;
        rtc_period_pdec = rtc_period & 0xFFF;
        
        rtc_counter_diff = (0x100000000 + rtc_counter - rtc_last_counter) % (0x100000000);
        rtc_time_diff = rtc_counter_diff * rtc_period_pint
            + (uint32)((rtc_counter_diff * rtc_period_pdec * 1.0)/4095.0 + 0.5);
            
        rtc_last_counter = rtc_counter;
        
        micro += rtc_time_diff;
        if (micro >= rtc_divider) {
            current_time.second += micro / rtc_divider;
            micro %= rtc_divider;
            if (current_time.second >= 60) {
                current_time.minute += current_time.second / 60;
                current_time.second %= 60;
                if (current_time.minute >= 60) {
                    current_time.hour += current_time.minute / 60;
                    current_time.minute %= 60;
                    if (current_time.hour >= 24) {
                        current_time.day += current_time.hour / 24;
                        current_time.hour %= 24;
                        if (current_time.day > daysOfMonth(current_time.year, current_time.month))
                        {
                            current_time.day = 1;
                            current_time.month++;
                            if (current_time.month > 12)
                            {
                                current_time.month = 1;
                                current_time.year++;
                            }
                        }
                    }
                }
            }
        }
        iotgoInfo("[%04u-%02u-%02u %02u:%02u:%02u]", 
            current_time.year, current_time.month, current_time.day, 
            current_time.hour, current_time.minute, current_time.second);
    }
}

void ICACHE_FLASH_ATTR setGMTTime(IoTgoGMTTime gmt_time)
{
    os_timer_disarm(&local_rtc_timer);
    current_time = gmt_time;
    os_timer_arm(&local_rtc_timer, 1000, 1);
}

void ICACHE_FLASH_ATTR getGMTTime(IoTgoGMTTime *gmt_time) 
{
    if (gmt_time) 
    {
        gmt_time->year =  current_time.year;
        gmt_time->month =  current_time.month;
        gmt_time->day =  current_time.day;
        gmt_time->hour =  current_time.hour;
        gmt_time->minute =  current_time.minute;
        gmt_time->second =  current_time.second;
    }
}

void ICACHE_FLASH_ATTR startTimeService(void)
{   
    os_timer_disarm(&local_rtc_timer);
    os_timer_setfn(&local_rtc_timer, (os_timer_func_t *)localRTC, NULL);
    os_timer_arm(&local_rtc_timer, 1000, 1);
    iotgoInfo("startTimeService done");
}

void ICACHE_FLASH_ATTR printCron(const IoTgoDeviceCron *cron)
{
    if (!cron) return;
    
    iotgoInfo("[\nWeeks:%d,%d,%d,%d,%d,%d,%d", 
        cron->weeks[0], cron->weeks[1], cron->weeks[2], cron->weeks[3], 
        cron->weeks[4], cron->weeks[5], cron->weeks[6]);
    iotgoInfo("Hour:%d", cron->hour);
    iotgoInfo("Minute:%d\n]", cron->minute);
}

/*
"0 * * * *" 最短: 9
"10 * * * *"
"10 8 * * *"
"10 8 * * 1"
"10 8 * * 0,1,2"
"10 8 * * 0,3,2,6"
"10 18 * * 0,1,2,3,4,5,6" 最长: 23
*/
bool ICACHE_FLASH_ATTR parseCronFromString(const char *str, IoTgoDeviceCron *cron)
{
    uint8 str_len;
    int32 field_len;
    char *p = (char *)str;
    char *temp;
    char buffer[25];
    uint8 i;
    uint8 j;
    
    if (NULL == str || NULL == cron)
    {
        return false;
    }
    str_len = os_strlen(str);
    if (str_len < 9 || str_len > 23)
    {
        return false;
    }

    /* 解析分钟 */
    temp = (char *)os_strstr(p, " ");
    if (!temp || (temp - p <= 0))
    {
        return false;
    }
    field_len = temp - p;
    os_strncpy(buffer, p, field_len);
    buffer[field_len] = '\0';
    cron->minute = atoi(buffer);
    if (cron->minute < 0 || cron->minute > 59)
    {
        return false;
    }
    
    temp++;
    p = temp;
    
    /* 解析小时 */
    temp = (char *)os_strstr(p, " ");
    if (!temp || (temp - p <= 0))
    {
        return false;
    }
    field_len = temp - p;
    os_strncpy(buffer, p, field_len);
    buffer[field_len] = '\0';
    if (0 == os_strcmp(buffer, "*"))
    {
        cron->hour = 24;
    }
    else
    {
        cron->hour = atoi(buffer);    
        if (cron->hour < 0 || cron->hour > 23)
        {
            return false;
        }
    }

    /* 忽略月日 */
    p = temp + 5; 

    /* 解析周 */
    for (i = 0; i < 7; i++)
    {
        cron->weeks[i] = false;
    }

    /* 1 */
    /* 0,1,2,3,4,5,6 */
    os_strcpy(buffer, p);
    if (0 == os_strcmp(buffer, "*"))
    {
        for (i = 0; i < 7; i++)
        {
            cron->weeks[i] = true;
        }
    }
    else 
    {
        for (j = 0; j < os_strlen(buffer); j++)
        {
            if (buffer[j] != ',')
            {
                i = buffer[j] - 48;
                
                if (i < 0 || i > 6)
                {
                    return false;
                }
                cron->weeks[i] = true;
            }
        }
    }
    //printCron(cron);
    return true;
    
}
