#include "iotgo.h"
#ifdef COMPILE_IOTGO_FWTRX_TC

#include "iotgo_U2art_schedule.h"
#include "iotgo_uart0_send_queue.h"

#define KEY_DEBONCE_INTERVAL    (25)
#define KEY_LONG_PRESS_TIME     (5000)
#define KEY_DEBONCE_FIRST_TIME  (KEY_DEBONCE_INTERVAL * 2)
#define KEY_DEBONCE_SECOND_TIME (KEY_DEBONCE_INTERVAL * 2)
#define SIG_DEVICE_UPDATE_TIMER       (10000)

static uint32 key_pressed_level_counter = 0;
static uint32 key_released_level_counter = 0;

static os_timer_t key_isr_timer;
static os_timer_t app_high_ctrl_timer;
static os_timer_t watch_dog_timer;
static os_timer_t send_data_again_timer;
static os_timer_t query_state_timer;
static tcLinkQueue tc_link_queue = {0};
static uint16_t tcreq_couter = 0;
static uint16_t tcreq_period = TCREQ_PERIOD;
static uint16_t tcreq_max = TCREQ_MAX;
static os_timer_t flow_control_timer;
static os_timer_t modify_baudrate_timer;
static bool tmr_msg_flag = false;

enum{
    TC_STATE_NORMAL = 1,
    TC_STATE_NO_WIFI = 2,
    TC_STATE_DISCONNECT = 3,
    TC_STATE_UNREGISTER = 4,
    TC_STATE_AP_SETTING = 5,
    TC_STATE_ESPTOUCH_SETTING = 6,
    TC_STATE_UPGRADE = 7,
};
typedef enum{
    TC_SETTING_AP = 1,
    TC_SETTING_ESPTOUCH = 2,
    TC_SETTING_EXIT = 3,
}TcSettingMode;
typedef struct {
    uint32 baudrate;             /*波特率*/
    bool crc_enable;             /*CRC校验*/
    uint8 watch_dog;             /*看门狗*/
    bool sled_off;            /*状态灯*/
    uint8_t state_info;             /*状态订阅*/
    bool timer_sync;             /*定时器同步通知*/
} tcConfig;

static tcConfig tc_config = {9600,false,0,false,0,false};

static U2artError ICACHE_FLASH_ATTR processTickCmd(void *arg)
{
    uint8_t array_num = 0;
    cJSON *root = arg;
    if(NULL == root 
    || root->type != cJSON_Array
    || (1 != (cJSON_GetArraySize(root))))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(cmdSendDataToMcu("[\"ret\",\"tock\"]",14))
    {
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}
static void ICACHE_FLASH_ATTR modifyBaudrate(void *arg)
{   
    uint32_t baudrate = (uint32_t)(arg);
    uart_init(baudrate, IOTGO_UART1_BAUDRATE);
}

static U2artError ICACHE_FLASH_ATTR processBaudCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_baud = NULL;
    if(NULL == root 
        || root->type != cJSON_Array
        || (2 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_baud = cJSON_GetArrayItem(root,1)))
        || (cjson_baud->type != cJSON_Number)
        || (cjson_baud->valueint != 9600 && cjson_baud->valueint != 115200))
    {
        return U2ART_INVALID_PARAMS;
    }
    tc_config.baudrate = cjson_baud->valueint;
    devSaveConfigToFlash(&tc_config, sizeof(tc_config));
    if(cmdSendDataToMcu("[\"ret\",0]",9))
    {
        os_timer_disarm(&modify_baudrate_timer);
        os_timer_setfn(&modify_baudrate_timer, (os_timer_func_t *)modifyBaudrate, (void *)tc_config.baudrate);
        os_timer_arm(&modify_baudrate_timer, 100, 0);
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}
static void ICACHE_FLASH_ATTR cbWatchDogHighCtrlTimer(void *arg)
{
    GPIO_OUTPUT_SET(TA_HIGH_PRIORITY_CTRL_GPIO, GPIO_HIGH);
    os_timer_disarm(&watch_dog_timer);
    tc_config.watch_dog = 0;
    devSaveConfigToFlash(&tc_config, sizeof(tc_config));
}

static void ICACHE_FLASH_ATTR taWatchDogCallback(void *arg)
{
    GPIO_OUTPUT_SET(TA_HIGH_PRIORITY_CTRL_GPIO, GPIO_LOW);
    os_timer_disarm(&app_high_ctrl_timer);
    os_timer_setfn(&app_high_ctrl_timer, (os_timer_func_t *)cbWatchDogHighCtrlTimer, NULL);
    os_timer_arm(&app_high_ctrl_timer, TA_HIGH_PRIORITY_CTRL_LOW_WIDTH, 0);
}

static U2artError ICACHE_FLASH_ATTR processDogCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_dog = NULL;
    if(NULL == root 
        || root->type != cJSON_Array
        || (2 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_dog = cJSON_GetArrayItem(root,1)))
        || (cjson_dog->type != cJSON_Number)
        || (cjson_dog->valueint < 0 || cjson_dog->valueint > 60))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(0 == cjson_dog->valueint)
    {
        os_timer_disarm(&watch_dog_timer);
    }
    else
    {
        os_timer_arm(&watch_dog_timer, (1000 * cjson_dog->valueint), 1);
    }
    tc_config.watch_dog = cjson_dog->valueint;
    devSaveConfigToFlash(&tc_config, sizeof(tc_config));
    if(cmdSendDataToMcu("[\"ret\",0]",9))
    {
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}
static U2artError ICACHE_FLASH_ATTR processSledCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_led = NULL;
    if(NULL == root 
        || root->type != cJSON_Array
        || (2 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_led = cJSON_GetArrayItem(root,1)))
        || (cjson_led->type != cJSON_Number))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(0 == cjson_led->valueint)
    {
        normal_mode_led_off = true;
    }
    else
    {
        normal_mode_led_off = false;
    }
    tc_config.sled_off = normal_mode_led_off;
    devSaveConfigToFlash(&tc_config, sizeof(tc_config));
    if(cmdSendDataToMcu("[\"ret\",0]",9))
    {
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}

static U2artError ICACHE_FLASH_ATTR processStateInfoCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_state_info = NULL;
    if(NULL == root 
        || root->type != cJSON_Array
        || (2 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_state_info = cJSON_GetArrayItem(root,1)))
        || (cjson_state_info->type != cJSON_Number))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(0 == cjson_state_info->valueint)
    {
        os_timer_disarm(&query_state_timer);
    }
    else
    {
        os_timer_arm(&query_state_timer, 1000, 1);
    }
    tc_config.state_info = cjson_state_info->valueint;
    devSaveConfigToFlash(&tc_config, sizeof(tc_config));
    if(cmdSendDataToMcu("[\"ret\",0]",9))
    {
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}

