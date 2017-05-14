#include "iotgo_upgrade.h"

#define IOTGO_UPGRADE_BIN_MAX_LEN   (460*1024)

#define IOTGO_UPGRADE_BIN1_START_SECTOR (0x01)
#define IOTGO_UPGRADE_BIN2_START_SECTOR (0x81)

#define IOTGO_UPGRADE_PROTOCOL_INVALID  (0x00)
#define IOTGO_UPGRADE_PROTOCOL_HTTP     (0x01)
#define IOTGO_UPGRADE_PROTOCOL_HTTPS    (0x02)

#define IOTGO_UPGRADE_HTTP_DEF_PORT     (80)
#define IOTGO_UPGRADE_HTTPS_DEF_PORT    (443)

#define IOTGO_UPGRADE_HTTP_BUFFER_SIZE  (512)

#define IOTGO_UPGRADE_FLASH_FLAG_START   (0x00)
#define IOTGO_UPGRADE_FLASH_FLAG_PROCEED (0x01)
#define IOTGO_UPGRADE_FLASH_FLAG_FINISH  (0x02)

#define IOTGO_UPGRADE_ERROR_OK           (0)
#define IOTGO_UPGRADE_ERROR_BADREQ                (400)
#define IOTGO_UPGRADE_ERROR_DOWNLOAD              (404)
#define IOTGO_UPGRADE_ERROR_MODEL                 (406)
#define IOTGO_UPGRADE_ERROR_DIGEST                (409)
#define IOTGO_UPGRADE_ERROR_VERSION               (410)

#define IOTGO_UPGRADE_TIMEOUT           (180)
#define IOTGO_UPGRADE_ONEPKE_MAX_LENGTH     (4096)
static char *upgrade_http_buffer = NULL;
static IoTgoUpgradeInfo *iotgo_upgrade_info = NULL;
static uint8 *download_sector_buffer = NULL;

static struct espconn upgrade_dl_conn;
static esp_tcp upgrade_dl_conn_tcp;

static bool started = false;
//static uint32 file_length = 0;
//static uint32 file_counter = 0;
static os_timer_t upgrade_progress_timer;
static os_timer_t upgrade_enter_timer;
static os_timer_t upgrade_exit_timer;
static uint16 upgrade_timeout = 0;
static Spro *upgrade_spro = NULL;
static os_timer_t connect_server_again_timer;
static uint8_t connect_server_again_counter = 0;
static uint16 flash_sector_index = 0;
static bool upgrade_started_flag = false;

typedef struct _UPGRADEFILE
{
    int32_t file_length;
    uint32_t range_start;
    uint32_t range_end;
    int32_t downloaded_length;
    uint16_t couter;
}UPGRADEFILE;

static UPGRADEFILE upgrade_file = {0};

static bool ICACHE_FLASH_ATTR flashOneSector(uint16 sector, uint8 *src)
{
    if (SPI_FLASH_RESULT_OK == spi_flash_erase_sector(sector)
        && SPI_FLASH_RESULT_OK == spi_flash_write(sector * SPI_FLASH_SEC_SIZE, (uint32 *)src, SPI_FLASH_SEC_SIZE)
        ) {        
        //iotgoInfo("flash sector 0x%02X ok", sector);   
        return true;
    }
    iotgoError("flash sector 0x%02X err", sector);
    return false;
}


static bool ICACHE_FLASH_ATTR storeFileToFlash(uint16 len)
{
    bool ret = false;
    if(ret = flashOneSector(flash_sector_index + iotgo_upgrade_info->sector, download_sector_buffer))
    {
        flash_sector_index++;
    }    
    return ret;
}

static bool ICACHE_FLASH_ATTR modelMatch(const char *model)
{
    if (model)
    {
        if(0 == os_strcmp(iotgo_flash_param.factory_data.device_model, model))
        {
            return true;
        }
    }
    return false;
}

static bool ICACHE_FLASH_ATTR verifyFirmwareVersion(const char *version)
{
    if (!version) 
    {
        iotgoError("version is NULL!");
        return false;
    }

    if (os_strcmp(version, IOTGO_FM_VERSION) > 0)
    {
        iotgoInfo("New version:%s", version);
        return true;
    }
    else 
    {
        iotgoWarn("Invalid version:%s", version);
        return false;
    }
}


/**
 * 验证Flash中的固件的sha256是否正确。
 * 
 * @return true 表示成功，false 表示失败。
 *
 * @note 该函数功能与 verifyFlashData 一样，但是 verifyFlashData 仅仅是升级过程中才可以调用，它依赖一个缓冲区。
 *  而该函数则会动态分配内存。
 */
