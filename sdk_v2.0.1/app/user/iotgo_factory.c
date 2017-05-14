#include "iotgo_factory.h"
#include "iotgo_flash.h"
#include "addition/sha256.h"

#define IOTGO_UART0_RX_BUFFER_SIZE (1024)
static uint8 *uart0_rx_buffer;
static uint16 buffer_counter = 0;
static os_timer_t proc_uart0_timer;
static os_timer_t recv_data_timer;
static os_timer_t recv_data_timerout;
/*
 * hex2 = "d1"
 */
static uint8 ICACHE_FLASH_ATTR getValueFromHex(const char *hex2)
{
    uint8 reth = hex2[0];
    uint8 retl = hex2[1];
    
    if (reth >= 'a')
    {
        reth -= 'a';
        reth += 10;
    }
    else
    {
        reth -= '0';
    }
    
    if (retl >= 'a')
    {
        retl -= 'a';
        retl += 10;
    }
    else
    {
        retl -= '0';
    }

    return (reth << 4) | retl;
}

/*
 * str - 这样格式的地址 d0:27:00:00:00:20， 前17个字符有效
 * mac - 长度为6的uint8数组，用于存放数值mac地址
 */
static void ICACHE_FLASH_ATTR iotgoStr2Mac(uint8 mac[6], const char *str)
{
    uint8 i;
    char temp[18] = {0};
    
    if (mac == NULL || str == NULL)
    {
        iotgoError("mac or str is NULL!");
        return;    
    }
    
    os_strncpy(temp, str, 17);
    
    /* 全部变成小写字符 */
    for (i = 0; i < 17; i++)
    {
        if (temp[i] >= 'A' && temp[i] <= 'F')
        {
            temp[i] += 32;
        }
    }
    
    for (i = 0; i < 6; i++)
    {
        mac[i] = getValueFromHex(temp + (i * 3));
    }
    
}
static char *ICACHE_FLASH_ATTR iotgoMac2Str(char *str,uint8 mac[6])
{
    uint8 i = 0;
    uint8 j = 0;
    for(j = 0; j < 18; j++)
    {
        if((j+1) % 3 == 0)
        {
            str[j] = ':';
        }
        else
        {
            os_sprintf(&str[j], "%02x", mac[i]);
            i++;
            j++;
        }
    }
    str[17] = '\0';
    iotgoInfo("Received data = [%s]", str);
    return str;
}


