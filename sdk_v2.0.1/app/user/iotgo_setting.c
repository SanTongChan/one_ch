#include "iotgo_setting.h"

#define SETTING_MODE_JSON_BUFFER_SIZE   (IOTGO_JSON_BUFFER_SIZE)
#define SETTING_MODE_HTTP_BUFFER_SIZE   (IOTGO_JSON_BUFFER_SIZE + 512)

static uint8 *setting_mode_http_buffer = NULL;
static uint8 *setting_mode_json_buffer = NULL;
static Spro *spro = NULL;

static struct espconn server_tcp;
static esp_tcp server_tcp_proto_tcp;

static os_timer_t start_tcp_listen_timer;

static os_timer_t setting_delay_resp_app_timer;


static os_timer_t waiting_client_timer;
static bool is_setting_started = false;


static uint32 __setting_mode_timeout_listener_timer_counter;
static os_timer_t __setting_mode_timeout_listener_timer;

static bool smartconfig_started_flag = false;
static os_timer_t __smartconfig_join_ap_timer;
static bool smartconfig_ap_joined_flag = false;

static const struct jsontree_string json_post = JSONTREE_STRING(IOTGO_STRING_POST);

JSONTREE_OBJECT(data_to_app_obj,
                JSONTREE_PAIR(IOTGO_STRING_DEVICEID, &json_deviceid),
                JSONTREE_PAIR(IOTGO_STRING_APIKEY, &json_factory_apikey),
                JSONTREE_PAIR(IOTGO_STRING_ACCEPT, &json_post),
                );

JSONTREE_OBJECT(id_to_app,
                JSONTREE_PAIR("id_to_app", &data_to_app_obj)
                );

static void ICACHE_FLASH_ATTR cbWaitingClientTmr(void *arg)
{
    iotgoInfo("No APP (as client) accepted and stop server and start SC!");
    iotgoSettingServerStop();
    system_os_post(IOTGO_CORE, MSG_CORE_SETTING_SC_START, 0);
}

