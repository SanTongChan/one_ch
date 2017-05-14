#include "iotgo.h"
#if defined(COMPILE_IOTGO_FWSW_01)
#define KEY_DEBONCE_INTERVAL    (25)
#define KEY_LONG_PRESS_TIME     (5000)
#define KEY_DEBONCE_FIRST_TIME  (KEY_DEBONCE_INTERVAL * 2)
#define KEY_DEBONCE_SECOND_TIME (KEY_DEBONCE_INTERVAL * 2)

typedef enum {
    ITDB04_CONFIG_STARTUP_OFF = 0x0,
    ITDB04_CONFIG_STARTUP_ON = 0x1,
    ITDB04_CONFIG_STARTUP_STAY = 0x2,
} Itdb04ConfigConstant;

/* 
 * 该结构体必须长度4字节对齐，否则flash会出问题，切记!
 * 为保证兼容性，此结构体的数据字段顺序绝对不能修改，只能向后添加字段。
 */
typedef struct {
    uint32 startup;             /* 0x0:off, 0x1:on, 其他情况均为off */
    uint32 latest_switch_state; /* 最新的继电器状态 */
    uint32 repower_times;       /* repower times for default setting mode directly */
    uint32 __reserved;          /* DO NOT touch it! */
} Itdb04Config;

/**
 * itdb04_config 变量的操作步骤是
 *   1. 从Flash加载数据并初始化该对象；
 *   2. 修改该对象的成员变量；
 *   3. 将该对象写入Flash；
 *
 * @warning
 *  第二步和第三步是必须是原子结合的操作，否则会造成脏数据；
 */
static Itdb04Config itdb04_config = {0};

#define STR_SWITCH_ON  "on"
#define STR_SWITCH_OFF "off"
#define TIMER_TYPE_ONCE "once"
#define TIMER_TYPE_REPEAT "repeat"
#define TIMER_TYPE_DURATION "duration"
#define GMT_STRING_MIN_LENGTH (24)

static char device_json_buffer[IOTGO_JSON_BUFFER_SIZE];
static uint8 switch_state = SWITCH_OFF;

static struct jsontree_string json_field_timers = JSONTREE_STRING(IOTGO_STRING_TIMERS);
static struct jsontree_string json_field_switch = JSONTREE_STRING(IOTGO_STRING_SWITCH);
static struct jsontree_string json_switch_value = JSONTREE_STRING(IOTGO_STRING_OFF);
static struct jsontree_string json_starup_value = JSONTREE_STRING(IOTGO_STRING_OFF);
static struct jsontree_string json_sta_mac  = JSONTREE_STRING("");

static uint32 key_pressed_level_counter = 0;
static uint32 key_released_level_counter = 0;
static os_timer_t key_isr_timer;

#define ITDB04_REPOWER_TIMES_TIMER_DELAY_SECONDS    (10)
static os_timer_t itdb04_repower_times_timer;

JSONTREE_OBJECT(switch_params_obj,
                JSONTREE_PAIR(IOTGO_STRING_SWITCH, &json_switch_value),
                );

JSONTREE_OBJECT(switch_params_with_version_obj,
                JSONTREE_PAIR(IOTGO_STRING_SWITCH, &json_switch_value),
                JSONTREE_PAIR(IOTGO_STRING_FWVERSION, &json_fw_version),
                JSONTREE_PAIR(IOTGO_STRING_RSSI, &json_int_rssi),
                JSONTREE_PAIR(IOTGO_STRING_STA_MAC, &json_sta_mac),
                JSONTREE_PAIR(IOTGO_STRING_STARTUP,&json_starup_value),
                );

JSONTREE_OBJECT(iotgo_tree_update_obj,
                JSONTREE_PAIR(IOTGO_STRING_USERAGENT, &json_device),
                JSONTREE_PAIR(IOTGO_STRING_APIKEY, &json_owner_uuid),
                JSONTREE_PAIR(IOTGO_STRING_DEVICEID, &json_deviceid),
                JSONTREE_PAIR(IOTGO_STRING_ACTION, &json_action_update),
                JSONTREE_PAIR(IOTGO_STRING_PARAMS, &switch_params_obj),
                );
                
JSONTREE_OBJECT(iotgo_tree_update,
                JSONTREE_PAIR("iotgo_tree_update", &iotgo_tree_update_obj)
                );

JSONTREE_OBJECT(iotgo_tree_update_with_version_obj,
                JSONTREE_PAIR(IOTGO_STRING_USERAGENT, &json_device),
                JSONTREE_PAIR(IOTGO_STRING_APIKEY, &json_owner_uuid),
                JSONTREE_PAIR(IOTGO_STRING_DEVICEID, &json_deviceid),
                JSONTREE_PAIR(IOTGO_STRING_ACTION, &json_action_update),
                JSONTREE_PAIR(IOTGO_STRING_PARAMS, &switch_params_with_version_obj),
                );
                
