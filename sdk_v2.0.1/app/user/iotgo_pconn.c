#include "iotgo_pconn.h"

JSONTREE_OBJECT(iotgo_tree_register_obj,
                JSONTREE_PAIR(IOTGO_STRING_USERAGENT, &json_device),
                JSONTREE_PAIR(IOTGO_STRING_APIKEY, &json_factory_apikey),
                JSONTREE_PAIR(IOTGO_STRING_DEVICEID, &json_deviceid),
                JSONTREE_PAIR(IOTGO_STRING_ACTION, &json_action_register),
                JSONTREE_PAIR(IOTGO_STRING_VERSION, &json_int_iot_version),
                JSONTREE_PAIR(IOTGO_STRING_ROMVERSION, &json_fw_version),
                JSONTREE_PAIR(IOTGO_STRING_MODEL, &json_model),
                JSONTREE_PAIR(IOTGO_STRING_TS, &json_int_reqts),
                );

JSONTREE_OBJECT(iotgo_tree_register,
                JSONTREE_PAIR("iotgo_tree_register", &iotgo_tree_register_obj)
                );
                
JSONTREE_OBJECT(iotgo_tree_date_obj,
                JSONTREE_PAIR(IOTGO_STRING_USERAGENT, &json_device),
                JSONTREE_PAIR(IOTGO_STRING_APIKEY, &json_owner_uuid),
                JSONTREE_PAIR(IOTGO_STRING_DEVICEID, &json_deviceid),
                JSONTREE_PAIR(IOTGO_STRING_ACTION, &json_action_date),
                );

JSONTREE_OBJECT(iotgo_tree_date,
                JSONTREE_PAIR("iotgo_tree_date", &iotgo_tree_date_obj)
                );


#define IOTGO_PROTOCOL_ERROR_NOT_FOUND   (404)
#define IOTGO_PROTOCOL_ERROR_FORBIDDEN   (403)


typedef enum
{
    IOTGO_SEND_OK           = 0,
    IOTGO_WILD_POINT        = 1,
    IOTGO_DATA_LENGTH_OVER  = 2,
    IOTGO_NETWORK_ABNORMAL  = 3,
    IOTGO_UPGRADE_LOCK      = 4,
    IOTGO_SEND_ERROR        = 5,
} IoTgoSendQueueStatu;


static Spro *network_spro = NULL;
static struct espconn client_tcp;
static esp_tcp client_tcp_proto_tcp;
static bool client_tcp_initialized_flag = false;
static char network_buffer[IOTGO_JSON_BUFFER_SIZE + 512];
static os_timer_t send_queue_timer;
static uint8 send_counter = 0;

void ICACHE_FLASH_ATTR iotgoPconnDeviceDate(void)
{
    uint8 *temp_buffer = (uint8 *)os_malloc(IOTGO_JSON_BUFFER_SIZE);
    if (temp_buffer)
    {
        os_memset(temp_buffer, 0, IOTGO_JSON_BUFFER_SIZE);
        json_ws_send((struct jsontree_value *)&iotgo_tree_date, "iotgo_tree_date", temp_buffer);
        if(!iotgoQueueAdd(temp_buffer,IOTGO_DATE,false))
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

static void ICACHE_FLASH_ATTR deviceRespDateCallback(const char *data)
{
    /* Extract date and revise local GMT time */
    int8 type;
    char gmt_date[25] = {0};
    bool flag_gmt_date = false;
    
    struct jsonparse_state *pjs = (struct jsonparse_state *)os_zalloc(sizeof(struct jsonparse_state));
    jsonparse_setup(pjs, data, os_strlen(data));
    while ((type = jsonparse_next(pjs)) != 0)
    {
        if (JSON_TYPE_PAIR_NAME != type)
        {
            continue;
        }
        if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_DATE))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_DATE, gmt_date, sizeof(gmt_date)))
            {
                flag_gmt_date = true;
            }
            else
            {
                break;
            }
        }
    }
    os_free(pjs);
    
    if (flag_gmt_date) 
    {
        IoTgoGMTTime gmt_time;
        if (parseGMTTimeFromString(gmt_date, &gmt_time))
        {
            setGMTTime(gmt_time);
            system_os_post(IOTGO_CORE, MSG_CORE_DATE_OK, 0);
        }
    }
}

