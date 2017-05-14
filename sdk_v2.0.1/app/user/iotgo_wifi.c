#include "iotgo_wifi.h"

#define IOTGO_RSSI_QUEUE_LEN        (10)

static os_timer_t join_ap_timer;
static os_timer_t listen_rssi_timer;
static uint8_t counter = 0;
static int32 iotgo_device_rssi = IOTGO_WIFI_RSSI_DEFAULT;

static uint8 slave_frame_err[4] = {SLAVE_FRAME_START, SLAVE_FRAME_CTRL_TEST_ERR, SLAVE_FRAME_DATA, SLAVE_FRAME_STOP};
static uint8 slave_frame_ok[4] = {SLAVE_FRAME_START, SLAVE_FRAME_CTRL_TEST_OK, SLAVE_FRAME_DATA, SLAVE_FRAME_STOP};

static int8 __iotgo_rssi_queue[IOTGO_RSSI_QUEUE_LEN];

static void ICACHE_FLASH_ATTR queueInit(int8 val)
{
    uint16 i;

    for (i = 0; i < IOTGO_RSSI_QUEUE_LEN; i++)
    {
        __iotgo_rssi_queue[i] = val;
    }
}

static void ICACHE_FLASH_ATTR queueShift(int8 val)
{
    uint16 i;

    for (i = IOTGO_RSSI_QUEUE_LEN - 1; i >= 1; i--)
    {
        __iotgo_rssi_queue[i] = __iotgo_rssi_queue[i - 1];
    }
    __iotgo_rssi_queue[0] = val;
}

static int8 ICACHE_FLASH_ATTR queueAverage(void)
{
    int32 sum = 0;
    uint16 i;

    for (i = 0; i < IOTGO_RSSI_QUEUE_LEN; i++)
    {
        sum += __iotgo_rssi_queue[i];
    }
    return (sum / (IOTGO_RSSI_QUEUE_LEN * 1.0) + 0.5);
}

static inline void ICACHE_FLASH_ATTR printStationReason(uint8_t jap_status)
{
    switch (jap_status)
    {
        case STATION_GOT_IP:
        {
            iotgoInfo("STATION_GOT_IP");
            break;
        }
        case STATION_CONNECTING:
        {
            iotgoInfo("STATION_CONNECTING");
            break;
        }
        case STATION_WRONG_PASSWORD:
        {
            iotgoInfo("STATION_WRONG_PASSWORD");
            break;
        }
        case STATION_NO_AP_FOUND:
        {
            iotgoInfo("STATION_NO_AP_FOUND");
            break;
        }
        case STATION_CONNECT_FAIL:
        {
            iotgoInfo("STATION_CONNECT_FAIL");
            break;
        }
        case STATION_IDLE:
        {
            iotgoInfo("STATION_IDLE");
            break;
        }
        default:
        {
            break;
        }
    }
}

static void ICACHE_FLASH_ATTR joinAPStage2(void *pdata)
{
    static struct ip_info temp_ip;

    uint8 jap_state; 

    counter++;
    if (counter > 4)
    {
        iotgoInfo("try times = %u\r\n", counter);
    }
    
    jap_state = wifi_station_get_connect_status();
    if(STATION_GOT_IP == jap_state)
    {
        sint8 rssi = wifi_station_get_rssi();
        os_bzero(&temp_ip, sizeof(temp_ip));
        wifi_get_ip_info(STATION_IF, &temp_ip);

        iotgoInfo("STA IP:\"%d.%d.%d.%d\"\r\n", IP2STR(&temp_ip.ip));
        
        queueInit((rssi < 0) ? rssi : IOTGO_WIFI_RSSI_DEFAULT);
        iotgo_device_rssi = queueAverage();
        json_int_rssi.value = iotgo_device_rssi;
        iotgo_wifi_rssi = iotgo_device_rssi;
        if (iotgoDeviceMode() == DEVICE_MODE_SETTING)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SETTING_RESP_TO_APP, jap_state);
        }
        else
        {
            system_os_post(IOTGO_CORE, MSG_CORE_AP_JOINED, 0);
        }
        counter = 0;
        os_timer_disarm(&join_ap_timer);
        return;
    }
    else if(counter >= 10)
    {
        iotgoWifiLeave();
        printStationReason(jap_state);
        if (iotgoDeviceMode() == DEVICE_MODE_SETTING)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SETTING_RESP_TO_APP, jap_state);
        }
        else
        {
            system_os_post(IOTGO_CORE, MSG_CORE_JOIN_AP_ERR, 0);
        }
        counter = 0;        
        os_timer_disarm(&join_ap_timer);
        return;
    }
}