bool ICACHE_FLASH_ATTR iotgoUpgradeVerifyFlashData(uint32 start_addr, uint32 size, 
    const uint8 digest_hex[65])
{
    sha256_context sha256_ctx;
    uint8 digest[32];
    uint8 digest_hex2[65];
    uint16 i;
    uint8 *sector_buffer = (uint8 *)os_malloc(SPI_FLASH_SEC_SIZE);
    if (!sector_buffer)
    {
        iotgoError("sector_buffer is NULL!");
        return false;
    }
    
    sha256_starts(&sha256_ctx);
    for (i = 0; i < (size/SPI_FLASH_SEC_SIZE); i++)
    {
        if(SPI_FLASH_RESULT_OK == spi_flash_read(start_addr + (i * SPI_FLASH_SEC_SIZE), (uint32 *)sector_buffer, SPI_FLASH_SEC_SIZE))
        {
            sha256_update(&sha256_ctx, sector_buffer, SPI_FLASH_SEC_SIZE);
        }
        else
        {
            iotgoError("read sector 0x%02X err", start_addr/SPI_FLASH_SEC_SIZE + i);
        }
    }
    
    spi_flash_read(start_addr + (i * SPI_FLASH_SEC_SIZE), (uint32 *)sector_buffer, size % SPI_FLASH_SEC_SIZE);
    sha256_update(&sha256_ctx, sector_buffer, size % SPI_FLASH_SEC_SIZE);
    sha256_finish(&sha256_ctx, digest);
    
    os_free(sector_buffer);

    for (i = 0; i < 64; i += 2)
    {
        os_sprintf(&digest_hex2[i], "%02x", digest[i/2]);
    }
    digest_hex2[64] = 0;
    
    iotgoDebug("digest_flash = [%s]", digest_hex2);
    iotgoDebug("digest_origin = [%s]", digest_hex);
    
    if (0 == os_strcmp(digest_hex2, digest_hex))
    {
        return true;
    }
    else
    {
        return false;
    }
}


/**
 * 检验新版本固件完整性(根据 iotgo_flash_param.new_bin_info )。 
 * 检验项: 1. 版本号大于当前版本；2. 固件编号与当前固件对称；3. sha256校验一致。
 */
IoTgoUpgradeNewBinResult ICACHE_FLASH_ATTR iotgoUpgradeVerifyNewBin(void)
{
    uint8 current_bin = system_upgrade_userbin_check();
    IoTgoNewBinInfo info = iotgo_flash_param.new_bin_info;
    uint32 start_addr;
    
    /*
     * 验证固件编号是否对称
     */
    if (UPGRADE_FW_BIN1 == current_bin)
    {
        if (os_strncmp(info.name, IOTGO_STRING_USER2BIN, os_strlen(IOTGO_STRING_USER2BIN)))
        {
            return IOTGO_UPGRADE_NEW_BIN_RESULT_BAD_NAME;
        } 
    }
    else
    {
        if (os_strncmp(info.name, IOTGO_STRING_USER1BIN, os_strlen(IOTGO_STRING_USER1BIN)))
        {
            return IOTGO_UPGRADE_NEW_BIN_RESULT_BAD_NAME;
        } 
    }

    /*
     * 判断版本号是否大于当前版本
     */
    if (!verifyFirmwareVersion(info.version))
    {
        return IOTGO_UPGRADE_NEW_BIN_RESULT_BAD_VERSION;
    }

    /*
     * 获取对称BIN的起始地址
     */
    if (UPGRADE_FW_BIN1 == current_bin)
    {
        start_addr = IOTGO_UPGRADE_BIN2_START_SECTOR * SPI_FLASH_SEC_SIZE;
    }
    else
    {
        start_addr = IOTGO_UPGRADE_BIN1_START_SECTOR * SPI_FLASH_SEC_SIZE;
    }

    /*
     * 校验固件SHA256值
     */
    if (!iotgoUpgradeVerifyFlashData(start_addr, info.length, info.sha256))
    {
        return IOTGO_UPGRADE_NEW_BIN_RESULT_BAD_DIGEST;
    }
    
    return IOTGO_UPGRADE_NEW_BIN_RESULT_OK;
}

static bool ICACHE_FLASH_ATTR verifyFlashData(uint32 start_addr, uint32 size, const uint8 digest_hex[65])
{
    sha256_context sha256_ctx;
    uint8 digest[32];
    uint8 digest_hex2[65];
    uint16 i;
    
    sha256_starts(&sha256_ctx);
    for (i = 0; i < (size/SPI_FLASH_SEC_SIZE); i++)
    {
        if(SPI_FLASH_RESULT_OK == spi_flash_read(start_addr + (i * SPI_FLASH_SEC_SIZE), (uint32 *)download_sector_buffer, SPI_FLASH_SEC_SIZE))
        {
            sha256_update(&sha256_ctx, download_sector_buffer, SPI_FLASH_SEC_SIZE);
        }
        else
        {
            iotgoError("read sector 0x%02X err", start_addr/SPI_FLASH_SEC_SIZE + i);
        }
    }
    
    spi_flash_read(start_addr + (i * SPI_FLASH_SEC_SIZE), (uint32 *)download_sector_buffer, size % SPI_FLASH_SEC_SIZE);
    sha256_update(&sha256_ctx, download_sector_buffer, size % SPI_FLASH_SEC_SIZE);
    sha256_finish(&sha256_ctx, digest);

    for (i = 0; i < 64; i += 2)
    {
        os_sprintf(&digest_hex2[i], "%02x", digest[i/2]);
    }
    digest_hex2[64] = 0;
    
    iotgoDebug("digest_flash = [%s]", digest_hex2);
    iotgoDebug("digest_origin = [%s]", digest_hex);
    
    if (0 == os_strcmp(digest_hex2, digest_hex))
    {
        return true;
    }
    else
    {
        return false;
    }
}

