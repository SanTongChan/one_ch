#include "addition/Spro.h"

static uint16 ICACHE_FLASH_ATTR serverRxAvailable(Spro *obj);
static void ICACHE_FLASH_ATTR serverRxFlush(Spro *obj);
static void ICACHE_FLASH_ATTR serverRxRead(Spro *obj);
static void ICACHE_FLASH_ATTR serverRxWrite(Spro *obj, char *pdata,uint16_t len);
static int ICACHE_FLASH_ATTR indexOf(char *str1,char *str2);
static void ICACHE_FLASH_ATTR subString(char *dest,char *src,int start,int end);
static int ICACHE_FLASH_ATTR charToNum(Spro *obj, char *start , char *end);
static void ICACHE_FLASH_ATTR processHTTPPackage(Spro *obj, void *arg, char *pdata, unsigned short len);
static void ICACHE_FLASH_ATTR processWebsocketPackage(Spro *obj, void *arg, char *pdata, unsigned short len);
static void ICACHE_FLASH_ATTR processUpgradePackage(Spro *obj, void *arg, char *pdata, unsigned short len);

static void ICACHE_FLASH_ATTR startTime(Spro *obj)
{
    os_timer_arm(&obj->_timer, 10000, 0);
}

static void ICACHE_FLASH_ATTR spro10sTimerCallback(void *arg)
{
    Spro *obj = (Spro *)arg;
    if(NULL == obj)
    {
        return;
    }
    if(obj->_pkg_proc_cb)
    {
        obj->_pkg_proc_cb(obj->_arg,obj->_buffer,obj->_buffer_index,obj->_pkg_one_flag);
    }
    serverRxFlush(obj);
    os_timer_disarm(&obj->_timer);
    obj->_timer_opened_flag = false;
    iotgoDebug("timer is over\n");
}

static uint16 ICACHE_FLASH_ATTR serverRxAvailable(Spro *obj)
{
    return obj->_buffer_index;
}

static void ICACHE_FLASH_ATTR serverRxFlush(Spro *obj)
{
    obj->_buffer_index = 0;
    os_memset(obj->_buffer, '\0', obj->_buffer_size);
}

static void ICACHE_FLASH_ATTR serverRxRead(Spro *obj)
{
    // if the head isn't ahead of the tail, we don't have any characters
    uint16_t i = 0;
    uint16_t j = 0;
    if(obj->_pkg_len == 0)
    {
        return ;
    }
    else if(obj->_pkg_len == obj->_buffer_index)
    {
        serverRxFlush(obj);
    }
    else
    {
        for(i = obj->_pkg_len; i < obj->_buffer_index; i++)
        {
            obj->_buffer[j] = obj->_buffer[i];
            j++;
        }
        obj->_buffer_index = obj->_buffer_index - obj->_pkg_len;
        os_memset(&obj->_buffer[obj->_buffer_index], '\0', obj->_buffer_index);
    }
}

static void ICACHE_FLASH_ATTR serverRxWrite(Spro *obj, char *pdata,uint16_t len)
{   
    uint16_t i = 0;
    for(i =  0; i< len ; i++)
    {
        if(obj->_buffer_index < obj->_buffer_size)
        {
            obj->_buffer[obj->_buffer_index] = pdata[i];
            obj->_buffer_index++;
        }
        else
        {
            obj->_buffer_index = obj->_buffer_size;
            iotgoError("buffer is over\n");
            break;
        }
    }
}
static int ICACHE_FLASH_ATTR indexOf(char *str1,char *str2)  
{  
    char *p = NULL;    
    if(NULL == str1 || NULL == str2)
    {
        return -1;
    }
    p = strstr(str1,str2);  
    if(p == NULL)  
    {
        return -1;
    }
    
    return (p - str1);
}  
static void ICACHE_FLASH_ATTR subString(char *dest,char *src,int start,int end)  
{  
    int i=start;  
    if(start>os_strlen(src))
    {
        return;
    }
    if(end>os_strlen(src))
    {
        end=os_strlen(src);
    }
    while(i<end)  
    {     
        dest[i-start]=src[i];  
        i++;  
    }  
    dest[i-start]='\0';  
    return;  
}  
static int ICACHE_FLASH_ATTR charToNum(Spro *obj, char *start , char *end)
{
    uint8_t buffer[30];
    os_memset(buffer, '\0', 30);
    uint16_t i = 0;
    sint32 num = 0;
    int index1 = indexOf(obj->_buffer,start)+os_strlen(start);
    int index2 = indexOf(&(obj->_buffer[index1]),end) ;
    if(index2 > 10)
    {
        num = 0;
        iotgoDebug(" num = %d\n",num);
        return num;
    }
    subString(buffer,obj->_buffer,index1,(index2+index1));
    num = atoi(buffer);
    iotgoDebug(" num = %d\n",num);
    return num;
}

