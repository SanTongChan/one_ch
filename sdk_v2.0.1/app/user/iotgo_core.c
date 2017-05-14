#include "iotgo_core.h"

JSONTREE_OBJECT(iotgo_tree_resp_server_error_0_obj,
                JSONTREE_PAIR(IOTGO_STRING_ERROR, &json_error_0),
                JSONTREE_PAIR(IOTGO_STRING_USERAGENT, &json_device),
                JSONTREE_PAIR(IOTGO_STRING_APIKEY, &json_owner_uuid),
                JSONTREE_PAIR(IOTGO_STRING_DEVICEID, &json_deviceid),
                );

JSONTREE_OBJECT(iotgo_tree_resp_server_error_0,
                JSONTREE_PAIR("iotgo_tree_resp_server_error_0", &iotgo_tree_resp_server_error_0_obj)
                );


#define IOTGO_HEARTBEAT_PERIOD_BASE      (7)

static os_event_t task_iotgo_network_center_q[IOTGO_TASK_CORE_QLEN];

static os_timer_t setting_exit_timer;
static os_timer_t setting_selfap_exit_timer;

static os_timer_t __heartbeat_timer;
static os_timer_t __try_distor_timer;
static os_timer_t __try_conn_timer;
static os_timer_t __try_reg_timer;
static uint32 __iotgo_heartbeat_max_offset = 7;
static uint32 __reg_interval = 15; /* seconds */

static uint32 __last_ws_pkg_time = 0;
static uint32 try_conn_distor_counter = 0;
static uint32 try_conn_server_counter = 0;
static uint32 try_register_counter = 0;
static bool is_first_action_date = false;

static uint32 ping_counter = 0;
static uint32 pong_counter = 0;

static os_timer_t gmt_time_update_timer;

void ICACHE_FLASH_ATTR iotgoCoreRefreshLastPkgTime(void)
{
    __last_ws_pkg_time = system_get_time();
}

uint32 ICACHE_FLASH_ATTR iotgoCoreLastPkgTime(void)
{
    return __last_ws_pkg_time;
}

static void ICACHE_FLASH_ATTR sendError0ToServer(void)
{
    uint8 *temp_buffer = (uint8 *)os_malloc(IOTGO_JSON_BUFFER_SIZE);
    if (temp_buffer)
    {
        os_memset(temp_buffer, 0, IOTGO_JSON_BUFFER_SIZE);
        json_ws_send((struct jsontree_value *)&iotgo_tree_resp_server_error_0, "iotgo_tree_resp_server_error_0", temp_buffer);
        if(!iotgoQueueAdd(temp_buffer,IOTGO_ERROR0,false))
        {
            iotgoInfo("add data to queue error");
        }
    }
    else
    {
        iotgoError("os_malloc err!");
    }
    
    if (temp_buffer)
    {
        os_free(temp_buffer);
    }
}


static void ICACHE_FLASH_ATTR cbModeMonitorTimer(void *arg)
{
    static uint32 mode_monitor_cnt = 0;

    IoTgoDeviceMode mode = iotgoDeviceMode();
    
    switch (mode)
    {
        case DEVICE_MODE_WORK_AP_ERR:
        case DEVICE_MODE_WORK_AP_OK:
        case DEVICE_MODE_WORK_INIT:
        {
            mode_monitor_cnt++;
            if (mode_monitor_cnt >= 60)
            {
                mode_monitor_cnt = 0;
                iotgoInfo("mode monitor expired and restart network now!");

                /* 重新连接路由 */
                system_os_post(IOTGO_CORE, MSG_CORE_JOIN_AP, 0);
            }
        }
        break;
        default:
        {
            mode_monitor_cnt = 0;
        }
    }
}

static void ICACHE_FLASH_ATTR iotgoModeMonitorStart(void)
{
    static os_timer_t mode_monitor_timer;

    os_timer_disarm(&mode_monitor_timer);
    os_timer_setfn(&mode_monitor_timer, (os_timer_func_t *)cbModeMonitorTimer, NULL);
    os_timer_arm(&mode_monitor_timer, 1000, 1);
}


uint32 ICACHE_FLASH_ATTR iotgoCoreRegInterval(void)
{
    return __reg_interval;
}