void ICACHE_FLASH_ATTR iotgoUpgradeDownload(void)
{
    char ts[20] = {0};
    uint8_t range[50] = {0};
    sha256_context sha256_ctx;
    uint8 digest[32];
    uint8 digest_hex[65] = {0};
    int i;
    upgrade_file.range_start = upgrade_file.couter * 4096;
    if(upgrade_file.range_start + 4095 >= upgrade_file.file_length - 1)
    {
        upgrade_file.range_end = upgrade_file.file_length - 1;
    }
    else
    {
        upgrade_file.range_end = upgrade_file.range_start + 4095;
    }
    os_sprintf(range,"%s%d-%d%s","Range: bytes=",upgrade_file.range_start,upgrade_file.range_end,"\r\n");
    iotgoInfo("range[%s]",range);
    os_sprintf(ts, "%u", (uint32)rand());
    sha256_starts(&sha256_ctx);
    sha256_update(&sha256_ctx, iotgo_device.deviceid, os_strlen(iotgo_device.deviceid));
    sha256_update(&sha256_ctx, ts, os_strlen(ts));
    sha256_update(&sha256_ctx, iotgo_device.factory_apikey, os_strlen(iotgo_device.factory_apikey));
    sha256_finish(&sha256_ctx, digest);
        
    for (i = 0; i < 64; i += 2)
    {
        os_sprintf(&digest_hex[i], "%02x", digest[i/2]);
    }
    digest_hex[64] = 0;
    iotgoInfo("digest_hex = [%s]", digest_hex);

    os_bzero(upgrade_http_buffer, IOTGO_UPGRADE_HTTP_BUFFER_SIZE);
    os_sprintf(upgrade_http_buffer, "GET %s?deviceid=%s&ts=%s&sign=%s HTTP/1.1\r\n", 
        iotgo_upgrade_info->path, iotgo_device.deviceid, ts, digest_hex);
    os_strcat(upgrade_http_buffer, "Host: dl.itead.cn\r\n");
    os_strcat(upgrade_http_buffer,range);
    os_strcat(upgrade_http_buffer, "User-Agent: itead-device\r\n");
    os_strcat(upgrade_http_buffer, "\r\n");
    
    iotgoDebug("deviceid:[%s]", iotgo_device.deviceid);
    iotgoDebug("ts:[%s]", ts);
    iotgoDebug("factory_apikey:[%s]", iotgo_device.factory_apikey);
    iotgoInfo("http len:[%u]", os_strlen(upgrade_http_buffer));
    iotgoInfo("http req:[%s]", upgrade_http_buffer);
    
    if (ESPCONN_OK == espconn_send(&upgrade_dl_conn, upgrade_http_buffer, os_strlen(upgrade_http_buffer)))
    {
        iotgoInfo("launchDownload send ok");
    }
    else
    {
        iotgoInfo("launchDownload send err");
        system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_RECONNECT_AGAIN, 0);
    }
}

static void ICACHE_FLASH_ATTR upgradeDownloadTCPSendCallback(void *arg)
{
    iotgoInfo("\nupgradeDownloadTCPSendCallback called\n");
}

static void ICACHE_FLASH_ATTR upgradeDownloadTCPDisconCallback(void *arg)
{
    iotgoInfo("\nupgradeDownloadTCPDisconCallback called\n");
    system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_RECONNECT_AGAIN, 0);
}

static void ICACHE_FLASH_ATTR upgradeDownloadTCPReconCallback(void *arg, sint8 errType)
{
    iotgoInfo("\nupgradeDownloadTCPReconCallback called and errType = %d\n", errType);
    system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_RECONNECT_AGAIN, 0);
}

static int32_t  ICACHE_FLASH_ATTR getContentLength(uint8 *pdata,uint16 len)
{
    int32_t pkg_len = -1;
    uint16_t i;
    uint8 *t1;
    uint8 *t2;
    if(NULL == pdata)
    {
        return -1;
    }
    t1 = (uint8 *)os_strstr(pdata, "Content-Length: ");
    if (t1 != NULL && len >(t1 - pdata))
    {
        t2 = (uint8 *)os_strstr(t1, "\r\n");
        if (t2 != NULL && len > (t2 - pdata))
        {
            t1 += os_strlen("Content-Length: ");
            pkg_len = (uint32)atoi(t1);
        }
    }
    return pkg_len;
}

static void ICACHE_FLASH_ATTR storeUpgradeDataToFlash(uint16_t len)
{
    if(storeFileToFlash(len))
    {
        upgrade_file.couter++;
        upgrade_file.downloaded_length += len;
        iotgoInfo("downedlength = %u",upgrade_file.downloaded_length);
        if(upgrade_file.downloaded_length >= upgrade_file.file_length)
        {
            iotgoInfo("Download and store in flash successfully!");
            if (verifyFlashData(iotgo_upgrade_info->sector * SPI_FLASH_SEC_SIZE, upgrade_file.file_length, iotgo_upgrade_info->sha256))
            {
                iotgoInfo("Flash verify ok");
                system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_EXIT, IOTGO_UPGRADE_ERROR_OK);
            }
            else
            {
                iotgoInfo("Flash verify err");
                system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_EXIT, IOTGO_UPGRADE_ERROR_DIGEST);
            }
        }
        else
        {
            system_os_post(IOTGO_CORE, MSG_CORE_GET_UPGRADE_DATA, 0);
        }
    }
    else
    {
        iotgoError("store upgrade data to flash is error");
        system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_EXIT, IOTGO_UPGRADE_ERROR_DOWNLOAD);
    }
}

