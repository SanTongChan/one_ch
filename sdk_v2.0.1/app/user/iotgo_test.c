#include "iotgo_test.h"

#define TEST_START_FLASH_TIMER_DELAY      (1000)

#define IOTGO_UART0_RX_BUFFER_SIZE  (50)
static uint8 uart0_rx_buffer[IOTGO_UART0_RX_BUFFER_SIZE];
static uint8 iotgo_rx_uart0_buffer_index = 0;
static os_timer_t read_uart_timer;
static os_timer_t test_dht11_timer;
static os_timer_t join_test_ap_timer;
static os_timer_t start_flash_timer;
static os_timer_t double_mcu_timer;
static os_timer_t send_ok_timer;
static uint8_t test_counter = 0;
static uint8_t test_mode_flag = 0;
static uint8_t double_mcu_flag = 0;
static uint32 hlw8012_cf_cnt = 0;
static os_timer_t hlw8012_timer;
struct station_config test_sta_config = {0}; 


#define IOTGO_UART0_TX_BUFFER_SIZE  (50)
#define PSC_HLW8012_POWER_INPUT_GPIO                (14)
#define PSC_HLW8012_POWER_INPUT_GPIO_NAME           (PERIPHS_IO_MUX_MTMS_U)
#define PSC_HLW8012_POWER_INPUT_GPIO_FUNC           (FUNC_GPIO14)
#define PSC_HLW8012_POWER_INPUT_GPIO_EINT_METHOD    (GPIO_PIN_INTR_NEGEDGE)

static uint8 tx_buffer[IOTGO_UART0_TX_BUFFER_SIZE];

#define TEST_DHT11_DATA_GPIO         (14)
#define TEST_DHT11_DATA_GPIO_NAME    (PERIPHS_IO_MUX_MTMS_U)
#define TEST_DHT11_DATA_GPIO_FUNC    FUNC_GPIO14

static void ICACHE_FLASH_ATTR wifiLeaveFromTestAP(void)
{
    test_counter = 0;
    os_timer_disarm(&join_test_ap_timer);
    if (wifi_station_disconnect())
    {
        iotgoInfo("leave ap ok\n");
    }
    else
    {
        iotgoWarn("leave ap err!\n");
    }
}

static void ICACHE_FLASH_ATTR joinTestAPStage2(void *pdata)
{
    static struct ip_info temp_ip;

    uint8 jap_state; 

    test_counter++;
    if (test_counter > 4)
    {
        iotgoInfo("try times = %u\r\n", test_counter);
    }
    
    jap_state = wifi_station_get_connect_status();
    if(STATION_GOT_IP == jap_state)
    {
        os_bzero(&temp_ip, sizeof(temp_ip));
        wifi_get_ip_info(STATION_IF, &temp_ip);
        iotgoInfo("Test STA IP:\"%d.%d.%d.%d\"\r\n", IP2STR(&temp_ip.ip));
        os_strcpy(tx_buffer,"AT+SIGNAL=");
        sint8 rssi = wifi_station_get_rssi();
        if (rssi < 0)
        {
            iotgoInfo("\"SIG\":%d", rssi);   
            tx_buffer[os_strlen(tx_buffer)] = rssi;
        }
        else
        {
            iotgoWarn("wifi_station_get_rssi err!");
            tx_buffer[os_strlen(tx_buffer)] = 0x00;
        }
        os_strcat(tx_buffer,"\r\n");
        uart0_tx_buffer(tx_buffer, os_strlen(tx_buffer));
        test_counter = 0;
        os_timer_disarm(&join_test_ap_timer);
        os_memset(tx_buffer,'\0',IOTGO_UART0_TX_BUFFER_SIZE);
        return;
    }
    else if(test_counter >= 10)
    {
        wifiLeaveFromTestAP();
        os_strcpy(tx_buffer,"AT+SIGNAL=");
        tx_buffer[os_strlen(tx_buffer)] = 0x00;
        os_strcat(tx_buffer,"\r\n");
        uart0_tx_buffer(tx_buffer, os_strlen(tx_buffer));
        test_counter = 0;        
        os_timer_disarm(&join_test_ap_timer);
        os_memset(tx_buffer,'\0',IOTGO_UART0_TX_BUFFER_SIZE);
        return;
    }
}