void ICACHE_FLASH_ATTR iotgoPconnDeviceRegister(void)
{
    spSetPkgType(network_spro, SPRO_PKG_TYPE_WEBSOCKET13);

    uint8 *temp_buffer = (uint8 *)os_malloc(IOTGO_JSON_BUFFER_SIZE);
    if (temp_buffer)
    {
        json_int_reqts.value = iotgoGenerateTimestamp();
        
        os_strcpy(iotgo_device.owner_uuid, IOTGO_STRING_INVALID_OWNER_UUID);
        os_memset(temp_buffer, 0, IOTGO_JSON_BUFFER_SIZE);
        json_ws_send((struct jsontree_value *)&iotgo_tree_register, "iotgo_tree_register", temp_buffer);
        if(!iotgoQueueAdd(temp_buffer,IOTGO_REGISTER,false))
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


static void doNotifyConfig(int32 hb, int32 hbi)
{
    if (1 == hb)
    {
        iotgoCoreHeartbeatSetMaxOffset(hbi);
        iotgoCoreHeartbeatStart();
    }
    else
    {
        iotgoCoreHeartbeatStop();
    }
}

static void ICACHE_FLASH_ATTR clientTCPSendCallback(void *arg)
{
    iotgoInfo("\nclientTCPSendCallback called\n");
}

static void ICACHE_FLASH_ATTR clientTCPDisconCallback(void *arg)
{
    iotgoInfo("\nclientTCPDisconCallback called\n");
    system_os_post(IOTGO_CORE, MSG_CORE_SERVER_DISCONNECTED, 0);
}

static void ICACHE_FLASH_ATTR clientTCPReconCallback(void *arg, sint8 errType)
{
    iotgoError("\nclientTCPReconCallback called and errType = %d\n", errType);
    system_os_post(IOTGO_CORE, MSG_CORE_RECONNECT_SERVER, 0);
}



static IoTgoPkgType ICACHE_FLASH_ATTR parseTypeOfIoTgoPkg(const char *data)
{
    IoTgoPkgType pkg_type = IOTGO_PKG_TYPE_INVALID;
    
    int cnt = 0;

    int8 type;
    int8 type1;
    int8 type2;
    
    int32 error = -1;
    char reason[50] = {0};
    char apikey[IOTGO_OWNER_UUID_SIZE] = {0};
    char deviceid[IOTGO_DEVICEID_SIZE] = {0};
    char action[10] = {0};
    char unknown_field[20] = {0};
    char gmt_date_time[IOTGO_GMT_DATE_TIME_SIZE] = {0}; /* 2015-03-02T07:23:13.073Z */
    int32 hb = 0;
    int32 hbi = 0;
    int32 regi = 0;
    char server_ip[IOTGO_HOST_NAME_SIZE] = {0};
    int32 server_port = 0;
    char cmd[20] = {0};
    
    bool field_error_flag     = false;
    bool field_reason_flag    = false;
    bool field_apikey_flag    = false;
    bool field_deviceid_flag  = false;
    bool field_action_flag    = false;
    bool field_params_flag    = false;
    bool field_date_flag      = false;

    bool field_config_flag      = false;
    bool field_hb_flag          = false;
    bool field_hbi_flag         = false;
    bool field_regi_flag        = false;
    bool field_server_ip_flag   = false;
    bool field_server_port_flag = false;
    bool field_cmd_flag         = false;

    bool field_server_sequence_flag = false;
    char server_sequence[IOTGO_SERVER_SEQUENCE_SIZE] = {0};
    
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
            
        if (!field_apikey_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_APIKEY))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_APIKEY, apikey, sizeof(apikey)))
            {
                field_apikey_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_deviceid_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_DEVICEID))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_DEVICEID, deviceid, sizeof(deviceid)))
            {
                field_deviceid_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_error_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_ERROR))
        {
            if (jsonIoTgoGetNumber(pjs, IOTGO_STRING_ERROR, &error))
            {
                field_error_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_action_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_ACTION))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_ACTION, action, sizeof(action)))
            {
                field_action_flag = true;
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
            if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_OBJECT == type2)
            {
                iotgoDebug("Found params:object\n");
                field_params_flag = true;
                
            }
            else if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_ARRAY == type2)
            {
                iotgoDebug("Found params:array\n");
                field_params_flag = true;
            }
            else if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_NUMBER == type2)
            {
                int32 number = jsonparse_get_value_as_int(pjs);
                iotgoDebug("Found params:[] or {} (number = %d)\n", number);
                field_params_flag = true;
            }
            else
            {
                iotgoWarn("Invalid params field! Ignore this package!\n");
                break;
            }
        }
        else if (!field_reason_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_REASON))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_REASON, reason, sizeof(reason)))
            {
                field_reason_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_date_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_DATE))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_DATE, gmt_date_time, sizeof(gmt_date_time)))
            {
                field_date_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_config_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_CONFIG))
        {
            field_config_flag = true;
        }
        else if (!field_hb_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_HB))
        {
            if (jsonIoTgoGetNumber(pjs, IOTGO_STRING_HB, &hb))
            {
                field_hb_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_hbi_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_HBINTERVAL))
        {
            if (jsonIoTgoGetNumber(pjs, IOTGO_STRING_HBINTERVAL, &hbi))
            {
                field_hbi_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_regi_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_REGINTERVAL))
        {
            if (jsonIoTgoGetNumber(pjs, IOTGO_STRING_REGINTERVAL, &regi))
            {
                field_regi_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_server_ip_flag &&  0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_IP))
        {   
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_IP, server_ip, sizeof(server_ip)))
            {
                field_server_ip_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_server_port_flag &&  0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_PORT))
        {
            if (jsonIoTgoGetNumber(pjs, IOTGO_STRING_PORT, &server_port))
            {
                field_server_port_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_cmd_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_CMD))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_CMD, cmd, sizeof(cmd)))
            {
                field_cmd_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_server_sequence_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_SEQUENCE))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_SEQUENCE, server_sequence, sizeof(server_sequence)))
            {
                field_server_sequence_flag = true;
                os_strcpy(server_sequence_value, server_sequence);
            }
        }
        else
        {
            /* do nothing! */
        }
    }
    
    iotgoDebug("index = %d, type = %d (%c), vlen = %d\n", ++cnt, type, type, 
        jsonparse_get_len(pjs));
    iotgoDebug("while parse done\n"); 
    
    os_free(pjs);
    
    
    
    if (field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_action_flag 
        && 0 == os_strcmp(action, IOTGO_STRING_UPDATE)
        && field_params_flag
        ) /* update request from server */
    {
        iotgoDebug("to deal with update request pkg");
        pkg_type = IOTGO_PKG_TYPE_REQ_OF_UPDATE;
    }
    else if (field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_action_flag 
        && 0 == os_strcmp(action, IOTGO_STRING_UPGRADE)
        && field_params_flag
        ) /* upgrade request from server */
    {
        pkg_type = IOTGO_PKG_TYPE_REQ_OF_UPGRADE;
    }
    else if (field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_action_flag 
        && 0 == os_strcmp(action, IOTGO_STRING_RESTART)
        ) /* restart request from server */
    {
        pkg_type = IOTGO_PKG_TYPE_RET_OF_RESTART;
    }
    else if (field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_action_flag 
        && 0 == os_strcmp(action, IOTGO_STRING_REDIRECT)
        && field_server_ip_flag
        && field_server_port_flag
        ) /* redirect request from server */
    {
        pkg_type = IOTGO_PKG_TYPE_REQ_OF_REDIRECT;
        distorSetServerInfo(server_ip, server_port);
        
    }
    else if (field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_action_flag 
        && 0 == os_strcmp(action, IOTGO_STRING_NOTIFY)
        && field_cmd_flag
        && 0 == os_strcmp(cmd, IOTGO_STRING_CONFIG)
        && field_hb_flag
        && field_hbi_flag
        ) /* notify config from server */
    {
        pkg_type = IOTGO_PKG_TYPE_REQ_OF_NOTIFY_CONFIG;
        doNotifyConfig(hb, hbi);
        
    }
    else if (field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_action_flag 
        && 0 == os_strcmp(action, IOTGO_STRING_QUERYDEV)
        && field_params_flag
        ) /* query request from server */
    {
        iotgoDebug("to deal with query request pkg");
        pkg_type = IOTGO_PKG_TYPE_REQ_OF_QUERY;
    }
    else if (field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_error_flag
        && 0 == error
        && field_params_flag
        ) /* query response */
    {
        iotgoDebug("to deal with query response pkg");
        pkg_type = IOTGO_PKG_TYPE_RESP_OF_QUERY;
    }
    else if (field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_error_flag
        && 0 == error
        && !field_params_flag
        && !field_date_flag
        ) /* update response */
    {
        iotgoDebug("to deal with update response pkg");
        pkg_type = IOTGO_PKG_TYPE_RESP_OF_UPDATE;
    }
    else if (field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_error_flag
        && 0 == error
        && field_date_flag
        ) /* date response */
    {
        iotgoDebug("to deal with date response pkg");
        pkg_type = IOTGO_PKG_TYPE_RESP_OF_DATE;
    }
    else if (field_apikey_flag 
        && os_strlen(apikey) == os_strlen(iotgo_device.factory_apikey)
        && 0 != os_strcmp(apikey, iotgo_device.factory_apikey)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        && field_error_flag
        && 0 == error
        && !field_params_flag
        ) /* init response */
    {
        iotgoDebug("to deal with init response pkg");
        pkg_type = IOTGO_PKG_TYPE_RESP_OF_INIT;
        
        os_strcpy(iotgo_device.owner_uuid, apikey);
        iotgoInfo("Device init ok and UUID = %s\n", iotgo_device.owner_uuid);

        /* deal with config */
        if (field_config_flag && field_hb_flag && field_hbi_flag)
        {
            doNotifyConfig(hb, hbi);
            iotgoInfo("config done");
        }
        else
        {
            iotgoInfo("without config");
        }
        
    }
    else if (field_error_flag
        && IOTGO_PROTOCOL_ERROR_NOT_FOUND == error
        && field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.factory_apikey)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        ) /* error IOTGO_PROTOCOL_ERROR_NOT_FOUND */
    {
        iotgoWarn("Response Package ERROR: error = %d, reason = %s, "
            "apikey = %s, deviceid = %s\n", 
            error, reason, apikey, deviceid);
        pkg_type = IOTGO_PKG_TYPE_RESP_OF_INIT_FAILED;
        
        if (field_regi_flag)
        {
            iotgoCoreSetRegInterval((uint32)regi);
            iotgoInfo("regi = %d", regi);
        }
        else
        {
            iotgoInfo("without regi");
        }

    }
    else if (field_error_flag
        && IOTGO_PROTOCOL_ERROR_FORBIDDEN == error
        && field_apikey_flag 
        && 0 == os_strcmp(apikey, iotgo_device.owner_uuid)
        && field_deviceid_flag
        && 0 == os_strcmp(deviceid, iotgo_device.deviceid)
        ) /* error IOTGO_PROTOCOL_ERROR_FORBIDDEN */
    {
        iotgoWarn("Response Package ERROR: error = %d, reason = %s, "
            "apikey = %s, deviceid = %s\n", 
            error, reason, apikey, deviceid);
        pkg_type = IOTGO_PKG_TYPE_ERROR_OF_FORBIDDEN;
    }
    else /* Cannot handle it! */
    {
        iotgoWarn("Unknown content of pkg and ignored!\n");
        pkg_type = IOTGO_PKG_TYPE_INVALID;
    }

    return pkg_type;
}