static void ICACHE_FLASH_ATTR svrSproOnePkgCb(void *arg, uint8 *pdata, uint16 len, bool flag)
{
    struct espconn *pespconn = (struct espconn *)arg;
    char *temp, *temp1, *temp2, *temp3;
    uint32 i;
    bool ssid = false;
    bool pass = false;
    bool distor_domain_flag = false;
    bool distor_port_flag = false;
    char ssid_str[32] = {0};
    char pass_str[64] = {0};
    char distor_domain[IOTGO_HOST_NAME_SIZE] = {0};
    int32 distor_port = 0;
    
    int type, type1, type2;
    
    struct jsonparse_state *pjs;

    iotgoInfo("\nRecv len: %u data=[%s]\n", len, pdata);
    
    /* 处理 APP 发送过来的 GET /device 请求, 设备应该将 deviceid 和 apikey 发送给 APP */
    temp = (char *)os_strstr(pdata, "GET /device HTTP/1.1");
    if (NULL != temp)
    {
        system_os_post(IOTGO_CORE, MSG_CORE_SETTING_SEND_ID_TO_APP, 0);
        return;
    }

    temp = (char *)os_strstr(pdata, "POST /ap HTTP/1.1");
    temp1 = (char *)os_strstr(pdata, "{");
    temp2 = (char *)os_strstr(pdata, "}");

    if (temp && temp1 && temp2)
    {
        pjs = (struct jsonparse_state *)os_zalloc(sizeof(struct jsonparse_state));
        jsonparse_setup(pjs, temp1, os_strlen(temp1));
        while ((type = jsonparse_next(pjs)) != 0)
        {
            if (JSON_TYPE_PAIR_NAME != type)
            {
                continue;
            }
            iotgoDebug("type = %d (%c), vlen = %d\n", type, type, jsonparse_get_len(pjs));
            
            if (0 == jsonparse_strcmp_value(pjs, "ssid"))
            {
                type1 = jsonparse_next(pjs);
                type2 = jsonparse_next(pjs);
                if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_STRING == type2)
                {
                    jsonparse_copy_value(pjs, ssid_str, sizeof(ssid_str));
                    ssid = true;
                    iotgoInfo("Found ssid:%s\n", ssid_str);
                }
                else
                {
                    iotgoWarn("Invalid ssid field! Ignore this package!\n");
                    break;
                }
            }
            else if (0 == jsonparse_strcmp_value(pjs, "password"))
            {
                type1 = jsonparse_next(pjs);
                type2 = jsonparse_next(pjs);
                if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_STRING == type2)
                {
                    jsonparse_copy_value(pjs, pass_str, sizeof(pass_str));
                    pass = true;
                    iotgoInfo("Found password:%s\n", pass_str);
                }
                else
                {
                    iotgoWarn("Invalid password field! Ignore this package!\n");
                    break;
                }
            }
            else if (0 == jsonparse_strcmp_value(pjs, "serverName"))
            {
                if (jsonIoTgoGetString(pjs, "serverName", distor_domain, sizeof(distor_domain)))
                {
                    distor_domain_flag = true;
                    iotgoInfo("Found serverName:%s\n", distor_domain);
                }
                else
                {
                    break;
                }
            }
            else if (0 == jsonparse_strcmp_value(pjs, "port"))
            {
                if (jsonIoTgoGetNumber(pjs, "port", &distor_port))
                {
                    distor_port_flag = true;
                    iotgoInfo("Found port:%d\n", distor_port);
                }
                else
                {
                    break;
                }
            }
        }
        
        os_free(pjs);
    }

    
    if (DEVICE_MODE_SETTING == iotgoDeviceMode() 
        && distor_domain_flag 
        && distor_port_flag)
    {
        os_memcpy(iotgo_flash_param.iot_distributor.host, distor_domain, sizeof(distor_domain));
        iotgo_flash_param.iot_distributor.port = distor_port;
        iotgoFlashSaveParam(&iotgo_flash_param);
        system_os_post(IOTGO_CORE, MSG_CORE_SETTING_RESP_TO_APP, STATION_GOT_IP);
    }   
    else if (DEVICE_MODE_SETTING_SELFAP == iotgoDeviceMode() 
        && ssid
        && pass
        && distor_domain_flag 
        && distor_port_flag)
    {
        os_memcpy(iotgo_flash_param.sta_config.ssid, ssid_str, sizeof(ssid_str));
        os_memcpy(iotgo_flash_param.sta_config.password, pass_str, sizeof(pass_str));
        os_memcpy(iotgo_flash_param.iot_distributor.host, distor_domain, sizeof(distor_domain));
        iotgo_flash_param.iot_distributor.port = distor_port;
        iotgoFlashSaveParam(&iotgo_flash_param);
        system_os_post(IOTGO_CORE, MSG_CORE_SETTING_RESP_TO_APP, STATION_GOT_IP);
    }
    else 
    {
        iotgoError("Bad mode %d", iotgoDeviceMode());
    }
}


static void ICACHE_FLASH_ATTR svrTCPRecvCallback(void *arg, char *pdata, 
    unsigned short len)
{
    if (is_setting_started)
    {
        spTcpRecv(spro, arg, pdata, len);
    }
    else
    {
        iotgoError("Fatal Error in Setting Mode!");
    }
}

static void ICACHE_FLASH_ATTR svrTCPReconnCallback(void *arg, sint8 errType)
{
    iotgoDebug("\nsvrTCPReconnCallback called and errType = %d\n", errType);
}

static void ICACHE_FLASH_ATTR svrTCPDisconnCallback(void *arg)
{
    iotgoDebug("\nsvrTCPDisconnCallback called\n");
}

static void ICACHE_FLASH_ATTR svrTCPSendCallback(void *arg)
{
    iotgoDebug("\nsvrTCPSendCallback called\n");
}

static void ICACHE_FLASH_ATTR svrTCPConnectedCallback(void *arg)
{
    static uint32 counter = 0;
    iotgoDebug("svrTCPConnectedCallback counter = %u\n", ++counter);
}