static U2artError ICACHE_FLASH_ATTR processStateCmd(void *arg)
{
    cJSON *root = arg;
    uint8_t state = TC_STATE_NORMAL;
    char temp[20] = {0};
    if(NULL == root 
        || root->type != cJSON_Array
        || (1 != (cJSON_GetArraySize(root))))
    {
        return U2ART_INVALID_PARAMS;
    }
    IoTgoDeviceMode device_state = iotgoDeviceMode();
    if(DEVICE_MODE_WORK_NORMAL == device_state)
    {
        state = TC_STATE_NORMAL;
    }
    else if(DEVICE_MODE_WORK_AP_ERR >= device_state)
    {
        state = TC_STATE_NO_WIFI;
    }
    else if(DEVICE_MODE_WORK_AP_OK == device_state)
    {
        state = TC_STATE_DISCONNECT;
    }
    else if(DEVICE_MODE_WORK_INIT == device_state)
    {
        state = TC_STATE_UNREGISTER;
    }
    else if(DEVICE_MODE_SETTING == device_state)
    {
        state = TC_STATE_AP_SETTING;
    }
    else if(DEVICE_MODE_SETTING_SELFAP == device_state)
    {
        state = TC_STATE_ESPTOUCH_SETTING;
    }
    else if(DEVICE_MODE_UPGRADE == device_state)
    {
        state = TC_STATE_UPGRADE;
    }
    else
    {
        /*do nothing*/
    }
    os_sprintf(temp,"[\"ret\",%d]",state);
    if(cmdSendDataToMcu(temp,os_strlen(temp)))
    {
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}

static U2artError ICACHE_FLASH_ATTR processRssiCmd(void *arg)
{
    uint8_t array_num = 0;
    cJSON *root = arg;
    char rssi[20] = {0};
    if(DEVICE_MODE_WORK_NORMAL != iotgoDeviceMode())
    {
        return U2ART_ILLEGAL_OPERATION;
    }
    if(NULL == root 
    || root->type != cJSON_Array
    || (1 != (cJSON_GetArraySize(root))))
    {
        return U2ART_INVALID_PARAMS;
    }
    os_sprintf(rssi,"[\"ret\",%d]",iotgo_wifi_rssi);
    if(cmdSendDataToMcu(rssi,os_strlen(rssi)))
    {
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}
static U2artError ICACHE_FLASH_ATTR processTimeCmd(void *arg)
{
    uint8_t array_num = 0;
    cJSON *root = arg;
    char time[50] = {0};
    IoTgoGMTTime gmt_time;
    if(NULL == root 
    || root->type != cJSON_Array
    || (1 != (cJSON_GetArraySize(root))))
    {
        return U2ART_INVALID_PARAMS;
    }
    getGMTTime(&gmt_time);
    if(1970 == gmt_time.year)
    {
        os_sprintf(time,"[\"ret\",1,\"%04u-%02u-%02uT%02u:%02u:%02uZ\"]",
            gmt_time.year, gmt_time.month, gmt_time.day, 
            gmt_time.hour, gmt_time.minute, gmt_time.second);
    }
    else
    {
        os_sprintf(time,"[\"ret\",0,\"%04u-%02u-%02uT%02u:%02u:%02uZ\"]",
            gmt_time.year, gmt_time.month, gmt_time.day, 
            gmt_time.hour, gmt_time.minute, gmt_time.second);
    }
    if(cmdSendDataToMcu(time,os_strlen(time)))
    {
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}
static void ICACHE_FLASH_ATTR enterAp(void *arg)
{
    system_os_post(IOTGO_CORE, MSG_CORE_ENTER_SETTING_SELFAP_MODE, 0);
}

static void ICACHE_FLASH_ATTR enterEsptouch(void *arg)
{
    system_os_post(IOTGO_CORE, MSG_CORE_ENTER_SETTING_MODE, 0);
}
static int8_t ICACHE_FLASH_ATTR dealSettingMode(TcSettingMode mode)
{
    static os_timer_t enter_ap_mode_timer;
    static os_timer_t enter_esptouch_mode_timer;
    IoTgoDeviceMode current_mode = iotgoDeviceMode();
    /*接收到的是进入AP配置模式的指令*/
    if(TC_SETTING_AP == mode)
    {
        /*通知8266进入AP配置模式*/
        if (DEVICE_MODE_SETTING_SELFAP != current_mode)
        {
             if (DEVICE_MODE_SETTING == current_mode)
            {
                system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_MODE, 0);
                os_timer_disarm(&enter_ap_mode_timer);
                os_timer_setfn(&enter_ap_mode_timer, (os_timer_func_t *)enterAp, NULL);
                os_timer_arm(&enter_ap_mode_timer, 1500, 0);
            }
            else
            {
                system_os_post(IOTGO_CORE, MSG_CORE_ENTER_SETTING_SELFAP_MODE, 0);
            }
        }
        else
        {
            return -1;
        }
    }
    /*接收到的是进入touch配置模式的指令*/
    else if(TC_SETTING_ESPTOUCH == mode)
    {        
        /*通知8266进入touch配置模式*/
        if (DEVICE_MODE_SETTING != current_mode)
        {
            if (DEVICE_MODE_SETTING_SELFAP == current_mode)
            {
                system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_SELFAP_MODE, 0);
                os_timer_disarm(&enter_esptouch_mode_timer);
                os_timer_setfn(&enter_esptouch_mode_timer, (os_timer_func_t *)enterEsptouch, NULL);
                os_timer_arm(&enter_esptouch_mode_timer, 1500, 0);
            }
            else
            {
                system_os_post(IOTGO_CORE, MSG_CORE_ENTER_SETTING_MODE, 0);
            }
        }
        else
        {
            return -1;
        }
    }
      /*接收到的是退出配置模式的指令*/
    else if(TC_SETTING_EXIT == mode)
    {
        /*通知8266退出AP配置模式*/
        if (DEVICE_MODE_SETTING_SELFAP == current_mode)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_SELFAP_MODE, 0);
        }
        /*通知8266退出ESPTOUCH配置模式*/
        else if (DEVICE_MODE_SETTING == current_mode)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_MODE, 0);
        }
        else
        {
            return -1;
        }
    }
    else
    {
        return -2;
    }
    return 0;
}