static void ICACHE_FLASH_ATTR clientTCPSproOnePkgCb(void *arg, uint8 *pdata, uint16 len, bool flag)
{
    bool pkg_len_ok = false;
    uint16 data_len = 0;
    char *data = NULL;
    
    iotgoInfo("Recv len: %u", len);

    if (NULL != (char *)os_strstr(pdata, "HTTP/1.1 101 Switching Protocols"))
    {
        system_os_post(IOTGO_CORE, MSG_CORE_WS_CONNECTED, 0);
        return;
    }
    
    /* Certify the integrality of ws package */
    if (pdata[0] == 0x81)
    {
        if (pdata[1] >= 0 && pdata[1] <= 125)
        {
            data_len = pdata[1];
            if ((data_len + 2) == len)
            { 
                pkg_len_ok = true;
                data = &pdata[2];
                iotgoInfo("Len=%u, Payload=[%s]\n", data_len, data);    
            }
            else
            {
                iotgoWarn("received ws package length err 1 and ignored!\n");
                return;
            }
        }
        else if (pdata[1] == 126)
        {
            data_len = pdata[2]*256 + pdata[3];
            if ((data_len + 4) == len)
            {
                pkg_len_ok = true;
                data = &pdata[4];
                iotgoInfo("Len=%u, Payload=[%s]\n", data_len, data);
            }
            else
            {
                iotgoWarn("received ws package length err 2 and ignored!\n");
                return;
            }
        }
        else
        {
            iotgoWarn("received ws package length err 3 and ignored!\n");
            return;
        }
    }
    else if (pdata[0] == 0x8A && pdata[1] == 0x00)
    {
        iotgoDebug("Received Pong from Server\n");
        system_os_post(IOTGO_CORE, MSG_CORE_WS_PING_OK, 0);
        return;
    }
    else
    {
        iotgoWarn("Unknown type of package and ignored!\n");
        return;
    }
    
    if (!pkg_len_ok)
    {
        return;
    }

    
    iotgoCoreRefreshLastPkgTime();
    
    switch (parseTypeOfIoTgoPkg(data))
    {
        case IOTGO_PKG_TYPE_RESP_OF_UPDATE:
        {
            iotgoSetDeviceMode(DEVICE_MODE_WORK_NORMAL);
            if (iotgo_device.respOfUpdateCallback)
            {
                iotgoDebug("calling respOfUpdateCallback");
                iotgo_device.respOfUpdateCallback(data);
            }
        } break;
        case IOTGO_PKG_TYPE_RESP_OF_QUERY:
        {
            iotgoSetDeviceMode(DEVICE_MODE_WORK_NORMAL);
            if (iotgo_device.respOfQueryCallback)
            {
                iotgoDebug("calling respOfQueryCallback");
                iotgo_device.respOfQueryCallback(data);
            }
        } break;
        case IOTGO_PKG_TYPE_RESP_OF_DATE:
        {
            iotgoSetDeviceMode(DEVICE_MODE_WORK_NORMAL);
            deviceRespDateCallback(data);
            
        } break;
        case IOTGO_PKG_TYPE_REQ_OF_UPDATE:
        {
            iotgoSetDeviceMode(DEVICE_MODE_WORK_NORMAL);
            if (iotgo_device.reqOfUpdateCallback)
            {
                iotgoDebug("calling reqOfUpdateCallback");
                iotgo_device.reqOfUpdateCallback(data);
            }
        } break;
        case IOTGO_PKG_TYPE_REQ_OF_REDIRECT:
        {
            iotgoSetDeviceMode(DEVICE_MODE_WORK_NORMAL);
            system_os_post(IOTGO_CORE, MSG_CORE_REDIRECT, 0);
            
        } break;
        case IOTGO_PKG_TYPE_REQ_OF_NOTIFY_CONFIG:
        {
            iotgoSetDeviceMode(DEVICE_MODE_WORK_NORMAL);
            if(iotgo_device.reqofNotityCallback)
            {
                /*判断接收的数据中是否有devConfig字段*/
                iotgo_device.reqofNotityCallback(data);
            }
            else
            {
                system_os_post(IOTGO_CORE, MSG_CORE_NOTIFY_CONFIG, 0);
            }
        } break;
        case IOTGO_PKG_TYPE_REQ_OF_QUERY:
        {
            iotgoSetDeviceMode(DEVICE_MODE_WORK_NORMAL);
            if (iotgo_device.reqOfQueryCallback)
            {
                iotgoDebug("calling reqOfQueryCallback");
                iotgo_device.reqOfQueryCallback(data);
            }
        } break;
        case IOTGO_PKG_TYPE_RESP_OF_INIT:
        {
            if(iotgo_device.reqofRegisterCallback)
            {
                /*判断接收的数据中是否有devConfig字段*/
                iotgo_device.reqofRegisterCallback(data);
            }
            system_os_post(IOTGO_CORE, MSG_CORE_REGISTERED, 0);
            
        } break;
        case IOTGO_PKG_TYPE_REQ_OF_UPGRADE:
        {
            iotgoInfo("IOTGO_PKG_TYPE_REQ_OF_UPGRADE detected");
            iotgoUpgradeProcessRequest(data);
            
        } break;
        case IOTGO_PKG_TYPE_RET_OF_RESTART:
        {
            iotgoInfo("IOTGO_PKG_TYPE_RET_OF_RESTART detected");
            iotgoUpgradeRestartForNewBin();
            
        } break;
        case IOTGO_PKG_TYPE_RESP_OF_INIT_FAILED:
        {
            iotgoWarn("IOTGO_PKG_TYPE_RESP_OF_INIT_FAILED");
            system_os_post(IOTGO_CORE, MSG_CORE_REGISTER_ERR, 0);                        
        } break;
        case IOTGO_PKG_TYPE_ERROR_OF_FORBIDDEN:
        {
            iotgoWarn("IOTGO_PKG_TYPE_ERROR_OF_FORBIDDEN");
            /* Do nothing for now! */
            
        } break;
        case IOTGO_PKG_TYPE_INVALID:
        {
            iotgoWarn("Invalid iotgo pkg type and ignored!");
        } break;
        default:
        {
            iotgoError("Bad iotgo pkg type and ignored!");
        } break;
    }    
}

