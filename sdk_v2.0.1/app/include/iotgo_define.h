#ifndef __IOTGO_DEFINE_H__
#define __IOTGO_DEFINE_H__

#define GPIO_HIGH       (0x1)
#define GPIO_LOW        (0x0)

#define IOTGO_CORE            (1)
#define IOTGO_TASK_CORE_QLEN  (64)

#define IOTGO_DEVICE_CENTER            (0)
#define IOTGO_TASK_DEVICE_CENTER_QLEN  (64)

#define IOTGO_WIFI_RSSI_DEFAULT         (0)

#define IOTGO_STRING_INVALID_OWNER_UUID     "Invalid owner uuid"
#define IOTGO_STRING_INVALID_SEQUENCE       "Invalid sequence"

#define IOTGO_STRING_FWVERSION         "fwVersion"
#define IOTGO_STRING_RSSI           "rssi"
#define IOTGO_STRING_USERAGENT         "userAgent"
#define IOTGO_STRING_DEVICE         "device"
#define IOTGO_STRING_APIKEY         "apikey"
#define IOTGO_STRING_DEVICEID       "deviceid"
#define IOTGO_STRING_REASON         "reason"
#define IOTGO_STRING_SEQUENCE       "sequence"
#define IOTGO_STRING_DSEQ           "d_seq"
#define IOTGO_STRING_UPGRADESTATE   "upgradeState"
#define IOTGO_STRING_AUTORESTART    "autoRestart"
#define IOTGO_STRING_ERROR          "error"
#define IOTGO_STRING_ACTION         "action"
#define IOTGO_STRING_PARAMS         "params"
#define IOTGO_STRING_REGISTER       "register"
#define IOTGO_STRING_UPDATE         "update"
#define IOTGO_STRING_QUERY          "query"
#define IOTGO_STRING_DATE           "date"
#define IOTGO_STRING_ACCEPT         "accept"
#define IOTGO_STRING_POST           "post"
#define IOTGO_STRING_UPGRADE        "upgrade"
#define IOTGO_STRING_RESTART        "restart"
#define IOTGO_STRING_BINLIST        "binList"
#define IOTGO_STRING_NAME           "name"
#define IOTGO_STRING_DIGEST         "digest"
#define IOTGO_STRING_DOWNLOADURL    "downloadUrl"
#define IOTGO_STRING_MODEL          "model"
#define IOTGO_STRING_VERSION        "version"
#define IOTGO_STRING_ROMVERSION     "romVersion"
#define IOTGO_STRING_TS             "ts"
#define IOTGO_STRING_CONFIG         "config"
#define IOTGO_STRING_HB             "hb"
#define IOTGO_STRING_HBINTERVAL     "hbInterval"
#define IOTGO_STRING_REGINTERVAL    "regInterval"
#define IOTGO_STRING_IP             "IP"
#define IOTGO_STRING_PORT           "port"
#define IOTGO_STRING_NOTIFY         "notify"
#define IOTGO_STRING_CMD            "cmd"
#define IOTGO_STRING_REDIRECT       "redirect"
#define IOTGO_STRING_STA_MAC        "staMac"
#define IOTGO_STRING_DEV_CONFIG     "devConfig"         

#define IOTGO_STRING_USER1BIN       "user1.bin"
#define IOTGO_STRING_USER2BIN       "user2.bin"

#define IOTGO_STRING_TIMERS     "timers"
#define IOTGO_STRING_STARTUP     "startup"
#define IOTGO_STRING_SWITCH     "switch"
#define IOTGO_STRING_SWITCHES   "switches"
#define IOTGO_STRING_OUTLET     "outlet"
#define IOTGO_STRING_OFF        "off"
#define IOTGO_STRING_ON         "on"
#define IOTGO_STRING_AT         "at"
#define IOTGO_STRING_ENABLED    "enabled"
#define IOTGO_STRING_TYPE       "type"
#define IOTGO_STRING_ONCE       "once"
#define IOTGO_STRING_REPEAT     "repeat"
#define IOTGO_STRING_POWER      "power"
#define IOTGO_STRING_SPEED      "speed"
#define IOTGO_STRING_SHAKE      "shake"
#define IOTGO_STRING_STOP       "stop"
#define IOTGO_STRING_EVENT      "event"
#define IOTGO_STRING_NULL      "null"
#define IOTGO_STRING_STAY      "stay"

