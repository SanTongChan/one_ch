#ifndef __IOTGO_FWSW_01_CONFIG_H__
#define __IOTGO_FWSW_01_CONFIG_H__
#if defined(COMPILE_IOTGO_FWSW_01)

#define IOTGO_MODULE_ID (IOTGO_MODULE_ID_NULL)

#define IOTGO_FM_VERSION "1.6.0" /* 1.6.0 版本号仅用于调试新版OTA */

#define SYS_KEY_GPIO_TRIGGER_METHOD GPIO_PIN_INTR_NEGEDGE
#define SYS_KEY_GPIO_RELEASED_LEVEL GPIO_HIGH

#define SWITCH_INPUT_GPIO           (0)
#define SWITCH_INPUT_GPIO_NAME      PERIPHS_IO_MUX_GPIO0_U
#define SWITCH_INPUT_GPIO_FUNC      FUNC_GPIO0

#define SWITCH_ON       GPIO_HIGH     
#define SWITCH_OFF      GPIO_LOW         

#define DEV_SLED_ON     GPIO_LOW
#define DEV_SLED_OFF    GPIO_HIGH

#define SWITCH_OUTPUT_GPIO          (12)
#define SWITCH_OUTPUT_GPIO_NAME     PERIPHS_IO_MUX_MTDI_U
#define SWITCH_OUTPUT_GPIO_FUNC     FUNC_GPIO12

#define DEV_SLED_GPIO             (13)
#define DEV_SLED_GPIO_NAME        PERIPHS_IO_MUX_MTCK_U
#define DEV_SLED_GPIO_FUNC        FUNC_GPIO13

#else
#error "You should select the right device to compile `_'!"
#endif /* #ifdef COMPILE_IOTGO_FWSW_01 */

#endif /* #ifndef __IOTGO_FWSW_01_CONFIG_H__ */
