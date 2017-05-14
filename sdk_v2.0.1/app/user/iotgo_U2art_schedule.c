#include "iotgo_U2art_schedule.h"
#include "addition/cJSON.h"
#include "iotgo_debug.h"
#define  READ_UART0_BUFFER     (1024)

typedef struct{
    bool start;
    bool cf;
    uint32_t length;
    uint32_t extend_length;
    uint32_t crc32;
    cJSON *root;
}U2artData;

typedef struct{
    CMD *cmd;
    uint8_t length;
}CmdDef;

static U2artData u2art_data = {0};
U2artError u2art_result = U2ART_NULL;
static CmdDef u2art_cmd = {0};
static os_timer_t read_uart_timer;
static uint8_t uart0_buffer[READ_UART0_BUFFER] = {0};
bool crc_enable = false;

#define CF_LENGTH            (4)
#define EXTEND_LENGTH2       (2)
#define EXTEND_LENGTH4       (4)

#if 1//正规查表法
#define POLY 0xEDB88320L // CRC32生成多项式  
static unsigned int crc_table[16]={
    0x0,0x1db71064,0x3b6e20c8,0x26d930ac,
    0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
    0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,
    0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c
};  
unsigned int getHalfBytePoly(unsigned char data)  
{  
    int j = 0;
    unsigned int sum_poly = data;  
    for(j = 0; j < 4; j++)  
    {  
        int hi = sum_poly&0x01; // 取得reg的最高位  
        sum_poly >>= 1;  
        if(hi) sum_poly = sum_poly^POLY;  
    }  
    return sum_poly;  
}  
unsigned int getCrc32(unsigned char* data, int len)  
{  
    int i = 0;
    unsigned int reg = 0xFFFFFFFF;
    for(i = 0; i < len; i++)  
    {  
        reg = (reg>>4) ^ crc_table[(reg&0x0F) ^ (data[i] & 0x0f)];
        reg = (reg>>4) ^ crc_table[(reg&0x0F) ^ (data[i] >> 4)];
    }  
    return ~reg;  
}  
#endif

