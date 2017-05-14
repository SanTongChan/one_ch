#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

/*
 * 软件发布操作提示:
 *  1. 是否合理更新了版本号
 *  2. 是否正确选择了所需的设备类型
 *  3. 是否发布了调试版和发布版
 *  4. 是否按要求禁用或启用SSL
 *  5. 是否通过了功能性测试和稳定性测试
 */

/*
 * Define IOTGO_LOG for log output and comment it when release firmware. 
 */
#define IOTGO_LOG

/*
 * Select only a device to compile
 */

//#define COMPILE_IOTGO_FWSW_01
#define COMPILE_IOTGO_FWTRX_TC

#include "iotgo_config.h"

#endif /* #ifndef __USER_CONFIG_H__ */
