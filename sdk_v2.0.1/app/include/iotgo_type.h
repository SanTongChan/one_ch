#ifndef __IOTGO_TYPE_H__
#define __IOTGO_TYPE_H__

#include "sdk_include.h"

typedef enum {
    IOTGO_DEVICE_FW_VERSION_SIZE = 5 + 1,
    IOTGO_DEVICE_MODEL_SIZE      = 20 + 1,
    IOTGO_DEVICEID_SIZE          = 10 + 1,
    IOTGO_OWNER_UUID_SIZE        = 36 + 1,
    IOTGO_GMT_DATE_TIME_SIZE     = 24 + 1,
    IOTGO_HOST_NAME_SIZE        = 50 + 1,
    IOTGO_SERVER_SEQUENCE_SIZE  = 50 + 1,
} IoTgoDeviceConstant;

typedef struct {
    char device_model[IOTGO_DEVICE_MODEL_SIZE];
    char deviceid[IOTGO_DEVICEID_SIZE];
    char factory_apikey[IOTGO_OWNER_UUID_SIZE];
    uint8 sta_mac[6];
    uint8 sap_mac[6];
} FactoryData;

typedef enum 
{
    MSG_CORE_JOIN_AP                      = 2,
    MSG_CORE_AP_JOINED                    = 3,
    MSG_CORE_JOIN_AP_ERR                  = 4,   
        
    MSG_CORE_SERVER_CONNECTED             = 100,
    MSG_CORE_SERVER_DISCONNECTED          = 101,
    MSG_CORE_CONNECT_SERVER               = 102,
    MSG_CORE_RECONNECT_SERVER             = 103,
    MSG_CORE_CONNECT_SERVER_ERR           = 104,
    MSG_CORE_CONNECT_SERVER_AGAIN         = 105,
    MSG_CORE_DISTRIBUTOR                  = 106, /* with error_code parameter */
    MSG_CORE_DISTRIBUTOR_FINISHED         = 107, /* with error_code parameter */
    
    MSG_CORE_WS_CONNECTED                 = 200,
    MSG_CORE_WS_CONNECT                   = 201,
    
    MSG_CORE_REGISTER                    = 202,
    MSG_CORE_REGISTERED                  = 203,
    MSG_CORE_REGISTER_ERR                = 204,
    MSG_CORE_DATE                        = 205,
    MSG_CORE_DATE_OK                     = 206,
    MSG_CORE_SEND_JSON                   = 207,
    MSG_CORE_SEND_JSON_AGAIN             = 208,
    MSG_CORE_SEND_JSON_FINISHED          = 209,
    MSG_CORE_REDIRECT                    = 300,
    MSG_CORE_NOTIFY_CONFIG               = 301,
    
    SIG_DEVICE_UPDATE_BY_LOCAL          = 402,
    SIG_DEVICE_UPDATE_BY_TIMER          = 403,
    SIG_DEVICE_UPDATE_BY_REMOTE         = 404,
    SIG_DEVICE_UPDATE                   = 405,
    SIG_DEVICE_UPDATE_OK                = 406,
    SIG_DEVICE_QUERY                    = 407,
    SIG_DEVICE_QUERY_OK                 = 408,
    SIG_DEVICE_QUERY_UPDATE_BY_REMOTE   = 409,
    
    SIG_DEVICE_ENTER_TEST               = 411,
    SIG_DEVICE_EXIT_TEST                = 412,
    SIG_DEVICE_LOCAL_EVENT              = 413, 
    SIG_DEVICE_READYED                  = 414,
	SIG_DEVICE_TRANSPARENT_UPDATE       = 415,
    MSG_CORE_WS_PING                 = 500, 
    MSG_CORE_WS_PING_OK              = 502,

    MSG_CORE_ENTER_SETTING_MODE      = 700,
    MSG_CORE_EXIT_SETTING_MODE       = 701,
    MSG_CORE_SETTING_RESP_TO_APP     = 702,
    MSG_CORE_SETTING_SEND_ID_TO_APP  = 703,
    MSG_CORE_SETTING_SC_START        = 705,
    MSG_CORE_SETTING_SC_STOP         = 706,
    MSG_CORE_SETTING_SC_RESTART      = 707,
    MSG_CORE_ENTER_SETTING_SELFAP_MODE = 708,
    MSG_CORE_EXIT_SETTING_SELFAP_MODE = 709,

    MSG_CORE_UPGRADE_ENTER      = 800,
    MSG_CORE_UPGRADE_EXIT       = 801,
    MSG_CORE_GET_UPGRADE_DATA        = 802,
    MSG_CORE_UPGRADE_RECONNECT_AGAIN = 803,

    
    SIG_DEVICE_USER1                    = 11001, /**< ONLY used by device center for private purpose */
    SIG_DEVICE_USER2                    = 11002, /**< ONLY used by device center for private purpose */
    SIG_DEVICE_USER3                    = 11003, /**< ONLY used by device center for private purpose */

} IoTgoSignalSet;