JSONTREE_OBJECT(iotgo_tree_update_with_version,
                JSONTREE_PAIR("iotgo_tree_update_with_version", &iotgo_tree_update_with_version_obj)
                );

JSONTREE_ARRAY(switch_params_array,
                JSONTREE_PAIR_ARRAY(&json_field_timers),
                );
                
JSONTREE_OBJECT(iotgo_tree_query_obj,
                JSONTREE_PAIR(IOTGO_STRING_USERAGENT, &json_device),
                JSONTREE_PAIR(IOTGO_STRING_APIKEY, &json_owner_uuid),
                JSONTREE_PAIR(IOTGO_STRING_DEVICEID, &json_deviceid),
                JSONTREE_PAIR(IOTGO_STRING_ACTION, &json_action_query),
                JSONTREE_PAIR(IOTGO_STRING_PARAMS, &switch_params_array),
                );
                
JSONTREE_OBJECT(iotgo_tree_query,
                JSONTREE_PAIR("iotgo_tree_query", &iotgo_tree_query_obj)
                );

JSONTREE_OBJECT(iotgo_tree_resp_server_error_0_obj,
                JSONTREE_PAIR(IOTGO_STRING_ERROR, &json_error_0),
                JSONTREE_PAIR(IOTGO_STRING_USERAGENT, &json_device),
                JSONTREE_PAIR(IOTGO_STRING_APIKEY, &json_owner_uuid),
                JSONTREE_PAIR(IOTGO_STRING_DEVICEID, &json_deviceid),
                JSONTREE_PAIR(IOTGO_STRING_SEQUENCE, &json_server_sequence),
                );

JSONTREE_OBJECT(iotgo_tree_resp_server_error_0,
                JSONTREE_PAIR("iotgo_tree_resp_server_error_0", &iotgo_tree_resp_server_error_0_obj)
                );

