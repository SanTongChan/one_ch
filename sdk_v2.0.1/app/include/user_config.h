#ifndef __USER_CONFIG_H__
#define __USER_CONFIG_H__

/*
 * �������������ʾ:
 *  1. �Ƿ��������˰汾��
 *  2. �Ƿ���ȷѡ����������豸����
 *  3. �Ƿ񷢲��˵��԰�ͷ�����
 *  4. �Ƿ�Ҫ����û�����SSL
 *  5. �Ƿ�ͨ���˹����Բ��Ժ��ȶ��Բ���
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