void ICACHE_FLASH_ATTR iotgoCoreSetRegInterval(uint32 regi)
{
    __reg_interval = regi;
}


void ICACHE_FLASH_ATTR iotgoCoreHeartbeatSetMaxOffset(uint32 delay_second)
{
    __iotgo_heartbeat_max_offset = delay_second;
}

static inline uint32 ICACHE_FLASH_ATTR getHeartbeatMaxOffset(void)
{
    return __iotgo_heartbeat_max_offset;
}

static void ICACHE_FLASH_ATTR sendPingToServer(void *arg)
{
    static uint32 _iotgo_heartbeat_rand_offset = 0;
    uint32 ws_pkg_time_diff;
    ws_pkg_time_diff = (0x100000000 + system_get_time() - iotgoCoreLastPkgTime()) % (0x100000000);
    if (ws_pkg_time_diff >= (getHeartbeatMaxOffset() + _iotgo_heartbeat_rand_offset) * (1000000))
    {
        iotgoCoreRefreshLastPkgTime();
        _iotgo_heartbeat_rand_offset = iotgoRand(0, IOTGO_HEARTBEAT_PERIOD_BASE);
        iotgoInfo("_iotgo_heartbeat_rand_offset = %d",_iotgo_heartbeat_rand_offset);
        system_os_post(IOTGO_CORE, MSG_CORE_WS_PING, 0);
    }
}

void ICACHE_FLASH_ATTR iotgoCoreHeartbeatStart(void)
{
    os_timer_disarm(&__heartbeat_timer);
    os_timer_setfn(&__heartbeat_timer, (os_timer_func_t *)sendPingToServer, NULL);
    os_timer_arm(&__heartbeat_timer, 1000, 1);
    iotgoCoreRefreshLastPkgTime();
    ping_counter = 0;
    pong_counter = 0;
}

void ICACHE_FLASH_ATTR iotgoCoreHeartbeatStop(void)
{
    os_timer_disarm(&__heartbeat_timer);    
    ping_counter = 0;
    pong_counter = 0;
}

static void ICACHE_FLASH_ATTR tryDistorTmrCb(void *arg)
{
    IoTgoDeviceMode mode = iotgoDeviceMode();
    if (DEVICE_MODE_WORK_INIT != mode
        && DEVICE_MODE_WORK_NORMAL != mode
        && DEVICE_MODE_WORK_AP_ERR != mode
        && DEVICE_MODE_SETTING != mode)
    {
        system_os_post(IOTGO_CORE, MSG_CORE_DISTRIBUTOR, 0);
    }

}

static void ICACHE_FLASH_ATTR gmtTimerCallback(void *arg)
{
    if (DEVICE_MODE_WORK_NORMAL == iotgoDeviceMode())
    {
        system_os_post(IOTGO_CORE, MSG_CORE_DATE, 0);
    }
}



static void ICACHE_FLASH_ATTR rebootCallback(void *arg)
{
    os_free(arg);
    system_restart();
}

static void ICACHE_FLASH_ATTR setExitTimerCallback(void *arg)
{
    uint8 jap_state = STATION_IDLE;

    iotgoSettingServerStop();
        
    jap_state = wifi_station_get_connect_status();
    if (STATION_GOT_IP == jap_state)
    {
        iotgoSetDeviceMode(DEVICE_MODE_WORK_AP_OK);
        system_os_post(IOTGO_CORE, MSG_CORE_DISTRIBUTOR, 0);
    }
    else
    {
        system_os_post(IOTGO_CORE, MSG_CORE_JOIN_AP, 0);
    }
}

static void ICACHE_FLASH_ATTR setSelfapExitTimerCallback(void *arg)
{
    iotgoSettingServerStop();
    iotgoWifiToStationMode();
    iotgoSetDeviceMode(DEVICE_MODE_WORK_AP_ERR);
    system_os_post(IOTGO_CORE, MSG_CORE_JOIN_AP, 0);
}