static bool ICACHE_FLASH_ATTR getExtendLength(void)
{
    bool  ret = false;
    if(u2art_data.length < 126 && u2art_data.length > 0)
    {
        u2art_data.extend_length = 0;
        ret = true;
    }
    else if(u2art_data.length == 126)
    {
        if(uart0_rx_available() >= EXTEND_LENGTH2)
        {
            uint16_t a = uart0_rx_read();
            uint8_t b = uart0_rx_read();
            u2art_data.extend_length = (a << 8) | b;
            ret = true;
        }
    }
    else if(u2art_data.length == 127)
    {
        if(uart0_rx_available() >= EXTEND_LENGTH4)
        {
            uint32_t a = uart0_rx_read();
            uint32_t b = uart0_rx_read();
            uint16_t c = uart0_rx_read();
            uint8_t d = uart0_rx_read();
            u2art_data.extend_length = (a << 24) | (b << 16) | (c << 8) | d;
            ret = true;
        }
    }
    else
    {
        ret = false;
        u2art_result = U2ART_FORMAT_ERROR;
    }
    return ret;
}
static bool ICACHE_FLASH_ATTR getData(void)
{
    uint32_t length = 0;
    uint32_t crc_result = 0;
    length = (u2art_data.length >= 126)?(u2art_data.extend_length):(u2art_data.length);
    uint32_t len = (true == u2art_data.cf)?(length + 4):(length);
    bool get_crc32_flag = false;
    uint32_t ret = 0;
    if(length > READ_UART0_BUFFER)
    {
        u2art_result = U2ART_FORMAT_ERROR;
        return false;
    }
    if((ret = uart0_rx_available()) >= len)
    {
        uint8_t i = 0;
        os_memset(uart0_buffer,0,sizeof(uart0_buffer));
        for(i = 0; i < length;i++)
        {
            if(u2art_data.cf && !get_crc32_flag)
            {
                uint32_t a = uart0_rx_read();
                uint32_t b = uart0_rx_read();
                uint16_t c = uart0_rx_read();
                uint8_t d = uart0_rx_read();
                u2art_data.crc32 = ((a << 24) | (b << 16) | (c << 8) | d);
                get_crc32_flag = true;
            }
            uint8_t e = uart0_rx_read();
            uart0_buffer[i] = e;
        }
        if(u2art_data.cf)
        {
            crc_result = getCrc32(uart0_buffer,length);
        }
        if(crc_result == u2art_data.crc32)
        {
            u2art_data.root = cJSON_Parse(uart0_buffer);
            if(NULL == u2art_data.root || cJSON_Array != u2art_data.root->type)
            {
                u2art_result = U2ART_FORMAT_ERROR;
                return false;
            }
            return true;
        }
        else
        {
            iotgoError("crc error ....................................");
            u2art_result = U2ART_CRC32_ERROR;
            return false;
        }
    }
    return false;
}
static bool ICACHE_FLASH_ATTR processCmd(void)
{
    bool ret = false;
    uint8_t i = 0;
    cJSON *cmd = NULL;
    uint8_t array_num = 0;
    if((NULL == u2art_data.root) 
        || (cJSON_Array != u2art_data.root->type)
        || (NULL == (cmd = cJSON_GetArrayItem(u2art_data.root,0))))
    {
        u2art_result = U2ART_FORMAT_ERROR;
        return false;
    }
    for(i = 0; i < u2art_cmd.length; i++)
    {
        if(0 == os_strcmp(u2art_cmd.cmd[i].cmd,cmd->valuestring))
        {
            if(u2art_cmd.cmd[i].action)
            {
                u2art_result = u2art_cmd.cmd[i].action(u2art_data.root);
                if(U2ART_SUCCESS != u2art_result)
                {
                    return false;
                }
                return true;
            }
            else
            {
                u2art_result = U2ART_INSIDE_ERROR;
                return false;
            }
        }
    }
    u2art_result = U2ART_NO_SUPPORT;
    return false;
}
static void ICACHE_FLASH_ATTR processErrorCmd(void)
{
    if(u2art_data.root != NULL)
    {
        cJSON_Delete(u2art_data.root);
        u2art_data.root = NULL;
    }
    os_memset(&u2art_data,0,sizeof(U2artData));
    u2art_data.start = false;
    switch(u2art_result)
    {
        case U2ART_NO_SUPPORT:
        {
            cmdSendRetToMcu("100",3);
        }break;
        case U2ART_FORMAT_ERROR:
        {
            cmdSendRetToMcu("101",3);
        }break;
        case U2ART_INSIDE_ERROR:
        {
            cmdSendRetToMcu("102",3);
        }break;
        case U2ART_INVALID_PARAMS:
        {
            cmdSendRetToMcu("103",3);
        }break;
        case U2ART_ILLEGAL_OPERATION:
        {
            cmdSendRetToMcu("104",3);
        }break;
        case U2ART_CRC32_ERROR:
        {
            cmdSendRetToMcu("105",3);
        }break;
        case U2ART_GMT_ERROR:
        {
            cmdSendRetToMcu("201",3);
        }break;
        case U2ART_NETWORK_ERROR:
        {
            cmdSendRetToMcu("202",3);
        }break;
        case U2ART_BUSY_ERROR:
        {
            cmdSendRetToMcu("106",3);
        }break;
        default :
        {
            /*do nothing*/
        }
    }
    u2art_result = U2ART_NULL;
}
static void  parseU2artData(void)
{
    static bool get_length = false;
    static uint8_t timerout_couter = 0;
    timerout_couter++;
    if(false == u2art_data.start)
    {
        while(uart0_rx_available() >= 3)
        {
            uint8_t a = uart0_rx_read();
            /*get 0xaa*/
            if(0xAA != a)
            {
                continue;
            }
            a = uart0_rx_read();
            /*get 0x55,get start signal*/
            if(0x55 == a)
            {
                timerout_couter = 0;
                u2art_data.start = true;
                a = uart0_rx_read();
                u2art_data.cf = a >> 7;
                u2art_data.length = a & 0x7f;
                break;
            }
        }
    }
    if(u2art_data.start)
    {   
        /*get u2art_data length*/
        if(!get_length)
        {
            get_length = getExtendLength();
            if(U2ART_NULL != u2art_result)
            {
                get_length = false;
                processErrorCmd();
            }
        }
        /*get length,will get data now*/
        if(get_length)
        {
            if(getData())
            {
                processCmd();
                get_length = false;
            }
            if(U2ART_NULL != u2art_result)
            {
                processErrorCmd();
            }
        }
    }
    if(timerout_couter >= TIMEROUT_TIME)
    {
        timerout_couter = 0;
        u2art_result = U2ART_NULL;
        processErrorCmd();
    }
}
static void ICACHE_FLASH_ATTR readUart0TimerHandler(void *arg)
{
    parseU2artData();
}
void ICACHE_FLASH_ATTR cmdProcessorStart(CMD *cmd,uint8_t length)
{
    u2art_cmd.cmd = cmd;
    u2art_cmd.length = length;
    os_timer_disarm(&read_uart_timer);
    os_timer_setfn(&read_uart_timer, (os_timer_func_t *)readUart0TimerHandler, NULL);
    os_timer_arm(&read_uart_timer, 100, 1);
}