typedef enum 
{
    DEVICE_MODE_START          = 0,
    DEVICE_MODE_WORK_AP_ERR    = 1,
    DEVICE_MODE_WORK_AP_OK     = 2,    
    DEVICE_MODE_WORK_INIT      = 3,
    DEVICE_MODE_WORK_NORMAL    = 4,
    DEVICE_MODE_SETTING        = 5,
    DEVICE_MODE_FACTORY        = 7,
    DEVICE_MODE_UPGRADE        = 8,
    DEVICE_MODE_SETTING_SELFAP = 9,
    
    
    DEVICE_MODE_INVALID        = 255,
} IoTgoDeviceMode;


typedef enum  
{
    IOTGO_PKG_TYPE_INVALID      = 0,
    IOTGO_PKG_TYPE_RESP_OF_INIT    = 1,
    IOTGO_PKG_TYPE_RESP_OF_UPDATE  = 2,
    IOTGO_PKG_TYPE_RESP_OF_QUERY   = 3,
    IOTGO_PKG_TYPE_RESP_OF_DATE   = 4,
    IOTGO_PKG_TYPE_REQ_OF_UPDATE   = 5,
    IOTGO_PKG_TYPE_REQ_OF_QUERY    = 6,
    IOTGO_PKG_TYPE_REQ_OF_UPGRADE  = 7,
    IOTGO_PKG_TYPE_REQ_OF_REDIRECT  = 8,
    IOTGO_PKG_TYPE_REQ_OF_NOTIFY_CONFIG  = 9,
    IOTGO_PKG_TYPE_RET_OF_RESTART   = 10,       /* 重启使用新固件 */
    
    IOTGO_PKG_TYPE_ERROR_OF_FORBIDDEN  = 99,
    IOTGO_PKG_TYPE_RESP_OF_INIT_FAILED        = 100,
} IoTgoPkgType;

/*
 * 硬件标识定义
 */
typedef enum
{
    IOTGO_MODULE_ID_NULL = 0,   /* 用于不需要测试的硬件 */
    IOTGO_MODULE_ID_PSA,        /* 单通道开关 */
    IOTGO_MODULE_ID_PSB,        /* 多通道开关 */
    IOTGO_MODULE_ID_PSC,        /* 功率开关 */
    IOTGO_MODULE_ID_PWM_LED,    /* PWM 5路输出 */
    IOTGO_MODULE_ID_IFAN1,      /* 风扇模块 */
    IOTGO_MODULE_ID_SONOFFTH,   /* PSA-BHA-GL */
    IOTGO_MODULE_ID_PSF,
    /* 在上面添加新的硬件 */
    IOTGO_MODULE_ID_MAX /* 结束符 */
} IoTgoModuleIdentifier;

typedef struct {
    char host[IOTGO_HOST_NAME_SIZE];
    int32 port;
} IoTgoHostInfo;

typedef struct {
    char name[20];                              /* user1.bin 或者 user2.bin */
    char version[IOTGO_DEVICE_FW_VERSION_SIZE]; /* X.Y.Z */
    uint32 length;                              /* 固件长度(起始地址根据name可以算出) */
    char sha256[65];                            /* 全小写字符 */
} IoTgoNewBinInfo;

/* 
 * 该数据结构不能修改，这牵扯到新老设备的 Flash 参数兼容问题，新的数据只能增加在后面 
 * 严重注意: SDK 版本升级是否会导致该数据中的类型发生变化，甚至是变量的顺序发生变化
 *           也会造成灾难性的后果，只有召回解决。将来必须要优化。
 */
typedef struct  {
    struct station_config sta_config; 
    FactoryData factory_data;    
    struct ip_info sap_ip_info;
    struct softap_config sap_config;
    uint32_t flashed_magic_number; 
    IoTgoHostInfo iot_distributor; /* IoT2.0 新增该参数，只能依次存放 */
    uint32_t iot_distributor_last_ip; /* 保存上一次成功访问的分配服务器IP地址 */
    IoTgoNewBinInfo new_bin_info;   /* 保持最新下载的固件信息(无论是否使用) */
    
    uint32_t __pad;
} IoTgoFlashParam;

typedef struct  {
    uint8 flag; 
    uint8 pad[3];
    uint32_t __pad;
} IoTgoFlashParamFlag;