static void ICACHE_FLASH_ATTR joinAPTimerCallback(void *arg)
{
    iotgoWifiLeave();

    if (wifi_station_set_config(&iotgo_flash_param.sta_config))
    {
        iotgoInfo("sta config ok\n");
    }
    else
    {
        iotgoWarn("sta config err!\n");
    }
    
    if (wifi_station_connect())
    {
        iotgoInfo("wifi_station_connect ok\n");
        os_timer_disarm(&join_ap_timer);
        os_timer_setfn(&join_ap_timer, (os_timer_func_t *)joinAPStage2, NULL);
        os_timer_arm(&join_ap_timer, 2000, 1);
    }
    else
    {
        iotgoWarn("wifi_station_connect err!\n");
    }
}

static void ICACHE_FLASH_ATTR wifiListenRSSITmrCb(void *arg)
{
    static uint32 cnt = 0;
    
    sint8 rssi;
    cnt++;
    
    rssi = wifi_station_get_rssi(); /* dBm */
    if (rssi < 0)
    {
        queueShift(rssi);
        if (0 == (cnt % 10))
        {
            int8 rssi_av = queueAverage();
            int32 diff_rssi = iotgo_device_rssi - rssi_av;
            diff_rssi = (diff_rssi < 0) ? (0 - diff_rssi) : diff_rssi;
            if (diff_rssi >= 3)
            {
                iotgoDebug("rssi changed (last = %d, now = %d)", iotgo_device_rssi, rssi_av);
                iotgo_device_rssi = rssi_av;
                if (iotgo_device_rssi != 0 && DEVICE_MODE_WORK_NORMAL == iotgoDeviceMode())
                {        
                    json_int_rssi.value = iotgo_device_rssi;
                    iotgo_wifi_rssi = iotgo_device_rssi;
                    system_os_post(IOTGO_DEVICE_CENTER, SIG_DEVICE_UPDATE, 0);
                }
            }
            else
            {
                iotgoDebug("rssi stable (last = %d, now = %d)", iotgo_device_rssi, rssi_av);
            }
        }
    }
    else
    {
        iotgoWarn("rssi err:%d", rssi);
        iotgo_device_rssi = 0; /* 0:no rssi */
    }
}

void ICACHE_FLASH_ATTR iotgoWifiLeave(void)
{
    counter = 0;
    os_timer_disarm(&join_ap_timer);
    if (wifi_station_disconnect())
    {
        iotgoInfo("leave ap ok\n");
    }
    else
    {
        iotgoWarn("leave ap err!\n");
    }
}

void ICACHE_FLASH_ATTR iotgoWifiJoin(void)
{
    os_timer_disarm(&join_ap_timer);
    os_timer_setfn(&join_ap_timer, (os_timer_func_t *)joinAPTimerCallback, NULL);
    os_timer_arm(&join_ap_timer, 1000, 0);

    os_timer_disarm(&listen_rssi_timer);
    os_timer_setfn(&listen_rssi_timer, (os_timer_func_t *)wifiListenRSSITmrCb, NULL);
    os_timer_arm(&listen_rssi_timer, 6000, 1);
}