static int32_t ICACHE_FLASH_ATTR getUserBinLength(uint8 *pdata,uint16_t len)
{
    int32_t file_len = -1;
    uint8_t *t1 = NULL;
    uint8_t *t2 = NULL;
    uint8_t *t3 = NULL;
    if(NULL == pdata)
    {
        return -1;
    }
    t1 = (uint8 *)os_strstr(pdata, "Content-Range");
    if(NULL != t1 && len > (t1 - pdata))
    {
        t2 = (uint8 *)os_strstr(t1, "/");
        if(NULL != t2 && len > (t2 - pdata))
        {
            t3 = (uint8 *)os_strstr(t2, "\r\n");
            if(NULL != t3 && len > (t3 - pdata))
            {
                file_len = (int32_t)atoi(t2 + 1);
            }
        }
        
    }
    return file_len;
}


static void ICACHE_FLASH_ATTR upgradeTCPSproOnePkgCb(void *arg, uint8 *pdata, uint16 len, bool flag)
{
    if(!flag)
    {
        goto error_ret;
    }
    else
    {
        uint8_t *t1 = NULL;
        uint8_t *t2 = NULL;
        uint16_t pkg_len = 0;
        if(!started)
        {
            started = true;
            upgrade_file.file_length = getUserBinLength(pdata,len);
            iotgoInfo("file_length = %d",upgrade_file.file_length);
            if(upgrade_file.file_length > IOTGO_UPGRADE_BIN_MAX_LEN)
            {
                started = false;
                goto error_ret;
            }
        }
        t1 = (uint8 *)os_strstr(pdata, "HTTP/1.1 206 Partial Content\r\n");
        if(NULL == t1 || len <= (t1 - pdata))
        {
            goto error_ret;
        }
        pkg_len = getContentLength(t1,len);
        iotgoInfo("pkg_len = %d",pkg_len);
        if(pkg_len == -1 || pkg_len > SPI_FLASH_SEC_SIZE || pkg_len < 0)
        {
            goto error_ret;
        }
        t2 = (uint8_t *)os_strstr(pdata, "\r\n\r\n");
        if(NULL == t2 || len <= (t2 - pdata))
        {
            goto error_ret;
        }
        os_memset(download_sector_buffer, 0xff, SPI_FLASH_SEC_SIZE);
        os_memcpy(download_sector_buffer,(uint8 *)(t2 + 4),pkg_len);
        storeUpgradeDataToFlash(pkg_len);
    }
    return;
error_ret:
    iotgoWarn("in error_ret");
    system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_EXIT, IOTGO_UPGRADE_ERROR_DOWNLOAD);

}

static void ICACHE_FLASH_ATTR upgradeDownloadTCPRecvCallback(void *arg, char *pdata, 
    unsigned short len)
{
    spTcpRecv(upgrade_spro, arg, pdata, len);
}

static void ICACHE_FLASH_ATTR upgradeDownloadTCPConnectedCallback(void *arg)
{
    struct espconn *pespconn = (struct espconn *)arg;
    iotgoInfo("upgradeDownloadTCPConnectedCallback");
    espconn_regist_recvcb(&upgrade_dl_conn, upgradeDownloadTCPRecvCallback);
    espconn_regist_disconcb(&upgrade_dl_conn, upgradeDownloadTCPDisconCallback);
    espconn_regist_sentcb(&upgrade_dl_conn, upgradeDownloadTCPSendCallback);
    system_os_post(IOTGO_CORE, MSG_CORE_GET_UPGRADE_DATA, 0);
}

static void ICACHE_FLASH_ATTR establishTCP(struct espconn *tcp_conn)
{
    sint8 ret;
    os_timer_t *timer;
    ret = espconn_connect(tcp_conn);
    if (ret == 0)
    {
        iotgoInfo("establishTCP ok\n");    
    }
    else
    {
        iotgoWarn("establishTCP err!\n"); 
        system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_RECONNECT_AGAIN, 0);
    }
}

static void ICACHE_FLASH_ATTR upgradeDnsFoundCallback(const char *name, 
    ip_addr_t *ipaddr, void *arg)
{
    sint8 ret;
    struct espconn *pespconn = (struct espconn *) arg;
    
    if (name != NULL)
    {
        iotgoInfo("name = [%s]\n", name);
    }
    
    if (ipaddr == NULL) {
        iotgoWarn("upgrade dns found nothing but NULL\n");
        system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_RECONNECT_AGAIN, 0);
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
        establishTCP(pespconn);
    }
    else
    {
        iotgoWarn("dns err!\n");
        system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_RECONNECT_AGAIN, 0);
    }
}