static void ICACHE_FLASH_ATTR joinTestAPTimerCallback(void *arg)
{
    wifiLeaveFromTestAP();
    if (wifi_station_set_config(&test_sta_config))
    {
        iotgoInfo("sta config ok\n");
    }
    else
    {
        iotgoWarn("sta config err!\n");
    }
    
    if (wifi_station_connect())
    {
        iotgoInfo("wifi_station_connect ok\n");
        os_timer_disarm(&join_test_ap_timer);
        os_timer_setfn(&join_test_ap_timer, (os_timer_func_t *)joinTestAPStage2, NULL);
        os_timer_arm(&join_test_ap_timer, 2000, 1);
    }
    else
    {
        iotgoWarn("wifi_station_connect err!\n");
    }
}

static void ICACHE_FLASH_ATTR wifiJoinTestAP(void)
{
    os_timer_disarm(&join_test_ap_timer);
    os_timer_setfn(&join_test_ap_timer, (os_timer_func_t *)joinTestAPTimerCallback, NULL);
    os_timer_arm(&join_test_ap_timer, 1000, 0);
    os_memset(tx_buffer,'\0',IOTGO_UART0_TX_BUFFER_SIZE);
}
static void ICACHE_FLASH_ATTR uart0RxBufferFlush(void)
{
    uart0_rx_flush();
    os_memset(uart0_rx_buffer, '\0' , IOTGO_UART0_RX_BUFFER_SIZE);
    iotgo_rx_uart0_buffer_index = 0;
}
static void ICACHE_FLASH_ATTR startFlash5sTimer(void *arg)
{
    os_timer_disarm(&start_flash_timer);
    if(!test_mode_flag)
    {
        os_timer_disarm(&read_uart_timer);
        uart0RxBufferFlush();
        iotgoSledStart();
        startFlashDataMode();
    }
}

static void ICACHE_FLASH_ATTR doubleMcuCallback(void *arg)
{
    os_timer_disarm(&double_mcu_timer);
    double_mcu_flag = 0;
    uart0_tx_string("OK\r\n");
}

static void ICACHE_FLASH_ATTR testModeInit(void)
{
    PIN_FUNC_SELECT(TEST_DHT11_DATA_GPIO_NAME, TEST_DHT11_DATA_GPIO_FUNC);
    GPIO_OUTPUT_SET(TEST_DHT11_DATA_GPIO, GPIO_LOW);
    iotgoWifiToStationMode();
    os_memset(uart0_rx_buffer, '\0' , IOTGO_UART0_RX_BUFFER_SIZE);
    uart0_rx_flush();
    /* ÅäÖÃ²âÊÔWiFi */
    os_strcpy(test_sta_config.ssid, IOTGO_DEVICE_TEST_SSID);
    os_strcpy(test_sta_config.password, IOTGO_DEVICE_TEST_PASS);
    os_timer_disarm(&start_flash_timer);
    os_timer_setfn(&start_flash_timer, (os_timer_func_t *)startFlash5sTimer, NULL);
    os_timer_arm(&start_flash_timer, TEST_START_FLASH_TIMER_DELAY, 0);
    uart0_tx_string("\r\n");

}

static void ICACHE_FLASH_ATTR testDht11Timer(void *arg)
{
    if(!test_mode_flag)
    {
        if(GPIO_INPUT_GET(TEST_DHT11_DATA_GPIO) == GPIO_LOW) 
        {
            GPIO_OUTPUT_SET(TEST_DHT11_DATA_GPIO, GPIO_HIGH);
        }
        else
        {
            GPIO_OUTPUT_SET(TEST_DHT11_DATA_GPIO, GPIO_LOW);
        }
    }
}