static void setStartupParam(char *startup_val)
{
    //os_strcpy(json_starup_value.value,startup_val);
    json_starup_value.value = startup_val;
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
        iotgoInfo("[IOTGO Debug:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);
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

    /*只有 interval */
    if(temp == NULL)
    {
        field_len = str_len-(p -at_str);
        os_bzero(buffer,sizeof(buffer));
        os_strncpy(buffer,p,field_len);
        buffer[field_len] = '\0';
        *interval = atoi(buffer);
        *duration = 0;
        iotgoInfo("[IOTGO Debug:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);
        return true; ;
    }

    field_len = temp -p;
    os_bzero(buffer,sizeof(buffer));
    os_strncpy(buffer,p,field_len);
    buffer[field_len] = '\0';
    *interval = atoi(buffer);
    
    /* 解析duartion */
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

static inline void ICACHE_FLASH_ATTR switchON(void)
{
    GPIO_OUTPUT_SET(SWITCH_OUTPUT_GPIO, SWITCH_ON);
    switch_state = SWITCH_ON;
    itdb04_config.latest_switch_state= switch_state;
    devSaveConfigToFlash(&itdb04_config, sizeof(itdb04_config));
}

static inline void ICACHE_FLASH_ATTR switchOFF(void)
{
    GPIO_OUTPUT_SET(SWITCH_OUTPUT_GPIO, SWITCH_OFF);
    switch_state = SWITCH_OFF;
    itdb04_config.latest_switch_state= switch_state;
    devSaveConfigToFlash(&itdb04_config, sizeof(itdb04_config));   
}

static void ICACHE_FLASH_ATTR switchActionCallback(void *switch_val)
{
    
    char *p = (char*)switch_val;
    if(p == NULL)
    {
        iotgoInfo("switch_val is NULL pointer");
        return;
    }
    if(os_strcmp(p,STR_SWITCH_ON) == 0)
    {       
        switchON();
    }
    else if(os_strcmp(p,STR_SWITCH_OFF) == 0)
    {
        switchOFF();
    }
    iotgoInfo("switch_val is %s",(char*)switch_val);
    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_BY_TIMER, 0);
}

static bool ICACHE_FLASH_ATTR procOneTimer(struct jsonparse_state *pjs)
{
    int cnt = 0;

    int8 type;
    int8 type1;
    int8 type2;

    int32 timer_enabled = 0;
    char timer_type[10] = {0}; /* once | repeat */
    char timer_at[50] = {0};
    char timer_gmt[30] = {0};
    char timer_do_switch[10] = {0};
    char timer_start_do[10] = {0};
    char timer_end_do[10] = {0};

    /* ?a??intervaloíduration */
    uint32 timer_duration = 0;
    uint32 timer_interval = 0;

    bool field_enabled_flag = false;
    bool field_type_flag = false;
    bool field_at_flag = false;
    bool field_do_flag = false;
    bool field_start_do_flag = false;
    bool field_end_do_flag = false;
    while (
        (!field_enabled_flag || !field_type_flag || !field_at_flag || !(field_do_flag ||(field_start_do_flag && field_end_do_flag)))
        && (type = jsonparse_next(pjs)) != ']' 
        && type != 0
        )
    {
        iotgoDebug("index = %d, type = %d (%c), vlen = %d\n", ++cnt, type, type, 
            jsonparse_get_len(pjs));

        if (JSON_TYPE_PAIR_NAME != type)
        {
            continue;
        }

        if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_ENABLED))
        {
            if (jsonIoTgoGetNumber(pjs, IOTGO_STRING_ENABLED, &timer_enabled))
            {
                field_enabled_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_TYPE))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_TYPE, timer_type, sizeof(timer_type)))
            {
                field_type_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_AT))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_AT, timer_at, sizeof(timer_at)))
            {
                field_at_flag = true;
            }
            else
            {
                break;
            }
        }    
        else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_DO))
        {
            type1 = jsonparse_next(pjs);
            type2 = jsonparse_next(pjs);
            if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_OBJECT== type2)
            {
                while ((type = jsonparse_next(pjs)) != '}' && type != 0)
                {
                    if (JSON_TYPE_PAIR_NAME != type)
                    {
                        continue;
                    } 
                    if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_SWITCH))
                    {
                        if(jsonIoTgoGetString(pjs, IOTGO_STRING_SWITCH, timer_do_switch, sizeof(timer_do_switch)))
                        {
                            field_do_flag = true;
                            iotgoInfo("<<<<parase do flag right >>>>>>>>");
                        }
                        else
                        {
                            break;
                        }
                    }

                }

            }
      
        }
        else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_START_DO))
        {
            type1 = jsonparse_next(pjs);
            type2 = jsonparse_next(pjs);
            if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_OBJECT== type2)
            {
                while ((type = jsonparse_next(pjs)) != '}' && type != 0)
                {
                    if (JSON_TYPE_PAIR_NAME != type)
                    {
                        continue;
                    } 
                    if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_SWITCH))
                    {
                        if(jsonIoTgoGetString(pjs, IOTGO_STRING_SWITCH, timer_start_do, sizeof(timer_start_do)))
                        {
                            field_start_do_flag = true;
                        }
                        else 
                        {
                            break;
                        }
                    }

                }

            }
      
        }
        else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_END_DO))
        {
            type1 = jsonparse_next(pjs);
            type2 = jsonparse_next(pjs);
            if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_OBJECT== type2)
            {
                while ((type = jsonparse_next(pjs)) != '}' && type != 0)
                {
                    if (JSON_TYPE_PAIR_NAME != type)
                    {
                        continue;
                    } 
                    if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_SWITCH))
                    {
                        if(jsonIoTgoGetString(pjs, IOTGO_STRING_SWITCH, timer_end_do, sizeof(timer_end_do)))
                        {
                            field_end_do_flag = true;
                        }
                        else 
                        {
                            break;
                        }
                    }
                }

            }
      
        }


     }
    iotgoInfo("procOneTimer()[field_do_flag == [%d],field_start_do_flag == [%d],field_end_do_flag == [%d]]",field_do_flag,field_start_do_flag,field_end_do_flag);

    if (field_enabled_flag
        && field_type_flag
        && field_at_flag && (field_do_flag || field_start_do_flag || field_end_do_flag))
    {
        if(os_strcmp(timer_type,TIMER_TYPE_ONCE) == 0)
        {  
             /* GMT类型定时器 uint32强制类型转换，要引起重视 */
             addTimeObjectByGMTString((bool)timer_enabled,timer_at,switchActionCallback,timer_do_switch,os_strlen(timer_do_switch));
             return true;
        }
        else if(os_strcmp(timer_type,TIMER_TYPE_REPEAT) == 0)
        {
             /* CRON型定时器 */
             addTimeObjectByCronString((bool)timer_enabled,timer_at,switchActionCallback,timer_do_switch,os_strlen(timer_do_switch));
             return true;
        }
  
        else if(os_strcmp(timer_type,TIMER_TYPE_DURATION) == 0)
        {
            iotgoInfo("timer_at == [%s]",timer_at);
            if(parseFromATString(timer_at,timer_gmt,&timer_interval,&timer_duration))
            {
                if((timer_duration <= 0) && (timer_interval > 0) && field_do_flag)
                {
                    /* 每隔型的定时器 */
                    addTimeObjectInterval((bool)timer_enabled,timer_gmt,timer_interval,switchActionCallback,
                                                timer_do_switch,os_strlen(timer_do_switch));
                    iotgoInfo("program in addTimeObjectInterval");                            
                    return true;                            
                }
                else if((timer_duration >0) && (timer_interval > timer_duration))
                {
                    if((field_do_flag))
                    {
                        /* 每隔型+持续型定时器 */
                        addTimeObjectIntervalDuration((bool)timer_enabled,timer_gmt,timer_interval,timer_duration,
                                                switchActionCallback,timer_do_switch,os_strlen(timer_do_switch));
                        iotgoInfo("program in addTimeObjectIntervalDuration");        
                        return true;     
                    }
                    else if(field_start_do_flag && field_end_do_flag)
                    {   
                        /* 每隔型+持续型定时器+starDo+endDo */
                        addTimeObjectDurationStartEnd((bool)timer_enabled,timer_gmt,timer_interval,timer_duration,
                                                switchActionCallback,timer_start_do,os_strlen(timer_start_do),switchActionCallback,timer_end_do,os_strlen(timer_end_do));
                        iotgoInfo("program in addTimeObjectDurationStartEnd");                         
                        return true;                        
                    }
                }
                else
                {
                    iotgoInfo("timer_duration failed!\n");
                    return false;
                }    
            }
            else 
            {
                iotgoInfo("praseFromAtString failed! \n");
                return false ;
            }
            
        }
    
    }
    else
    {
        iotgoWarn("Timer fields lost! Remove all timers!");
        stopTimerMonitor();
        return false;
    }
}
    



