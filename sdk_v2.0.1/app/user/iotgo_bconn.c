/*
 * TODO:
 *   1. Request server information.
 *      1. build tcp connection to distributor server. 
 *      2. send HTTP request for server information. 
 *      3. extract ip and port and store into local variable. 
 *      4. send post to network center with result(success or failure).
 * 
 *   2. Provide API for users who need the server information. 
 * 
 */

#include "iotgo_bconn.h"


static sint8 distorSendHTTPRequest(void);

static IoTgoHostInfo server_info = {0, 0};

static struct espconn distor_conn;
static esp_tcp distor_conn_tcp;
static os_timer_t distor_conn_timer;
static os_timer_t distor_timeout_timer;
static bool post_result_flag = false;
static Spro *spro = NULL;
static uint32_t distor_last_ip = 0;
static uint8_t distor_error_cnt = 0;

static void distorPostResultToNetworkCenter(int32 error_code)
{
    if (!post_result_flag)
    {
        os_timer_disarm(&distor_timeout_timer);
        os_timer_disarm(&distor_conn_timer);
        post_result_flag = true;
        system_os_post(IOTGO_CORE, MSG_CORE_DISTRIBUTOR_FINISHED, error_code);    

        if (IOTGO_DISTRIBUTOR_RESULT_OK == error_code)    
        {
            distor_error_cnt = 0;
            if (0 != distor_last_ip && iotgo_flash_param.iot_distributor_last_ip != distor_last_ip)
            {
                iotgo_flash_param.iot_distributor_last_ip = distor_last_ip;
                iotgoFlashSaveParam(&iotgo_flash_param);
                iotgoInfo("Saved iot_distributor_last_ip = [0x%08X] to flash!", 
                    iotgo_flash_param.iot_distributor_last_ip);
            }
        }
        else
        {
            distor_error_cnt++;
        }
    }
    else
    {
        iotgoInfo("useless calling and ignored!");
    }
}

static void ICACHE_FLASH_ATTR cbDistorConnTimer(void *arg)
{   
    sint8 ret;
    ret = distorSendHTTPRequest();
    if (0 == ret)
    {
        iotgoInfo("distor req send ok");
    }
    else
    {
        distorPostResultToNetworkCenter(IOTGO_DISTRIBUTOR_RESULT_ERR_REQ);
    }
}

static void ICACHE_FLASH_ATTR cbDistorTimeoutTimer(void *arg)
{
    distorPostResultToNetworkCenter(IOTGO_DISTRIBUTOR_RESULT_ERR_TIMEOUT);
}

static void ICACHE_FLASH_ATTR distorSproOnePkgCb(void *arg, uint8 *pdata, uint16 len, bool flag)
{
    struct espconn *pespconn = (struct espconn *)arg;
    char *temp, *temp1, *temp2, *temp3;
    
    bool error_flag = false;
    bool reason_flag = false;
    bool long_server_ip_flag = false;
    bool long_server_port_flag = false;
    
    int32 error = -1;
    char reason[50] = {0};
    char long_server_ip[IOTGO_HOST_NAME_SIZE] = {0};
    int32 long_server_port = 0;
    
    iotgoInfo("\ndistor Recv len: %u\n", len);
    iotgoPrintf("distor data[%s]\n", pdata);

    temp = (char *)os_strstr(pdata, "HTTP/1.1 200 OK\r\n");
    temp1 = (char *)os_strstr(pdata, "{");
    temp2 = (char *)os_strstr(pdata, "}");
    temp3 = (char *)os_strstr(pdata, "\r\n\r\n");

    if (temp && temp1 && temp2 && temp3)
    {
        struct jsonparse_state *pjs;
        int32 type, type1, type2;
        
        pjs = (struct jsonparse_state *)os_zalloc(sizeof(struct jsonparse_state));
        jsonparse_setup(pjs, temp1, os_strlen(temp1));
        while ((type = jsonparse_next(pjs)) != 0)
        {
            if (JSON_TYPE_PAIR_NAME != type)
            {
                continue;
            }
            iotgoDebug("type = %d (%c), vlen = %d\n", type, type, jsonparse_get_len(pjs));
            
            if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_IP))
            {
                if (jsonIoTgoGetString(pjs, IOTGO_STRING_IP, long_server_ip, sizeof(long_server_ip)))
                {
                    long_server_ip_flag = true;
                }
                else
                {
                    break;
                }
            }
            else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_PORT))
            {
                if (jsonIoTgoGetNumber(pjs, IOTGO_STRING_PORT, &long_server_port))
                {
                    long_server_port_flag = true;
                }
                else
                {
                    break;
                }
            }
            else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_ERROR))
            {
                if (jsonIoTgoGetNumber(pjs, IOTGO_STRING_ERROR, &error))
                {
                    error_flag = true;
                }
                else
                {
                    break;
                }
            }
            else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_REASON))
            {
                if (jsonIoTgoGetString(pjs, IOTGO_STRING_REASON, reason, sizeof(reason)))
                {
                    reason_flag = true;
                }
                else
                {
                    break;
                }
            }
        }
        
        os_free(pjs);
    }

    if (error_flag 
        && 0 == error 
        && long_server_ip_flag 
        && long_server_port_flag)
    {
        distorSetServerInfo(long_server_ip, long_server_port);
        distorPostResultToNetworkCenter(IOTGO_DISTRIBUTOR_RESULT_OK);
    }
    else
    {
        iotgoWarn("error:%d, reason:%s", error, reason);
        distorPostResultToNetworkCenter(IOTGO_DISTRIBUTOR_RESULT_ERR_RESP);
    }
}