static void ICACHE_FLASH_ATTR clientTCPRecvCallback(void *arg, char *pdata, 
    unsigned short len)
{
    spTcpRecv(network_spro, arg, pdata, len);
}

static void ICACHE_FLASH_ATTR clientTCPConnectedCallback(void *arg)
{
    static uint32 counter = 0;
    
    espconn_regist_recvcb(&client_tcp, clientTCPRecvCallback);
    espconn_regist_disconcb(&client_tcp, clientTCPDisconCallback);
    espconn_regist_sentcb(&client_tcp, clientTCPSendCallback);

    iotgoDebug("clientTCPConnectedCallback counter = %u\n", ++counter);
    
    system_os_post(IOTGO_CORE, MSG_CORE_SERVER_CONNECTED, 0);
}

static void ICACHE_FLASH_ATTR connTCP(struct espconn *client_tcp)
{
    sint8 ret;

#ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL    
    ret = espconn_secure_connect(client_tcp);
#else
    ret = espconn_connect(client_tcp);
#endif /* #ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL  */

    if (ret == 0)
    {
        iotgoInfo("tcp conn ok\n");    
        iotgoPrintHeapSize();
        //system_os_post(IOTGO_CORE, MSG_CORE_SERVER_CONNECTED, 0);
    }
    else
    {
        iotgoWarn("tcp conn err!\n"); 
        system_os_post(IOTGO_CORE, MSG_CORE_CONNECT_SERVER_ERR, 0);
    }
    client_tcp_initialized_flag = true;
}