static bool ICACHE_FLASH_ATTR jsonIoTgoProcTimers(struct jsonparse_state *pjs)
{
    int8 type;
    int8 type1;
    int8 type2;
    
    type1 = jsonparse_next(pjs);
    type2 = jsonparse_next(pjs);
    if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_ARRAY == type2)
    {
        stopTimerMonitor();
        while ((type = jsonparse_next(pjs)) != ']' && type != 0)
        {
            if (JSON_TYPE_OBJECT == type)
            {
                if (!procOneTimer(pjs))
                {
                    iotgoWarn("Invalid timers!");
                    return false;
                }
            }
        }
        
        if (type == ']')
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_NUMBER == type2)
    {
        stopTimerMonitor();
        int32 number = jsonparse_get_value_as_int(pjs);
        iotgoInfo("timers = [] and number = %d", number);
        return true;
    }
    else
    {
        iotgoWarn("Invalid timers field!");
        return false;
    }
}

static void ICACHE_FLASH_ATTR sendError0ToServer(void)
{
    os_memset(device_json_buffer, 0, sizeof(device_json_buffer));
    json_ws_send((struct jsontree_value *)&iotgo_tree_resp_server_error_0, "iotgo_tree_resp_server_error_0", device_json_buffer);
    iotgoQueueAdd(device_json_buffer, IOTGO_NULL, false);
}


static void ICACHE_FLASH_ATTR switchUpdate(void)
{
    if (SWITCH_ON == switch_state)
    {
        json_switch_value.value = IOTGO_STRING_ON;
    }
    else
    {
        json_switch_value.value = IOTGO_STRING_OFF;
    }
    os_memset(device_json_buffer, 0, sizeof(device_json_buffer));
    json_ws_send((struct jsontree_value *)&iotgo_tree_update, "iotgo_tree_update", device_json_buffer);
    iotgoQueueAdd(device_json_buffer, IOTGO_NULL, false);
}

static void ICACHE_FLASH_ATTR switchUpdateWithVersion(void)
{
    if (SWITCH_ON == switch_state)
    {
        json_switch_value.value = IOTGO_STRING_ON;
    }
    else
    {
        json_switch_value.value = IOTGO_STRING_OFF;
    }
    
    os_memset(device_json_buffer, 0, sizeof(device_json_buffer));
    json_ws_send((struct jsontree_value *)&iotgo_tree_update_with_version, "iotgo_tree_update_with_version", device_json_buffer);
    iotgoQueueAdd(device_json_buffer, IOTGO_NULL, false);
}

static void ICACHE_FLASH_ATTR switchRespUpdateCallback(const char *data)
{
    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_OK, 0);
}


static void ICACHE_FLASH_ATTR switchQuery(void)
{
    os_memset(device_json_buffer, 0, sizeof(device_json_buffer));
    json_ws_send((struct jsontree_value *)&iotgo_tree_query, "iotgo_tree_query", device_json_buffer);
    iotgoQueueAdd(device_json_buffer, IOTGO_NULL, false);
}