static void ICACHE_FLASH_ATTR startTCPListen(void *arg)
{
    bool ret;
    struct ip_info temp_ip;
    
    wifi_get_ip_info(SOFTAP_IF, &temp_ip);
    iotgoInfo("SAP IP:\"%d.%d.%d.%d\"\n", IP2STR(&temp_ip.ip));

    os_bzero(&server_tcp, sizeof(server_tcp));
    os_bzero(&server_tcp_proto_tcp, sizeof(server_tcp_proto_tcp));
    
    server_tcp.type = ESPCONN_TCP;
    server_tcp.state = ESPCONN_NONE;
    server_tcp.proto.tcp = &server_tcp_proto_tcp;
    server_tcp.proto.tcp->local_port = 80;
    
    espconn_regist_connectcb(&server_tcp, svrTCPConnectedCallback);
    espconn_regist_recvcb(&server_tcp, svrTCPRecvCallback);
    espconn_regist_reconcb(&server_tcp, svrTCPReconnCallback);
    espconn_regist_disconcb(&server_tcp, svrTCPDisconnCallback);
    espconn_regist_sentcb(&server_tcp, svrTCPSendCallback);

    ret = espconn_accept(&server_tcp);
    iotgoInfo("espconn_accept ret = %d\n", ret);
    
    ret = espconn_regist_time(&server_tcp, 180, 0);
    iotgoInfo("espconn_regist_time ret = %d\n", ret);   
}


void ICACHE_FLASH_ATTR iotgoSettingSendDeviceInfoToApp(void)
{
    char temp[20];

    os_timer_disarm(&waiting_client_timer);
    
    os_memset(setting_mode_json_buffer, '\0', SETTING_MODE_JSON_BUFFER_SIZE);
    os_memset(setting_mode_http_buffer, '\0', SETTING_MODE_HTTP_BUFFER_SIZE);
    
    json_ws_send((struct jsontree_value*)&id_to_app, "id_to_app", setting_mode_json_buffer);
    os_sprintf(temp, "%d", os_strlen(setting_mode_json_buffer));
    
    os_strcpy(setting_mode_http_buffer, "HTTP/1.1 200 OK\r\n");
    os_strcat(setting_mode_http_buffer, "Content-Type: application/json\r\n");
    os_strcat(setting_mode_http_buffer, "Connection: keep-alive\r\n");
    os_strcat(setting_mode_http_buffer, "Content-Length: ");
    os_strcat(setting_mode_http_buffer, temp);
    os_strcat(setting_mode_http_buffer, "\r\n\r\n");
    os_strcat(setting_mode_http_buffer, setting_mode_json_buffer);
    
    espconn_send(&server_tcp, setting_mode_http_buffer, os_strlen(setting_mode_http_buffer));
}

static void ICACHE_FLASH_ATTR settingDelayRespAppTimerCb(void *arg)
{
    int8 ret; 
    bool *exit_flag = arg;
    
    ret = espconn_send(&server_tcp, setting_mode_http_buffer, os_strlen(setting_mode_http_buffer));
    if (0 == ret)
    {
        iotgoInfo("send RESP_TO_APP ok!\n");
    }
    else
    {
        iotgoWarn("send RESP_TO_APP err! and ret = %d\n", ret);
    }
    
    if (*exit_flag) 
    {
        IoTgoDeviceMode mode = iotgoDeviceMode();
        if (DEVICE_MODE_SETTING == mode)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_MODE, 0);    
        }
        else if (DEVICE_MODE_SETTING_SELFAP == mode)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_EXIT_SETTING_SELFAP_MODE, 0);    
        }
        else 
        {
            iotgoError("Bad mode %d", mode);
        }
    }
    
}

