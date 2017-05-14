#include "iotgo_global.h"

IoTgoFlashParam iotgo_flash_param;


IoTgoDevice iotgo_device;

bool normal_mode_led_off = false;


const struct jsontree_string json_device = JSONTREE_STRING(IOTGO_STRING_DEVICE);
const struct jsontree_string json_fw_version = JSONTREE_STRING(IOTGO_FM_VERSION);
const struct jsontree_string json_model = JSONTREE_STRING(iotgo_flash_param.factory_data.device_model);
struct jsontree_int json_int_rssi = {JSON_TYPE_INT, IOTGO_WIFI_RSSI_DEFAULT};
int8 iotgo_wifi_rssi = IOTGO_WIFI_RSSI_DEFAULT;
const struct jsontree_string json_action_register = JSONTREE_STRING(IOTGO_STRING_REGISTER);
const struct jsontree_string json_action_update = JSONTREE_STRING(IOTGO_STRING_UPDATE);
const struct jsontree_string json_action_query = JSONTREE_STRING(IOTGO_STRING_QUERY);
const struct jsontree_string json_action_date = JSONTREE_STRING(IOTGO_STRING_DATE);


const struct jsontree_int json_error_0 = {JSON_TYPE_INT, 0};
const struct jsontree_int json_int_iot_version = {JSON_TYPE_INT, IOTGO_IOT_PROTOCOL_VERSION};
struct jsontree_int json_int_reqts = {JSON_TYPE_INT, 0};

const struct jsontree_int json_int_0 = {JSON_TYPE_INT, 0};
const struct jsontree_int json_int_1 = {JSON_TYPE_INT, 1};
const struct jsontree_int json_int_2 = {JSON_TYPE_INT, 2};
const struct jsontree_int json_int_3 = {JSON_TYPE_INT, 3};


const struct jsontree_string json_owner_uuid = JSONTREE_STRING(iotgo_device.owner_uuid);
const struct jsontree_string json_factory_apikey = JSONTREE_STRING(iotgo_device.factory_apikey);
const struct jsontree_string json_deviceid = JSONTREE_STRING(iotgo_device.deviceid);

char server_sequence_value[IOTGO_SERVER_SEQUENCE_SIZE] = IOTGO_STRING_INVALID_SEQUENCE;
struct jsontree_string json_server_sequence = JSONTREE_STRING(server_sequence_value);

static IoTgoDeviceMode device_mode = DEVICE_MODE_INVALID;

IoTgoDeviceMode ICACHE_FLASH_ATTR iotgoDeviceMode(void)
{
    return device_mode;
}

void ICACHE_FLASH_ATTR iotgoSetDeviceMode(IoTgoDeviceMode mode)
{
    device_mode = mode;
}

uint32 ICACHE_FLASH_ATTR iotgoGenerateTimestamp(void)
{
    static uint32 counter = 0;
    return (counter++) + (rand() % 1000);
}

/* 
 * 生成介于n1和n2之间的随机数(包含n1和n2)
 * 
 * @param n1 - the smaller.
 * @param n2 - the bigger.
 * @retval random number between n1 and n2. 
 */
uint32 ICACHE_FLASH_ATTR iotgoRand(uint32 n1, uint32 n2)
{  
    uint32 n_min = 0;
    uint32 n_max = 0;
    uint32 n_diff = 0;
    uint32 ret = 0;
    
    if (n2 > n1)
    {
        n_max = n2;
        n_min = n1;
    }
    else if (n2 < n1)
    {
        n_max = n1;
        n_min = n2;
    }
    else /* if (n1 == n2) */
    {
        return n2;
    }

    n_diff = n_max - n_min;
    ret = n_min + (rand() % (n_diff + 1) + (n_diff + 1)) % (n_diff + 1);
    return ret;
}

/* This section of flash is for device config information. */
#define DEVICE_CONFIG_START_SEC		    (0x7C)
#define DEVICE_CONFIG_SAVE_0            (0x00) /* 0x7C */
#define DEVICE_CONFIG_SAVE_1            (0x01) /* 0x7D */
#define DEVICE_CONFIG_FLAG              (0x02) /* 0x7E */

/*
 * @warning len must align by 4Bytes. 
 * @par Usage
 * @code

// 保存到 Flash 的参数大小务必 4 字节对齐，否则后果不堪设想
typedef struct 
{
    float hlw8012_param_a;
    float hlw8012_param_b;

    // 保证该结构体的大小是四字节对齐的
    uint8_t pad[
    (
        4 - 
            (   sizeof(float) 
                + sizeof(float) 
            ) % 4
        ) % 4
    ];
} PSCConfig;

 * @endcode
 */