static void ICACHE_FLASH_ATTR switchRespQueryCallback(const char *data)
{
    bool ret = false;
    int cnt = 0;

    int8 type;
    int8 type1;
    int8 type2;
    
    char param_timer[10] = {0};
    bool field_params_flag = false;

#if 0    
    data = "{"
"\"error\":0,"
"\"params\":{"
    "\"switch\":\"off\""
    ",\"timers\":["
        "{\"enabled\":1,\"type\":\"once\",\"at\":\"2015-03-05T09:18:00.000Z\",\"do\":{\"switch\":\"on\"}}"
        ",{\"enabled\":1,\"type\":\"once\",\"at\":\"2015-03-05T09:19:00.000Z\",\"do\":{\"switch\":\"off\"}}"
        ",{\"enabled\":1,\"type\":\"once\",\"at\":\"2015-03-05T09:20:00.000Z\",\"do\":{\"switch\":\"on\"}}"
        ",{\"enabled\":1,\"type\":\"repeat\",\"at\":\"21 9 * * *\",\"do\":{\"switch\":\"off\"}}"
        ",{\"enabled\":1,\"type\":\"repeat\",\"at\":\"22 9 * * 0,1,2\",\"do\":{\"switch\":\"on\"}}"
        ",{\"enabled\":1,\"type\":\"repeat\",\"at\":\"23 * * * 0,1,4\",\"do\":{\"switch\":\"off\"}}"        
    "]"
"}"
",\"deviceid\":\"0180000001\""
",\"apikey\":\"5f1b8f7a-19ba-4daa-9c9b-538d503644cc\""
"}";

    iotgoInfo("data = [%s]", data);
#endif


    struct jsonparse_state *pjs = (struct jsonparse_state *)os_zalloc(sizeof(struct jsonparse_state));
    
    jsonparse_setup(pjs, data, os_strlen(data));
    while ((type = jsonparse_next(pjs)) != 0)
    {
        iotgoDebug("index = %d, type = %d (%c), vlen = %d\n", ++cnt, type, type, 
            jsonparse_get_len(pjs));

        if (JSON_TYPE_PAIR_NAME != type)
        {
            continue;
        }
        
        if (!field_params_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_PARAMS))
        {
            type1 = jsonparse_next(pjs);
            type2 = jsonparse_next(pjs);
            if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_OBJECT== type2)
            {
                field_params_flag = true;
            }
            else if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_NUMBER == type2)
            {
                field_params_flag = true;
                stopTimerMonitor();
                ret = true;
                break;
            }
            else
            {   
                break;
            }
        }
        else if (field_params_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_TIMERS))
        {
            ret = jsonIoTgoProcTimers(pjs);
            break;
        }
    }
    iotgoDebug("while parse done\n");
    
    os_free(pjs);

    if (field_params_flag && ret)
    {
        startTimerMonitor();
        system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_QUERY_OK, 0);
    }
}