static void ICACHE_FLASH_ATTR distorConnTCPRecvCallback(void *arg, char *pdata, 
    unsigned short len)
{
    spTcpRecv(spro, arg, pdata, len);
}

static void ICACHE_FLASH_ATTR distorConnTCPSendCallback(void *arg)
{
    iotgoInfo("\ndistorConnTCPSendCallback called\n");
}

static void ICACHE_FLASH_ATTR distorConnTCPDisconCallback(void *arg)
{
    iotgoInfo("\ndistorConnTCPDisconCallback called\n");
    distorPostResultToNetworkCenter(IOTGO_DISTRIBUTOR_RESULT_ERR_CONN);
}

static void ICACHE_FLASH_ATTR distorConnTCPReconCallback(void *arg, sint8 errType)
{
    iotgoInfo("\ndistorConnTCPReconCallback called and errType = %d\n", errType);
    distorPostResultToNetworkCenter(IOTGO_DISTRIBUTOR_RESULT_ERR_CONN);
}

static void ICACHE_FLASH_ATTR distorConnTCPConnectedCallback(void *arg)
{
    iotgoInfo("distorConnTCPConnectedCallback");
    espconn_regist_recvcb(&distor_conn, distorConnTCPRecvCallback);
    espconn_regist_disconcb(&distor_conn, distorConnTCPDisconCallback);
    espconn_regist_sentcb(&distor_conn, distorConnTCPSendCallback);
    os_timer_disarm(&distor_conn_timer);
    os_timer_setfn(&distor_conn_timer, (os_timer_func_t *)cbDistorConnTimer, NULL);
    os_timer_arm(&distor_conn_timer, 200, 0);
}

static void ICACHE_FLASH_ATTR connToDistributor(struct espconn *pespconn)
{
    sint8 ret;
#ifdef IOTGO_DISTRIBUTOR_ENABLE_SSL    
    ret = espconn_secure_connect(pespconn);
#else
    ret = espconn_connect(pespconn);
#endif
    if (ret == 0)
    {
        iotgoInfo("distor tcp conn ok\n");    
    }
    else
    {
        iotgoWarn("distor tcp conn err!\n"); 
        distorPostResultToNetworkCenter(IOTGO_DISTRIBUTOR_RESULT_ERR_CONN);
    }
}

static void ICACHE_FLASH_ATTR distorDnsFoundCallback(const char *name, 
    ip_addr_t *ipaddr, void *arg)
{
    sint8 ret;
    struct espconn *pespconn = (struct espconn *) arg;
    
    if (name != NULL)
    {
        iotgoInfo("name = [%s]\n", name);
    }
    
    if (ipaddr == NULL) {
        iotgoWarn("distor dns found nothing but NULL\n");
        distorPostResultToNetworkCenter(IOTGO_DISTRIBUTOR_RESULT_ERR_DNS);
        return;
    }
    
    iotgoInfo("DNS found: %X.%X.%X.%X\n",
        *((uint8 *) &ipaddr->addr),
        *((uint8 *) &ipaddr->addr + 1),
        *((uint8 *) &ipaddr->addr + 2),
        *((uint8 *) &ipaddr->addr + 3));
        
    if (ipaddr->addr != 0)
    {
        distor_last_ip = ipaddr->addr;
        os_memcpy(pespconn->proto.tcp->remote_ip, &ipaddr->addr, 4);     
        iotgoInfo("serverip = 0x%X\n", *((uint32*)pespconn->proto.tcp->remote_ip));
        connToDistributor(pespconn);
    }
    else
    {
        iotgoWarn("dns err!\n");
        distorPostResultToNetworkCenter(IOTGO_DISTRIBUTOR_RESULT_ERR_DNS);
    }
}

static uint32 ICACHE_FLASH_ATTR distorGetDistorServerIP(void)
{
    uint32 ret = 0xFFFFFFFF;
    if (distor_error_cnt >= 3) 
    {
        distor_error_cnt = 0;
        if (0 != iotgo_flash_param.iot_distributor_last_ip)
        {
            ret = iotgo_flash_param.iot_distributor_last_ip;
            iotgoInfo("using iot_distributor_last_ip = [0x%08X] again!", iotgo_flash_param.iot_distributor_last_ip);
        }
    }
    else
    {
        ret = ipaddr_addr(iotgo_flash_param.iot_distributor.host);
    }
    return ret;
}