void ICACHE_FLASH_ATTR iotgoSettingSendRespToApp(uint32 par)
{
    static bool exit_setting_flag = false;
    char temp[20];
    sint8 ret;
    os_memset(setting_mode_json_buffer, '\0', SETTING_MODE_JSON_BUFFER_SIZE);
    os_memset(setting_mode_http_buffer, '\0', SETTING_MODE_HTTP_BUFFER_SIZE);
    exit_setting_flag = false;
    switch (par)
    {
        case STATION_GOT_IP:
        {
            os_strcpy(setting_mode_json_buffer, "{\"error\":0}");
            exit_setting_flag = true;
            break;
        }
        case STATION_CONNECTING:
        {
            os_strcpy(setting_mode_json_buffer, "{\"error\":400,\"reason\":\"timeout\"}");
            break;
        }
        case STATION_WRONG_PASSWORD:
        {
            os_strcpy(setting_mode_json_buffer, "{\"error\":401,\"reason\":\"Wrong password\"}");
            break;
        }
        case STATION_NO_AP_FOUND:
        {
            os_strcpy(setting_mode_json_buffer, "{\"error\":402,\"reason\":\"No AP found\"}");
            break;
        }
        case STATION_CONNECT_FAIL:
        {
            os_strcpy(setting_mode_json_buffer, "{\"error\":403,\"reason\":\"Connect failed\"}");
            break;
        }
        default:
        {
            os_strcpy(setting_mode_json_buffer, "{\"error\":404,\"reason\":\"Unknown exception\"}");
            break;
        }
    }
    
    os_sprintf(temp, "%d", os_strlen(setting_mode_json_buffer));
    os_strcpy(setting_mode_http_buffer, "HTTP/1.1 200 OK\r\n");
    os_strcat(setting_mode_http_buffer, "Content-Type: application/json\r\n");
    os_strcat(setting_mode_http_buffer, "Connection: keep-alive\r\n");
    os_strcat(setting_mode_http_buffer, "Content-Length: ");
    os_strcat(setting_mode_http_buffer, temp);
    os_strcat(setting_mode_http_buffer, "\r\n\r\n");
    os_strcat(setting_mode_http_buffer, setting_mode_json_buffer);

    iotgoInfo("Waiting for 3 ms...");

    os_timer_disarm(&setting_delay_resp_app_timer);
    os_timer_setfn(&setting_delay_resp_app_timer, (os_timer_func_t *)settingDelayRespAppTimerCb, &exit_setting_flag);
    os_timer_arm(&setting_delay_resp_app_timer, 3, 0);
}

static void ICACHE_FLASH_ATTR cbSettingModeTimeoutListenerTimer(void *arg)
{
    __setting_mode_timeout_listener_timer_counter++;
    if (__setting_mode_timeout_listener_timer_counter >= 180)
    {
        __setting_mode_timeout_listener_timer_counter = 0;
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
}

void ICACHE_FLASH_ATTR iotgoSettingTimeoutMonitorStop(void)
{
    __setting_mode_timeout_listener_timer_counter = 0;
    os_timer_disarm(&__setting_mode_timeout_listener_timer);
}

void ICACHE_FLASH_ATTR iotgoSettingTimeoutMonitorStart(void)
{
    __setting_mode_timeout_listener_timer_counter = 0;
    os_timer_disarm(&__setting_mode_timeout_listener_timer);
    os_timer_setfn(&__setting_mode_timeout_listener_timer, (os_timer_func_t *)cbSettingModeTimeoutListenerTimer, NULL);
    os_timer_arm(&__setting_mode_timeout_listener_timer, 1000, 1);
}


void ICACHE_FLASH_ATTR iotgoSettingServerStop(void)
{
    is_setting_started = false;
    os_timer_disarm(&waiting_client_timer);

    espconn_disconnect(&server_tcp);
    
    if (setting_mode_http_buffer)
    {
        os_free(setting_mode_http_buffer);
        setting_mode_http_buffer = NULL;
    }
    
    if (setting_mode_json_buffer)
    {
        os_free(setting_mode_json_buffer);
        setting_mode_json_buffer = NULL;
    }

    if (spro)
    {
        spReleaseObject(spro);
        spro = NULL;
    }
}

void ICACHE_FLASH_ATTR iotgoSettingServerStart(void)
{
    spro = spCreateObject(SPRO_PKG_TYPE_HTTP11, svrSproOnePkgCb, IOTGO_IOT_TCP_CLIENT_RECV_BUFFER_SIZE);
    if (NULL == spro)
    {
        iotgoError("spCreateObject err!");
    }

    setting_mode_http_buffer = (uint8 *)os_malloc(SETTING_MODE_HTTP_BUFFER_SIZE);
    setting_mode_json_buffer = (uint8 *)os_malloc(SETTING_MODE_JSON_BUFFER_SIZE);
    if ((setting_mode_http_buffer == NULL) || (setting_mode_json_buffer == NULL))
    {
        iotgoError("os_malloc failed and halt now!");
    }

    is_setting_started = true;
    os_timer_disarm(&start_tcp_listen_timer);
    os_timer_setfn(&start_tcp_listen_timer, (os_timer_func_t *)startTCPListen, NULL);
    os_timer_arm(&start_tcp_listen_timer, 1000, 0);

    os_timer_disarm(&waiting_client_timer);
    os_timer_setfn(&waiting_client_timer, (os_timer_func_t *)cbWaitingClientTmr, NULL);
    if (DEVICE_MODE_SETTING == iotgoDeviceMode())
    {    
        os_timer_arm(&waiting_client_timer, (30 * 1000), 0);
    }

}

static void ICACHE_FLASH_ATTR cbSmartConfigJoinApTimer(void *arg)
{   
    if (!smartconfig_ap_joined_flag)
    {
        if (DEVICE_MODE_SETTING == iotgoDeviceMode())
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SETTING_SC_RESTART, 0);
        }
    }
}