static void ICACHE_FLASH_ATTR switchReqUpdateCallback(const char *data)
{
    int cnt = 0;

    int8 type;
    int8 type1;
    int8 type2;
    
    char server_sequence[30] = {0};
    char param_switch[10] = {0};
    char param_startup[10] = {0};
    
    bool field_server_sequence_flag = false;
    bool field_params_flag          = false;
    bool field_param_switch_flag    = false;
    bool field_timers_flag          = false;
    bool field_timers_ret           = false;    
    bool field_param_startup_flag   = false;
    
#if 0    
    data = "{"
"\"error\":0,"
"\"params\":{"
    "\"switch\":\"off\""
    ",\"timers\":["
        "{\"enabled\":1,\"type\":\"once\",\"at\":\"2015-02-27T06:52:53.325Z\",\"do\":{\"switch\":\"on\"}}"
        ",{\"enabled\":1,\"type\":\"repeat\",\"at\":\"30 8 * * 1,3,5\",\"do\":{\"switch\":\"on\"}}"
        ",{\"enabled\":1,\"type\":\"repeat\",\"at\":\"30 8 * * 2,4,6\",\"do\":{\"switch\":\"off\"}}"
    "]"
"}"
",\"deviceid\":\"0180000001\""
",\"apikey\":\"5f1b8f7a-19ba-4daa-9c9b-538d503644cc\""
"}";

    iotgoInfo("data = [%s]", data);
#endif

    struct jsonparse_state *pjs = (struct jsonparse_state *)os_zalloc(sizeof(struct jsonparse_state));
    
    jsonparse_setup(pjs, data, os_strlen(data));
    while ((type = jsonparse_next(pjs)) != 0)
    {
        iotgoDebug("index = %d, type = %d (%c), vlen = %d\n", ++cnt, type, type, 
            jsonparse_get_len(pjs));

        if (JSON_TYPE_PAIR_NAME != type)
        {
            continue;
        }
        
        if (!field_server_sequence_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_SEQUENCE))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_SEQUENCE, server_sequence, sizeof(server_sequence)))
            {
                field_server_sequence_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_params_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_PARAMS))
        {
            type1 = jsonparse_next(pjs);
            type2 = jsonparse_next(pjs);
            if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_OBJECT== type2)
            { 
 
                field_params_flag = true;
                while ((type = jsonparse_next(pjs)) != '}' && type != 0)
                {
                    if (JSON_TYPE_PAIR_NAME != type)
                    {
                        continue;
                    }
                    
                    if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_SWITCH))
                    {
                        if (jsonIoTgoGetString(pjs, IOTGO_STRING_SWITCH, param_switch, sizeof(param_switch)))
                        {
                            field_param_switch_flag = true;
                        }
                        else
                        {
                            break;
                        }
                    }
                    else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_TIMERS))
                    {
                        field_timers_flag = true;
                        field_timers_ret = jsonIoTgoProcTimers(pjs);
                        if (!field_timers_ret)
                        {
                            break;
                        }
                    }
                    else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_STARTUP))
                    {
                        if (jsonIoTgoGetString(pjs, IOTGO_STRING_STARTUP, param_startup, sizeof(param_startup)))
                        {
                            field_param_startup_flag = true;
                        }
                        else
                        {
                            break;
                        }
                    }
                    else
                    {
                        /* do nothing! */
                    }
                }
            }
            else
            {
                iotgoWarn("Invalid params field! Ignore this package!\n");
                break;
            }
        }
        else
        {
            /* do nothing! */
        }
    }
    iotgoDebug("while parse done\n");
    
    os_free(pjs);

    if (field_params_flag)
    {
        if (field_param_switch_flag)
        {
            if (0 == os_strcmp(param_switch, IOTGO_STRING_ON))
            {
                switchON();
            }
            else
            {
                switchOFF();
            }
        }
        
        if (field_timers_flag && field_timers_ret)
        {
            startTimerMonitor();
        }

        if (field_param_startup_flag)
        {
            if (0 == os_strcmp(IOTGO_STRING_ON, param_startup))
            {
                iotgoInfo("get IOTGO_STRING_ON");
                itdb04_config.startup = ITDB04_CONFIG_STARTUP_ON;
                setStartupParam(IOTGO_STRING_ON);
                devSaveConfigToFlash(&itdb04_config, sizeof(itdb04_config));
            }
            else if (0 == os_strcmp(IOTGO_STRING_OFF, param_startup))
            {
                iotgoInfo("get IOTGO_STRING_OFF");
                itdb04_config.startup = ITDB04_CONFIG_STARTUP_OFF;
                setStartupParam(IOTGO_STRING_OFF);
                devSaveConfigToFlash(&itdb04_config, sizeof(itdb04_config));

            }
            else if (0 == os_strcmp(IOTGO_STRING_STAY, param_startup))
            {
                iotgoInfo("get IOTGO_STRING_STAY");
                itdb04_config.startup = ITDB04_CONFIG_STARTUP_STAY;
                setStartupParam(IOTGO_STRING_STAY);
                devSaveConfigToFlash(&itdb04_config, sizeof(itdb04_config));

            }
            else
            {
                iotgoWarn("Invalid field(%s:%s)", IOTGO_STRING_STARTUP, param_startup);
            }
        }
        
        if (field_server_sequence_flag)
        {
            os_strcpy(server_sequence_value, server_sequence);
        }
        else
        {
            os_strcpy(server_sequence_value, IOTGO_STRING_INVALID_SEQUENCE);
        }
        
        system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_BY_REMOTE, 0);
    }
    else
    {
        iotgoError("Fields lost and ignored pkg!");
    }
    
}

static void ICACHE_FLASH_ATTR switchDevicePostCenter(os_event_t *events)
{
    static bool first_updated_flag = true;
    switch (events->sig)
    {
        case SIG_DEVICE_READYED: /* 设备已经完成启动 */
        {
            iotgoInfo("SIG_DEVICE_READYED");
            stopTimerMonitor();
            system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE, 0);
            first_updated_flag = true;
        }
        break;
        case SIG_DEVICE_UPDATE:
        {
            iotgoInfo("SIG_DEVICE_UPDATE");
            switchUpdateWithVersion();
            
        }break;
        case SIG_DEVICE_UPDATE_OK:
        {
            iotgoInfo("SIG_DEVICE_UPDATE_OK");
            if (first_updated_flag)
            {
                first_updated_flag = false;
                system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_QUERY, 0);
            }
            
        }break;
        case SIG_DEVICE_QUERY:
        {
            iotgoInfo("SIG_DEVICE_QUERY");
            switchQuery();

        }break;
        case SIG_DEVICE_QUERY_OK:
        {
            iotgoInfo("SIG_DEVICE_QUERY_OK");
            
        } break;
        case SIG_DEVICE_UPDATE_BY_LOCAL:
        {
            iotgoInfo("SIG_DEVICE_UPDATE_BY_LOCAL");
            switchUpdateWithVersion();
            
        }break;
        case SIG_DEVICE_UPDATE_BY_TIMER:
        {
            iotgoInfo("SIG_DEVICE_UPDATE_BY_TIMER");
            switchUpdateWithVersion();
            
        } break;
        case SIG_DEVICE_UPDATE_BY_REMOTE:
        {
            iotgoInfo("SIG_DEVICE_UPDATE_BY_REMOTE");
            sendError0ToServer();
            
        }break;
        default:
        {
            iotgoError("Unknown SIGNAL DEVICE = %u and ignored!", events->sig);
        }
    }
}