static void ICACHE_FLASH_ATTR dnsFoundCallback(const char *name, 
    ip_addr_t *ipaddr, void *arg)
{
    sint8 ret;
    struct espconn *pespconn = (struct espconn *) arg;
    
    if (name != NULL)
    {
        iotgoInfo("name = [%s]\n", name);
    }
    
    if (ipaddr == NULL) {
        iotgoWarn("dns found nothing but NULL\n");
        system_os_post(IOTGO_CORE, MSG_CORE_CONNECT_SERVER_ERR, 0);
        return;
    }
    
    iotgoInfo("DNS found: %X.%X.%X.%X\n",
        *((uint8 *) &ipaddr->addr),
        *((uint8 *) &ipaddr->addr + 1),
        *((uint8 *) &ipaddr->addr + 2),
        *((uint8 *) &ipaddr->addr + 3));
        
    if (ipaddr->addr != 0)
    {
        os_memcpy(pespconn->proto.tcp->remote_ip, &ipaddr->addr, 4);     
        iotgoInfo("serverip = 0x%X\n", *((uint32*)pespconn->proto.tcp->remote_ip));
        connTCP(pespconn);
    }
    else
    {
        iotgoWarn("dns err!\n");
    }
}

void ICACHE_FLASH_ATTR iotgoPconnConnect(IoTgoHostInfo host_info)
{
    sint8 ret;
    
    ip_addr_t server_ip = {0};
    char *ip_str = host_info.host; 
    uint32 ip = ipaddr_addr(ip_str);
    
    os_bzero(&client_tcp, sizeof(client_tcp));
    os_bzero(&client_tcp_proto_tcp, sizeof(client_tcp_proto_tcp));
    client_tcp.type = ESPCONN_TCP;
    client_tcp.state = ESPCONN_NONE;
    client_tcp.proto.tcp = &client_tcp_proto_tcp;
    client_tcp.proto.tcp->local_port = espconn_port();    
    client_tcp.proto.tcp->remote_port = host_info.port;
    
    os_memcpy(client_tcp.proto.tcp->remote_ip, &ip, 4);
    
    espconn_regist_connectcb(&client_tcp, clientTCPConnectedCallback);
    espconn_regist_reconcb(&client_tcp, clientTCPReconCallback);
    
    if(ip == 0xFFFFFFFF)
    {
        sint8 ret;
        ret = espconn_gethostbyname(&client_tcp, ip_str, &server_ip, dnsFoundCallback);
    }
    else
    {
        connTCP(&client_tcp);
    }
}

