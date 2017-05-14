#ifndef __IOTGO_GLOBAL_H__
#define __IOTGO_GLOBAL_H__

#include "iotgo_debug.h"
#include "iotgo_define.h"
#include "iotgo_json.h"
#include "iotgo_type.h"
#include "iotgo_flash.h"
#include "iotgo_queue.h"
#include "addition/cJSON.h"

extern IoTgoFlashParam iotgo_flash_param;


extern IoTgoDevice iotgo_device;
extern struct jsontree_int json_int_rssi;
extern int8 iotgo_wifi_rssi;

//add by czp
extern bool normal_mode_led_off;
extern bool crc_enable;
//add by czp


extern const struct jsontree_string json_device;
extern const struct jsontree_string json_fw_version;
extern const struct jsontree_int json_int_iot_version;
extern const struct jsontree_string json_model;
extern struct jsontree_int json_int_reqts;

extern const struct jsontree_string json_action_register;
extern const struct jsontree_string json_action_update;
extern const struct jsontree_string json_action_query;
extern const struct jsontree_string json_action_date;

extern const struct jsontree_int json_error_0;
extern const struct jsontree_int json_int_0;
extern const struct jsontree_int json_int_1;
extern const struct jsontree_int json_int_2;
extern const struct jsontree_int json_int_3;

extern const struct jsontree_string json_owner_uuid;
extern const struct jsontree_string json_factory_apikey;
extern const struct jsontree_string json_deviceid;

extern char server_sequence_value[IOTGO_SERVER_SEQUENCE_SIZE];
extern struct jsontree_string json_server_sequence;

uint32 ICACHE_FLASH_ATTR iotgoGenerateTimestamp(void);
IoTgoDeviceMode ICACHE_FLASH_ATTR iotgoDeviceMode(void);
void ICACHE_FLASH_ATTR iotgoSetDeviceMode(IoTgoDeviceMode mode);

static inline bool ICACHE_FLASH_ATTR jsonIoTgoGetNumber(struct jsonparse_state *pjs, 
    const char *field_name, int32 *number)
{
    int type1;
    int type2;
    
    type1 = jsonparse_next(pjs);
    type2 = jsonparse_next(pjs);
    if (JSON_TYPE_PAIR == type1 && JSON_TYPE_NUMBER == type2)
    {
        *number = jsonparse_get_value_as_int(pjs);
        iotgoDebug("Found field %s:%d", field_name, *number);
        return true;
    }
    else
    {
        iotgoWarn("Invalid field %s", field_name);
        return false;
    }
        
}

static inline bool ICACHE_FLASH_ATTR jsonIoTgoGetString(struct jsonparse_state *pjs, 
    const char *field_name, char *buffer, uint16 len)
{
    int type1;
    int type2;
    
    type1 = jsonparse_next(pjs);
    type2 = jsonparse_next(pjs);
    if (JSON_TYPE_PAIR == type1 && JSON_TYPE_STRING == type2)
    {
        os_bzero(buffer, len);
        jsonparse_copy_value(pjs, buffer, len);
        iotgoDebug("Found field %s:%s", field_name, buffer);
        return true;
    }
    else
    {
        iotgoWarn("Invalid field %s", field_name);
        return false;
    }
}

/*
 * @param len - size of param(less than 4KB)
 * @warning len must align by 4Bytes. 
 */
void ICACHE_FLASH_ATTR devLoadConfigFromFlash(void *param, uint32 len);

/*
 * @param len - size of param(less than 4KB)
 * @warning len must align by 4Bytes. 
 */
void ICACHE_FLASH_ATTR devSaveConfigToFlash(void *param, uint32 len);

void ICACHE_FLASH_ATTR devEraseConfigInFlash(void);

uint32 ICACHE_FLASH_ATTR iotgoRand(uint32 n1, uint32 n2);

uint32 ICACHE_FLASH_ATTR iotgoBootReason(void);

/*  
 * 该函数返回值表示是否需要保持GPIO状态(即不调用gpio_init以及其他默认上电时的gpio操作)
 * @retval true - keep
 * @retval false - no keep
 */
bool iotgoIsXrstKeepGpio(void);

IoTgoModuleIdentifier iotgoGetModuleIdentifier(void);

uint32_t ICACHE_FLASH_ATTR iotgoMagicNumber(void);

const char* ICACHE_FLASH_ATTR iotgoStationMac(void);
uint32 ICACHE_FLASH_ATTR iotgoPower(uint32 base, uint32 power);
void ICACHE_FLASH_ATTR iotgoRestartForNewBin(void);

#endif /* #ifndef __IOTGO_GLOBAL_H__ */
