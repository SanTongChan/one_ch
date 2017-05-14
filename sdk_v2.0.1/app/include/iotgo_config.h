#ifndef __IOTGO_CONFIG_H__
#define __IOTGO_CONFIG_H__

/*
 *******************************************************************************
 *
 *                              !!! ���ؾ��� !!!
 * 
 * !!! �������ݾ��Բ����������������֪��������ʲô����Ը��е���� !!!
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
 * ��������޸ĵ��Դ��ڵĲ����ʣ�����뿴��SDK�ײ��������Ϣ��Ӧ�������ʵ���Ϊ74880��
 */
#define IOTGO_UART1_BAUDRATE                (74880)

/**
 * ����0Ĭ�ϲ�����
 * ��Ҫ��ʾ: �޸Ĵ˴�����Ӱ��������ԡ�������д!!!
 * ���Ҫ�޸ĳ������Ժ�������д�Ĳ�������ǰ�� iotgoTestModeStart
 */
#ifndef IOTGO_UART0_BAUDRATE
#define IOTGO_UART0_BAUDRATE               (19200)
#endif

#endif /* #ifndef __IOTGO_CONFIG_H__ */