static void ICACHE_FLASH_ATTR setGpioLow(void)
{
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);
    GPIO_OUTPUT_SET(0, 0x0);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
    GPIO_OUTPUT_SET(12, 0x0);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
    GPIO_OUTPUT_SET(13, 0x0);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
    GPIO_OUTPUT_SET(14, 0x0);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
    GPIO_OUTPUT_SET(15, 0X0);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
    GPIO_OUTPUT_SET(4, 0X0);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
    GPIO_OUTPUT_SET(5, 0X0);
    if(IOTGO_MODULE_ID_PSF == iotgoGetModuleIdentifier())
    {
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA2_U, FUNC_GPIO9);
        GPIO_OUTPUT_SET(9, 0X0);
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA3_U, FUNC_GPIO10);
        GPIO_OUTPUT_SET(10, 0X0);
    }
    
}

static void ICACHE_FLASH_ATTR setGpioHigh(void)
{
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);
    GPIO_OUTPUT_SET(0, 0x1);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO12);
    GPIO_OUTPUT_SET(12, 0x1);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_GPIO13);
    GPIO_OUTPUT_SET(13, 0x1);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
    GPIO_OUTPUT_SET(14, 0x1);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
    GPIO_OUTPUT_SET(15, 0x1);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);
    GPIO_OUTPUT_SET(4, 0X1);
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
    GPIO_OUTPUT_SET(5, 0X1);
    if(IOTGO_MODULE_ID_PSF == iotgoGetModuleIdentifier())
    {
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA2_U, FUNC_GPIO9);
        GPIO_OUTPUT_SET(9, 0X1);
        PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA3_U, FUNC_GPIO10);
        GPIO_OUTPUT_SET(10, 0X1);
    }
}
static void ICACHE_FLASH_ATTR respondToMcu(void)
{
    if(double_mcu_flag)
    {
        uart0_tx_buffer(uart0_rx_buffer,os_strlen(uart0_rx_buffer));
    }
    else
    {
        uart0_tx_string("OK\r\n");
    }
    uart0RxBufferFlush();
}
static void ICACHE_FLASH_ATTR sendOk500msTimer(void *arg)
{
    uart0_tx_string("OK\r\n");
    iotgoInfo("send ok ok\n");
}
static void ICACHE_FLASH_ATTR cbHLW8012Timer1ms(void *arg)
{
    static uint32 hlw8012_1ms_cnt = 0; 
    static uint8 timer_count = 0;
    hlw8012_1ms_cnt++;

    if (hlw8012_1ms_cnt >= 1000)
    { 
        iotgoInfo("CF Freq:%u", hlw8012_cf_cnt);
        iotgoInfo("timer_count: %u",timer_count);
        timer_count++;
        if(hlw8012_cf_cnt > 15 && hlw8012_cf_cnt < 40)
        {
            os_timer_disarm(&hlw8012_timer);
            ETS_GPIO_INTR_DISABLE();
            GPIO_OUTPUT_SET(PSC_HLW8012_POWER_INPUT_GPIO,0x0);
            respondToMcu();
        }
        else if(timer_count == 2)
        {
            os_timer_disarm(&hlw8012_timer);
            ETS_GPIO_INTR_DISABLE();
            gpio_pin_intr_state_set(GPIO_ID_PIN(PSC_HLW8012_POWER_INPUT_GPIO), GPIO_PIN_INTR_DISABLE); 
            GPIO_OUTPUT_SET(PSC_HLW8012_POWER_INPUT_GPIO,0x0);
            timer_count = 0;
        }
        hlw8012_cf_cnt = 0;
        hlw8012_1ms_cnt = 0;
    }
}

static void keyISR(void *pdata)
{
    uint32 gpio_status;
    ETS_GPIO_INTR_DISABLE();
    
    gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);

    if (gpio_status & BIT(PSC_HLW8012_POWER_INPUT_GPIO))
    {
        hlw8012_cf_cnt++;
    }
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
    ETS_GPIO_INTR_ENABLE();
}

static void ICACHE_FLASH_ATTR interruptInit(void)
{
    ETS_GPIO_INTR_DISABLE();
    ETS_GPIO_INTR_ATTACH(keyISR, (void *)PSC_HLW8012_POWER_INPUT_GPIO);

    PIN_FUNC_SELECT(PSC_HLW8012_POWER_INPUT_GPIO_NAME, PSC_HLW8012_POWER_INPUT_GPIO_FUNC);
    GPIO_DIS_OUTPUT(PSC_HLW8012_POWER_INPUT_GPIO); /* Set as input pin */
    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(PSC_HLW8012_POWER_INPUT_GPIO)); /* clear status */
    gpio_pin_intr_state_set(GPIO_ID_PIN(PSC_HLW8012_POWER_INPUT_GPIO), PSC_HLW8012_POWER_INPUT_GPIO_EINT_METHOD); /* enable interrupt */

    ETS_GPIO_INTR_ENABLE();
}