static void ICACHE_FLASH_ATTR processHTTPPackage(Spro *obj, void *arg, char *pdata, unsigned short len)
{
    static uint32 Recv_counter = 0;
    iotgoDebug("svrTCPRecvCallback Recv_counter = %u , length = %u \n ", ++Recv_counter,len);
    uint16_t i;
    sint16 temp2,temp4;
    serverRxWrite(obj,pdata,len);
    temp2 = indexOf(obj->_buffer,"\r\n\r\n");
    iotgoDebug("temp2 start = %d\n",temp2);
    if( temp2 == -1)
    {
        if(!obj->_timer_opened_flag)
        {            
            obj->_pkg_one_flag = false;
            obj->_timer_opened_flag = true;
            startTime(obj);
            iotgoDebug("resp -> data is not enough\n");
        }
    }
    while( temp2 != -1)
    {
        temp4 = indexOf(obj->_buffer,"Content-Length");
        if(  temp4 != -1 )
        {
            if( temp2 < temp4)
            {
                obj->_pkg_one_flag = true;
                obj->_pkg_len = indexOf(obj->_buffer,"\r\n\r\n") + 4;
                iotgoDebug("have 2 package,one_package_length = %d\n",obj->_pkg_len);
            }
            else
            {
                sint16_t length_temp = charToNum(obj,"Content-Length: ","\r\n");
                if( length_temp <= (serverRxAvailable(obj) - indexOf(obj->_buffer,"\r\n\r\n") - 4))
                {
                    obj->_pkg_one_flag = true;
                    obj->_pkg_len = length_temp + indexOf(obj->_buffer,"\r\n\r\n") + 4;
                    iotgoDebug("have 1 package,one_package_length = %d\n",obj->_pkg_len);
                }
                else
                {
                     obj->_pkg_one_flag = false;
                     iotgoDebug("have 1 package,and package is no enough \n");
                }
            }
        }
        else
        {
             obj->_pkg_one_flag = true;
             obj->_pkg_len = indexOf(obj->_buffer,"\r\n\r\n") + 4;
        }
        if(obj->_pkg_one_flag)
        {
            os_timer_disarm(&obj->_timer);
            obj->_timer_opened_flag = false;
            if(obj->_pkg_proc_cb)
            {
                obj->_pkg_proc_cb(arg,obj->_buffer,obj->_pkg_len,obj->_pkg_one_flag);
            }
            serverRxRead(obj);
            iotgoDebug("resp -> data is ok\n");
            iotgoDebug("buffer size is %d \n", serverRxAvailable(obj));
            if( serverRxAvailable(obj) > 0)
            {
                obj->_pkg_one_flag = false;
                startTime(obj);
            }
            temp2 = indexOf(obj->_buffer,"\r\n\r\n");          
        }
        else
        {
            if(!obj->_timer_opened_flag)
            {
                obj->_timer_opened_flag = true;
                startTime(obj);
                iotgoDebug("resp -> data is not enough\n");
            }
            break;
        }
        iotgoDebug("temp2 = %d\n",temp2);
   }
}
static void ICACHE_FLASH_ATTR processWebsocketPackage(Spro *obj, void *arg, char *pdata, unsigned short len)
{
        static uint32 Recv_counter = 0;
        uint16_t i;
        iotgoDebug("svrTCPRecvCallback Recv_counter = %u , length = %u \n ", ++Recv_counter,len);
        serverRxWrite(obj,pdata,len);
        if(obj->_buffer[0] != 0x81 && obj->_buffer[0] != 0x82 \
                                                && obj->_buffer[0] != 0x8a && obj->_buffer[0] != 0x89)
        {
            obj->_pkg_one_flag = false;
            if(obj->_pkg_proc_cb)
            {
                obj->_pkg_proc_cb(arg,obj->_buffer,obj->_buffer_index,obj->_pkg_one_flag);
            }
            serverRxFlush(obj);
            iotgoDebug("there is Not one of the three scenarios\n");
        }
        else
        {
            obj->_pkg_one_flag = true;
        }
        while(obj->_pkg_one_flag)
        {
            if(obj->_buffer[0] == 0x81 || obj->_buffer[0] == 0x82)
            {
                if(serverRxAvailable(obj) >= 2) 
                {
                    if( obj->_buffer[1] <= 125 )
                    {
                       if( serverRxAvailable(obj) < (obj->_buffer[1] + 2))
                        {
                            obj->_pkg_one_flag = false;
                            iotgoDebug("it is data_length < 125,but data is not enough\n");
                        }
                        else
                        {
                            obj->_pkg_one_flag = true;
                            obj->_pkg_len = obj->_buffer[1] + 2;
                            iotgoDebug("it is data_length < 125,and data is enough\n");
                        }
                    }
                    else if( obj->_buffer[1] == 126)
                    {
                        if( serverRxAvailable(obj) < 4 || ((obj->_buffer[2]*256 + obj->_buffer[3] +4) > serverRxAvailable(obj)))
                        {
                            obj->_pkg_one_flag = false;
                            iotgoDebug("it is data_length < 65536,but data is not enough\n");
                        }
                        else
                        {
                            obj->_pkg_one_flag = true;
                            obj->_pkg_len = obj->_buffer[2]*256 + obj->_buffer[3] +4;
                            iotgoDebug("it is data_length < 65536,and data is enough\n");
                        }
                    } 
                    else
                    {
                        iotgoDebug("data is too long\n");
                        obj->_pkg_one_flag = false;
                        if(obj->_pkg_proc_cb)
                        {
                            obj->_pkg_proc_cb(arg,obj->_buffer,obj->_buffer_index,obj->_pkg_one_flag);
                        }
                        serverRxFlush(obj);
                        break;
                    }
                }
                else  
                {
                    obj->_pkg_one_flag = false;
                    iotgoDebug("data length is < 2\n");
                }
            }
            else if( obj->_buffer[0] == 0x8a || obj->_buffer[0] == 0x89)
            {
                if(serverRxAvailable(obj) < 2)
                {
                    obj->_pkg_one_flag = false;
                    iotgoDebug("it is 0x8a/0x89, data length is < 2\n");
                }
                
                if(serverRxAvailable(obj) >= 2)
                {
                    if( obj->_buffer[1] == 0x00)
                    {
                        obj->_pkg_one_flag = true;
                        obj->_pkg_len = 2;
                        iotgoDebug("it is 0x8a/0x89, data is enough\n");
                    }
                    else
                    {
                        obj->_pkg_one_flag = false;
                        if(obj->_pkg_proc_cb)
                        {
                            obj->_pkg_proc_cb(arg,obj->_buffer,obj->_buffer_index,obj->_pkg_one_flag);
                        }
                        serverRxFlush(obj);
                        iotgoDebug("it is 0x8a/0x89, but data is error\n");
                        break;
                    }
                }
                                              
            }
            else  
            {
                 obj->_pkg_one_flag = false;
                 if(obj->_pkg_proc_cb)
                 {
                    obj->_pkg_proc_cb(arg,obj->_buffer,obj->_buffer_index,obj->_pkg_one_flag);
                 }  
                 serverRxFlush(obj);
                 iotgoDebug("there is Not one of the three scenarios\n");
                 break;
            }
    
            if( obj->_pkg_one_flag ) 
            {
                os_timer_disarm(&obj->_timer);
                obj->_timer_opened_flag = false;
                if(obj->_pkg_proc_cb)
                {
                    obj->_pkg_proc_cb(arg,obj->_buffer,obj->_pkg_len,obj->_pkg_one_flag);
                }
                serverRxRead(obj);
                iotgoDebug("websocket -> data is ok\n");
                iotgoDebug("buffer size is %d \n", serverRxAvailable(obj));
                if( serverRxAvailable(obj) > 0)
                {
                    if(obj->_buffer[0] != 0x81 && obj->_buffer[0] != 0x82 \
                                                && obj->_buffer[0] != 0x8a && obj->_buffer[0] != 0x89)
                    {
                         obj->_pkg_one_flag = false;
                         if(obj->_pkg_proc_cb)
                         {
                            obj->_pkg_proc_cb(arg,obj->_buffer,obj->_buffer_index,obj->_pkg_one_flag);
                         }
                         serverRxFlush(obj);
                    }
                    else
                    {
                        obj->_pkg_one_flag = true;
                        if(!obj->_timer_opened_flag)
                        {
                            obj->_timer_opened_flag = true;
                            startTime(obj);
                            iotgoDebug("websocket -> data is not enough\n");
                        }
                    }
                }
                else
                {
                    obj->_pkg_one_flag = false;
                }
            }
            else  
            {
                if(!obj->_timer_opened_flag)
                {
                    obj->_timer_opened_flag = true;
                    startTime(obj);
                    iotgoDebug("websocket -> data is not enough\n");
                }
            }
        }
        
}