/*added for ifan1 */
#define IOTGO_STRING_FAN       "fan"
#define IOTGO_STRING_MODE      "mode"

/* added for pwm led */
#define IOTGO_STRING_CHANNEL0      "channel0"
#define IOTGO_STRING_CHANNEL1      "channel1"
#define IOTGO_STRING_CHANNEL2      "channel2"
#define IOTGO_STRING_CHANNEL3      "channel3"
#define IOTGO_STRING_CHANNEL4      "channel4"
#define IOTGO_STRING_STATE         "state"
#define IOTGO_STRING_STATE_ON      "on"
#define IOTGO_STRING_STATE_OFF     "off"
#define IOTGO_STRING_PWM_VALUE_INIT "0"

/* zlc add for PSX timers */
#define IOTGO_STRING_DO   "do"
#define IOTGO_STRING_START_DO   "startDo"
#define IOTGO_STRING_END_DO     "endDo"

#define IOTGO_STRING_D_SEQ      "d_seq"
#define IOTGO_STRING_ID          "id"
#define IOTGO_STRING_TIMERACT    "timerAct"
#define IOTGO_STRING_NEW         "new"
#define IOTGO_STRING_TIMER       "timer"
#define IOTGO_STRING_TIMERID     "timerID"
#define IOTGO_STRING_EDIT        "edit"
#define IOTGO_STRING_DEL         "del"
#define IOTGO_STRING_DELALL      "delAll"
#define IOTGO_STRING_ENABLE      "enable"
#define IOTGO_STRING_DISABLE     "disable"
#define IOTGO_STRING_ENABLEALL   "enableAll"
#define IOTGO_STRING_DISABLEALL  "disableAll"
#define IOTGO_STRING_DURATION    "duration"
#define IOTGO_STRING_TIMERINFO   "timerInfo"
#define IOTGO_STRING_TIMERINDEX  "timerIndex"
#define IOTGO_STRING_TIMERID     "timerID"
#define IOTGO_STRING_TIMERCNT    "timerCnt"
#define IOTGO_STRING_TIMERVER    "timerVer"
#define IOTGO_STRING_QUERYDEV    "queryDev"
#define IOTGO_STRING_TCREQ_PERIOD "tcReqPeriod"
#define IOTGO_STRING_TCREQMAX     "tcReqMax"


#define SLAVE_FRAME_START               (0xA0)
#define SLAVE_FRAME_CTRL                (0x00)
#define SLAVE_FRAME_DATA                (0x00)
#define SLAVE_FRAME_STOP                (0xA1)
#define SLAVE_FRAME_RESP                (0xA2)
#define SLAVE_FRAME_CTRL_QUERY          (0xF4)
#define SLAVE_FRAME_CTRL_ENTER_SETTING  (0xF5)
#define SLAVE_FRAME_CTRL_EXIT_SETTING   (0xF6)
#define SLAVE_FRAME_CTRL_ENTER_TEST     (0xF7)
#define SLAVE_FRAME_CTRL_TEST_OK        (0xF8)
#define SLAVE_FRAME_CTRL_TEST_ERR       (0xF9)

/*
 *******************************************************************************
 *
 *                              !!! 严重警告 !!!
 * 
 * !!! 以下内容绝对不能碰，除非你真的知道你在做什么而且愿意承担后果 !!!
 *
 *******************************************************************************
 */

/*
 * 不要修改该值，否则升级后的设备全部瘫痪
 */
#define IOTGO_FLASHED_MAGIC_NUMBER        (0x17EAD000)

#define iotgoPrintHeapSize() \
    iotgoInfo("heap size = %u Bytes", system_get_free_heap_size())

#define iotgoPrintClock() \
    iotgoInfo("time = %u us [%s, %d]", system_get_time(), __FUNCTION__, __LINE__)


#endif /* #ifndef __IOTGO_DEFINE_H__ */
