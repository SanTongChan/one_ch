#include "iotgo_sled.h"

static os_timer_t mode_indicator_timer;
static uint8 mode_indicator = DEV_SLED_OFF;

static inline void ICACHE_FLASH_ATTR modeIndicatorOn(void)
{
    GPIO_OUTPUT_SET(DEV_SLED_GPIO, DEV_SLED_ON);
    mode_indicator = DEV_SLED_ON;
}

static inline void ICACHE_FLASH_ATTR modeIndicatorOff(void)
{
    GPIO_OUTPUT_SET(DEV_SLED_GPIO, DEV_SLED_OFF);
    mode_indicator = DEV_SLED_OFF;
}

/*
TIME :  1  2  3  4  5  6  7  8  9  10  11  12  13  14  15  16  17  18  19  20
AP XX:  111111100000000000000000000000000000000000000000000000000000000000000    
AP OK:  111111100000011111111100000000000000000000000000000000000000000000000
INIT :  111111111111111111111111111111100000000000000000000000000000000000000
OK   :  111111111111111111111111111111111111111111111111111111111111111111111
SET  :  111111111111111100000000000000111111111111111111100000000000000000000
*/

static void ICACHE_FLASH_ATTR modeIndicatorTimerCallback(void *arg)
{
    static uint32 status_100ms_counter = 0;   
    static IoTgoDeviceMode old_mode = DEVICE_MODE_INVALID;
    static IoTgoDeviceMode mode = DEVICE_MODE_INVALID;

    status_100ms_counter++;
    mode = iotgoDeviceMode();
    
    if (old_mode != mode)
    {
        old_mode = mode;
        status_100ms_counter = 0;
    }

    if (status_100ms_counter >= 20)
    {
        status_100ms_counter = 0;
    }
    
    if (DEVICE_MODE_WORK_NORMAL == mode)
    {
        //if (DEV_SLED_ON != mode_indicator)        
        if(!normal_mode_led_off)
        {
            //modeIndicatorOn();
            if (DEV_SLED_ON != mode_indicator)
            {
                modeIndicatorOn();
            }
        }
        else
        {
            if(DEV_SLED_OFF != mode_indicator)
            {
                modeIndicatorOff();
            }
        }
    }
    else if (DEVICE_MODE_WORK_AP_OK == mode) 
    {
        if (1 == status_100ms_counter)
        {
            modeIndicatorOn();
        }
        else if (2 == status_100ms_counter)
        {
            modeIndicatorOff();
        }
        else if (3 == status_100ms_counter)
        {
            modeIndicatorOn();
        }
        else
        {
            modeIndicatorOff();
        }
        
    }
    else if (DEVICE_MODE_WORK_AP_ERR == mode)
    {
        if (1 == status_100ms_counter)
        {
            modeIndicatorOn();
        }
        else
        {
            modeIndicatorOff();
        }
    }
    else if (DEVICE_MODE_WORK_INIT == mode)
    {
        if ((status_100ms_counter % 10) == 0)
        {
            if (DEV_SLED_OFF == mode_indicator)
            {
                modeIndicatorOn();
            }
            else
            {
                modeIndicatorOff();
            }
        }
    }
    else if (DEVICE_MODE_SETTING == mode)
    {
        if (status_100ms_counter < 9)
        {
            if (DEV_SLED_ON != mode_indicator)
            {
                modeIndicatorOn();
            }
        }
        else
        {
            if ((status_100ms_counter % 2) == 0)
            {
                if (DEV_SLED_OFF == mode_indicator)
                {
                    modeIndicatorOn();
                }
                else
                {
                    modeIndicatorOff();
                }
            }
        }
    }
    else if (DEVICE_MODE_SETTING_SELFAP == mode)
    {
        if ((status_100ms_counter % 2) == 0)
        {
            if (DEV_SLED_OFF == mode_indicator)
            {
                modeIndicatorOn();
            }
            else
            {
                modeIndicatorOff();
            }
        }
    }
    else if (DEVICE_MODE_START == mode)
    {
        modeIndicatorOff();
    }
    else if (DEVICE_MODE_FACTORY == mode)
    {
        if (1 == status_100ms_counter)
        {
            modeIndicatorOn();
        }
        else if (2 == status_100ms_counter)
        {
            modeIndicatorOff();
        }
        else if (3 == status_100ms_counter)
        {
            modeIndicatorOn();
        }
        else if (4 == status_100ms_counter)
        {
            modeIndicatorOff();
        }
        else if (5 == status_100ms_counter)
        {
            modeIndicatorOn();
        }
        else if (6 == status_100ms_counter)
        {
            modeIndicatorOff();
        }
        else if (7 == status_100ms_counter)
        {
            modeIndicatorOn();
        }
        else if (8 == status_100ms_counter)
        {
            modeIndicatorOff();
        }
        else if (9 == status_100ms_counter)
        {
            modeIndicatorOn();
        }
        else
        {
            modeIndicatorOff();
        }
    }
    else
    {
        modeIndicatorOff();
    }
}

static void ICACHE_FLASH_ATTR initStatusIndicator(void)
{
    /* Init led set to high as default */
    PIN_FUNC_SELECT(DEV_SLED_GPIO_NAME, DEV_SLED_GPIO_FUNC);
    modeIndicatorOff();
}

void ICACHE_FLASH_ATTR iotgoSledStart(void)
{
    initStatusIndicator();
    os_timer_disarm(&mode_indicator_timer);
    os_timer_setfn(&mode_indicator_timer, (os_timer_func_t *)modeIndicatorTimerCallback, NULL);
    os_timer_arm(&mode_indicator_timer, 100, 1);
}