static void ICACHE_FLASH_ATTR readUartBuffer50msTimer(void *arg)
{
    while(uart0_rx_available() > 0)
    {
        uint8 c = uart0_rx_read();
        uart0_rx_buffer[iotgo_rx_uart0_buffer_index] = c;
        iotgo_rx_uart0_buffer_index++;
    }
    if(NULL != (char *)os_strstr(uart0_rx_buffer, "\r\n"))
    {
        if(0 == os_strcmp(uart0_rx_buffer,"AT+START\r\n"))
        {
            test_mode_flag = 1;
            double_mcu_flag = 1;
            iotgoInfo("send start ok");
            uart0_tx_buffer(uart0_rx_buffer,os_strlen(uart0_rx_buffer));
            uart0RxBufferFlush();
            os_timer_disarm(&double_mcu_timer);
            os_timer_setfn(&double_mcu_timer, (os_timer_func_t *)doubleMcuCallback, NULL);
            os_timer_arm(&double_mcu_timer, 100, 0);
        }
        else if( 0 == os_strcmp(uart0_rx_buffer,"AT+GPIO_LOW\r\n"))
        {
            setGpioLow();
            respondToMcu();
            iotgoInfo("send GPIO_LOW ok\n");
        }
        else if(0 == os_strcmp(uart0_rx_buffer,"AT+GPIO_HIGH\r\n"))
        {
            setGpioHigh();
            respondToMcu();
            iotgoInfo("send GPIO_HIGH ok\n");
        }
        else if(0 == os_strcmp(uart0_rx_buffer,"AT+FREQ\r\n"))
        {
            interruptInit();
            os_timer_disarm(&hlw8012_timer);
            os_timer_setfn(&hlw8012_timer, (os_timer_func_t *)cbHLW8012Timer1ms, NULL);
            os_timer_arm(&hlw8012_timer, 1, 1);
            uart0RxBufferFlush();
            
        }
        else if(0 == os_strcmp(uart0_rx_buffer,"OK\r\n"))
        {
            os_timer_disarm(&double_mcu_timer);
            uart0RxBufferFlush();
            os_timer_disarm(&send_ok_timer);
            os_timer_setfn(&send_ok_timer, (os_timer_func_t *)sendOk500msTimer, NULL);
            os_timer_arm(&send_ok_timer, 500, 0);
        }
        else if(0 == os_strcmp(uart0_rx_buffer,"AT+WIFI\r\n"))
        {
            uart0RxBufferFlush();
            wifiJoinTestAP();
        }
        else if(0 == os_strcmp(uart0_rx_buffer,"AT+END\r\n"))
        {
            os_timer_disarm(&read_uart_timer);
            if(double_mcu_flag)
            {
                uart0_tx_buffer(uart0_rx_buffer,os_strlen(uart0_rx_buffer));
            }
            uart0RxBufferFlush();
            iotgoSledStart();
            startFlashDataMode();
        }
        else
        {
             uart0RxBufferFlush();
        }
    }
}

static void ICACHE_FLASH_ATTR testMode50msTimerInit(void)
{
    os_timer_disarm(&read_uart_timer);
    os_timer_setfn(&read_uart_timer, (os_timer_func_t *)readUartBuffer50msTimer, NULL);
    os_timer_arm(&read_uart_timer, 50, 1);
    os_timer_disarm(&test_dht11_timer);
    os_timer_setfn(&test_dht11_timer, (os_timer_func_t *)testDht11Timer, NULL);
    os_timer_arm(&test_dht11_timer, 500, 1);
}

void ICACHE_FLASH_ATTR iotgoTestModeStart(void)
{
    gpio_init();
    uart_init(19200, IOTGO_UART1_BAUDRATE);
    testModeInit();
    testMode50msTimerInit();
}