static U2artError ICACHE_FLASH_ATTR processSettingCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_setting = NULL;
    int8_t ret = 0;
    iotgoInfo("process setting");
    if(NULL == root 
        || root->type != cJSON_Array
        || (2 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_setting = cJSON_GetArrayItem(root,1)))
        || (cjson_setting->type != cJSON_Number))
    {
        return U2ART_INVALID_PARAMS;
    }
    ret = dealSettingMode(cjson_setting->valueint);
    if(0 == ret)
    {
        if(cmdSendDataToMcu("[\"ret\",0]",9))
        {
            return U2ART_SUCCESS;
        }
        return U2ART_INSIDE_ERROR;
    }
    else if(-1 == ret)
    {
        return U2ART_ILLEGAL_OPERATION;
    }
    else if(-2 == ret)
    {
        return U2ART_INVALID_PARAMS;
    }
}
static U2artError ICACHE_FLASH_ATTR processVersionCmd(void *arg)
{
    uint8_t array_num = 0;
    cJSON *root = arg;
    char temp[30] = {0};
    if(NULL == root 
    || root->type != cJSON_Array
    || (1 != (cJSON_GetArraySize(root))))
    {
        return U2ART_INVALID_PARAMS;
    }
    os_strcpy(temp,"[\"ret\",\"");
    os_strcat(temp,IOTGO_FM_VERSION);
    os_strcat(temp,"\"]");
    if(cmdSendDataToMcu(temp,os_strlen(temp)))
    {
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}
static U2artError ICACHE_FLASH_ATTR processDeviceId(void *arg)
{
    uint8_t array_num = 0;
    cJSON *root = arg;
    char temp[30] = {0};
    if(NULL == root 
    || root->type != cJSON_Array
    || (1 != (cJSON_GetArraySize(root))))
    {
        return U2ART_INVALID_PARAMS;
    }
    os_strcpy(temp,"[\"ret\",\"");
    os_strcat(temp,iotgo_device.deviceid);
    os_strcat(temp,"\"]");
    if(cmdSendDataToMcu(temp,os_strlen(temp)))
    {
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}
static U2artError ICACHE_FLASH_ATTR processStamacCmd(void *arg)
{
    uint8_t array_num = 0;
    cJSON *root = arg;
    char sta_mac_str[30] = {0};
    if(NULL == root 
    || root->type != cJSON_Array
    || (1 != (cJSON_GetArraySize(root))))
    {
        return U2ART_INVALID_PARAMS;
    }
    os_strcpy(sta_mac_str,"[\"ret\",\"");
    os_strcat(sta_mac_str,iotgoStationMac());
    os_strcat(sta_mac_str,"\"]");
    if(sta_mac_str[1] != 0)
    {
        if(cmdSendDataToMcu(sta_mac_str,os_strlen(sta_mac_str)))
        {
            return U2ART_SUCCESS;
        }
        return U2ART_INSIDE_ERROR;
    }
    else
    {
        return U2ART_INSIDE_ERROR;
    }
    
}
static const char* ICACHE_FLASH_ATTR iotgoSapMac(void)
{
    static char sta_mac_str[18] = {0};
    uint8_t sta_mac[6] = {0};

    if (!sta_mac_str[0])
    {
        if(wifi_get_macaddr(SOFTAP_IF, sta_mac))
        {
            os_sprintf(sta_mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", 
                sta_mac[0],sta_mac[1],sta_mac[2],sta_mac[3],sta_mac[4],sta_mac[5]);
        }
        else
        {
            iotgoError("get station mac failed!");
        }
    }

    return  sta_mac_str;   
}
static U2artError ICACHE_FLASH_ATTR processSapMacCmd(void *arg)
{
    uint8_t array_num = 0;
    cJSON *root = arg;
    char sap_mac_str[18] = {0};
    if(NULL == root 
    || root->type != cJSON_Array
    || (1 != (cJSON_GetArraySize(root))))
    {
        return U2ART_INVALID_PARAMS;
    }
    os_strcpy(sap_mac_str,"[\"ret\",\"");
    os_strcat(sap_mac_str,iotgoSapMac());
    os_strcat(sap_mac_str,"\"]");
    if(sap_mac_str[1] != 0)
    {
        if(cmdSendDataToMcu(sap_mac_str,os_strlen(sap_mac_str)))
        {
            return U2ART_SUCCESS;
        }
        return U2ART_INSIDE_ERROR;
    }
    else
    {
        return U2ART_INSIDE_ERROR;
    }
    
}
static U2artError ICACHE_FLASH_ATTR processUpdateCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_update = NULL;
    cJSON *cjson_sequence = NULL;
    cJSON *cjson_params = NULL;
    char *out = NULL;
    tcreq_couter++;
    if(tcreq_couter > tcreq_max)
    {
        return U2ART_BUSY_ERROR;
    }
    if(NULL == root 
        || root->type != cJSON_Array
        || (3 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_sequence= cJSON_GetArrayItem(root,1)))
        || (NULL == (cjson_params = cJSON_GetArrayItem(root,2)))
        || (cjson_sequence->type != cJSON_Number)
        || (cjson_params->type != cJSON_Object))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(DEVICE_MODE_WORK_NORMAL != iotgoDeviceMode())
    {
        return U2ART_NETWORK_ERROR;
    }
    cjson_update = cJSON_CreateObject();
    if(NULL == cjson_update)
    {
        return U2ART_INSIDE_ERROR;
    }
    cJSON_AddStringToObject(cjson_update,IOTGO_STRING_USERAGENT, IOTGO_STRING_DEVICE);
    cJSON_AddStringToObject(cjson_update,IOTGO_STRING_APIKEY, iotgo_device.owner_uuid);
    cJSON_AddStringToObject(cjson_update,IOTGO_STRING_DEVICEID, iotgo_device.deviceid);
    cJSON_AddNumberToObject(cjson_update, IOTGO_STRING_D_SEQ, cjson_sequence->valueint);
    cJSON_AddStringToObject(cjson_update,IOTGO_STRING_ACTION, IOTGO_STRING_UPDATE);
    cJSON_AddItemToObject(cjson_update, IOTGO_STRING_PARAMS, cjson_params);
    out = cJSON_PrintUnformatted(cjson_update);
    cJSON_DetachItemFromObject(cjson_update,IOTGO_STRING_PARAMS);
    cJSON_Delete(cjson_update);
    if(0 == sendIoTgoPkg(out))
    {
        os_free(out);
        if(cmdSendDataToMcu("[\"ret\",0]",9))
        {
            return U2ART_SUCCESS;
        }
        return U2ART_INSIDE_ERROR;
    }
    else
    {
        os_free(out);
        return U2ART_INSIDE_ERROR;
    } 
}

static U2artError ICACHE_FLASH_ATTR processRespondCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_respond = NULL;
    cJSON *cjson_sequence = NULL;
    cJSON *cjson_error = NULL;
    char *out = NULL;
    os_timer_disarm(&send_data_again_timer);
    tcDeleteNode(&tc_link_queue);
    if(NULL == root 
        || root->type != cJSON_Array
        || (3 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_sequence= cJSON_GetArrayItem(root,1)))
        || (NULL == (cjson_error = cJSON_GetArrayItem(root,2)))
        || (cjson_sequence->type != cJSON_String)
        || (cjson_error->type != cJSON_Number))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(DEVICE_MODE_WORK_NORMAL != iotgoDeviceMode())
    {
        return U2ART_NETWORK_ERROR;
    }
    cjson_respond = cJSON_CreateObject();
    if(NULL == cjson_respond)
    {
        return U2ART_INSIDE_ERROR;
    }
    cJSON_AddStringToObject(cjson_respond,IOTGO_STRING_USERAGENT, IOTGO_STRING_DEVICE);
    cJSON_AddStringToObject(cjson_respond,IOTGO_STRING_APIKEY, iotgo_device.owner_uuid);
    cJSON_AddStringToObject(cjson_respond,IOTGO_STRING_DEVICEID, iotgo_device.deviceid);
    cJSON_AddStringToObject(cjson_respond, IOTGO_STRING_SEQUENCE, cjson_sequence->valuestring);
    cJSON_AddNumberToObject(cjson_respond,IOTGO_STRING_ERROR,cjson_error->valueint);
    out = cJSON_PrintUnformatted(cjson_respond);
    cJSON_DetachItemFromObject(cjson_respond,IOTGO_STRING_SEQUENCE);
    cJSON_DetachItemFromObject(cjson_respond,IOTGO_STRING_ERROR);
    cJSON_Delete(cjson_respond);
    if(0 == sendIoTgoPkg(out))
    {
        os_free(out);
        if(cmdSendDataToMcu("[\"ret\",0]",9))
        {
            return U2ART_SUCCESS;
        }
        return U2ART_INSIDE_ERROR;
    }
    else
    {
        os_free(out);
        return U2ART_INSIDE_ERROR;
    } 
    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_BY_REMOTE, 0);
}