static void ICACHE_FLASH_ATTR iotgoWiFiEventHander(System_Event_t *evt)
{
    iotgoInfo("WiFi Event <<<<<<<<<<<<<<<");
    iotgoInfo("event %x", evt->event);
    switch (evt->event) 
    {
        case EVENT_STAMODE_CONNECTED:
            iotgoInfo("connect to ssid %s, channel %d",
                evt->event_info.connected.ssid,
                evt->event_info.connected.channel);
        break;
        case EVENT_STAMODE_DISCONNECTED:
            iotgoInfo("disconnect from ssid %s, reason %d",
                evt->event_info.disconnected.ssid,
                evt->event_info.disconnected.reason);
                
            int32 mode = iotgoDeviceMode();
            if (DEVICE_MODE_WORK_AP_ERR == mode
                || DEVICE_MODE_WORK_AP_OK == mode
                || DEVICE_MODE_WORK_INIT == mode
                || DEVICE_MODE_WORK_NORMAL == mode
                )
            {
                uint8 sta_state = wifi_station_get_connect_status();
                if (STATION_GOT_IP != sta_state
                    && STATION_CONNECTING != sta_state)
                {
                    system_os_post(IOTGO_CORE, MSG_CORE_JOIN_AP, 0);
                }
            }
        break;
        case EVENT_STAMODE_AUTHMODE_CHANGE:
            iotgoInfo("mode: %d -> %d",
                evt->event_info.auth_change.old_mode,
                evt->event_info.auth_change.new_mode);
        break;
        case EVENT_STAMODE_GOT_IP:
            iotgoInfo("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
                IP2STR(&evt->event_info.got_ip.ip),
                IP2STR(&evt->event_info.got_ip.mask),
                IP2STR(&evt->event_info.got_ip.gw));
        break;
        case EVENT_SOFTAPMODE_STACONNECTED:
            iotgoInfo("station: " MACSTR "join, AID = %d",
                MAC2STR(evt->event_info.sta_connected.mac),
                evt->event_info.sta_connected.aid);
        break;
        case EVENT_SOFTAPMODE_STADISCONNECTED:
            iotgoInfo("station: " MACSTR "leave, AID = %d",
                MAC2STR(evt->event_info.sta_disconnected.mac),
                evt->event_info.sta_disconnected.aid);    
        break;
        
        case EVENT_SOFTAPMODE_PROBEREQRECVED:
            //iotgoInfo("EVENT_SOFTAPMODE_PROBEREQRECVED");
        break;
        default:
        break;
    }
    iotgoInfo("WiFi Event >>>>>>>>>>>>>>>");
}

void ICACHE_FLASH_ATTR iotgoWifiToStationAndApMode(void)
{
    if (STATIONAP_MODE != wifi_get_opmode())
    {
        if (wifi_set_opmode(STATIONAP_MODE))
        {
            iotgoInfo("Set to STATIONAP_MODE ok\n");
        }
        else
        {
            iotgoError("Set to STATIONAP_MODE err!\n");
        }
    }
    else
    {
        iotgoInfo("Already in STATIONAP_MODE!\r\n");
    }
}

void ICACHE_FLASH_ATTR iotgoWifiToStationMode(void)
{
    if (STATION_MODE != wifi_get_opmode())
    {
        if (wifi_set_opmode(STATION_MODE))
        {
            iotgoInfo("Set to STATION_MODE ok\n");
        }
        else
        {
            iotgoError("Set to STATION_MODE err!\n");
        }
    }
    else
    {
        iotgoInfo("Already in STATION_MODE!\r\n");
    }
}


void ICACHE_FLASH_ATTR iotgoWifiInit(void)
{
    iotgoWifiToStationAndApMode();
    
    wifi_set_event_handler_cb(iotgoWiFiEventHander);
    
    wifi_station_set_auto_connect(0); /* Do not connect AP saved */
    if (wifi_station_set_reconnect_policy(false))
    {
        iotgoInfo("wifi station set reconnect disabled");
    }
    else
    {
        iotgoError("wifi station set reconnect disable failed!");
    }
    
    if (wifi_softap_dhcps_stop())
    {
        iotgoInfo("wifi_softap_dhcps_stop ok");
    }
    else
    {
        iotgoError("wifi_softap_dhcps_stop err");
    }
  
    if (wifi_set_ip_info(SOFTAP_IF, &iotgo_flash_param.sap_ip_info))
    {
        iotgoInfo("set sap ip ok\r\n");
    }
    else
    {
        iotgoError("set sap ip err!\r\n");
    }

    if (wifi_softap_dhcps_start())
    {
        iotgoInfo("wifi_softap_dhcps_start ok");
    }
    else
    {
        iotgoError("wifi_softap_dhcps_start err");
    }
    
    if (wifi_softap_set_config(&iotgo_flash_param.sap_config))
    {
        iotgoInfo("sap config ok\r\n");
    }
    else
    {
        iotgoError("sap config err!\r\n");
    }
    
}