void ICACHE_FLASH_ATTR iotgoPconnDisconnect(void)
{
    if (!client_tcp_initialized_flag)
    {
        return;
    }
    
    iotgoCoreHeartbeatStop();
#ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL    
    espconn_secure_disconnect(&client_tcp);
#else
    espconn_disconnect(&client_tcp);
#endif /* #ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL  */
    
}


void ICACHE_FLASH_ATTR iotgoPconnSwitchToWebSocket(void)
{
    os_timer_t *timer;
    sint8 ret;
    char http_buffer[256] = {0};
    
    spSetPkgType(network_spro, SPRO_PKG_TYPE_HTTP11);
    

    http_buffer[0] = '\0';
    os_strcpy(http_buffer, "GET /api/ws HTTP/1.1\r\n");
    os_strcat(http_buffer, "Host: iotgo.iteadstudio.com\r\n");
    os_strcat(http_buffer, "Connection: upgrade\r\n");
    os_strcat(http_buffer, "Upgrade: websocket\r\n");
    os_strcat(http_buffer, "Sec-WebSocket-Key: ITEADTmobiM0x1DabcEsnw==\r\n");
    os_strcat(http_buffer, "Sec-WebSocket-Version: 13\r\n");
    os_strcat(http_buffer, "\r\n");
    
    iotgoInfo("http_buffer = [\n%s\n]\n", http_buffer);

#ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL    
    ret = espconn_secure_send(&client_tcp, http_buffer, os_strlen(http_buffer));
#else
    ret = espconn_send(&client_tcp, http_buffer, os_strlen(http_buffer));
#endif /* #ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL  */
    if (ret == 0)
    {
        iotgoInfo("send wshandshake ok\n");
    }
    else
    {
        iotgoWarn("send wshandshake err!\n");
    }
    
}

