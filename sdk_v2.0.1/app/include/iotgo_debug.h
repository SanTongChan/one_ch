#ifndef __IOTGO_DEBUG_H__
#define __IOTGO_DEBUG_H__

#include "user_config.h"

#ifdef IOTGO_LOG
#define IOTGO_OUTPUT_PRINTF             (1)
#define IOTGO_OUTPUT_ERROR              (1)
#define IOTGO_OUTPUT_WARN               (1)
#define IOTGO_OUTPUT_INFO               (1)
#define IOTGO_OUTPUT_DEBUG              (0)
#else
#define IOTGO_OUTPUT_PRINTF             (0)
#define IOTGO_OUTPUT_ERROR              (0)
#define IOTGO_OUTPUT_WARN               (0)
#define IOTGO_OUTPUT_INFO               (0)
#define IOTGO_OUTPUT_DEBUG              (0)
#endif

#define IOTGO_OUTPUT_ERROR_PREFIX       (1)
#define IOTGO_OUTPUT_WARN_PREFIX        (1)
#define IOTGO_OUTPUT_INFO_PREFIX        (0)
#define IOTGO_OUTPUT_DEBUG_PREFIX       (0)

#define iotgoPrintf(fmt, args...)\
    do {\
        if (IOTGO_OUTPUT_ERROR)\
        {\
    		os_printf(fmt, ##args);\
        }\
    } while(0)

#define iotgoError(fmt, args...)\
    do {\
        if (IOTGO_OUTPUT_ERROR)\
        {\
            if (IOTGO_OUTPUT_ERROR_PREFIX)\
    		    os_printf("[IOTGO Error:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);\
    		os_printf(fmt, ##args);\
    		os_printf("\n");\
        }\
    } while(0)

#define iotgoWarn(fmt, args...)\
    do {\
        if (IOTGO_OUTPUT_WARN)\
        {\
            if (IOTGO_OUTPUT_WARN_PREFIX)\
    		    os_printf("[IOTGO Warn:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);\
    		os_printf(fmt, ##args);\
    		os_printf("\n");\
        }\
    } while(0)

#define iotgoInfo(fmt, args...)\
    do {\
        if (IOTGO_OUTPUT_INFO)\
        {\
            if (IOTGO_OUTPUT_INFO_PREFIX)\
                os_printf("[IOTGO Info:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);\
            os_printf(fmt, ##args);\
            os_printf("\n");\
        }\
    } while(0)
        
#define iotgoDebug(fmt, args...)\
    do {\
        if (IOTGO_OUTPUT_DEBUG)\
        {\
            if (IOTGO_OUTPUT_DEBUG_PREFIX)\
                os_printf("[IOTGO Debug:%s,%d,%s] ",__FILE__,__LINE__,__FUNCTION__);\
            os_printf(fmt, ##args);\
            os_printf("\n");\
        }\
    } while(0)

#endif /* #ifndef __IOTGO_DEBUG_H__ */
