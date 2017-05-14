#ifndef __IOTGO_FWTRX_TC_CONFIG_H__
#define __IOTGO_FWTRX_TC_CONFIG_H__
#ifdef COMPILE_IOTGO_FWTRX_TC

#define IOTGO_FM_VERSION "1.0.0"
#define IOTGO_MODULE_ID (IOTGO_MODULE_ID_NULL)
#define SYS_KEY_GPIO_TRIGGER_METHOD GPIO_PIN_INTR_NEGEDGE
#define SYS_KEY_GPIO_RELEASED_LEVEL GPIO_HIGH

#define SWITCH_INPUT_GPIO           (0)
#define SWITCH_INPUT_GPIO_NAME      PERIPHS_IO_MUX_GPIO0_U
#define SWITCH_INPUT_GPIO_FUNC      FUNC_GPIO0

#define SWITCH_ON       GPIO_HIGH     
#define SWITCH_OFF      GPIO_LOW         

#define DEV_SLED_ON    GPIO_LOW 
#define DEV_SLED_OFF    GPIO_HIGH

#define DEV_SLED_GPIO             (13)
#define DEV_SLED_GPIO_NAME        PERIPHS_IO_MUX_MTCK_U
#define DEV_SLED_GPIO_FUNC        FUNC_GPIO13

#define TA_HIGH_PRIORITY_CTRL_GPIO         (14)
#define TA_HIGH_PRIORITY_CTRL_GPIO_NAME    (PERIPHS_IO_MUX_MTMS_U)
#define TA_HIGH_PRIORITY_CTRL_GPIO_FUNC    FUNC_GPIO14
#define TA_HIGH_PRIORITY_CTRL_LOW_WIDTH    (500) /* ms */
#define TA_FEED_DOG_TIME    (10)  /* ms */

#define IOTGO_UART0_BAUDRATE   (9600)

#define TCREQ_PERIOD    (1)
#define TCREQ_MAX       (20)

#else
#error "You should select the right device to compile `_'!"
#endif /* #ifdef COMPILE_IOTGO_FWTRX_TA */ 
#endif /* #ifndef __IOTGO_FWTRX_TA_CONFIG_H__ */ 