static int8 ICACHE_FLASH_ATTR sendWebSocketPing(void)
{
    sint8 ret;
    char buffer[10];
    uint16 header_index = 0;

    iotgoPrintHeapSize();
    
    buffer[header_index++] = 0x89;  /* Final package and ping pkg */
    buffer[header_index++] = 0x80;  /* Set mask and playload = 0 */
    buffer[header_index++] = 0x00;  /* masking key = 0 */
    buffer[header_index++] = 0x00;  /* masking key = 0 */  
    buffer[header_index++] = 0x00;  /* masking key = 0 */
    buffer[header_index++] = 0x00;  /* masking key = 0 */
    if(!iotgoQueueAdd(buffer,IOTGO_PING,false))
    {
        iotgoInfo("add data to queue error");
    }
}

static void ICACHE_FLASH_ATTR connAgainTimerCallback(void *arg)
{
    system_os_post(IOTGO_CORE, MSG_CORE_CONNECT_SERVER, 0);
}

static void ICACHE_FLASH_ATTR cancelRetryRegisterStrategy(void)
{
    os_timer_disarm(&__try_reg_timer);
    try_register_counter = 0;
}

static void ICACHE_FLASH_ATTR cancelRetryConnServerStrategy(void)
{
    os_timer_disarm(&__try_conn_timer);
    try_conn_server_counter = 0;
}

static void ICACHE_FLASH_ATTR cancelRetryDistorStrategy(void)
{
    os_timer_disarm(&__try_distor_timer);
    try_conn_distor_counter = 0;
}

static void ICACHE_FLASH_ATTR cbRegisterTimer(void *arg)
{   
    system_os_post(IOTGO_CORE, MSG_CORE_REGISTER, 0);
}

static void ICACHE_FLASH_ATTR retryRegisterStrategy(uint32 failed_cnt)
{    
    uint32 delay = 0;

    if (failed_cnt <= 30)
    {
        delay = iotgoRand(2, 4);
    }
    else
    {
        delay = iotgoRand(iotgoCoreRegInterval(), iotgoCoreRegInterval() * 1.5);
    }
    
    os_timer_disarm(&__try_reg_timer);
    os_timer_setfn(&__try_reg_timer, (os_timer_func_t *)cbRegisterTimer, NULL);
    os_timer_arm(&__try_reg_timer, delay * 1000, 0);
    iotgoWarn("Register failed (%u) and and try again after %u second(s)", failed_cnt, delay);
}

static void ICACHE_FLASH_ATTR retryConnServerStrategy(uint32 failed_cnt)
{    
    uint32 delay = 0;

    if (failed_cnt <= 5)
    {
        delay = iotgoRand(iotgoPower(2, failed_cnt), iotgoPower(2, failed_cnt + 1));
    }
    else
    {
        delay = iotgoRand(60, 120);
    }
    
    os_timer_disarm(&__try_conn_timer);
    os_timer_setfn(&__try_conn_timer, (os_timer_func_t *)connAgainTimerCallback, NULL);
    os_timer_arm(&__try_conn_timer, delay * 1000, 0);
    iotgoWarn("ConnServer failed (%u) and and try again after %u second(s)", failed_cnt, delay);
}

static void ICACHE_FLASH_ATTR retryDistorStrategy(uint32 failed_cnt)
{    
    uint32 delay = 0;

    if (failed_cnt <= 5)
    {
        delay = iotgoRand(iotgoPower(2, failed_cnt), iotgoPower(2, failed_cnt + 1));
    }
    else
    {
        delay = iotgoRand(60, 120);
    }
    
    os_timer_disarm(&__try_distor_timer);
    os_timer_setfn(&__try_distor_timer, (os_timer_func_t *)tryDistorTmrCb, NULL);
    os_timer_arm(&__try_distor_timer, delay * 1000, 0);
    iotgoWarn("Distor failed (%u) and and try again after %u second(s)", failed_cnt, delay);
}