bool ICACHE_FLASH_ATTR spSetPkgType(Spro *obj, SproPkgType pkg_type)
{
    if(NULL == obj)
    {
        return false;
    }
    if(obj->_pkg_type == pkg_type)
    {
        return true;
    }
    obj->_pkg_type = pkg_type;
    return true;
}

bool ICACHE_FLASH_ATTR spSetPkgProcCb(Spro *obj, SproPkgProcCb pkg_proc_cb)
{
    if(NULL == obj)
    {
        return false;
    }
    obj->_pkg_proc_cb = pkg_proc_cb;
    return true;
}

Spro* ICACHE_FLASH_ATTR spCreateObject(SproPkgType pkg_type, SproPkgProcCb pkg_proc_cb, uint16 buffer_size)
{
    Spro *spro_pkg = (Spro *)os_zalloc(sizeof(Spro) + buffer_size);
    if(NULL == spro_pkg)
    {
        return NULL;
    }
    spro_pkg->_pkg_type = pkg_type;
    spro_pkg->_pkg_proc_cb = pkg_proc_cb;
    spro_pkg->_buffer_size = buffer_size;
    spro_pkg->_buffer = (uint8 *)((uint32)spro_pkg + (uint32)sizeof(Spro));
    os_timer_disarm(&spro_pkg->_timer);
    os_timer_setfn(&spro_pkg->_timer, (os_timer_func_t *)spro10sTimerCallback, spro_pkg);
    spro_pkg->_buffer_index = 0;
    spro_pkg->_timer_opened_flag = false;
    spro_pkg->_pkg_one_flag = false;
    spro_pkg->_pkg_len = 0;
    return spro_pkg;
}