void ICACHE_FLASH_ATTR devLoadConfigFromFlash(void *param, uint32 len)
{
    IoTgoFlashParamFlag flag;
    len %= SPI_FLASH_SEC_SIZE + 1;
    len -= len % 4;
    
    spi_flash_read((DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_FLAG) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)&flag, sizeof(IoTgoFlashParamFlag));

    if (flag.flag == 0) {
        spi_flash_read((DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_SAVE_0) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)param, len);
    } else {
        spi_flash_read((DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_SAVE_1) * SPI_FLASH_SEC_SIZE,
                       (uint32 *)param, len);
    }
}

/*
 * @warning len must align by 4Bytes. 
 */
void ICACHE_FLASH_ATTR devSaveConfigToFlash(void *param, uint32 len)
{
    IoTgoFlashParamFlag flag;
    len %= SPI_FLASH_SEC_SIZE + 1;
    len -= len % 4;

    spi_flash_read((DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_FLAG) * SPI_FLASH_SEC_SIZE,
                   (uint32 *)&flag, sizeof(IoTgoFlashParamFlag));

    if (flag.flag == 0) {
        spi_flash_erase_sector(DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_SAVE_1);
        spi_flash_write((DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_SAVE_1) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)param, len);
        flag.flag = 1;
        spi_flash_erase_sector(DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_FLAG);
        spi_flash_write((DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_FLAG) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)&flag, sizeof(IoTgoFlashParamFlag));
    } else {
        spi_flash_erase_sector(DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_SAVE_0);
        spi_flash_write((DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_SAVE_0) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)param, len);
        flag.flag = 0;
        spi_flash_erase_sector(DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_FLAG);
        spi_flash_write((DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_FLAG) * SPI_FLASH_SEC_SIZE,
                        (uint32 *)&flag, sizeof(IoTgoFlashParamFlag));
    }
}

/*
 * 用来擦除设备相关的配置(即擦除设备配置3个扇区)
 */
void ICACHE_FLASH_ATTR devEraseConfigInFlash(void)
{
    spi_flash_erase_sector(DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_FLAG);
    spi_flash_erase_sector(DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_SAVE_0);
    spi_flash_erase_sector(DEVICE_CONFIG_START_SEC + DEVICE_CONFIG_SAVE_1);
    iotgoInfo("devEraseConfigInFlash done");
}

void uart0RxUserIsr(uint8 data)
{
    if (iotgo_device.uart0RxCallback)
    {
        iotgo_device.uart0RxCallback(data);
    }
}

uint32 ICACHE_FLASH_ATTR iotgoBootReason(void)
{
    static struct rst_info* rstp = NULL;
    if (!rstp) 
    {
        rstp = system_get_rst_info();
    }
    return rstp->reason;
}   

/*  
 * 该函数返回值表示是否需要保持GPIO状态(即不调用gpio_init以及其他默认上电时的gpio操作)
 * @retval true - keep
 * @retval false - no keep
 */
bool iotgoIsXrstKeepGpio(void)
{
    uint32 device_rst_reason = iotgoBootReason();
    if (REASON_SOFT_RESTART == device_rst_reason
        || REASON_SOFT_WDT_RST == device_rst_reason
        || REASON_EXCEPTION_RST == device_rst_reason)
    {
        return true;
    }
    return false;
}

/*
 * 返回当前固件运行的硬件，主要给测试方案使用
 *
 * @retval IOTGO_MODULE_ID_NULL:表示未指定硬件，不需要测试；
 * @retval IOTGO_MODULE_ID_<XXX>:表示该固件使用的硬件，需要测试；
 */
IoTgoModuleIdentifier iotgoGetModuleIdentifier(void)
{
    if (IOTGO_MODULE_ID >= IOTGO_MODULE_ID_NULL
        && IOTGO_MODULE_ID < IOTGO_MODULE_ID_MAX)
    {
        return IOTGO_MODULE_ID;
    }
    else
    {
        iotgoError("Bad IOTGO_MODULE_ID = %d", IOTGO_MODULE_ID);
        return IOTGO_MODULE_ID_NULL;
    }
}


uint32_t ICACHE_FLASH_ATTR iotgoMagicNumber(void)
{
    return iotgo_flash_param.flashed_magic_number;
}

const char* ICACHE_FLASH_ATTR iotgoStationMac(void)
{
    static char sta_mac_str[18] = {0};
    uint8_t sta_mac[6] = {0};

    if (!sta_mac_str[0])
    {
        if(wifi_get_macaddr(STATION_IF, sta_mac))
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

uint32 ICACHE_FLASH_ATTR iotgoPower(uint32 base, uint32 power)
{
    uint32 i = 0;
    uint32 ret = 1;
    for (i = 0; i < power; i++)
    {
        ret *= base;
    }
    return ret;
}

void ICACHE_FLASH_ATTR iotgoRestartForNewBin(void)
{
    iotgoInfo("reboot for new bin now!");
    system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
    system_upgrade_reboot();
}