static void ICACHE_FLASH_ATTR keyISRStage2(void *arg)
{
    static bool long_press_processed = false;
    
    /* Read pin */
    if (SYS_KEY_GPIO_RELEASED_LEVEL == GPIO_INPUT_GET(SWITCH_INPUT_GPIO)) 
    {
        if (key_pressed_level_counter > 0)
        {
            key_released_level_counter++;
        }
    }
    else
    {
        if (0 == key_released_level_counter)
        {
            key_pressed_level_counter++;
            if (key_pressed_level_counter >= (KEY_LONG_PRESS_TIME / KEY_DEBONCE_INTERVAL))
            {
                key_pressed_level_counter = (KEY_LONG_PRESS_TIME / KEY_DEBONCE_INTERVAL);

                IoTgoDeviceMode current_mode = iotgoDeviceMode();
                if (DEVICE_MODE_SETTING_SELFAP != current_mode)
                {
                    if (!long_press_processed)
                    {
                        long_press_processed = true;
                        system_os_post(IOTGO_CORE, 
                            MSG_CORE_ENTER_SETTING_SELFAP_MODE, 0);
                    }
                }
                else
                {
                    long_press_processed = true;
                    //iotgoInfo("long key pressed do nothing!");
                }
            }
        }
    }

    if (key_released_level_counter >= (KEY_DEBONCE_SECOND_TIME / KEY_DEBONCE_INTERVAL))
    {
        if (key_pressed_level_counter >= (KEY_DEBONCE_FIRST_TIME / KEY_DEBONCE_INTERVAL)
            && !long_press_processed)
        {
            IoTgoDeviceMode current_mode = iotgoDeviceMode();
            if (DEVICE_MODE_SETTING_SELFAP == current_mode)
            {
                system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_SELFAP_MODE, 0);
            }
            else
            {
                if (SWITCH_ON == switch_state)
                {
                    switchOFF();
                }
                else
                {
                    switchON();
                }

                if (DEVICE_MODE_WORK_NORMAL == current_mode) 
                {
                    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_BY_LOCAL, 0);   
                }
            }
        }
        
        long_press_processed = false;
        os_timer_disarm(&key_isr_timer);
        key_released_level_counter = 0;
        key_pressed_level_counter = 0;
        gpio_pin_intr_state_set(GPIO_ID_PIN(SWITCH_INPUT_GPIO), SYS_KEY_GPIO_TRIGGER_METHOD);
    }
}

static void keyISR(void *pdata)
{
    uint32 gpio_status;
    ETS_GPIO_INTR_DISABLE();
    
    gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

    if (gpio_status & BIT((uint32)pdata)) 
    {
        gpio_pin_intr_state_set(GPIO_ID_PIN(SWITCH_INPUT_GPIO), GPIO_PIN_INTR_DISABLE);
        
        key_pressed_level_counter = 0;
        key_released_level_counter = 0;
        os_timer_disarm(&key_isr_timer);
        os_timer_setfn(&key_isr_timer, (os_timer_func_t *)keyISRStage2, NULL);
        os_timer_arm(&key_isr_timer, KEY_DEBONCE_INTERVAL, 1);
    }
    
    //clear interrupt status
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
    ETS_GPIO_INTR_ENABLE();
}

static void ICACHE_FLASH_ATTR interruptInit(void)
{
    /* Init switch input gpio interrupt */
    ETS_GPIO_INTR_DISABLE();
    ETS_GPIO_INTR_ATTACH(keyISR, (void *)SWITCH_INPUT_GPIO);
    PIN_FUNC_SELECT(SWITCH_INPUT_GPIO_NAME, SWITCH_INPUT_GPIO_FUNC);
    GPIO_DIS_OUTPUT(SWITCH_INPUT_GPIO); /* Set as input pin */
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(SWITCH_INPUT_GPIO)); /* clear status */
    gpio_pin_intr_state_set(GPIO_ID_PIN(SWITCH_INPUT_GPIO), SYS_KEY_GPIO_TRIGGER_METHOD); /* enable interrupt */
    ETS_GPIO_INTR_ENABLE();
}