static int8 ICACHE_FLASH_ATTR iotgoNetworkSendString(const char *data_to_send)
{
    sint8 ret;
    uint16 len;
    uint32 data_len;
    uint16 header_index = 0;
    os_memset(network_buffer,0,sizeof(network_buffer));
    if(data_to_send[0] == 0x89 && data_to_send[1] == 0x80 && data_to_send[2] == 0x00
        && data_to_send[3] == 0x00 && data_to_send[4] == 0x00 && data_to_send[5] == 0x00)
    {
        os_memcpy(network_buffer, data_to_send,6);
        len = 6;
    }
    else
    {
        
        data_len = os_strlen(data_to_send);
        iotgoInfo("data_len = %u\n", data_len);
        iotgoInfo("data to send = [\n%s\n]\n", data_to_send);
        
        header_index = 0;
        network_buffer[header_index++] = 0x81;  /* Final package and text data type */
        if (data_len >= 0 && data_len <= 125)
        {
            network_buffer[header_index++] = 0x80 + data_len; /* mask set 1 and payload length */    
        }
        else if (data_len >= 126 && data_len <= 65500)
        {
            network_buffer[header_index++] = 0x80 + 126;
            network_buffer[header_index++] = (data_len >> 8) & 0xFF;
            network_buffer[header_index++] = (data_len) & 0xFF;
        }
        else
        {
            iotgoError("The length of data is too large! Discard it!\n");
            return IOTGO_DATA_LENGTH_OVER;
        }
        
        network_buffer[header_index++] = 0x00;  /* masking key = 0 */
        network_buffer[header_index++] = 0x00;  /* masking key = 0 */  
        network_buffer[header_index++] = 0x00;  /* masking key = 0 */
        network_buffer[header_index++] = 0x00;  /* masking key = 0 */
        
        os_memcpy(&network_buffer[header_index], data_to_send, data_len);
        
        len = data_len + header_index;
        if(len > (IOTGO_JSON_BUFFER_SIZE + 512))
        {
            return IOTGO_DATA_LENGTH_OVER;
        }
    }
#ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL    
    ret = espconn_secure_send(&client_tcp, network_buffer, len);
#else
    ret = espconn_send(&client_tcp, network_buffer, len);
#endif /* #ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL  */
    
    if (ret == 0)
    {
        iotgoInfo("send ok!\n");
    }
    else
    {
        iotgoWarn("send err! and ret = %d\n", ret);
    }
    return ret;

}


void ICACHE_FLASH_ATTR iotgoPconnSendJson(void)
{
    IoTgoDeviceMode mode = iotgoDeviceMode();
    if(NULL == iotgoQueueHeadData())
    {
        system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON_FINISHED, IOTGO_WILD_POINT);
    }
    else if(DEVICE_MODE_WORK_NORMAL != mode && DEVICE_MODE_WORK_INIT != mode)
    {
        system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON_FINISHED, IOTGO_NETWORK_ABNORMAL);
    }
    else
    {
        int8 status = iotgoNetworkSendString(iotgoQueueHeadData());
        if(IOTGO_SEND_OK == status)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON_FINISHED, IOTGO_SEND_OK);
        }
        else if(IOTGO_UPGRADE_LOCK == status)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON_FINISHED, IOTGO_UPGRADE_LOCK);
        }
        else if(IOTGO_DATA_LENGTH_OVER == status)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON_FINISHED, IOTGO_DATA_LENGTH_OVER);
        }
        else
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON_AGAIN, IOTGO_SEND_ERROR);
        }
    }
}

void ICACHE_FLASH_ATTR iotgoPconnSendJsonAgain(void)
{
    if(send_counter >= 3)
    {
        iotgoInfo("send error.................");
        os_timer_disarm(&send_queue_timer);
        if(IOTGO_REGISTER == iotgoQueueHeadType())
        {
            iotgoQueueDeleteAll();
            system_os_post(IOTGO_CORE, MSG_CORE_REGISTER_ERR, 0);
        }
        else
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON_FINISHED, IOTGO_SEND_ERROR);
        }
        send_counter = 0;
    }
    else
    {
        if(send_counter == 0)
        {
            iotgoInfo("start 1s timer to send queue");
            os_timer_arm(&send_queue_timer,1000, 0);
        }
        else if(send_counter == 1)
        {
            iotgoInfo("start 2s timer to send queue");
            os_timer_arm(&send_queue_timer,2000, 0);
        }
        else if(send_counter == 2)
        {
            iotgoInfo("start 4s timer to send queue");
            os_timer_arm(&send_queue_timer,4000, 0);
        }
        send_counter++;
    }
      
}

void ICACHE_FLASH_ATTR iotgoPconnSendNextJson(uint32 error_code)
{
    uint8_t type = iotgoQueueHeadType();
    iotgoQueueDeleteHead();  
    send_counter = 0; 
    if(error_code == IOTGO_WILD_POINT)
    {
        iotgoInfo("queue is empty");
    }
    else if(error_code == IOTGO_DATA_LENGTH_OVER)
    {
        iotgoInfo("queue data is length over");
        if(iotgoQueueLength()> 0)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON, 0);
        }
    }
    else if(error_code == IOTGO_SEND_ERROR)
    {
        if(iotgoQueueLength() > 0)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON, 0);
        }
    }
    else if(error_code == IOTGO_NETWORK_ABNORMAL)
    {
        iotgoInfo("network is abnormal");
    }
    else if(error_code == IOTGO_UPGRADE_LOCK)
    {
        iotgoInfo("upgrade now");
    }
    else if(error_code == IOTGO_SEND_OK)
    {
        if(iotgoQueueLength() > 0)
        {
            system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON, 0);
        }
    }
    else
    {
        iotgoError("the pkg is error,please check");
    }
    if(error_code != IOTGO_SEND_OK && type >= IOTGO_USER_DEFAULT)
    {
        iotgoInfo("send fail to MCU");
        uart0_tx_string("AT+SEND=fail");    
        uart_tx_one_char(27);
    }
    else if(error_code == IOTGO_SEND_OK && type >= IOTGO_USER_DEFAULT)
    {
        iotgoInfo("send ok to MCU");
        uart0_tx_string("AT+SEND=ok"); 
        uart_tx_one_char(27);
    }
}