static void ICACHE_FLASH_ATTR connectToDownloadServer(void)
{
    sint8 ret;
    
    ip_addr_t server_ip = {0};
    uint32 ip = ipaddr_addr(iotgo_upgrade_info->host);
    iotgoInfo("connect to upgrade server");
    os_bzero(&upgrade_dl_conn, sizeof(upgrade_dl_conn));
    os_bzero(&upgrade_dl_conn_tcp, sizeof(upgrade_dl_conn_tcp));
    upgrade_dl_conn.type = ESPCONN_TCP;
    upgrade_dl_conn.state = ESPCONN_NONE;
    upgrade_dl_conn.proto.tcp = &upgrade_dl_conn_tcp;
    upgrade_dl_conn.proto.tcp->local_port = espconn_port();    
    upgrade_dl_conn.proto.tcp->remote_port = iotgo_upgrade_info->port;
    
    os_memcpy(upgrade_dl_conn.proto.tcp->remote_ip, &ip, 4);
    
    espconn_regist_connectcb(&upgrade_dl_conn, upgradeDownloadTCPConnectedCallback);
    espconn_regist_reconcb(&upgrade_dl_conn, upgradeDownloadTCPReconCallback);
    if(ip == 0xFFFFFFFF)
    {
        espconn_gethostbyname(&upgrade_dl_conn, iotgo_upgrade_info->host, &server_ip, upgradeDnsFoundCallback);
    }
    else
    {
        establishTCP(&upgrade_dl_conn);
    }
}

/*
 * 根据参数 bin_info->name字符串，查找对应的digest和dlurl填充相应字段作为返回值
 */
static bool ICACHE_FLASH_ATTR procOneBinListInfo(struct jsonparse_state *pjs, IoTgoUpgradeBinInfo *bin_info)
{
    int cnt = 0;
    int type;
    char bin_name[20];
    bool field_name_flag    = false;
    bool field_dlurl_flag   = false;
    bool field_sha256_flag  = false;
    
    while ((type = jsonparse_next(pjs)) != ']' 
            && type != '}' 
            && type != JSON_TYPE_ERROR
        )
    {
        iotgoDebug("index = %d, type = %d (%c), vlen = %d\n", ++cnt, type, type, 
            jsonparse_get_len(pjs));

        if (JSON_TYPE_PAIR_NAME != type)
        {
            continue;
        }

        if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_NAME))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_NAME, bin_name, sizeof(bin_name)))
            {
                field_name_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_DIGEST))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_DIGEST, bin_info->sha256, sizeof(bin_info->sha256)))
            {
                field_sha256_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_DOWNLOADURL))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_DOWNLOADURL, bin_info->dlurl, sizeof(bin_info->dlurl)))
            {
                field_dlurl_flag = true;
            }
            else
            {
                break;
            }
        }
    }
    
    if (field_name_flag
        && field_sha256_flag
        && field_dlurl_flag
        && 0 == os_strcmp(bin_name, bin_info->name)
        && 64 == os_strlen(bin_info->sha256)
        )
    {
        iotgoInfo("binInfo matched!");
        return true;
    }
    else
    {
        iotgoDebug("binInfo not matched!");
        return false;
    }
}

/*
 * 根据参数 bin_info->name字符串，查找对应的digest和dlurl填充相应字段作为返回值
 */
static bool ICACHE_FLASH_ATTR jsonIoTgoProcBinList(struct jsonparse_state *pjs, IoTgoUpgradeBinInfo *bin_info)
{
    int type;
    int type1;
    int type2;
    
    type1 = jsonparse_next(pjs);
    type2 = jsonparse_next(pjs);
    if (JSON_TYPE_PAIR ==  type1 && JSON_TYPE_ARRAY == type2)
    {
        while ((type = jsonparse_next(pjs)) != ']' && type != 0)
        {
            if (JSON_TYPE_OBJECT == type)
            {
                if (procOneBinListInfo(pjs, bin_info))
                {
                    return true;
                }
            }
        }
    }
    iotgoWarn("Invalid binList!");
    return false;
}

static inline void ICACHE_FLASH_ATTR printIoTgoUpgradeInfo(const IoTgoUpgradeInfo *info)
{
    if (info)
    {
        iotgoInfo("name:[%s]", info->name);
        iotgoInfo("protocol:[%u]", info->protocol);
        iotgoInfo("host:[%s]", info->host);
        iotgoInfo("port:[%u]", info->port);
        iotgoInfo("path:[%s]", info->path);
        iotgoInfo("sector:[0x%02X]", info->sector);
        iotgoInfo("sha256:[%s]", info->sha256);
        iotgoInfo("sequence:[%s]", info->sequence);
        iotgoInfo("auto_restart:[%d]", info->auto_restart);
    }
}

/* 
 * 从 dlurl 中解析出protocol、host、port、path并填充info
 * 
 * http://172.16.7.184:8088/ota/rom/123456abcde/user1.1024.new.bin
 * http://172.16.7.184/ota/rom/123456abcde/user1.1024.new.bin
 * https://172.16.7.184:8088/ota/rom/123456abcde/user1.1024.new.bin
 * https://172.16.7.184/ota/rom/123456abcde/user1.1024.new.bin    
 * http://172.16.7.184:8088/ota/rom/123456abcde/user1.1024.new.bin?deviceid=12&ts=1001&sign=xxx
 */