void ICACHE_FLASH_ATTR spReleaseObject(Spro *obj)
{
    if (obj)
    {
        os_timer_disarm(&obj->_timer);
        os_free(obj);    
    }
    
}

void ICACHE_FLASH_ATTR spTcpRecv(Spro *obj, void *arg, uint8 *pdata, uint16 len)
{
    if(NULL == obj || NULL == pdata)
    {
        return;
    }
    obj->_arg = arg;
    switch(obj->_pkg_type)
    {
        case SPRO_PKG_TYPE_HTTP11:
        {
            processHTTPPackage(obj,arg, pdata, len);
        }break;
        case SPRO_PKG_TYPE_WEBSOCKET13:
        {
            processWebsocketPackage(obj,arg, pdata, len);
        }break;
        case SPRO_PKG_TYPE_IOT_UPGRADE:
        {
            //processUpgradePackage(obj,arg, pdata, len);
            processHTTPPackage(obj,arg, pdata, len);
        }break;
        case SPRO_PKG_TYPE_NULL:
        {
            if(obj->_pkg_proc_cb)
            {
                obj->_pkg_proc_cb(arg,pdata,len,true);
            }
        }break;
        default:
        {
            if(obj->_pkg_proc_cb)
            {
                obj->_pkg_proc_cb(arg,pdata,len,true);
            }
        }
    }
}