static void ICACHE_FLASH_ATTR cbSendQueueTimer(void *arg)
{
    iotgoInfo("in send queue timer.................................");
    if(iotgoQueueLength() > 0)
    {
        system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON, 0);
    }
}

static void initSendQueueTime(void)
{
    os_timer_disarm(&send_queue_timer);
    os_timer_setfn(&send_queue_timer, (os_timer_func_t *)cbSendQueueTimer, NULL);
}

/*
 * 使用cJSON接口返回错误码给长连接服务器
 * 注意: 该函数使用动态内存，不依赖任何缓冲区。
 */
void ICACHE_FLASH_ATTR iotgoPconnRespondErrorCode(int32_t err_code, const char *seq)
{
    cJSON *root = NULL;
    char *json = NULL;
    root=cJSON_CreateObject();
    if(!root)
    {
        iotgoError("create cjson object is error");
        return;
    }
    cJSON_AddStringToObject(root,IOTGO_STRING_USERAGENT, IOTGO_STRING_DEVICE);
    cJSON_AddStringToObject(root,IOTGO_STRING_APIKEY, iotgo_device.owner_uuid);
    cJSON_AddStringToObject(root,IOTGO_STRING_DEVICEID, iotgo_device.deviceid);
    cJSON_AddNumberToObject(root,IOTGO_STRING_ERROR, err_code);
    cJSON_AddStringToObject(root,IOTGO_STRING_SEQUENCE, seq);
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    iotgoQueueAdd(json,IOTGO_ERROR0,false);
    os_free(json);
}

#if 1 //by czp

static char net_buffer[IOTGO_JSON_BUFFER_SIZE + 512];

int8 ICACHE_FLASH_ATTR sendIoTgoPkg(const char *data_to_send)
{
    sint8 ret;
    uint16 len;
    uint32 data_len;
    uint16 header_index = 0;
    os_memset(net_buffer,0,sizeof(net_buffer));
    if(data_to_send[0] == 0x89 && data_to_send[1] == 0x80 && data_to_send[2] == 0x00
        && data_to_send[3] == 0x00 && data_to_send[4] == 0x00 && data_to_send[5] == 0x00)
    {
        os_memcpy(net_buffer, data_to_send,6);
        len = 6;
    }
    else
    {        
        data_len = os_strlen(data_to_send);
        iotgoInfo("data_len = %u\n", data_len);
        iotgoInfo("data to send = [\n%s\n]\n", data_to_send);
        
        header_index = 0;
        net_buffer[header_index++] = 0x81;  /* Final package and text data type */
        if (data_len >= 0 && data_len <= 125)
        {
            net_buffer[header_index++] = 0x80 + data_len; /* mask set 1 and payload length */    
        }
        else if (data_len >= 126 && data_len <= 65500)
        {
            net_buffer[header_index++] = 0x80 + 126;
            net_buffer[header_index++] = (data_len >> 8) & 0xFF;
            net_buffer[header_index++] = (data_len) & 0xFF;
        }
        else
        {
            iotgoError("The length of data is too large! Discard it!\n");
            return IOTGO_DATA_LENGTH_OVER;
        }
        
        net_buffer[header_index++] = 0x00;  /* masking key = 0 */
        net_buffer[header_index++] = 0x00;  /* masking key = 0 */  
        net_buffer[header_index++] = 0x00;  /* masking key = 0 */
        net_buffer[header_index++] = 0x00;  /* masking key = 0 */
        
        os_memcpy(&net_buffer[header_index], data_to_send, data_len);
        
        len = data_len + header_index;
        if(len > (IOTGO_JSON_BUFFER_SIZE + 512))
        {
            return IOTGO_DATA_LENGTH_OVER;
        }
    }
#ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL    
    ret = espconn_secure_send(&client_tcp, net_buffer, len);
#else
    ret = espconn_send(&client_tcp, net_buffer, len);
#endif /* #ifdef IOTGO_OPERATION_SERVER_ENABLE_SSL  */
    
    if (ret == 0)
    {
        iotgoInfo("send ok!\n");
    }
    else
    {
        iotgoWarn("send err! and ret = %d\n", ret);
    }
    return ret;

}

#endif//by czp



void ICACHE_FLASH_ATTR iotgoPconnInit(void)
{
    initSendQueueTime();
    /* This object's memory has no need to free for using all the time. */
    network_spro = spCreateObject(SPRO_PKG_TYPE_HTTP11, clientTCPSproOnePkgCb, IOTGO_IOT_TCP_CLIENT_RECV_BUFFER_SIZE);
    if (NULL == network_spro)
    {
        iotgoError("spCreateObject err!");
    }
}