static void ICACHE_FLASH_ATTR parseDownloadURLToUpgradeInfo(const char *dlurl, IoTgoUpgradeInfo *info)
{
    char *slash;
    char *colon;

    if (!dlurl || !info)
    {
        return;
    }
    
    if (0 == os_strncmp(dlurl, "http://", 7))
    {
        iotgoDebug("http upgrade protocol");
        info->protocol = IOTGO_UPGRADE_PROTOCOL_HTTP;
    }
    else
    {
        iotgoWarn("Unsupported upgrade protocol!");
        info->protocol = IOTGO_UPGRADE_PROTOCOL_INVALID;
    }

    if (IOTGO_UPGRADE_PROTOCOL_HTTP == info->protocol)
    {   
        char temp[10] = {0};
        os_bzero(info->host, sizeof(info->host));
        colon = (char *)os_strstr(&dlurl[7], ":");
        slash = (char *)os_strstr(&dlurl[7], "/");
        if (colon && slash)
        {
            os_strncpy(info->host, &dlurl[7], colon - &dlurl[7]);
            os_strncpy(temp, colon + 1, slash - colon + 1);
            info->port = atoi(temp);
            os_strcpy(info->path, slash);
        }
        else if (slash)
        {
            os_strncpy(info->host, &dlurl[7], slash - &dlurl[7]);
            info->port = IOTGO_UPGRADE_HTTP_DEF_PORT;
            os_strcpy(info->path, slash);
        }
        else
        {
            iotgoWarn("bad downloadUrl!");
        }
    }
}


static void ICACHE_FLASH_ATTR cbUpgradeEnterTimer(void * arg)
{
    system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_ENTER, 0);
}


/*
 * 解析下载服务器的主机名，端口号，下载路径(根据当前运行的固件区域选择对立的固件下载)
 * 下载固件并写入flash并校验
 * 给服务器返回结果
 * 注意: 锁定升级模式，不允许执行其他操作(不处理来自服务器的操作，不上报来自本地的操作
 *      不允许进入配置模这会导致网络中断升级失败)
 */
void ICACHE_FLASH_ATTR iotgoUpgradeProcessRequest(const char *data)
{
    int cnt = 0;
    int type;
    int type1;
    int type2;
    
    char server_sequence[IOTGO_SERVER_SEQUENCE_SIZE] = {0};
    char model[IOTGO_DEVICE_MODEL_SIZE] = {0};
    char version[IOTGO_DEVICE_FW_VERSION_SIZE] = {0};
    int auto_restart = 0;
    
    bool field_server_sequence_flag     = false;
    bool field_binlist_flag             = false;
    bool field_userbin_flag             = false;
    bool field_userbin_ret              = false;
    bool field_model_flag               = false;
    bool field_model_ret                = false;
    bool field_version_flag             = false;
    bool field_version_ret              = false;
    bool field_auto_restart_flag        = false;
    
    uint8 current_bin;

    /*
     * bin_info 用来临时存放一些信息，在本函数内部最后会释放掉。
     */
    IoTgoUpgradeBinInfo bin_info = {0};
    
    /*
     * 获取升级BIN的名字，并赋值给bin_info.name
     */
    current_bin = system_upgrade_userbin_check();
    if (UPGRADE_FW_BIN1 == current_bin) 
    {
        os_strcpy(bin_info.name, "user2.bin");
    }
    else
    {
        os_strcpy(bin_info.name, "user1.bin");
    }
    iotgoDebug("%s to download", bin_info.name);

    /*
     * 从data中解析出bin_info.sha256、bin_info.dlurl、sequence、model、version
     */
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
        
        if (!field_binlist_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_BINLIST))
        {
            field_binlist_flag = true;
            field_userbin_ret = jsonIoTgoProcBinList(pjs, &bin_info);
        }
        else if (!field_server_sequence_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_SEQUENCE))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_SEQUENCE, server_sequence, sizeof(server_sequence)))
            {
                field_server_sequence_flag = true;
            }
        }
        else if (!field_model_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_MODEL))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_MODEL, model, sizeof(model)))
            {
                field_model_flag = true;
                field_model_ret = modelMatch(model);
            }
        }
        else if (!field_version_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_VERSION))
        {
            if (jsonIoTgoGetString(pjs, IOTGO_STRING_VERSION, version, sizeof(version)))
            {
                field_version_flag = true;
                field_version_ret = verifyFirmwareVersion(version);
            }
        }
        else if (!field_auto_restart_flag && 0 == jsonparse_strcmp_value(pjs, IOTGO_STRING_AUTORESTART))
        {
            if (jsonIoTgoGetNumber(pjs, IOTGO_STRING_AUTORESTART, &auto_restart))
            {
                field_auto_restart_flag = true;
            }
        }
        
    }
    iotgoDebug("while parse done\n");
    
    os_free(pjs);

    if (!(field_binlist_flag && field_server_sequence_flag && field_userbin_ret && field_version_flag
        && field_auto_restart_flag))
    {
        iotgoError("Invalid upgrade req from server!");
        iotgoPconnRespondErrorCode(IOTGO_UPGRADE_ERROR_BADREQ, server_sequence);
        return;
    }

    if (!field_model_ret)
    {
        iotgoPconnRespondErrorCode(IOTGO_UPGRADE_ERROR_MODEL, server_sequence);
        return;
    }

    if (!field_version_ret)
    {
        iotgoPconnRespondErrorCode(IOTGO_UPGRADE_ERROR_VERSION, server_sequence);
        return;
    }
    
    /* 
     * 为 iotgo_upgrade_info 分配内存
     * 这片内存在 iotgoUpgradeFree 释放
     */
    if (iotgo_upgrade_info)
    {
        os_free(iotgo_upgrade_info);
        iotgo_upgrade_info = NULL;
    }
    iotgo_upgrade_info = (IoTgoUpgradeInfo *)os_zalloc(sizeof(IoTgoUpgradeInfo));
    if (!iotgo_upgrade_info)
    {
        iotgoError("os_malloc failed!");
    }
    
    /* 
     * 通过 bin_info中的 name sha256 dlurl信息来填充 iotgo_upgrade_info 
     */
    iotgoDebug("name:[%s]", bin_info.name);
    iotgoDebug("digest:[%s]", bin_info.sha256);
    iotgoDebug("downloadUrl:[%s]", bin_info.dlurl);
    os_strcpy(iotgo_upgrade_info->name, bin_info.name);
    os_strcpy(iotgo_upgrade_info->version, version);
    os_strcpy(iotgo_upgrade_info->sha256, bin_info.sha256);
    os_strcpy(iotgo_upgrade_info->sequence, server_sequence);
    if (UPGRADE_FW_BIN1 == current_bin)
    {
        iotgo_upgrade_info->sector = IOTGO_UPGRADE_BIN2_START_SECTOR;
    }
    else
    {
        iotgo_upgrade_info->sector = IOTGO_UPGRADE_BIN1_START_SECTOR;
    }
    
    parseDownloadURLToUpgradeInfo(bin_info.dlurl, iotgo_upgrade_info);
    iotgo_upgrade_info->auto_restart = auto_restart;
    
    printIoTgoUpgradeInfo(iotgo_upgrade_info);
    
    iotgoPconnRespondErrorCode(IOTGO_UPGRADE_ERROR_OK, server_sequence);

    /*
     * 此处必须等待一段时间，否则系统会异常重启。
     */
    os_timer_disarm(&upgrade_enter_timer);
    os_timer_setfn(&upgrade_enter_timer, (os_timer_func_t *)cbUpgradeEnterTimer, NULL);
    os_timer_arm(&upgrade_enter_timer, 50, 0);
}