static U2artError ICACHE_FLASH_ATTR processQueryCmd(void *arg)
{

    cJSON *root = arg;
    cJSON *cjson_query = NULL;
    cJSON *cjson_sequence = NULL;
    cJSON *cjson_params = NULL;
    char *out = NULL;
    tcreq_couter++;
    if(tcreq_couter > tcreq_max)
    {
        return U2ART_BUSY_ERROR;
    }
    if(NULL == root 
        || root->type != cJSON_Array
        || (3 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_sequence= cJSON_GetArrayItem(root,1)))
        || (NULL == (cjson_params = cJSON_GetArrayItem(root,2)))
        || (cjson_sequence->type != cJSON_Number)
        || (cjson_params->type != cJSON_Array))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(DEVICE_MODE_WORK_NORMAL != iotgoDeviceMode())
    {
        return U2ART_NETWORK_ERROR;
    }
    cjson_query = cJSON_CreateObject();
    if(NULL == cjson_query)
    {
        return U2ART_INSIDE_ERROR;
    }
    cJSON_AddStringToObject(cjson_query,IOTGO_STRING_USERAGENT, IOTGO_STRING_DEVICE);
    cJSON_AddStringToObject(cjson_query,IOTGO_STRING_APIKEY, iotgo_device.owner_uuid);
    cJSON_AddStringToObject(cjson_query,IOTGO_STRING_DEVICEID, iotgo_device.deviceid);
    cJSON_AddNumberToObject(cjson_query, IOTGO_STRING_D_SEQ, cjson_sequence->valueint);
    cJSON_AddStringToObject(cjson_query,IOTGO_STRING_ACTION, IOTGO_STRING_QUERY);
    cJSON_AddItemToObject(cjson_query, IOTGO_STRING_PARAMS, cjson_params);
    out = cJSON_PrintUnformatted(cjson_query);
    cJSON_DetachItemFromObject(cjson_query,IOTGO_STRING_PARAMS);
    cJSON_Delete(cjson_query);
    if(0 == sendIoTgoPkg(out))
    {
        os_free(out);
        if(cmdSendDataToMcu("[\"ret\",0]",9))
        {
            return U2ART_SUCCESS;
        }
        return U2ART_INSIDE_ERROR;
    }
    else
    {
        os_free(out);
        return U2ART_INSIDE_ERROR;
    } 
}
static U2artError ICACHE_FLASH_ATTR processTimerUpdateWithParamCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_timer = NULL;
    cJSON *cjson_timer_act = NULL;
    int8_t ret = 0;
    if(NULL == root 
        || root->type != cJSON_Array
        || (2 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_timer_act = cJSON_GetArrayItem(root,0)))
        || (NULL == (cjson_timer = cJSON_GetArrayItem(root,1)))
        || (cjson_timer_act->type != cJSON_String)
        || (cjson_timer->type != cJSON_Object))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(0 == os_strcmp("tmrNew",cjson_timer_act->valuestring))
    {
        cJSON_AddStringToObject(cjson_timer,IOTGO_STRING_TIMERACT,IOTGO_STRING_NEW);
    }
    else if(0 == os_strcmp("tmrEdit",cjson_timer_act->valuestring))
    {
        cJSON_AddStringToObject(cjson_timer,IOTGO_STRING_TIMERACT,IOTGO_STRING_EDIT);
    }
    else if(0 == os_strcmp("tmrDel",cjson_timer_act->valuestring))
    {
        cJSON_AddStringToObject(cjson_timer,IOTGO_STRING_TIMERACT,IOTGO_STRING_DEL);
    }
    else if(0 == os_strcmp("tmrEnable",cjson_timer_act->valuestring))
    {
        cJSON_AddStringToObject(cjson_timer,IOTGO_STRING_TIMERACT,IOTGO_STRING_ENABLE);
    }
    else if(0 == os_strcmp("tmrDisable",cjson_timer_act->valuestring))
    {
        cJSON_AddStringToObject(cjson_timer,IOTGO_STRING_TIMERACT,IOTGO_STRING_DISABLE);
    }
    else
    {
        return U2ART_INVALID_PARAMS;
    }
    timerProcUpdate(cjson_timer,TIMER_MCU);
    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_TIMER, 0);
    return U2ART_SUCCESS;
}
static U2artError ICACHE_FLASH_ATTR processTimerUpdateCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_timer = NULL;
    cJSON *cjson_timer_act = NULL;
    int8_t ret = 0;
    if(NULL == root 
        || root->type != cJSON_Array
        || (1 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_timer_act = cJSON_GetArrayItem(root,0)))
        || (cjson_timer_act->type != cJSON_String))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(NULL == (cjson_timer = cJSON_CreateObject()))
    {
        return U2ART_INSIDE_ERROR;
    }
    if(0 == os_strcmp("tmrDelAll",cjson_timer_act->valuestring))
    {
        cJSON_AddStringToObject(cjson_timer,IOTGO_STRING_TIMERACT,IOTGO_STRING_DELALL);
    }
    else if(0 == os_strcmp("tmrEnableAll",cjson_timer_act->valuestring))
    {
        cJSON_AddStringToObject(cjson_timer,IOTGO_STRING_TIMERACT,IOTGO_STRING_ENABLEALL);
    }
    else if(0 == os_strcmp("tmrDisableAll",cjson_timer_act->valuestring))
    {
        cJSON_AddStringToObject(cjson_timer,IOTGO_STRING_TIMERACT,IOTGO_STRING_DISABLEALL);
    }
    else
    {
        return U2ART_INVALID_PARAMS;
    }
    timerProcUpdate(cjson_timer,TIMER_MCU);
    cJSON_Delete(cjson_timer);
    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_TIMER, 0);
    return U2ART_SUCCESS;
}

static U2artError ICACHE_FLASH_ATTR processTimerQueryDevCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_timer = NULL;
    int8_t ret = 0;
    if(NULL == root 
        || root->type != cJSON_Array
        || (2 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_timer = cJSON_GetArrayItem(root,1)))
        || (cjson_timer->type != cJSON_Array))
    {
        return U2ART_INVALID_PARAMS;
    }
    timerProcQuery(cjson_timer,TIMER_MCU);
    return U2ART_SUCCESS;
}
static U2artError ICACHE_FLASH_ATTR processRetCmd(void *arg)
{
    cJSON *root = arg;
    os_timer_disarm(&send_data_again_timer);
    tcDeleteNode(&tc_link_queue);
    if(NULL == root 
        || root->type != cJSON_Array)
    {
        return U2ART_INVALID_PARAMS;
    }
    return U2ART_SUCCESS;
}
static U2artError ICACHE_FLASH_ATTR processCrcCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_crc = NULL;
    if(NULL == root 
        || root->type != cJSON_Array
        || (2 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_crc = cJSON_GetArrayItem(root,1)))
        || (cjson_crc->type != cJSON_Number))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(cmdSendDataToMcu("[\"ret\",0]",9))
    {
        if(0 == cjson_crc->valueint)
        {
            crc_enable = false;
            tc_config.crc_enable = crc_enable;
            devSaveConfigToFlash(&tc_config, sizeof(tc_config));
            return U2ART_SUCCESS;
        }
        else
        {
            crc_enable = true;
            tc_config.crc_enable = crc_enable;
            devSaveConfigToFlash(&tc_config, sizeof(tc_config));
            return U2ART_SUCCESS;
        }
    }
    return U2ART_INSIDE_ERROR;
}