static void ICACHE_FLASH_ATTR launchTCPConnToDistributor(void)
{
    ip_addr_t server_ip = {0};
    uint32 ip = distorGetDistorServerIP();
    
    os_bzero(&distor_conn, sizeof(distor_conn));
    os_bzero(&distor_conn_tcp, sizeof(distor_conn_tcp));
    
    distor_conn.type = ESPCONN_TCP;
    distor_conn.state = ESPCONN_NONE;
    distor_conn.proto.tcp = &distor_conn_tcp;
    distor_conn.proto.tcp->local_port = espconn_port();    

#ifdef IOTGO_DISTRIBUTOR_ENABLE_SSL
     distor_conn.proto.tcp->remote_port = iotgo_flash_param.iot_distributor.port;     
#else
     distor_conn.proto.tcp->remote_port = IOTGO_SERVER_NONSSL_PORT;
#endif

    os_memcpy(distor_conn.proto.tcp->remote_ip, &ip, 4);
    
    espconn_regist_connectcb(&distor_conn, distorConnTCPConnectedCallback);
    //espconn_regist_recvcb(&distor_conn, distorConnTCPRecvCallback);
    espconn_regist_reconcb(&distor_conn, distorConnTCPReconCallback);
    //espconn_regist_disconcb(&distor_conn, distorConnTCPDisconCallback);
    //espconn_regist_sentcb(&distor_conn, distorConnTCPSendCallback);
    
    if(ip == 0xFFFFFFFF)
    {
        espconn_gethostbyname(&distor_conn, iotgo_flash_param.iot_distributor.host, &server_ip, distorDnsFoundCallback);
    }
    else
    {
        connToDistributor(&distor_conn);
    }
}


static sint8 distorSendHTTPRequest(void)
{
    uint8_t *data_buf = (uint8_t *)os_zalloc(512);
    uint8_t *http_buf = (uint8_t *)os_zalloc(512 + 256);
    sint8 ret;
    os_sprintf(data_buf,
        "{"
            "\"accept\":\"ws;%u\","
            "\"version\":%u,"
            "\"ts\":%u,"
            "\"deviceid\":\"%s\","
            "\"apikey\":\"%s\","
            "\"model\":\"%s\","
            "\"romVersion\":\"%s\""
        "}",
        IOTGO_IOT_PROTOCOL_VERSION,
        IOTGO_DISTRUBUTOR_PROTOCOL_VERSION,
        iotgoGenerateTimestamp(),
        iotgo_flash_param.factory_data.deviceid,
        iotgo_flash_param.factory_data.factory_apikey,
        iotgo_flash_param.factory_data.device_model,
        IOTGO_FM_VERSION
        );
    os_sprintf(http_buf, 
        "POST /dispatch/device HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "\r\n"
        "%s",
        iotgo_flash_param.iot_distributor.host,
        os_strlen(data_buf),
        data_buf
        );
    iotgoInfo("distor conn http len=%d\ndata[%s]", os_strlen(http_buf), http_buf);
    
#ifdef IOTGO_DISTRIBUTOR_ENABLE_SSL    
    ret = espconn_secure_send(&distor_conn, http_buf, os_strlen(http_buf));
#else
    ret = espconn_send(&distor_conn, http_buf, os_strlen(http_buf));
#endif

    os_free(data_buf);
    os_free(http_buf);
    return ret;
}



/*
 * Get the server information(ip, port). 
 */
IoTgoHostInfo ICACHE_FLASH_ATTR distorGetServerInfo(void)
{
    iotgoInfo("Get server_info[%s, %d]", server_info.host, server_info.port);
    return server_info;
}

/*
 * Set the server information(ip, port). 
 */
void ICACHE_FLASH_ATTR distorSetServerInfo(const char *host, int32 port)
{
    os_strcpy(server_info.host, host);
    server_info.port = port;    
    iotgoInfo("Set server_info[%s, %d]", server_info.host, server_info.port);
}

/*
 * Start request server information(ip, port) from distributor. 
 * 
 * An post will be sent to network center when request finished.
 */
void ICACHE_FLASH_ATTR startDistorRequest(void)
{    
    distor_last_ip = 0;

    os_timer_disarm(&distor_timeout_timer);
    os_timer_setfn(&distor_timeout_timer, (os_timer_func_t *)cbDistorTimeoutTimer, NULL);
    os_timer_arm(&distor_timeout_timer, 10000, 0);

    spro = spCreateObject(SPRO_PKG_TYPE_HTTP11, distorSproOnePkgCb, IOTGO_IOT_TCP_CLIENT_RECV_BUFFER_SIZE);
    if (NULL == spro)
    {
        iotgoError("spCreateObject err!");
    }

    post_result_flag = false;
    launchTCPConnToDistributor();
}

/*
 * Stop distor request. 
 */
void ICACHE_FLASH_ATTR stopDistorRequest(void)
{
#ifdef IOTGO_DISTRIBUTOR_ENABLE_SSL
    espconn_secure_disconnect(&distor_conn);
#else
    espconn_disconnect(&distor_conn);
#endif

    if (spro)
    {
        spReleaseObject(spro);
        spro = NULL;
    }
}