static void ICACHE_FLASH_ATTR switchInitIoTgoDevice(void)
{
    /* 初始化 sys key */
    interruptInit(); 
    /* Init switch output relay gpio and set to low as default */
    PIN_FUNC_SELECT(SWITCH_OUTPUT_GPIO_NAME, SWITCH_OUTPUT_GPIO_FUNC);

    /* 从Flash读取Config参数 */
    devLoadConfigFromFlash(&itdb04_config,sizeof(itdb04_config));

    if (!iotgoIsXrstKeepGpio())
    {
        /* 根据用户配置决定初始化状态 */
        if (ITDB04_CONFIG_STARTUP_OFF == itdb04_config.startup)
        {
            switchOFF();
            setStartupParam(IOTGO_STRING_OFF);
        }
        else if (ITDB04_CONFIG_STARTUP_ON == itdb04_config.startup)
        {
            switchON();
            setStartupParam(IOTGO_STRING_ON);
        }
        else if (ITDB04_CONFIG_STARTUP_STAY == itdb04_config.startup)
        {
            if(SWITCH_ON == itdb04_config.latest_switch_state)
            {
                switchON();
            }
            else 
            {
                switchOFF();    
            }
            setStartupParam(IOTGO_STRING_STAY);
        }
        else
        {
            switchOFF();
            setStartupParam(IOTGO_STRING_OFF);  
        }
    }
    else
    {
        if (GPIO_INPUT_GET(SWITCH_OUTPUT_GPIO) == SWITCH_ON)
        {
            switchON();
        }
        else
        {
            switchOFF();
        }

        if (ITDB04_CONFIG_STARTUP_ON == itdb04_config.startup)
        {
            setStartupParam(IOTGO_STRING_ON);
        }
        else if (ITDB04_CONFIG_STARTUP_STAY == itdb04_config.startup)
        {
            setStartupParam(IOTGO_STRING_STAY);
        }
        else
        {
            setStartupParam(IOTGO_STRING_OFF);  
        }
    }
    json_sta_mac.value = iotgoStationMac();
}

/**
 * 这个API的实现不能修改，因为需要和之前已生产的改装件数据的默认状态保持一致！
 */
static void switchInitDeviceConfig(void)
{
    devEraseConfigInFlash();
}

static void ICACHE_FLASH_ATTR itdb04RepowerTimesTimerCallback(void *arg)
{
    itdb04_config.repower_times = 0;
    devSaveConfigToFlash(&itdb04_config, sizeof(itdb04_config));
    iotgoInfo("repower_times reset to 0");
}

/**
 * 网络中心启动时回调该函数
 * 
 * @return  true - enter default setting mode, false - workflow.
 * @note
 *  1. 从Flash读取充上电次数；
 *  2. 是否小于5且不等于0xFFFFFFFF；
 *  3. 如果等于5，则清零存储到Flash；进入默认配置模式；
 *  4. 否则上电次数累加，存入Flash；同时开启10秒后清零存储到Flash的定时器；
 */
static bool ICACHE_FLASH_ATTR itdb04IsEnterDefaultSettingMode(void) 
{
    bool ret = false;
    /* 从Flash读取上电次数（在earlyInit中已经加载了参数，这里无需再重新读取） */

    iotgoInfo("repower_times = %08X", itdb04_config.repower_times);
    os_timer_disarm(&itdb04_repower_times_timer);
    os_timer_setfn(&itdb04_repower_times_timer, (os_timer_func_t *)itdb04RepowerTimesTimerCallback, NULL);

    if (0xFFFFFFFF != itdb04_config.repower_times && itdb04_config.repower_times >= 4)
    {
        itdb04_config.repower_times = 0;
        ret = true;
    }
    else if (0xFFFFFFFF == itdb04_config.repower_times)
    {
        itdb04_config.repower_times = 1;
        os_timer_arm(&itdb04_repower_times_timer, 
            ITDB04_REPOWER_TIMES_TIMER_DELAY_SECONDS * (1000), 0);
        iotgoInfo("first start after factory!");
    }
    else
    {
        itdb04_config.repower_times++;
        os_timer_arm(&itdb04_repower_times_timer, 
            ITDB04_REPOWER_TIMES_TIMER_DELAY_SECONDS * (1000), 0);
    }
    
    devSaveConfigToFlash(&itdb04_config, sizeof(itdb04_config));
    return ret;
}

void ICACHE_FLASH_ATTR iotgoRegisterCallbackSet(IoTgoDevice *device)
{
    device->earlyInit = switchInitIoTgoDevice;
    device->postCenter = switchDevicePostCenter;
    device->respOfUpdateCallback = switchRespUpdateCallback;
    device->reqOfUpdateCallback = switchReqUpdateCallback;
    device->respOfQueryCallback = switchRespQueryCallback;

    device->devInitDeviceConfig = switchInitDeviceConfig;
}


#endif /* #if defined(COMPILE_IOTGO_FWSW_01) */