static U2artError ICACHE_FLASH_ATTR processTmrMsgCmd(void *arg)
{
    cJSON *root = arg;
    cJSON *cjson_tmr_msg = NULL;
    if(NULL == root 
        || root->type != cJSON_Array
        || (2 != (cJSON_GetArraySize(root)))
        || (NULL == (cjson_tmr_msg = cJSON_GetArrayItem(root,1)))
        || (cjson_tmr_msg->type != cJSON_Number))
    {
        return U2ART_INVALID_PARAMS;
    }
    if(0 == cjson_tmr_msg->valueint)
    {
        tmr_msg_flag = false;
    }
    else
    {
        tmr_msg_flag = true;
    }
    tc_config.timer_sync = tmr_msg_flag;
    devSaveConfigToFlash(&tc_config, sizeof(tc_config));  
    if(cmdSendDataToMcu("[\"ret\",0]",9))
    { 
        return U2ART_SUCCESS;
    }
    return U2ART_INSIDE_ERROR;
}

static void ICACHE_FLASH_ATTR watchDogHighPriorityInit(void)
{
    /* Init app high priority control pin */
    PIN_FUNC_SELECT(TA_HIGH_PRIORITY_CTRL_GPIO_NAME, TA_HIGH_PRIORITY_CTRL_GPIO_FUNC);
    GPIO_OUTPUT_SET(TA_HIGH_PRIORITY_CTRL_GPIO, GPIO_HIGH);
    os_timer_disarm(&watch_dog_timer);
    os_timer_setfn(&watch_dog_timer, (os_timer_func_t *)taWatchDogCallback, NULL);
}

static bool ICACHE_FLASH_ATTR deleteField(cJSON *cjson_root)
{
    cJSON *child = NULL;
    if(NULL == cjson_root || cjson_root->type != cJSON_Object)
    {
        return false;
    }
    child = cjson_root->child;
    while(child != NULL)
    {
        cJSON *next = child->next;
        if(NULL != (char *)os_strstr(child->string,"zyx_"))
        {
            cJSON_DeleteItemFromObject(cjson_root,child->string);
        }
        child = next;
    }
    return true;
}
static void ICACHE_FLASH_ATTR switchActionCallback(void *switch_val)
{
    uint32_t length = 0;
    char *data = NULL;
    if(switch_val == NULL)
    {
        iotgoError("switch_val is NULL pointer");
        return;
    }
    iotgoInfo("switch_val is %s",(char*)switch_val);
    length = os_strlen((char *)switch_val) + 17;
    data = (char *)os_malloc(length);
    os_strcpy(data,"[\"timerAction\",");
    os_strcat(data,(char *)switch_val);
    os_strcat(data,"]");
    tcAddDataToQueue(&tc_link_queue,data,TC_NULL,false);
}
static void ICACHE_FLASH_ATTR sendDataToMcuAgain(void *arg)
{
    static uint8_t send_counter = 0;
    send_counter++;
    
    if(send_counter >= 2)
    {
        send_counter = 0;
        tcDeleteNode(&tc_link_queue);
    }
    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_BY_REMOTE, 0);
}
static void ICACHE_FLASH_ATTR transDataToMcu(void)
{
    if(NULL != tc_link_queue.head && tc_link_queue.length != 0)
    {
        cmdSendDataToMcu(tc_link_queue.head->data,os_strlen(tc_link_queue.head->data));
        os_timer_arm(&send_data_again_timer, 2000, 0);
    }
    else
    {
        iotgoInfo("uart0 queue is not data");
    }
}
static void ICACHE_FLASH_ATTR queryState(void *arg)
{
    static uint8_t state = TC_STATE_NORMAL;
    static uint8_t last_state = TC_STATE_UNREGISTER;
    IoTgoDeviceMode device_state = iotgoDeviceMode();
    char temp[30] = {0};
    if(DEVICE_MODE_WORK_NORMAL == device_state)
    {
        state = TC_STATE_NORMAL;
    }
    else if(DEVICE_MODE_WORK_AP_ERR >= device_state)
    {
        state = TC_STATE_NO_WIFI;
    }
    else if(DEVICE_MODE_WORK_AP_OK == device_state)
    {
        state = TC_STATE_DISCONNECT;
    }
    else if(DEVICE_MODE_WORK_INIT == device_state)
    {
        state = TC_STATE_UNREGISTER;
    }
    else if(DEVICE_MODE_SETTING == device_state)
    {
        state = TC_STATE_AP_SETTING;
    }
    else if(DEVICE_MODE_SETTING_SELFAP == device_state)
    {
        state = TC_STATE_ESPTOUCH_SETTING;
    }
    else
    {
        iotgoError("something error had happend");
    }
    if(state != last_state)
    {
        last_state = state;
        os_sprintf(temp,"[\"state\",%d]",state);
        tcAddDataToQueue(&tc_link_queue,temp,TC_NULL,false);
    }
}

