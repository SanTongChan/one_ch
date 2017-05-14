#include "iotgo_queue.h"
#include "iotgo_global.h"

static linkQueue send_data_quene;
static linkQueue *q = &send_data_quene;

const char* ICACHE_FLASH_ATTR iotgoQueueHeadData(void)
{
    return q->head->data;
}

uint8_t ICACHE_FLASH_ATTR iotgoQueueHeadType(void)
{
    return q->head->type;
}

uint8_t ICACHE_FLASH_ATTR iotgoQueueLength(void)
{
    return q->length;
}

//向队列插入一个结点  
static bool ICACHE_FLASH_ATTR iotgoInsertNode(char *data,uint8_t type)  
{  
    uint16 len = 0;
    if(NULL == q || NULL == data)
    {
        iotgoError("linkQuenu and data is NULL,please check");
        return false;
    }
    if(q->length >= 10)
    {
        iotgoInfo("the quene is overflow,wait a moment");
        return false;
    }
    qNode *node;  
    char *data_temp = NULL;
    if(type != IOTGO_PING)
    {
        data_temp = (char *)os_malloc(os_strlen(data) + 1);
        os_strcpy(data_temp,data);
    }
    else
    {
        len = 6;
        data_temp = (char *)os_malloc(len);
        os_memcpy(data_temp,data,len);
    }
    node = (qNode*)os_malloc(sizeof(qNode));  
    node->data = data_temp;  
    node->type = type;
    node->next = NULL;  
    if(q->tail == NULL)  
    {  
        q->head = node;  
        q->tail = node;  
        q->length = 1;
    }  
    else  
    {  
        q->tail->next = node;  
        q->tail = node;  
        q->length++;
    }  
    return true;  
}  
  
//从队列删除一个结点  
static bool ICACHE_FLASH_ATTR iotgoDeleteType(uint8_t type)  
{
    if(NULL == q)
    {
        iotgoError("linkQueue is NULL ,please check");
        return false;
    }
    qNode *node;  
    if(q->head == NULL)   
    {  
        iotgoInfo("the queue is empty");  
        return true;  
    }  
    node = q->head;
    if(node->type == type)
    {
        if(!iotgoQueueDeleteHead())
        {
            iotgoInfo("delete node error");
            return false;
        }
        return true;
    }
    while(node->next)
    {
        if(node->next->type == type)
        {
            qNode *node_temp = node->next;
            node->next = node->next->next;
            if(NULL == node->next)
            {   
                q->tail = node;
            }
            os_free(node_temp->data);
            node_temp->data = NULL;
            os_free(node_temp);
            node_temp = NULL;
            q->length--;
            break;
        }
        node = node->next;
    }
    return true;
    
}
//删除头结点
bool ICACHE_FLASH_ATTR iotgoQueueDeleteHead(void)  
{  
    if(NULL == q)
    {
        iotgoError("linkQueue is NULL ,please check");
        return false;
    }
    qNode *node;  
    if(q->head == NULL)   
    {  
        iotgoInfo("the queue is empty");  
        return true;  
    }  
      
    node = q->head;  
    if(q->head == q->tail)  
    {  
        q->head = NULL;  
        q->tail = NULL;
        os_free(node->data);
        os_free(node);  
        q->length = 0;
    }
    else  
    {  
        q->head = q->head->next;
        os_free(node->data);
        os_free(node);  
        q->length--;
    }  
    return true;  
}  
  
//顺序打印队列  
static bool ICACHE_FLASH_ATTR iotgoPrintLinkQueue(void)  
{  
    uint8_t couter = 0;
    if(NULL == q)
    {
        iotgoError("linkQueue is NULL ,please check");
        return false;
    }
    qNode *node;  
    node = q->head;  
    iotgoInfo("length = %d,and queue data is [",q->length);
    while(node)  
    {   
        couter++;
        if(node->type != IOTGO_PING)
        {
            iotgoInfo(" index = %d  -> %s",couter,node->data);
        }
        else
        {
            iotgoInfo("send ping ok");
        }
        node=node->next;  
    }  
    iotgoInfo("]");
    return true;
}  

bool ICACHE_FLASH_ATTR iotgoQueueDeleteAll(void)
{
    uint8_t i = 0;
    uint8_t length = 0;
    if (NULL == q)
    {
        iotgoError("linkQueue is NULL ,please check");
        return false;
    }
    length = q->length;
    for(i = 0; i < length; i++)
    {
        if(!iotgoQueueDeleteHead())
        {
            iotgoError("delete node is error,please check");
            return false;
        }
    }
    return true;
}
bool ICACHE_FLASH_ATTR iotgoQueueAdd(char *data,uint8_t type,bool del_type_flag)  
{
    uint8 length_before_delete = 0;
    if(del_type_flag)
    {
        length_before_delete = q->length;
        if(!iotgoDeleteType(type))
        {
            iotgoInfo("delete iotgo_switch error");
            return false;
        }
    }
    if(!iotgoInsertNode(data,type))
    {
        iotgoInfo("insert node error");
        return false;
    }
    if(!iotgoPrintLinkQueue())
    {
        iotgoInfo("print queue error");
        return false;
    }
    if(q->length == 1 && length_before_delete == 0)
    {
        system_os_post(IOTGO_CORE, MSG_CORE_SEND_JSON, 0);
    }
    return true;
}


