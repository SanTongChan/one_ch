#include "iotgo.h"

static void ICACHE_FLASH_ATTR cbUserInitDone(void)
{
    iotgoInfo("\n----------------------------------------");
    iotgoInfo("bin number:%u", system_upgrade_userbin_check());
    iotgoInfo("rst reason:%u", iotgoBootReason());
    iotgoInfo("sdk version:%s", system_get_sdk_version());
    iotgoInfo("uart0 baudrate:%u", IOTGO_UART0_BAUDRATE);
    iotgoInfo("uart1 baudrate:%u", IOTGO_UART1_BAUDRATE);
    iotgoInfo("fw version:%s", IOTGO_FM_VERSION);
    iotgoInfo("magic_number = [0x%X]", iotgoMagicNumber());
    iotgoInfo("deviceid = [%s]", iotgo_flash_param.factory_data.deviceid);
    iotgoInfo("device_model = [%s]", iotgo_flash_param.factory_data.device_model);
    iotgoInfo("distorhost = [%s]", iotgo_flash_param.iot_distributor.host);
    iotgoInfo("distorport = [%d]", iotgo_flash_param.iot_distributor.port);
    iotgoInfo("distorlastip = [0x%08X]", iotgo_flash_param.iot_distributor_last_ip);
    iotgoInfo("new_bin_info = [%s, %s, %u]", 
        iotgo_flash_param.new_bin_info.name, 
        iotgo_flash_param.new_bin_info.version,
        iotgo_flash_param.new_bin_info.length);
    
    system_print_meminfo();
    iotgoPrintHeapSize();
    iotgoInfo("----------------------------------------\n");
}

void ICACHE_FLASH_ATTR user_init(void)
{   
    static os_event_t task_iotgo_device_center_q[IOTGO_TASK_DEVICE_CENTER_QLEN];

    iotgoFlashLoadParam(&iotgo_flash_param);

    /* No factory data flashed and goto test mode. */
    if (IOTGO_FLASHED_MAGIC_NUMBER != iotgoMagicNumber())
    {
        iotgoRegisterCallbackSet(&iotgo_device);
        iotgoTestModeStart();
        return;
    }
    
    /* GPIO init */
    if (!iotgoIsXrstKeepGpio())
    {
        gpio_init();
    }

    /* UART init */
    uart_init(IOTGO_UART0_BAUDRATE, IOTGO_UART1_BAUDRATE);
    
    iotgoRegisterCallbackSet(&iotgo_device);
    
    if (iotgo_device.earliestInit)
    {
        iotgo_device.earliestInit();
    }

    iotgoInfo("\nuser_init begin\n");
    system_init_done_cb(cbUserInitDone);
    
    iotgoWifiInit();
    iotgoSetDeviceMode(DEVICE_MODE_START);
    iotgoSledStart();
    
    if (!iotgoFlashDecipher())
    {
        iotgoError("You are thief!");
        while(1);
    }
    
    if (iotgo_device.postCenter)
    {
        system_os_task(iotgo_device.postCenter, IOTGO_DEVICE_CENTER, 
            task_iotgo_device_center_q, IOTGO_TASK_DEVICE_CENTER_QLEN);
    }
    else
    {
        iotgoError("No device post center! Halt now!\n");
        while(1);
    }

    if (iotgo_device.earlyInit)
    {
        iotgo_device.earlyInit();
    }
    
    iotgoCoreRun();

    iotgoInfo("\nuser_init done\n");
}