static void ICACHE_FLASH_ATTR switchUpdateWithVersion(void)
{
    cJSON *root = NULL;
    char *out = NULL;
    cJSON *params = NULL;
    root=cJSON_CreateObject();
    if(NULL == root)
    {
        iotgoError("create cjson object is error");
        return;
    }
    cJSON_AddStringToObject(root,IOTGO_STRING_USERAGENT, IOTGO_STRING_DEVICE);
    cJSON_AddStringToObject(root,IOTGO_STRING_APIKEY, iotgo_device.owner_uuid);
    cJSON_AddStringToObject(root,IOTGO_STRING_DEVICEID, iotgo_device.deviceid);
    cJSON_AddStringToObject(root,IOTGO_STRING_ACTION, IOTGO_STRING_UPDATE);
    cJSON_AddItemToObject(root, IOTGO_STRING_PARAMS, params = cJSON_CreateObject());
    cJSON_AddNumberToObject(params,IOTGO_STRING_RSSI,iotgo_wifi_rssi);
    cJSON_AddStringToObject(params,IOTGO_STRING_FWVERSION, IOTGO_FM_VERSION);
    cJSON_AddStringToObject(params,IOTGO_STRING_STA_MAC, iotgoStationMac());
    out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    iotgoQueueAdd(out,IOTGO_SWITCH_VERSION,false);
    os_free(out);
}
static void ICACHE_FLASH_ATTR updateTimerToServer(void)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_params = NULL;
    cJSON *cjson_timerInfo = NULL;
    char *out = NULL;
    cjson_root = cJSON_CreateObject();
    if(NULL == cjson_root)
    {
        iotgoError("create cjson object is error");
        return;
    }
    cJSON_AddStringToObject(cjson_root,IOTGO_STRING_USERAGENT, IOTGO_STRING_DEVICE);
    cJSON_AddStringToObject(cjson_root,IOTGO_STRING_APIKEY, iotgo_device.owner_uuid);
    cJSON_AddStringToObject(cjson_root,IOTGO_STRING_DEVICEID, iotgo_device.deviceid);
    cJSON_AddStringToObject(cjson_root,IOTGO_STRING_ACTION, IOTGO_STRING_UPDATE);
    cJSON_AddItemToObject(cjson_root, IOTGO_STRING_PARAMS, cjson_params = cJSON_CreateObject());
    cJSON_AddItemToObject(cjson_params, IOTGO_STRING_TIMERINFO, cjson_timerInfo = cJSON_CreateObject());
    iotgoTimerInfo timer_info = getIotgoTimerInfo();
    cJSON_AddNumberToObject(cjson_timerInfo,IOTGO_STRING_TIMERCNT,timer_info.timer_cnt);
    cJSON_AddNumberToObject(cjson_timerInfo,IOTGO_STRING_TIMERVER,timer_info.timer_ver);
    out = cJSON_PrintUnformatted(cjson_root);
    cJSON_Delete(cjson_root);
    iotgoQueueAdd(out,IOTGO_UPDATE_TIMER,false);
    os_free(out);
}
static void ICACHE_FLASH_ATTR switchRespUpdateCallback(const char *data)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_dseq = NULL;
    cJSON *cjson_error = NULL;
    uint8_t buf[50] = {0};
    if(NULL == (cjson_root = cJSON_Parse((char *)data)) || cjson_root->type != cJSON_Object)
    {
        if(cjson_root != NULL)
        {
            cJSON_Delete(cjson_root);
        }
        iotgoError("pkg is error");
        return;
    }
    if(NULL != (cjson_dseq = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_D_SEQ))
        && NULL != (cjson_error = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_ERROR)))
    {
        os_sprintf(buf,"[\"respond\",%d,%d]",cjson_dseq->valueint,cjson_error->valueint);
        tcAddDataToQueue(&tc_link_queue,buf,TC_NULL,false);   
    }
    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_OK, 0);
    cJSON_Delete(cjson_root);
}
static void ICACHE_FLASH_ATTR switchReqQueryCallback(const char *data)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_params = NULL; 
    cJSON *cjson_sequence = NULL;
    cJSON *cjson_timer_act = NULL;
    cjson_root = cJSON_Parse((char *)data);
    if(cjson_root == NULL && cjson_root->type != cJSON_Object)
    {
        iotgoError("Error before: [%s]\n",cJSON_GetErrorPtr());
        cJSON_Delete(cjson_root);
        return;
    }
    /*截取"sequence"字段数据*/
    if(NULL != (cjson_sequence = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_SEQUENCE)))
    {
        os_strcpy(server_sequence_value, cjson_sequence->valuestring);
    }
    else
    {
        os_strcpy(server_sequence_value, IOTGO_STRING_INVALID_SEQUENCE);
        iotgoError("Error before: [%s]\n",cJSON_GetErrorPtr());
        cJSON_Delete(cjson_root);
        return;
    }
    if(NULL != (cjson_params = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_PARAMS))
        && cjson_params->type == cJSON_Array)
    {
        if(NULL == (cjson_timer_act = cJSON_GetArrayItem(cjson_params,0)))
        {
            iotgoError("cjson data error");
            cJSON_Delete(cjson_root);
            return;
        }
        if((0 ==  os_strcmp(cjson_timer_act->valuestring,"timerInfo"))
            || (0 == os_strcmp(cjson_timer_act->valuestring,"timerIndex"))
            || (0 == os_strcmp(cjson_timer_act->valuestring,"timerID")))        
        {
            timerProcQuery(cjson_params,TIMER_APP);
        }
        else
        {
            char *params = NULL;
            char *sequence = NULL;
            char *data = NULL;
            uint32_t length = 0;
            params = cJSON_PrintUnformatted(cjson_params);
            sequence = cJSON_PrintUnformatted(cjson_sequence);
            /*length保留了结束符*/
            length = os_strlen(params) + os_strlen(sequence) + 15;
            data = (char *)os_malloc(length);
            os_strcpy(data,"[\"queryDev\",");
            os_strcat(data,sequence);
            os_strcat(data,",");
            os_strcat(data,params);
            os_strcat(data,"]");
            tcAddDataToQueue(&tc_link_queue,data,TC_NULL,false);
            os_free(params);
            os_free(sequence);
            os_free(data);
        }
    }
    else
    {
        iotgoError("Fields lost and ignored pkg!");
    }
    cJSON_Delete(cjson_root);
}

/*回复query请求*/
static void ICACHE_FLASH_ATTR switchRespQueryCallback(const char *data)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_params = NULL;
    cJSON *cjson_sequence = NULL;
    cjson_root = cJSON_Parse((char *)data);
    if(cjson_root == NULL && cjson_root->type != cJSON_Object)
    {
        iotgoError("Error before: [%s]\n",cJSON_GetErrorPtr());
        cJSON_Delete(cjson_root);
        return;
    }
    if(NULL == (cjson_sequence = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_D_SEQ)))
    {
        iotgoError("Error before: [%s]\n",cJSON_GetErrorPtr());
        cJSON_Delete(cjson_root);
        return;
    }
    if(NULL != (cjson_params = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_PARAMS)))
    {
        if(deleteField(cjson_params))
        {
            char *params = NULL;
            char *sequence = NULL;
            char *data = NULL;
            uint32_t length = 0;
            params = cJSON_PrintUnformatted(cjson_params);
            sequence = cJSON_PrintUnformatted(cjson_sequence);
            /*length保留了结束符*/
            length = os_strlen(params) + os_strlen(sequence) + 14;
            data = (char *)os_malloc(length);
            os_strcpy(data,"[\"respond\",");
            os_strcat(data,sequence);
            os_strcat(data,",");
            os_strcat(data,params);
            os_strcat(data,"]");
            tcAddDataToQueue(&tc_link_queue,data,TC_NULL,false);
            os_free(params);
            os_free(sequence);
            os_free(data);
        }
        else
        {
            if(cjson_params->type == cJSON_Number && cjson_params->valueint == 0)
            {
                char *sequence = NULL;
                char *data = NULL;
                uint32_t length = 0;
                sequence = cJSON_PrintUnformatted(cjson_sequence);
                length = os_strlen(sequence) + 16;
                data = (char *)os_malloc(length);
                os_strcpy(data,"[\"respond\",");
                os_strcat(data,sequence);
                os_strcat(data,",{}]");
                tcAddDataToQueue(&tc_link_queue,data,TC_NULL,false);
                os_free(sequence);
                os_free(data);
            }
        }
    }
    else
    {
        iotgoError("Fields lost and ignored pkg!");
    }
    cJSON_Delete(cjson_root);
}