static void ICACHE_FLASH_ATTR recvData(void *arg)
{
    uint32 len = (uint32)arg;
    uint16 i;
    char c;
    IoTgoFlashParam flash_param;
    struct jsonparse_state *pjs;
    int cnt = 0;
    int8 type;
    int8 type1;
    int8 type2;
    bool field_device_model_flag = false;
    bool field_deviceid_flag = false;
    bool field_factory_apikey_flag = false;
    bool field_sta_mac_flag = false;
    bool field_sap_mac_flag = false;
    char device_model[IOTGO_DEVICE_MODEL_SIZE] = {0};
    char deviceid[IOTGO_DEVICEID_SIZE] = {0};
    char factory_apikey[IOTGO_OWNER_UUID_SIZE] = {0};
    char sta_mac[17 + 1] = {0};
    char sap_mac[17 + 1] = {0};
    char sta_mac_hex[17 + 1] = {0};
    char sap_mac_hex[17 + 1] = {0};
    uint8 sta_mac_num[6] = {0};
    uint8 sap_mac_num[6] = {0};
    char unknown[20] = {0};
    sha256_context sha256_ctx;
    uint8 digest[32];
    uint8 digest_hex[65] = {0};

    if (uart0_rx_available() < len)
    {
        return;
    }

    os_timer_disarm(&recv_data_timer);
    os_memset(uart0_rx_buffer, 0, IOTGO_UART0_RX_BUFFER_SIZE);
    for (i = 0; i < len; i++)
    {
        c = uart0_rx_read();
        uart0_rx_buffer[i] = c;
    }

    iotgoInfo("Received data = [%s]", uart0_rx_buffer);
    
    pjs = (struct jsonparse_state *)os_zalloc(sizeof(struct jsonparse_state));
    
    jsonparse_setup(pjs, uart0_rx_buffer, os_strlen(uart0_rx_buffer));
    while ((type = jsonparse_next(pjs)) != 0)
    {
        iotgoDebug("index = %d, type = %d (%c), vlen = %d\n", ++cnt, type, type, 
            jsonparse_get_len(pjs));

        if (JSON_TYPE_PAIR_NAME != type)
        {
            continue;
        }

        if (!field_device_model_flag && 0 == jsonparse_strcmp_value(pjs, "device_model"))
        {
            if (jsonIoTgoGetString(pjs, "device_model", device_model, sizeof(device_model)))
            {
                field_device_model_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_deviceid_flag && 0 == jsonparse_strcmp_value(pjs, "deviceid"))
        {
            if (jsonIoTgoGetString(pjs, "deviceid", deviceid, sizeof(deviceid)))
            {
                field_deviceid_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_factory_apikey_flag && 0 == jsonparse_strcmp_value(pjs, "factory_apikey"))
        {
            if (jsonIoTgoGetString(pjs, "factory_apikey", factory_apikey, sizeof(factory_apikey)))
            {
                field_factory_apikey_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_sta_mac_flag && 0 == jsonparse_strcmp_value(pjs, "sta_mac"))
        {
            if (jsonIoTgoGetString(pjs, "sta_mac", sta_mac, sizeof(sta_mac)))
            {
                field_sta_mac_flag = true;
            }
            else
            {
                break;
            }
        }
        else if (!field_sap_mac_flag && 0 == jsonparse_strcmp_value(pjs, "sap_mac"))
        {
            if (jsonIoTgoGetString(pjs, "sap_mac", sap_mac, sizeof(sap_mac)))
            {
                field_sap_mac_flag = true;
            }
            else
            {
                break;
            }
        }
        else
        {
            jsonparse_copy_value(pjs, unknown, sizeof(unknown));
            iotgoDebug("Unknown field =[%s] and ignored!\n", unknown);
        }
    }
    iotgoDebug("while parse done\n");
    
    os_free(pjs);

    if (field_deviceid_flag
        && field_deviceid_flag
        && 10 == os_strlen(deviceid)
        && field_factory_apikey_flag
        && 36 == os_strlen(factory_apikey)
        && field_sap_mac_flag
        && 17 == os_strlen(sap_mac)
        && field_sta_mac_flag
        && 17 == os_strlen(sta_mac)
        )
    {
        iotgoFlashLoadParam(&flash_param);
        
        if (IOTGO_FLASHED_MAGIC_NUMBER == flash_param.flashed_magic_number)
        {
            uart0_tx_string("\r\nFLASH FAILED: FLASHED ALREADY\r\n");
            uart0_tx_string("OK\r\n");
            iotgoInfo("FLASH FAILED: FLASHED ALREADY");
        }
        else
        {
            os_bzero(&flash_param, sizeof(flash_param));
            
            /* Set sta_config */
            os_strcpy(flash_param.sta_config.ssid, IOTGO_DEVICE_DEFAULT_SSID);
            os_strcpy(flash_param.sta_config.password, IOTGO_DEVICE_DEFAULT_PASSWORD);

            /* Set device model */
            os_strcpy(flash_param.factory_data.device_model, device_model);

            /* Set factory_data */
            os_strcpy(flash_param.factory_data.deviceid, deviceid);
            os_strcpy(flash_param.factory_data.factory_apikey, factory_apikey);
            
            iotgoStr2Mac(sta_mac_num, sta_mac);
            iotgoInfo("printf mac");
            iotgoInfo("[%02X:%02X:%02X:%02X:%02X:%02X]\n", sta_mac_num[0], sta_mac_num[1], sta_mac_num[2], 
                sta_mac_num[3], sta_mac_num[4], sta_mac_num[5]);
                
            iotgoStr2Mac(sap_mac_num, sap_mac);
            
            iotgoInfo("[%02X:%02X:%02X:%02X:%02X:%02X]\n", sap_mac_num[0], sap_mac_num[1], sap_mac_num[2], 
                sap_mac_num[3], sap_mac_num[4], sap_mac_num[5]);

            os_memcpy(flash_param.factory_data.sta_mac, sta_mac_num, sizeof(sta_mac_num));
            os_memcpy(flash_param.factory_data.sap_mac, sap_mac_num, sizeof(sap_mac_num));

            /* Set sap_ip_info */
            IP4_ADDR(&flash_param.sap_ip_info.ip,       10,10,7,1);
            IP4_ADDR(&flash_param.sap_ip_info.gw,       10,10,7,1);
            IP4_ADDR(&flash_param.sap_ip_info.netmask,  255,255,255,0);

            /* Set sap_config */
            os_strcpy(flash_param.sap_config.ssid, "ITEAD-");
            os_strcat(flash_param.sap_config.ssid, flash_param.factory_data.deviceid);
            flash_param.sap_config.ssid_len = os_strlen(flash_param.sap_config.ssid);
            os_strcpy(flash_param.sap_config.password, "12345678");
            flash_param.sap_config.channel        = 7;
            flash_param.sap_config.authmode       = 4;
            flash_param.sap_config.ssid_len       = 16;
            flash_param.sap_config.ssid_hidden    = 0; /* 0 - Not hidden, 1 - Hidden */
            flash_param.sap_config.max_connection = 4;
            flash_param.flashed_magic_number = IOTGO_FLASHED_MAGIC_NUMBER ;
            iotgoInfo("Flash params");
            iotgoFlashSaveParam(&flash_param);
            if (iotgoFlashCipher() && iotgoFlashDecipher())
            {
                sha256_starts(&sha256_ctx);
                sha256_update(&sha256_ctx, flash_param.factory_data.deviceid, os_strlen(flash_param.factory_data.deviceid));
                sha256_update(&sha256_ctx, flash_param.factory_data.factory_apikey, os_strlen(flash_param.factory_data.factory_apikey));
                sha256_update(&sha256_ctx, iotgoMac2Str(sta_mac_hex,flash_param.factory_data.sta_mac), os_strlen(sta_mac_hex));
                sha256_update(&sha256_ctx, iotgoMac2Str(sap_mac_hex,flash_param.factory_data.sap_mac), os_strlen(sap_mac_hex));
                sha256_update(&sha256_ctx, flash_param.factory_data.device_model, os_strlen(flash_param.factory_data.device_model));
                sha256_finish(&sha256_ctx, digest);
                for (i = 0; i < 64; i += 2)
                {
                    os_sprintf(&digest_hex[i], "%02x", digest[i/2]);
                }
                digest_hex[64] = '\0';
                uart0_tx_buffer(digest_hex,64);
                uart0_tx_string("\r\nOK\r\n");
                iotgoInfo("digest_hex = [\r\n%s\r\n]", digest_hex);
                if (iotgo_device.devInitDeviceConfig)
                {
                    iotgo_device.devInitDeviceConfig();
                }    
                iotgoInfo("FLASH OK");
            }
            else
            {
                uart0_tx_string("\r\nFLASH FAILED: IO ERROR\r\n");
                uart0_tx_string("OK\r\n");
                iotgoInfo("FLASH FAILED: IO ERROR");
            }
        }
    }
    else
    {
        uart0_tx_string("\r\nERROR FORMAT:fields lost\r\n");
        uart0_tx_string("OK\r\n");
        iotgoInfo("ERROR FORMAT:fields lost");
    }
    
    uart0_rx_flush();
    os_timer_disarm(&recv_data_timerout);
    os_timer_arm(&proc_uart0_timer, 50, 1);
}

static void ICACHE_FLASH_ATTR recvDataTimerout(void *arg)
{
    iotgoInfo("TIMER OUT");
    uart0_rx_flush();
    os_timer_disarm(&recv_data_timer);
    os_timer_arm(&proc_uart0_timer, 50, 1);
}

static void ICACHE_FLASH_ATTR procUART0(void *arg)
{
    static bool data_recv = false;
    
    char c;
    char *temp;
    char *temp1;
    uint32 len = 0;
    for (; uart0_rx_available() > 0 && buffer_counter < IOTGO_UART0_RX_BUFFER_SIZE; buffer_counter++)
    {
        c = uart0_rx_read();
        uart0_rx_buffer[buffer_counter] = c;
    }
    temp = (char *)os_strstr(uart0_rx_buffer, "\r\n"); /* AT+SEND=121\r\n */
    if(temp)
    {
        os_timer_disarm(&proc_uart0_timer);
        buffer_counter = 0;
        temp[0] = '\0';
        iotgoPrintf("rec = %s", uart0_rx_buffer);
        temp1 = (char *)os_strstr(uart0_rx_buffer, "AT+SEND="); /* AT+SEND=121\r\n */
        if(temp1)
        {
            temp1 += os_strlen("AT+SEND=");
            len = atoi(temp1);
            iotgoInfo("len = %d\n",len);
            if (len > 0 && len <= 512)
            {
                uart0_rx_flush();
                uart0_tx_string(">\r\n");
                iotgoInfo("i send >");
                os_memset(uart0_rx_buffer, '\0' , IOTGO_UART0_RX_BUFFER_SIZE);
                os_timer_disarm(&recv_data_timer);
                os_timer_setfn(&recv_data_timer, (os_timer_func_t *)recvData, (void*)len);
                os_timer_arm(&recv_data_timer, 100, 1);
                os_timer_disarm(&recv_data_timerout);
                os_timer_setfn(&recv_data_timerout, (os_timer_func_t *)recvDataTimerout, NULL);
                os_timer_arm(&recv_data_timerout, 2000, 0);
            }
            else
            {
                uart0_rx_flush();
                uart0_tx_string("\r\nLENGTH ERROR\r\n");
                os_memset(uart0_rx_buffer, '\0' , IOTGO_UART0_RX_BUFFER_SIZE);
                os_timer_arm(&proc_uart0_timer, 50, 1);
            }
        }
        else
        {
            uart0_rx_flush();
            uart0_tx_string("\r\nSYNTAX ERROR\r\n");
            os_memset(uart0_rx_buffer, '\0' , IOTGO_UART0_RX_BUFFER_SIZE);
            os_timer_arm(&proc_uart0_timer, 50, 1);
        }
    }
    
}


void ICACHE_FLASH_ATTR startFlashDataMode(void)
{
    iotgoInfo("flash data mode started\n");
    uart0_rx_buffer = (uint8 *)os_malloc(IOTGO_UART0_RX_BUFFER_SIZE);
    if (uart0_rx_buffer == NULL)
    {
        iotgoError("os_zalloc(IOTGO_UART0_RX_BUFFER_SIZE) failed and halt now!");
        while(1);
    }
    iotgoSetDeviceMode(DEVICE_MODE_FACTORY);
    os_memset(uart0_rx_buffer, '\0' , IOTGO_UART0_RX_BUFFER_SIZE);
    os_timer_disarm(&proc_uart0_timer);
    os_timer_setfn(&proc_uart0_timer, (os_timer_func_t *)procUART0, NULL);
    os_timer_arm(&proc_uart0_timer, 50, 1);
}

void ICACHE_FLASH_ATTR stopFlashDataMode(void)
{
    iotgoInfo("flash data mode stopped\n");
    if (uart0_rx_buffer != NULL)
    {
        os_free(uart0_rx_buffer);
    }
    os_timer_disarm(&proc_uart0_timer);
    os_timer_disarm(&recv_data_timer);
    uart0_rx_flush();
}
