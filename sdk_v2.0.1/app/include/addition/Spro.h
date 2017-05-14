/**
 * @file Spro.h
 * @brief The definition class Spro(Simple Protocol). 
 * @details Reveive data from tcp recv callback and pack them based on specific
 *  protocol(obviously simplified) such as HTTP/1.1 and WebSocket/13 etc. Then 
 *  call the registered callback to process one complete package. 
 *
 * @author Wu Pengfei<pengfei.wu@itead.cc> Chen Zengpeng<zengpeng.chen@itead.cc>
 * @date 2015.09.08
 */
#ifndef __SPRO_H__
#define __SPRO_H__

#include "sdk_include.h"

#include "iotgo_debug.h"

/**
 * @name ���� Spro ��
 * @{
 */
 
/**
 * Types supported by Spro. 
 */
typedef enum 
{
    SPRO_PKG_TYPE_NULL           = 0, /**< Directly passed data from tcp stream */
    SPRO_PKG_TYPE_HTTP11         = 1, /**< HTTP/1.1 */
    SPRO_PKG_TYPE_WEBSOCKET13    = 2, /**< WebSocket 13 */
    SPRO_PKG_TYPE_IOT_UPGRADE    = 3, /**< iotgo upgrade download */
} SproPkgType;

/**
 * Prototype of Package Process Callback. 
 */
typedef void (*SproPkgProcCb)(void *arg, uint8 *pkg, uint16 pkg_len, bool flag);

typedef struct 
{
    SproPkgType _pkg_type;
    uint8 *_buffer;
    uint16 _buffer_size;
    os_timer_t _timer;
    uint16 _pkg_len;
    uint16 _buffer_index;
    bool _pkg_one_flag;
    bool _timer_opened_flag;
    SproPkgProcCb _pkg_proc_cb;
    void *_arg;
} Spro;

/**
 * TCP�յ�����ʱ�Ļص�����
 * 
 * @param obj - Spro ����
 * @param pkg_proc_cb - Spro���������ݵĻص�����
 * @param buffer_size - ��������С
 * @return ����һ��Spro ����
 */
Spro* ICACHE_FLASH_ATTR spCreateObject(SproPkgType pkg_type, SproPkgProcCb pkg_proc_cb, uint16 buffer_size);

/**
 * �ͷ�Spro����
 * 
 * @param obj - Spro ����
 */
void  ICACHE_FLASH_ATTR spReleaseObject(Spro *obj);

/**
 * ����Spro���������ݵĻص�����
 * 
 * @param obj - Spro ����
 * @param pkg_proc_cb - Spro���������ݵĻص�����
 */
bool ICACHE_FLASH_ATTR spSetPkgProcCb(Spro *obj, SproPkgProcCb pkg_proc_cb);

/**
 * ����Spro�������ݰ�������
 * 
 * @param obj - Spro ����
 * @param pkg_type - Spro�������ݰ�����
 * @retval true - success.
 * @retval false - failure.
 */
bool ICACHE_FLASH_ATTR spSetPkgType(Spro *obj, SproPkgType pkg_type);

/**
 * TCP�յ�����ʱ�Ļص�����
 * 
 * @param obj - Spro ����
 * @param arg - TCP���ջص������������Ĳ���
 * @param pdata - ���յ�������
 * @param len - ���յ������ݳ���
 * @retval true - success.
 * @retval false - failure.
 */
void ICACHE_FLASH_ATTR spTcpRecv(Spro *obj, void *arg, uint8 *pdata, uint16 len);

/*
 * @}
 */
#endif /* #ifndef __SPRO_H__ */