static void ICACHE_FLASH_ATTR switchReqUpdateCallback(const char *data)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_params = NULL; 
    cJSON *cjson_sequence = NULL;
    cJSON *cjson_timer_act = NULL;
    cjson_root = cJSON_Parse((char *)data);
    if(cjson_root == NULL && cjson_root->type != cJSON_Object)
    {
        iotgoError("Error before: [%s]\n",cJSON_GetErrorPtr());
        cJSON_Delete(cjson_root);
        return;
    }
	/*截取"sequence"字段数据*/
	if(NULL != (cjson_sequence = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_SEQUENCE)))
    {
        os_strcpy(server_sequence_value, cjson_sequence->valuestring);
    }
    else
    {
        os_strcpy(server_sequence_value, IOTGO_STRING_INVALID_SEQUENCE);
    }
    if(NULL != (cjson_params = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_PARAMS)))
    {
        if(NULL != (cjson_timer_act = cJSON_GetObjectItem(cjson_params,IOTGO_STRING_TIMERACT)))
        {
            if(timerProcUpdate(cjson_params,TIMER_APP))
            { 
                /*以下应该是回复MCU的队列*/
                if(tmr_msg_flag)
                {
                    cJSON *cjson_timerInfo = NULL;
                    char *out = NULL;
                    if(NULL == (cjson_timerInfo = cJSON_CreateObject()))
                    {
                        iotgoError("create object error");
                        return;
                    }
                    iotgoTimerInfo timer_info = getIotgoTimerInfo();
                    cJSON_AddNumberToObject(cjson_timerInfo,IOTGO_STRING_TIMERCNT,timer_info.timer_cnt);
                    cJSON_AddNumberToObject(cjson_timerInfo,IOTGO_STRING_TIMERVER,timer_info.timer_ver);
                    out = cJSON_PrintUnformatted(cjson_timerInfo);
                    cJSON_Delete(cjson_timerInfo);
                    tcAddDataToQueue(&tc_link_queue,out,TC_NULL,false);
                    os_free(out);
                }
            } 
        }
        else
        {
            if(deleteField(cjson_params))
            {
                char *params = NULL;
                char *sequence = NULL;
                char *data = NULL;
                uint32_t length = 0;
                params = cJSON_PrintUnformatted(cjson_params);
                sequence = cJSON_PrintUnformatted(cjson_sequence);
                /*length保留了结束符*/
                length = os_strlen(params) + os_strlen(sequence) + 13;
                data = (char *)os_malloc(length);
                os_strcpy(data,"[\"update\",");
                os_strcat(data,sequence);
                os_strcat(data,",");
                os_strcat(data,params);
                os_strcat(data,"]");
                tcAddDataToQueue(&tc_link_queue,data,TC_NULL,false);
                os_free(params);
                os_free(sequence);
                os_free(data);
            }
        }
    }
    else
    {
        iotgoError("Fields lost and ignored pkg!");
    }
    cJSON_Delete(cjson_root);
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
        }break;
        case SIG_DEVICE_UPDATE_BY_REMOTE:
        {
            iotgoInfo("SIG_DEVICE_UPDATE_BY_REMOTE");
            transDataToMcu();
        }break;
        case SIG_DEVICE_UPDATE_TIMER:
        {
            iotgoInfo("SIG_DEVICE_UPDATE_TIMER");
            updateTimerToServer();
        }break;
        default:
        {
            iotgoError("Unknown SIGNAL DEVICE = %u and ignored!", events->sig);
        }
    }
}
static void ICACHE_FLASH_ATTR flowControl(void *arg)
{
    static uint16_t couter = 0;
    couter++;
    if(couter >= tcreq_period * 60)
    {
        tcreq_couter = 0;
        couter = 0;
    }
}
static void ICACHE_FLASH_ATTR enterSettingSelfap(os_timer_t *timer)
{
    system_os_post(IOTGO_CORE, MSG_CORE_ENTER_SETTING_SELFAP_MODE, 0);
    os_timer_disarm(timer);
    os_free(timer);
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
                if (DEVICE_MODE_SETTING != current_mode
                    && DEVICE_MODE_SETTING_SELFAP != current_mode)
                {
                    if (!long_press_processed)
                    {
                        long_press_processed = true;
                        system_os_post(IOTGO_CORE, MSG_CORE_ENTER_SETTING_MODE, 0);
                    }
                }
                else if (DEVICE_MODE_SETTING == current_mode)
                {
                    /* long key pressed when setting , to do:
                     *   - exit setting mode
                     *   - enter setting_selfap 
                     */
                    if (!long_press_processed)
                    {
                        long_press_processed = true;
                        system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_MODE, 0);
                        os_timer_t *timer = (os_timer_t *)os_zalloc(sizeof(os_timer_t));
                        os_timer_disarm(timer);
                        os_timer_setfn(timer, (os_timer_func_t *)enterSettingSelfap, timer);
                        os_timer_arm(timer, 1500, 0);
                        iotgoInfo("\n\n\nEnter setting_selfap after %u milliseconds\n\n\n", 1500);
                    }
                }
                else
                {
                    long_press_processed = true;
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
            if (DEVICE_MODE_SETTING == current_mode)
            {
                system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_MODE, 0);
            }
            else if (DEVICE_MODE_SETTING_SELFAP == current_mode)
            {
                system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_SELFAP_MODE, 0);
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