bool ICACHE_FLASH_ATTR cmdSendRetToMcu(char *data,uint32_t length)
{
    if(NULL == data || length == 0)
    {
        return false;
    }
    uint8_t len[4] = {0};
    char *data_temp = NULL;
    uart_tx_one_char(0xAA);
    uart_tx_one_char(0x55);
    uint32_t data_len = length + 8;
    data_temp = (char *)os_malloc(data_len);
    if(NULL == data_temp)
    {
        iotgoError("memory is error");
        return false;
    }
    os_strcpy(data_temp,"[\"ret\",");
    os_strcat(data_temp,data);
    os_strcat(data_temp,"]");
    if(data_len <= 125)
    {
        (false == crc_enable)?(uart_tx_one_char((uint8_t)data_len)):(uart_tx_one_char((uint8_t)(data_len | 0x80)));
    }
    else if(data_len >= 126 && data_len <= 65535)
    {
        len[0] = data_len / 256;
        len[1] = data_len % 256;
        (false == crc_enable)?(uart_tx_one_char(0x7e)):(uart_tx_one_char(0xfe));
        uart0_tx_buffer(len,2);
    }
    else
    {
        len[0] = (data_len / 65536) / 256;
        len[1] = (data_len / 65536) % 256;
        len[2] = (data_len % 65536) / 256;
        len[3] = (data_len % 65536) % 256;
        (false == crc_enable)?(uart_tx_one_char(0x7f)):(uart_tx_one_char(0xff));
        uart0_tx_buffer(len,4);
    }
    if(true == crc_enable)
    {
        uint32_t crc_result = getCrc32(data_temp,data_len);
        len[0] = (crc_result / 65536) / 256;
        len[1] = (crc_result / 65536) % 256;
        len[2] = (crc_result % 65536) / 256;
        len[3] = (crc_result % 65536) % 256;
        uart0_tx_buffer(len,4);
    }
    uart0_tx_buffer(data_temp,data_len);
    os_free(data_temp);
    data_temp = NULL;
    return true;
}
bool ICACHE_FLASH_ATTR cmdSendDataToMcu(char *data,uint32_t length)
{
    if(NULL == data || length == 0)
    {
        return false;
    }
    uint8_t len[4] = {0};
    uart_tx_one_char(0xAA);
    uart_tx_one_char(0x55);
    uint32_t data_len = length;
    if(data_len <= 125)
    {
        (false == crc_enable)?(uart_tx_one_char((uint8_t)data_len)):(uart_tx_one_char((uint8_t)(data_len | 0x80)));
    }
    else if(data_len >= 126 && data_len <= 65535)
    {
        len[0] = data_len / 256;
        len[1] = data_len % 256;
        (false == crc_enable)?(uart_tx_one_char(0x7e)):(uart_tx_one_char(0xfe));
        uart0_tx_buffer(len,2);
    }
    else
    {
        len[0] = (data_len / 65536) / 256;
        len[1] = (data_len / 65536) % 256;
        len[2] = (data_len % 65536) / 256;
        len[3] = (data_len % 65536) % 256;
        (false == crc_enable)?(uart_tx_one_char(0x7f)):(uart_tx_one_char(0xff));
        uart0_tx_buffer(len,4);
    }
    if(true == crc_enable)
    {
        uint32_t crc_result = getCrc32(data,data_len);
        len[0] = (crc_result / 65536) / 256;
        len[1] = (crc_result / 65536) % 256;
        len[2] = (crc_result % 65536) / 256;
        len[3] = (crc_result % 65536) % 256;
        uart0_tx_buffer(len,4);
    }
    uart0_tx_buffer(data,length);
    return true;
}