static void ICACHE_FLASH_ATTR iotgoUpgradeMalloc(void)
{
    if (upgrade_http_buffer)
    {
        os_free(upgrade_http_buffer);
        upgrade_http_buffer = NULL;
    }
    upgrade_http_buffer = (char *)os_malloc(IOTGO_UPGRADE_HTTP_BUFFER_SIZE);
    if (!upgrade_http_buffer)
    {
        iotgoError("os_malloc failed!");
    }

    if (download_sector_buffer) 
    {
        os_free(download_sector_buffer);
        download_sector_buffer = NULL;
    }
    download_sector_buffer = (uint8 *)os_zalloc(SPI_FLASH_SEC_SIZE);
    if (!download_sector_buffer)
    {
        iotgoError("os_zalloc failed");
    }
}

static void ICACHE_FLASH_ATTR iotgoUpgradeFree(void)
{
    if (upgrade_http_buffer)
    {
        os_free(upgrade_http_buffer);
        upgrade_http_buffer = NULL;
    }

    if (download_sector_buffer) 
    {
        os_free(download_sector_buffer);
        download_sector_buffer = NULL;
    }
    
    if (iotgo_upgrade_info) 
    {
        os_free(iotgo_upgrade_info);
        iotgo_upgrade_info = NULL;
    }
}

static void ICACHE_FLASH_ATTR cbUpgradeTimer(void *arg)
{
    if (upgrade_file.file_length >= upgrade_file.downloaded_length)
    {
        iotgoInfo("[Download: (%u/%u)]", upgrade_file.downloaded_length, upgrade_file.file_length);
    }
    if (upgrade_file.downloaded_length == upgrade_file.file_length)
    {
        os_timer_disarm(&upgrade_progress_timer);
    }
    upgrade_timeout++;
    if (upgrade_timeout >= IOTGO_UPGRADE_TIMEOUT 
        && upgrade_file.file_length != upgrade_file.downloaded_length
        && upgrade_file.file_length != 0)
    {
        iotgoWarn("upgrade_timeout!");
        system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_EXIT, IOTGO_UPGRADE_ERROR_DOWNLOAD);
    }
}

static void ICACHE_FLASH_ATTR connectUpgradeServerAgain(void *arg)
{
    connectToDownloadServer();
}

void ICACHE_FLASH_ATTR iotgoUpgradeStart(void)
{
    iotgoPrintHeapSize();
    upgrade_spro = spCreateObject(SPRO_PKG_TYPE_IOT_UPGRADE, upgradeTCPSproOnePkgCb, 1024 * 5);
    if (NULL == upgrade_spro)
    {
        iotgoError("spCreateObject err!");    
    }
    
    iotgoUpgradeMalloc();
    
    connect_server_again_counter = 0;
    started = false;
    upgrade_file.file_length = 0;
    upgrade_file.downloaded_length = 0;
    upgrade_file.couter = 0;
    upgrade_file.range_start = 0;
    upgrade_file.range_end = 0;
    upgrade_timeout = 0;
    connectToDownloadServer();
    
    os_timer_disarm(&upgrade_progress_timer);
    os_timer_setfn(&upgrade_progress_timer, (os_timer_func_t *)cbUpgradeTimer, NULL);
    os_timer_arm(&upgrade_progress_timer, 1000, 1);
    upgrade_started_flag = true;
}

static void ICACHE_FLASH_ATTR cbUpgradeExit0Timer(void * arg)
{
    iotgoRestartForNewBin();
}