static void ICACHE_FLASH_ATTR startSmartConfigJoinApTimer(void)
{
    smartconfig_ap_joined_flag = false;
    os_timer_disarm(&__smartconfig_join_ap_timer);
    os_timer_setfn(&__smartconfig_join_ap_timer, (os_timer_func_t *)cbSmartConfigJoinApTimer, NULL);
    os_timer_arm(&__smartconfig_join_ap_timer, (30*1000), 0);
}

static void ICACHE_FLASH_ATTR stopSmartConfigJoinApTimer(void)
{
    smartconfig_ap_joined_flag = false;
    os_timer_disarm(&__smartconfig_join_ap_timer);
}

static void ICACHE_FLASH_ATTR cbSmartConfig(sc_status status, void *pdata)
{
    switch(status) {
        case SC_STATUS_WAIT:
            iotgoPrintf("SC_STATUS_WAIT\n");
            break;
        case SC_STATUS_FIND_CHANNEL:
            iotgoPrintf("SC_STATUS_FIND_CHANNEL\n");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            iotgoPrintf("SC_STATUS_GETTING_SSID_PSWD\n");
			sc_type *type = pdata;
            if (*type == SC_TYPE_ESPTOUCH) {
                iotgoPrintf("SC_TYPE:SC_TYPE_ESPTOUCH\n");
            } else {
                iotgoPrintf("SC_TYPE:SC_TYPE_AIRKISS\n");
            }
            break;
        case SC_STATUS_LINK:
            iotgoPrintf("SC_STATUS_LINK\n");
            struct station_config *sta_conf = pdata;
            if (!sta_conf)
            {
                iotgoError("sta_conf is NULL!");
                break;
            }

            int8 len = os_strlen(sta_conf->password);
            int8 i = 0;
            for (i = 0; i < len; i++)
            {
                if ((i % 2) == 0)
                {
                    sta_conf->password[i] -= 7;   
                }
                else
                {
                    sta_conf->password[i] -= 2;
                }
            }

            iotgoInfo("ssid:[%s], password:[%s]", sta_conf->ssid, sta_conf->password);
	        os_memcpy(&iotgo_flash_param.sta_config, sta_conf, sizeof(struct station_config));  
	        iotgoFlashSaveParam(&iotgo_flash_param);
	        startSmartConfigJoinApTimer();
	        wifi_station_set_config(sta_conf);
	        wifi_station_disconnect();
	        wifi_station_connect();
            break;
        case SC_STATUS_LINK_OVER:
            iotgoPrintf("SC_STATUS_LINK_OVER\n");
            smartconfig_ap_joined_flag = true;
            if (pdata != NULL) {
                uint8 phone_ip[4] = {0};
                os_memcpy(phone_ip, (uint8*)pdata, 4);
                iotgoPrintf("Phone ip: %d.%d.%d.%d\n",phone_ip[0],phone_ip[1],phone_ip[2],phone_ip[3]);
            }
            
            system_os_post(IOTGO_CORE, MSG_CORE_SETTING_SC_STOP, 0);
            break;
    }
	
}


void ICACHE_FLASH_ATTR iotgoSettingSmartConfigStart(void)
{
    smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS);
    smartconfig_started_flag = true;
    smartconfig_start(cbSmartConfig);
}

void ICACHE_FLASH_ATTR iotgoSettingSmartConfigStop(void)
{
    stopSmartConfigJoinApTimer();

    if (smartconfig_started_flag)
    {
        smartconfig_stop();
        smartconfig_started_flag = false;
    }
}

