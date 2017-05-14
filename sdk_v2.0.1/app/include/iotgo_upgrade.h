#ifndef __IOTGO_UPGRADE_H__
#define __IOTGO_UPGRADE_H__
#include "sdk_include.h"

#include "user_config.h"
#include "driver/uart.h"
#include "addition/sha256.h"

#include "iotgo_define.h"
#include "iotgo_global.h"
#include "iotgo_flash.h"
#include "iotgo_factory.h"
#include "iotgo_sled.h"
#include "iotgo_core.h"
#include "iotgo_device.h"
#include "iotgo_clock.h"
#include "addition/Spro.h"
#include "iotgo_pconn.h"
#include "addition/cJSON.h"

/**
 * 表示重启使用新固件时，给服务器的返回值。不可随意更改，请参考协议。
 */
typedef enum {
    IOTGO_UPGRADE_NEW_BIN_RESULT_OK             = 0,    /**< 成功 */
    IOTGO_UPGRADE_NEW_BIN_RESULT_BAD_VERSION    = 411,  /**< 版本错误 */
    IOTGO_UPGRADE_NEW_BIN_RESULT_BAD_NAME       = 412,  /**< 固件编号不对称 */
    IOTGO_UPGRADE_NEW_BIN_RESULT_BAD_DIGEST     = 413,  /**< sha256校验失败 */
} IoTgoUpgradeNewBinResult;

void ICACHE_FLASH_ATTR iotgoUpgradeProcessRequest(const char *data);
void ICACHE_FLASH_ATTR iotgoUpgradeStart(void);
void ICACHE_FLASH_ATTR iotgoUpgradeStop(uint32 error_code);
void ICACHE_FLASH_ATTR iotgoUpgradeDownload(void);
IoTgoUpgradeNewBinResult ICACHE_FLASH_ATTR iotgoUpgradeVerifyNewBin(void);
bool ICACHE_FLASH_ATTR iotgoUpgradeVerifyFlashData(uint32 start_addr, uint32 size, 
    const uint8 digest_hex[65]);
void ICACHE_FLASH_ATTR iotgoUpgradeRestartForNewBin(void);

#endif /* #ifndef __IOTGO_UPGRADE_H__ */

