#ifndef __IOTGO_CONFIG_H__
#define __IOTGO_CONFIG_H__

/*
 *******************************************************************************
 *
 *                              !!! 严重警告 !!!
 * 
 * !!! 以下内容绝对不能碰，除非你真的知道你在做什么而且愿意承担后果 !!!
 *
 *******************************************************************************
 */

#if defined(COMPILE_IOTGO_FWSW_01)
    #include "device/FWSW_01_CONFIG.h"
#elif defined(COMPILE_IOTGO_FWTRX_TC)
    #include "device/FWTRX_TC_CONFIG.h"    
#else
    #error Sorry! You must select a device!
#endif



#define IOTGO_DEVICE_TEST_SSID  "ITEAD_PD_T"
#define IOTGO_DEVICE_TEST_PASS  "27955416"

#define IOTGO_ENABLED_SSL

#ifdef IOTGO_ENABLED_SSL
#define IOTGO_OPERATION_SERVER_ENABLE_SSL
#define IOTGO_DISTRIBUTOR_ENABLE_SSL
#define IOTGO_DEVICE_CLIENT_ENABLE_SSL_SIZE (1024*4)
#endif

#define IOTGO_SERVER_NONSSL_PORT               (8081)

#define IOTGO_DEVICE_DEFAULT_SSID       "8DB0839D"
#define IOTGO_DEVICE_DEFAULT_PASSWORD   "094FAFE8"

#define IOTGO_IOT_PROTOCOL_VERSION              (2)
#define IOTGO_DISTRUBUTOR_PROTOCOL_VERSION      (2)

#define IOTGO_IOT_TCP_CLIENT_RECV_BUFFER_SIZE   (2048)

#define USE_OPTIMIZE_PRINTF /**< Save string into flash */

/**
 * 这里可以修改调试串口的波特率，如果想看到SDK底层的启动信息，应将波特率调整为74880。
 */
#define IOTGO_UART1_BAUDRATE                (74880)

/**
 * 串口0默认波特率
 * 重要提示: 修改此处不会影响出厂测试、数据烧写!!!
 * 如果要修改出厂测试和数据烧写的波特率请前往 iotgoTestModeStart
 */
#ifndef IOTGO_UART0_BAUDRATE
#define IOTGO_UART0_BAUDRATE               (19200)
#endif

#endif /* #ifndef __IOTGO_CONFIG_H__ */