typedef struct  
{
    char deviceid[IOTGO_DEVICEID_SIZE];
    char factory_apikey[IOTGO_OWNER_UUID_SIZE];
    char owner_uuid[IOTGO_OWNER_UUID_SIZE];

    /* 最早期初始化，仅用于进行硬件资源配置及初始化，不能用于其他用途 */
    void (*earliestInit)(void);

    /* 早期的初始化 */
    void (*earlyInit)(void); 

    /* 必须处理 SIG_DEVICE_INIT 邮件 */
    void (*postCenter)(os_event_t * events); 

    /* 从服务器收到 之前的 Update 请求的回应时，回调该函数  */
    void (*respOfUpdateCallback)(const char *data); 

    /* 从服务器收到 之前的 Query 请求的回应时，回调该函数  */
    void (*respOfQueryCallback)(const char *data); 

    /* 从服务器收到 Update 请求时，回调该函数 */
    void (*reqOfUpdateCallback)(const char *data); 

    /* 从服务器收到 Query 请求时，回调该函数 */
    void (*reqOfQueryCallback)(const char *data); 

    /*从服务器收到的Register中有DevConfig,需要将DevConfig的内容发送出去时，回调该函数*/
    void (*reqofRegisterCallback)(const char *data);

    /*从服务器收到notify中有DevConfig，需要将DevConfig的内容发送出去时，回调该函数*/
    void (*reqofNotityCallback)(const char *data);
        
    /* 进入TOUCH配置模式时，回调该函数 */
    void (*enterEsptouchSettingModeCallback)(void);

    /* 退出touch配置模式时，回调该函数 */
    void (*exitEsptouchSettingModeCallback)(void);
    
    /* 进入AP配置模式时，回调该函数*/
    void (*enterApSettingModeCallback)(void);
    
    /* 退出AP配置模式时，回调该函数 */
    void (*exitApSettingModeCallback)(void);

    /* 出厂数据烧写成功后，初始化设备相关的参数 */
    void (*devInitDeviceConfig)(void);

    /* UART0 串口接收回调函数 */
    void (*uart0RxCallback)(uint8 data);

} IoTgoDevice;


typedef struct  
{
    uint16 year;
    uint8 month;
    uint8 day;
    uint8 hour;
    uint8 minute;
    uint8 second;
} IoTgoGMTTime;

typedef struct  {
    bool used_flag;
    int32 enabled;
    char type[10];
    char at[30];
    char switch_value[5];
    uint8 outlet; /* for multi switch */
} IoTgoDeviceSwitchTimer;

typedef struct  {
    bool used_flag;
    int32 enabled;
    char type[10];
    char at[30];
    char shake_value[5];
    uint8 speed_value;
} IoTgoDeviceSfaTimer;


typedef struct  {
    bool weeks[7];  /* weeks[0-6]: index = week, true or false */
    uint8 hour;     /* 0-23, 24=* */
    uint8 minute;   /* 0-59 */
} IoTgoCron;

typedef IoTgoCron IoTgoDeviceCron;

typedef void (*IoTgoTimeSchCallback)(void *ptr);

typedef enum {
    IOTGO_TIME_SCH_TYPE_INVALID    = 0,
    IOTGO_TIME_SCH_TYPE_GMT        = 1,
    IOTGO_TIME_SCH_TYPE_CRON       = 2,
} IoTgoTimeSchType;

typedef struct {
    IoTgoTimeSchType type;      /* GMT or CRON */ 
    union 
    {
        IoTgoGMTTime gmt;       /* once timing */
        IoTgoCron cron;         /* repeat timing */
    } timing;                   /* timing to action(call callback with ptr passed) */
    IoTgoTimeSchCallback tsch;  /* a function pointer */
    void *ptr;                  /* user pointer passed when tsch called */
    bool used;                  /* Ignored by device center */
} IoTgoTimeSch;


typedef struct  {
    char name[20];
    char version[IOTGO_DEVICE_FW_VERSION_SIZE];
    bool auto_restart;  
    uint8 protocol;     /* reserved :only http for now*/
    char host[50];      /* domain or ip */
    uint32 port;        /* port number */
    char path[100];     /* bin file path */
    char sha256[65];    /* digest of bin (lowercase) */
    uint16 sector;      /* start sector(4KB) of flash */
    char sequence[IOTGO_SERVER_SEQUENCE_SIZE];  /* sequence to server in response */
} IoTgoUpgradeInfo;

typedef struct {
    char name[20];
    char sha256[65];
    char dlurl[150];
} IoTgoUpgradeBinInfo;

#endif /* #ifndef __IOTGO_TYPE_H__ */