CMD tc_cmd[] = {
    {"tick",processTickCmd},{"baudrate",processBaudCmd},{"dog",processDogCmd}
    ,{"sled",processSledCmd},{"stateInfo",processStateInfoCmd},{"state",processStateCmd}
    ,{"rssi",processRssiCmd},{"time",processTimeCmd},{"setting",processSettingCmd}
    ,{"version",processVersionCmd},{"deviceid",processDeviceId},{"stamac",processStamacCmd}
    ,{"sapmac",processSapMacCmd},{"update",processUpdateCmd},{"respond",processRespondCmd}
    ,{"query",processQueryCmd},{"tmrNew",processTimerUpdateWithParamCmd},{"tmrEdit",processTimerUpdateWithParamCmd}
    ,{"tmrDel",processTimerUpdateWithParamCmd},{"tmrDelAll",processTimerUpdateCmd},{"tmrEnable",processTimerUpdateWithParamCmd}
    ,{"tmrEnableAll",processTimerUpdateCmd},{"tmrDisable",processTimerUpdateWithParamCmd},{"tmrDisableAll",processTimerUpdateCmd}
    ,{"tmrQuery",processTimerQueryDevCmd},{"crc",processCrcCmd},{"tmrMsg",processTmrMsgCmd},{"ret",processRetCmd}
};
static void ICACHE_FLASH_ATTR sendDataFromAppToMcu(void *arg)
{
    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE_BY_REMOTE, 0);
}
static void ICACHE_FLASH_ATTR initTcConfig(void)
{
    devLoadConfigFromFlash(&tc_config,sizeof(tc_config));
    if(9600 != tc_config.baudrate && 115200 != tc_config.baudrate)
    {
        tc_config.baudrate = 9600;
    }
    if(tc_config.watch_dog < 0 || tc_config.watch_dog > 60)
    {
        tc_config.watch_dog = 0;
    }
    uart_init(tc_config.baudrate, IOTGO_UART1_BAUDRATE);
    crc_enable = tc_config.crc_enable;
    if(tc_config.watch_dog > 0)
    {
        os_timer_arm(&watch_dog_timer, (1000 * tc_config.watch_dog), 1);
    }
    normal_mode_led_off = tc_config.sled_off;
    if(tc_config.state_info > 0)
    {
        os_timer_arm(&query_state_timer, 1000, 1);
    }
    tmr_msg_flag = tc_config.timer_sync;
}
static void ICACHE_FLASH_ATTR tellMcuStart(void)
{
    tcAddDataToQueue(&tc_link_queue,"[\"start\"]",TC_NULL,false);    
}
static void ICACHE_FLASH_ATTR switchInitIoTgoDevice(void)
{
    /* 初始化 sys key */
    interruptInit();
    tcCreateLinkQueue(&tc_link_queue,sendDataFromAppToMcu);
    watchDogHighPriorityInit();
    cmdProcessorStart(tc_cmd,sizeof(tc_cmd) / sizeof(CMD));
    timerInit(switchActionCallback);
    os_timer_disarm(&send_data_again_timer);
    os_timer_setfn(&send_data_again_timer, (os_timer_func_t *)sendDataToMcuAgain, NULL);
    os_timer_disarm(&query_state_timer);
    os_timer_setfn(&query_state_timer, (os_timer_func_t *)queryState, NULL);
    initTcConfig();
    os_timer_disarm(&flow_control_timer);
    os_timer_setfn(&flow_control_timer, (os_timer_func_t *)flowControl, NULL);
    os_timer_arm(&flow_control_timer, 1000, 1);
    tellMcuStart();
}
static void ICACHE_FLASH_ATTR sendDevConfigInRegister(const char *data)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_config = NULL; 
    cJSON *cjson_devconfig = NULL;
    cJSON *cjson_tcreq_period = NULL;
    cJSON *cjson_tcreq_max = NULL;
    cjson_root = cJSON_Parse((char *)data);
    if(cjson_root == NULL)
    {
        iotgoError("Error before: [%s]\n",cJSON_GetErrorPtr());
        cJSON_Delete(cjson_root);
        return;
    }
	if(NULL != (cjson_config = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_CONFIG)))
	{
	    /*截取数据并发送给MCU*/
        if(NULL != (cjson_devconfig = cJSON_GetObjectItem(cjson_config,IOTGO_STRING_DEV_CONFIG)))
        {
            if(NULL != (cjson_tcreq_period = cJSON_GetObjectItem(cjson_devconfig,IOTGO_STRING_TCREQ_PERIOD)))
            {
                if(cjson_tcreq_period->type == cJSON_Number)
                {
                    tcreq_period = cjson_tcreq_period->valueint;
                    iotgoInfo("tcreq_period = %d",tcreq_period);
                }
                cJSON_DeleteItemFromObject(cjson_devconfig,IOTGO_STRING_TCREQ_PERIOD);
            }
            if(NULL != (cjson_tcreq_max = cJSON_GetObjectItem(cjson_devconfig,IOTGO_STRING_TCREQMAX)))
            {
                if(cjson_tcreq_max->type == cJSON_Number)
                {
                    tcreq_max = cjson_tcreq_max->valueint;
                    iotgoInfo("tcreq_max = %d",tcreq_max);
                }
                cJSON_DeleteItemFromObject(cjson_devconfig,IOTGO_STRING_TCREQMAX);
            }   
            if(deleteField(cjson_devconfig))
            {
                char *devconfig = NULL;
                char *data = NULL;
                uint32_t length = 0;
                devconfig = cJSON_PrintUnformatted(cjson_devconfig);
                /*length保留了结束符*/
                length = os_strlen(devconfig) + 15;
                data = (char *)os_malloc(length);
                os_strcpy(data,"[\"regConfig\",");
                os_strcat(data,devconfig);
                os_strcat(data,"]");
                tcAddDataToQueue(&tc_link_queue,data,TC_NULL,false);
                os_free(devconfig);
                os_free(data);
            }
        }
        else
        {
            iotgoInfo("don't have devConfig,please check");
        }
	}
	else
	{
        iotgoError("the pkg is error,please check");
	}
	cJSON_Delete(cjson_root);
}

static void ICACHE_FLASH_ATTR sendDevConfigInNotify(const char *data)
{
    cJSON *cjson_root = NULL;
    cJSON *cjson_params = NULL;
    cJSON *cjson_devconfig = NULL;
    cJSON *cjson_tcreq_period = NULL;
    cJSON *cjson_tcreq_max = NULL;
    cjson_root = cJSON_Parse((char *)data);
    if(cjson_root == NULL)
    {
        iotgoError("Error before: [%s]\n",cJSON_GetErrorPtr());
        cJSON_Delete(cjson_root);
        return;
    }
    if(NULL != (cjson_params = cJSON_GetObjectItem(cjson_root,IOTGO_STRING_PARAMS)))
    {
        /*截取devConfig字段，并发送给MCU*/
        if(NULL != (cjson_devconfig = cJSON_GetObjectItem(cjson_params,IOTGO_STRING_DEV_CONFIG)))
        {
            if(NULL != (cjson_tcreq_period = cJSON_GetObjectItem(cjson_devconfig,IOTGO_STRING_TCREQ_PERIOD)))
            {
                if(cjson_tcreq_period->type == cJSON_Number)
                {
                    tcreq_period = cjson_tcreq_period->valueint;
                }
                cJSON_DeleteItemFromObject(cjson_devconfig,IOTGO_STRING_TCREQ_PERIOD);
            }
            if(NULL != (cjson_tcreq_max = cJSON_GetObjectItem(cjson_devconfig,IOTGO_STRING_TCREQMAX)))
            {
                if(cjson_tcreq_max->type == cJSON_Number)
                {
                    tcreq_max = cjson_tcreq_max->valueint;
                }
                cJSON_DeleteItemFromObject(cjson_devconfig,IOTGO_STRING_TCREQMAX);
            }   
            if(deleteField(cjson_devconfig))
            {
                char *devconfig = NULL;
                char *data = NULL;
                uint32_t length = 0;
                devconfig = cJSON_PrintUnformatted(cjson_devconfig);
                /*length保留了结束符*/
                length = os_strlen(devconfig) + 18;
                data = (char *)os_malloc(length);
                os_strcpy(data,"[\"notifyConfig\",");
                os_strcat(data,devconfig);
                os_strcat(data,"]");
                tcAddDataToQueue(&tc_link_queue,data,TC_NULL,false);
                os_free(devconfig);
                os_free(data);
            }
        }
        else
        {
            iotgoInfo("don't have devConfig,please check");
        }
    }
    else
    {
        iotgoError("the pkg is error,please check");
    }
    cJSON_Delete(cjson_root);
}
static void switchInitDeviceConfig(void)
{
    timerFlashErase();
    devEraseConfigInFlash();
    tcConfig tc_config_temp = {9600,false,0,false,0,false};
    devSaveConfigToFlash(&tc_config_temp, sizeof(tc_config_temp));
}
void ICACHE_FLASH_ATTR iotgoRegisterCallbackSet(IoTgoDevice *device)
{
    device->earlyInit = switchInitIoTgoDevice;
    device->postCenter = switchDevicePostCenter;
    
    device->respOfUpdateCallback = switchRespUpdateCallback;
    device->reqOfUpdateCallback = switchReqUpdateCallback;
    device->respOfQueryCallback = switchRespQueryCallback;
    device->reqOfQueryCallback = switchReqQueryCallback;
    device->reqofRegisterCallback = sendDevConfigInRegister;
    device->reqofNotityCallback = sendDevConfigInNotify;
    device->devInitDeviceConfig = switchInitDeviceConfig;
}
#endif /* #ifdef COMPILE_IOTGO_FWTRX_TC */

