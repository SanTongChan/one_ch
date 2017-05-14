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
 * @name 定义 Spro 类
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
 * TCP收到数据时的回调函数
 * 
 * @param obj - Spro 对象
 * @param pkg_proc_cb - Spro对象处理数据的回调函数
 * @param buffer_size - 缓冲区大小
 * @return 返回一个Spro 对象
 */
Spro* ICACHE_FLASH_ATTR spCreateObject(SproPkgType pkg_type, SproPkgProcCb pkg_proc_cb, uint16 buffer_size);

/**
 * 释放Spro对象
 * 
 * @param obj - Spro 对象
 */
void  ICACHE_FLASH_ATTR spReleaseObject(Spro *obj);

/**
 * 设置Spro对象处理数据的回调函数
 * 
 * @param obj - Spro 对象
 * @param pkg_proc_cb - Spro对象处理数据的回调函数
 */
bool ICACHE_FLASH_ATTR spSetPkgProcCb(Spro *obj, SproPkgProcCb pkg_proc_cb);

/**
 * 设置Spro对象数据包的类型
 * 
 * @param obj - Spro 对象
 * @param pkg_type - Spro对象数据包类型
 * @retval true - success.
 * @retval false - failure.
 */
bool ICACHE_FLASH_ATTR spSetPkgType(Spro *obj, SproPkgType pkg_type);

/**
 * TCP收到数据时的回调函数
 * 
 * @param obj - Spro 对象
 * @param arg - TCP接收回调函数传进来的参数
 * @param pdata - 接收到的数据
 * @param len - 接收到的数据长度
 * @retval true - success.
 * @retval false - failure.
 */
void ICACHE_FLASH_ATTR spTcpRecv(Spro *obj, void *arg, uint8 *pdata, uint16 len);

/*
 * @}
 */
#endif /* #ifndef __SPRO_H__ */