static void ICACHE_FLASH_ATTR sendUpgradeState(int32_t err_code, const char *seq)
{
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, IOTGO_STRING_UPGRADESTATE, err_code);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, IOTGO_STRING_ACTION, IOTGO_STRING_UPDATE);
    cJSON_AddStringToObject(root, IOTGO_STRING_USERAGENT, IOTGO_STRING_DEVICE);
    cJSON_AddStringToObject(root, IOTGO_STRING_APIKEY, iotgo_device.owner_uuid);
    cJSON_AddStringToObject(root, IOTGO_STRING_DEVICEID, iotgo_device.deviceid);
    cJSON_AddItemToObject(root, IOTGO_STRING_PARAMS, params);
    cJSON_AddStringToObject(root, IOTGO_STRING_DSEQ, seq);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    iotgoQueueAdd(json, IOTGO_NULL, false);
    os_free(json);
}

void ICACHE_FLASH_ATTR iotgoUpgradeStop(uint32 error_code)
{
    if (IOTGO_UPGRADE_ERROR_OK == error_code)
    {
        IoTgoNewBinInfo new_bin_info = {0};
        new_bin_info.length = upgrade_file.file_length;
        os_strcpy(new_bin_info.name, iotgo_upgrade_info->name);
        os_strcpy(new_bin_info.sha256, iotgo_upgrade_info->sha256);
        os_strcpy(new_bin_info.version, iotgo_upgrade_info->version);
        iotgo_flash_param.new_bin_info = new_bin_info;
        iotgoFlashSaveParam(&iotgo_flash_param);
        iotgoInfo("new bin info stored!");
    }
    
    upgrade_started_flag = false;
    iotgoPrintHeapSize();
    os_timer_disarm(&connect_server_again_timer);
    espconn_disconnect(&upgrade_dl_conn);
    connect_server_again_counter = 0;
    started = false;
    upgrade_file.file_length = 0;
    upgrade_file.downloaded_length = 0;
    upgrade_file.couter = 0;
    upgrade_file.range_start = 0;
    upgrade_file.range_end = 0;
    upgrade_timeout = 0;
    flash_sector_index = 0;
    os_timer_disarm(&upgrade_progress_timer);

    /* 发送下载结果给服务器 */
    sendUpgradeState(error_code, iotgo_upgrade_info->sequence);
        
    if (upgrade_spro)
    {
        spReleaseObject(upgrade_spro);
        upgrade_spro = NULL;
    }

    
    if(IOTGO_UPGRADE_ERROR_OK == error_code)
    {
        if (iotgo_upgrade_info->auto_restart)
        {
            iotgoInfo("firmware download ok with autorestart!");
            os_timer_disarm(&upgrade_exit_timer);
            os_timer_setfn(&upgrade_exit_timer, (os_timer_func_t *)cbUpgradeExit0Timer, NULL);
            os_timer_arm(&upgrade_exit_timer, 3000, 0);
        }
        else
        {
            iotgoInfo("firmware download ok without autorestart!");
        }
    }
    else
    {
        iotgoInfo("firmware download failed(%u)", error_code);
    }
    
    iotgoUpgradeFree();
}

void ICACHE_FLASH_ATTR iotgoUpgradeReconnect(void)
{
    if (!upgrade_started_flag) 
    {
        iotgoInfo("upgrade_started_flag is false and just return!");
        return;
    }
    
    connect_server_again_counter++;
    iotgoPrintHeapSize();
    //espconn_disconnect(&upgrade_dl_conn);
    if(connect_server_again_counter < 4)
    {
        os_timer_disarm(&connect_server_again_timer);
        os_timer_setfn(&connect_server_again_timer, (os_timer_func_t *)connectUpgradeServerAgain, NULL);
        os_timer_arm(&connect_server_again_timer, (1000 * connect_server_again_counter), 0);
    }
    else
    {
        system_os_post(IOTGO_CORE, MSG_CORE_UPGRADE_EXIT, IOTGO_UPGRADE_ERROR_DOWNLOAD);
    }
}

static void ICACHE_FLASH_ATTR cbRestartForNewBinTimer(void *arg)
{
    os_free(arg);
    iotgoRestartForNewBin();
}

/**
 * 处理来自服务器的action:restart请求，重启以使用新固件
 * 该函数完成如下动作:
 *   验证新版固件完整性；
 *   给服务器发送返回值；
 *   如果验证成功，重启使用新固件；
 */
void ICACHE_FLASH_ATTR iotgoUpgradeRestartForNewBin(void)
{
    IoTgoUpgradeNewBinResult result = iotgoUpgradeVerifyNewBin();
    iotgoPconnRespondErrorCode(result, server_sequence_value);
    
    if (IOTGO_UPGRADE_NEW_BIN_RESULT_OK == result)
    {
        os_timer_t *timer = (os_timer_t *)os_zalloc(sizeof(os_timer_t));
        os_timer_disarm(timer);
        os_timer_setfn(timer, (os_timer_func_t *)cbRestartForNewBinTimer, timer);
        os_timer_arm(timer, 3000, 0); 
        iotgoInfo("\n\nNew Bin OK and restart after 3 seconds...\n\n");
    }
    else
    {
        iotgoWarn("\n\niotgoUpgradeVerifyNewBin failed(%d)\n\n", result);
    }
}