static void ICACHE_FLASH_ATTR taskIoTgoNetworkCenter(os_event_t *events)
{
    switch (events->sig)
    {
        case MSG_CORE_JOIN_AP:
        {
            iotgoInfo("MSG_CORE_JOIN_AP\n");
            cancelRetryRegisterStrategy();
            cancelRetryConnServerStrategy();
            cancelRetryDistorStrategy();
            iotgoSetDeviceMode(DEVICE_MODE_WORK_AP_ERR);            
            iotgoWifiJoin();
        }
        break;
        case MSG_CORE_JOIN_AP_ERR:
        {
            iotgoInfo("MSG_CORE_JOIN_AP_ERR\n");
            iotgoInfo("try join home ap again!\n");
            iotgoSetDeviceMode(DEVICE_MODE_WORK_AP_ERR);
            system_os_post(IOTGO_CORE, MSG_CORE_JOIN_AP, 0);
            
        }
        break;
        case MSG_CORE_AP_JOINED:
        {
            iotgoInfo("MSG_CORE_AP_JOINED\n");
            iotgoSetDeviceMode(DEVICE_MODE_WORK_AP_OK);
            system_os_post(IOTGO_CORE, MSG_CORE_DISTRIBUTOR, 0);
            
        }
        break;        
        case MSG_CORE_DISTRIBUTOR:
        {
            iotgoInfo("MSG_CORE_DISTRIBUTOR\n");
            if (DEVICE_MODE_SETTING != iotgoDeviceMode())
            {                
                cancelRetryRegisterStrategy();
                cancelRetryConnServerStrategy();
                startDistorRequest();
            }        
        }
        break;
        case MSG_CORE_DISTRIBUTOR_FINISHED:
        {
            iotgoInfo("MSG_CORE_DISTRIBUTOR_FINISHED\n");
            stopDistorRequest();
            if (IOTGO_DISTRIBUTOR_RESULT_OK == events->par)
            {
                try_conn_distor_counter = 0;
                system_os_post(IOTGO_CORE, MSG_CORE_CONNECT_SERVER, 0);
            }
            else
            {
                try_conn_distor_counter++;
                retryDistorStrategy(try_conn_distor_counter);
            }
            
        }
        break;
        case MSG_CORE_REDIRECT:
        {
            iotgoInfo("MSG_CORE_REDIRECT\n");
            sendError0ToServer();
            iotgoPconnDisconnect();
            system_os_post(IOTGO_CORE, MSG_CORE_CONNECT_SERVER_AGAIN, 0);
            
        } 
        break;
        case MSG_CORE_NOTIFY_CONFIG:
        {
            iotgoInfo("MSG_CORE_NOTIFY_CONFIG\n");
            sendError0ToServer();
        } 
        break;
        case MSG_CORE_CONNECT_SERVER:
        {
            iotgoInfo("MSG_CORE_CONNECT_SERVER\n");
            IoTgoDeviceMode mode = iotgoDeviceMode();
            if (DEVICE_MODE_SETTING != mode
                && DEVICE_MODE_SETTING_SELFAP != mode
                && DEVICE_MODE_WORK_NORMAL != mode)
            {                   
                cancelRetryRegisterStrategy();
                IoTgoHostInfo info = distorGetServerInfo();
#if !defined(IOTGO_OPERATION_SERVER_ENABLE_SSL)
                info.port = IOTGO_SERVER_NONSSL_PORT;
#endif
                iotgoPconnConnect(info);       
            }
        }
        break;
        case MSG_CORE_CONNECT_SERVER_AGAIN:
        {
            iotgoInfo("MSG_CORE_CONNECT_SERVER_AGAIN\n");
            IoTgoDeviceMode mode = iotgoDeviceMode();
            if (DEVICE_MODE_SETTING != mode
                && DEVICE_MODE_SETTING_SELFAP != mode)
            {                                
                iotgoSetDeviceMode(DEVICE_MODE_WORK_AP_OK);
                try_conn_server_counter++;
                retryConnServerStrategy(try_conn_server_counter);                
            }
            
        } 
        break;
        case MSG_CORE_CONNECT_SERVER_ERR:
        {
            iotgoInfo("MSG_CORE_CONNECT_SERVER_ERR\n");
            iotgoCoreHeartbeatStop();
            system_os_post(IOTGO_CORE, MSG_CORE_CONNECT_SERVER_AGAIN, 0);
            
        }
        break;
        case MSG_CORE_RECONNECT_SERVER:
        {
            iotgoInfo("MSG_CORE_RECONNECT_SERVER\n");
            iotgoCoreHeartbeatStop();
            system_os_post(IOTGO_CORE, MSG_CORE_CONNECT_SERVER_AGAIN, 0);
            
        }
        break;
        case MSG_CORE_SERVER_DISCONNECTED:
        {
            iotgoInfo("MSG_CORE_SERVER_DISCONNECTED\n");            
            iotgoCoreHeartbeatStop();
            system_os_post(IOTGO_CORE, MSG_CORE_CONNECT_SERVER_AGAIN, 0);
            
        }
        break;
        case MSG_CORE_SERVER_CONNECTED:
        {
            iotgoInfo("MSG_CORE_SERVER_CONNECTED\n");
            iotgoQueueDeleteAll();
            try_conn_server_counter = 0;
            system_os_post(IOTGO_CORE, MSG_CORE_WS_CONNECT, 0);
            
        }
        break;
        case MSG_CORE_WS_CONNECT:
        {
            iotgoInfo("MSG_CORE_WS_CONNECT\n");
            iotgoPconnSwitchToWebSocket();
            
        }
        break;
        case MSG_CORE_WS_CONNECTED:
        {
            iotgoInfo("MSG_CORE_WS_CONNECTED\n");
            iotgoCoreHeartbeatStart();            
            system_os_post(IOTGO_CORE, MSG_CORE_REGISTER, 0);
            
        }
        break;
        case MSG_CORE_REGISTER:
        {
            iotgoInfo("MSG_CORE_REGISTER\n");
            if (DEVICE_MODE_SETTING != iotgoDeviceMode()
                && DEVICE_MODE_SETTING_SELFAP != iotgoDeviceMode())
            {
                iotgoSetDeviceMode(DEVICE_MODE_WORK_INIT);
                iotgoPconnDeviceRegister();
            }
        }
        break;
        case MSG_CORE_REGISTERED: /* Start device center */
        {
            iotgoInfo("MSG_CORE_REGISTERED\n");
            try_register_counter = 0;
            if (DEVICE_MODE_SETTING != iotgoDeviceMode()
                && DEVICE_MODE_SETTING_SELFAP != iotgoDeviceMode())
            {
                iotgoSetDeviceMode(DEVICE_MODE_WORK_NORMAL);

                is_first_action_date = true;
                system_os_post(IOTGO_CORE, MSG_CORE_DATE, 0);
                os_timer_disarm(&gmt_time_update_timer);
                os_timer_setfn(&gmt_time_update_timer, (os_timer_func_t *)gmtTimerCallback, NULL);
                os_timer_arm(&gmt_time_update_timer, (3600000/2), 1); 
                
                //system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_READYED, 0);
            }
            else
            {
                iotgoWarn("invalid MSG_CORE_REGISTERED and ignored!");
            }
            
        }
        break;    
        case MSG_CORE_REGISTER_ERR: /* Register again after one second */
        {
            iotgoInfo("MSG_CORE_REGISTER_ERR\n");
            try_register_counter++;
            retryRegisterStrategy(try_register_counter);
                        
        }
        break;    
        case MSG_CORE_DATE:
        {
            iotgoInfo("MSG_CORE_DATE\n");
            iotgoPconnDeviceDate();
            
        }
        break;
        case MSG_CORE_DATE_OK:
        {
            iotgoInfo("MSG_CORE_DATE_OK\n");
            if (is_first_action_date)
            {
                is_first_action_date = false;
                system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_READYED, 0);
            }
        }
        break;
        case MSG_CORE_WS_PING:
        {
            iotgoInfo("MSG_CORE_WS_PING\n");
            
            iotgoInfo("ping/pong = %u/%u", ping_counter, pong_counter);
            if ((ping_counter - pong_counter) >= 2)
            {
                iotgoWarn("Need to disconnect due to ping greater than pong by 2 times");
                iotgoPconnDisconnect();
            }
            
            sendWebSocketPing();
            ping_counter++;
            
        }
        break;
        case MSG_CORE_WS_PING_OK:
        {
            iotgoInfo("MSG_CORE_WS_PING_OK\n");
            iotgoCoreRefreshLastPkgTime();
            pong_counter++;
        }
        break;
        case MSG_CORE_UPGRADE_ENTER:
        {
            iotgoInfo("MSG_CORE_UPGRADE_ENTER\n");
            iotgoQueueDeleteAll();
            iotgoUpgradeStart();
            
        }
        break;
        case MSG_CORE_UPGRADE_EXIT:
        {
            iotgoInfo("MSG_CORE_UPGRADE_EXIT\n");
            iotgoSetDeviceMode(DEVICE_MODE_WORK_NORMAL);
            iotgoUpgradeStop(events->par);
        }
        break;
        case MSG_CORE_UPGRADE_RECONNECT_AGAIN:
        {
            iotgoInfo("MSG_CORE_UPGRADE_RECONNECT_AGAIN\n");
            iotgoUpgradeReconnect();
        }
        break;
        case MSG_CORE_GET_UPGRADE_DATA:
        {
            iotgoInfo("MSG_CORE_GET_UPGRADE_DATA\n");
            iotgoUpgradeDownload();
        }
        break;
        case MSG_CORE_SEND_JSON:
        {
            iotgoInfo("MSG_CORE_SEND_JSON\n");
            iotgoPconnSendJson();            
        }
        break;
        case MSG_CORE_SEND_JSON_AGAIN:
        {
            iotgoInfo("MSG_CORE_SEND_JSON_AGAIN\n");
            iotgoPconnSendJsonAgain();
        }
        break;
        case MSG_CORE_SEND_JSON_FINISHED:
        {
            iotgoInfo("MSG_CORE_SEND_JSON_FINISHED\n");
            iotgoPconnSendNextJson(events->par);
            
        }
        break;
        case MSG_CORE_ENTER_SETTING_MODE:
        {
            iotgoInfo("MSG_CORE_ENTER_SETTING_MODE\n");
            iotgoSetDeviceMode(DEVICE_MODE_SETTING);
            iotgoPconnDisconnect();
            cancelRetryRegisterStrategy();
            cancelRetryConnServerStrategy();
            cancelRetryDistorStrategy();
            iotgoWifiLeave();
            
            if(iotgo_device.enterEsptouchSettingModeCallback)
            {
                iotgo_device.enterEsptouchSettingModeCallback();
            }

            system_os_post(IOTGO_CORE, MSG_CORE_SETTING_SC_START, 0);
            
        }
        break;
        case MSG_CORE_EXIT_SETTING_MODE:
        {
            iotgoInfo("MSG_CORE_EXIT_SETTING_MODE\n");
            iotgoSettingTimeoutMonitorStop();
            iotgoSettingSmartConfigStop();
            if(iotgo_device.exitEsptouchSettingModeCallback)
            {
                iotgo_device.exitEsptouchSettingModeCallback();
            }
            
            os_timer_disarm(&setting_exit_timer);
            os_timer_setfn(&setting_exit_timer, (os_timer_func_t *)setExitTimerCallback, NULL);
            os_timer_arm(&setting_exit_timer, 1000, 0);
        }
        break;
        case MSG_CORE_SETTING_SEND_ID_TO_APP:
        {
            iotgoInfo("MSG_CORE_SETTING_SEND_ID_TO_APP\n");
            iotgoSettingSendDeviceInfoToApp();
            
        }
        break;
        case MSG_CORE_SETTING_RESP_TO_APP:
        {
            iotgoInfo("MSG_CORE_SETTING_RESP_TO_APP\n");
            iotgoSettingSendRespToApp(events->par);
            
        }
        break;
        case MSG_CORE_ENTER_SETTING_SELFAP_MODE:
        {
            iotgoInfo("MSG_CORE_ENTER_SETTING_SELFAP_MODE\n");
            iotgoSetDeviceMode(DEVICE_MODE_SETTING_SELFAP);
            if(iotgo_device.enterApSettingModeCallback)
            {
                iotgo_device.enterApSettingModeCallback();
            }
            iotgoPconnDisconnect();
            cancelRetryRegisterStrategy();
            cancelRetryConnServerStrategy();
            cancelRetryDistorStrategy();
            iotgoWifiLeave();
            iotgoWifiToStationAndApMode();
            iotgoSettingTimeoutMonitorStart();
            iotgoSettingServerStart();
        }
        break;        
        case MSG_CORE_EXIT_SETTING_SELFAP_MODE:
        {
            iotgoInfo("MSG_CORE_EXIT_SETTING_SELFAP_MODE\n");
            iotgoSettingTimeoutMonitorStop();
            if(iotgo_device.exitApSettingModeCallback)
            {
                iotgo_device.exitApSettingModeCallback();
            }
            os_timer_disarm(&setting_selfap_exit_timer);
            os_timer_setfn(&setting_selfap_exit_timer, (os_timer_func_t *)setSelfapExitTimerCallback, NULL);
            os_timer_arm(&setting_selfap_exit_timer, 1000, 0);           
        }
        break;
        case MSG_CORE_SETTING_SC_START:
        {
            iotgoInfo("MSG_CORE_SETTING_SC_START\n");
            iotgoSettingTimeoutMonitorStop();
            iotgoSettingTimeoutMonitorStart();
            iotgoWifiToStationMode();
            iotgoSettingSmartConfigStart();
        }
        break;
        case MSG_CORE_SETTING_SC_STOP:
        {
            iotgoInfo("MSG_CORE_SETTING_SC_STOP\n");
            iotgoSettingSmartConfigStop();
            iotgoSettingServerStart();
        }
        break;
        case MSG_CORE_SETTING_SC_RESTART:
        {
            iotgoInfo("MSG_CORE_SETTING_SC_RESTART\n");
            iotgoSettingSmartConfigStop();
            system_os_post(IOTGO_CORE, MSG_CORE_SETTING_SC_START, 0);
        }
        break;
        default:
        {
            iotgoError("\n\nUnknown SIGNAL NETWORK = %u and ignored!\n\n", events->sig);
        }
    }
}


/**
 * 必须释放定时器内存
 */
static void ICACHE_FLASH_ATTR startIoTgoNetworkCenter(void *timer)
{
    iotgoInfo("startIoTgoNetworkCenter\n");
    os_free(timer);

    iotgoPconnInit();
    
    startTimeService();

    iotgoWifiToStationMode();
    system_os_post(IOTGO_CORE, MSG_CORE_JOIN_AP, 0);  
}

void ICACHE_FLASH_ATTR iotgoCoreRun(void)
{
    iotgoModeMonitorStart();

#if defined(IOTGO_OPERATION_SERVER_ENABLE_SSL) || defined(IOTGO_DISTRIBUTOR_ENABLE_SSL)
    if (espconn_secure_set_size(1, IOTGO_DEVICE_CLIENT_ENABLE_SSL_SIZE))
    {
        iotgoInfo("ssl buffer size = %u", espconn_secure_get_size(1));
    }
    else
    {
        iotgoInfo("set ssl buffer err");
    }
#endif

    /* 
     * IoT1.0设备升级为IoT2.0协议后重启时，从设备中加载不合法的分配服务器IP地址
     * 即此时的 iotgo_flash_param.iot_distributor.host 字符串是没有结尾符的。
     * 这里在其末尾添加一个结尾符，必不能少了。
     */
    iotgo_flash_param.iot_distributor.host[IOTGO_HOST_NAME_SIZE - 1] = '\0';

    /*
     * 确保存入Flash的字符串有正确的结尾符。
     */
    iotgo_flash_param.new_bin_info.name[sizeof(iotgo_flash_param.new_bin_info.name) - 1] = 0;
    iotgo_flash_param.new_bin_info.version[sizeof(iotgo_flash_param.new_bin_info.version) - 1] = 0;
    iotgo_flash_param.new_bin_info.sha256[sizeof(iotgo_flash_param.new_bin_info.sha256) - 1] = 0;
    
    os_memcpy(iotgo_device.deviceid, iotgo_flash_param.factory_data.deviceid, 
        IOTGO_DEVICEID_SIZE);
        
    os_memcpy(iotgo_device.factory_apikey, iotgo_flash_param.factory_data.factory_apikey, 
        IOTGO_OWNER_UUID_SIZE);

    system_os_task(taskIoTgoNetworkCenter, 
        IOTGO_CORE, 
        task_iotgo_network_center_q, 
        IOTGO_TASK_CORE_QLEN);

    os_timer_t *timer = (os_timer_t *)os_zalloc(sizeof(os_timer_t));
    os_timer_disarm(timer);
    os_timer_setfn(timer, (os_timer_func_t *)startIoTgoNetworkCenter, timer);
    os_timer_arm(timer, 100, 0);
    
}

