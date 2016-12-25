#include "update_manager.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/udp.h"

#include "esp_ota_ops.h"
#include "esp_system.h"

#include "pin.h"

#include <string.h>
#include <stdint.h>

/***********************
 * Private Defines
 ***********************/
#define _RX_BUFFER_LENGTH   4096
#define _UPDATE_MANAGER_PORT 54322

#define _UPDATE_METADATA_MARKER "UPDM"
#define _UPDATE_DATA_MARKER "UPDD"

#define _PARTITION1 "ota_0"
#define _PARTITION2 "ota_1"


/***********************
 * Private Variables
 ***********************/
struct udp_pcb* pcb;
ip_addr_t update_manager_addr;
struct pbuf packet_buffer;


uint8_t _rx_buffer[_RX_BUFFER_LENGTH];
uint32_t _last_sequence_number = 0;
uint32_t _image_size_bytes = 0;
uint32_t _num_update_packets = 0;

bool _received_metadata = false;

esp_partition_t* _partition_to_load;
esp_ota_handle_t _ota_handle;

bool _update_complete = false;

//first packet we get from server on update, giving important meta data about the new image
typedef struct
{
    uint32_t marker;
    uint32_t sequence_number;  //THIS SHOULD ALWAYS BE 0 FOR UPDATE METADATA
    uint32_t image_size_bytes;
    uint32_t num_packets;
    uint32_t image_checksum;
}__attribute__((packed)) update_metadata_t;

//all image data will come in these packets
typedef struct
{
    uint32_t marker;
    uint32_t sequence_number;
    uint32_t chunk_size_bytes;
    uint8_t* image_chunk;
}__attribute__((packed)) update_data_t;

//This loop is entered when the device receives an update begin packet from the server.
//loop handles all tx/rx traffic, updating, setting new partition, restarting, etc.

void UpdateManager_RxCallback(void* arg, struct udp_pcb* upcb, struct pbuf* p, const ip_addr_t* remote_addr, u16_t port)
{
    //printf("update manager callback");
    //printf("payload length: %d", p->len);
    //printf("total length: %d", p->tot_len);
    uint8_t* buffer = (uint8_t*)p->payload;
    bool ok = false;

    if(memcmp(_UPDATE_METADATA_MARKER, buffer, 4) == 0)
    {
        //printf("found marker in update packet");
        if(_received_metadata)
        {
            //we already received meta data once, so there was an error somewhere and this is restarting the process
            esp_ota_end(_ota_handle);  //end previous op
        }
        //we need to get metadata
        update_metadata_t* metadata = (update_metadata_t*)buffer;
        if(metadata->sequence_number == 0)
        {
            _image_size_bytes = metadata->image_size_bytes;
            _num_update_packets = metadata->num_packets;

            printf("\r\nNew image size in bytes: %d\r\n", _image_size_bytes);
            printf("\r\nNum update packets required: %d\r\n", _num_update_packets);


            esp_partition_t* current_boot_partition = esp_ota_get_boot_partition();//esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, _PARTITION1);//esp_ota_get_boot_partition();
            if(current_boot_partition)
            {
                printf("\r\ncurrent boot partition name: %s\r\n", current_boot_partition->label);
                printf("\r\ncurrent boot partition address: %d\r\n", current_boot_partition->address);
                
                if(memcmp(current_boot_partition->label, _PARTITION1, sizeof(_PARTITION1)) == 0)
                {
                    //load to partition2
                    _partition_to_load = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, _PARTITION2);
                    esp_err_t load_result = esp_ota_begin(_partition_to_load, 0, &_ota_handle);
                    if(load_result != ESP_OK)
                    {
                        printf("\r\n couldn't load partition 2, reason: %d", load_result);
                        if(_partition_to_load)
                        {
                            printf("\r\npart size: %d\r\n", _partition_to_load->size);
                        }
                        ok = false;
                    }
                    else
                    {
                        printf("\r\nloading to partition name: %s\r\n", _partition_to_load->label);
                        printf("\r\nloading to partition address: %d\r\n", _partition_to_load->address);

                        _received_metadata = true;
                        ok = true;
                    }
                }
                else
                {
                    _partition_to_load = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, _PARTITION1);
                    esp_err_t load_result = esp_ota_begin(_partition_to_load, 0, &_ota_handle);
                    if(load_result != ESP_OK)
                    {
                        printf("\r\n couldn't load partition 1, reason: %d", load_result);
                        ok = false;
                    }
                    else
                    {
                        printf("\r\nloading to partition name: %s\r\n", _partition_to_load->label);
                        printf("\r\nloading to partition address: %d\r\n", _partition_to_load->address);

                        _received_metadata = true;
                        ok = true;
                    }
                }
            }
            else
            {
                printf("\r\nError: couldn't get current boot partition\r\n");
                ok = false;
            }
        }
    }
    else if(memcmp(_UPDATE_DATA_MARKER, buffer, 4) == 0)
    {
        //this should be an image packet so sequence number should be non zero
        update_data_t* update_data = (update_data_t*)buffer;
        if(update_data->sequence_number > 0)
        {
            ok = true;
            if(update_data->sequence_number == _last_sequence_number+1)
            {
                uint8_t* new_image_data = (uint8_t*)((uint32_t)&(update_data->image_chunk));
                esp_err_t write_res = esp_ota_write(_ota_handle, new_image_data, update_data->chunk_size_bytes);
                if(write_res != ESP_OK)
                {
                    printf("\r\nesp ota write failed. Reason: %d\r\n", write_res);
                }
                _last_sequence_number++;
                if(_last_sequence_number == _num_update_packets)
                {
                    printf("\r\nGOT LAST UPDATE PACKET!\r\n");
                    printf("\r\nDoing nothing....\r\n");
                    esp_ota_end(_ota_handle);
                    _update_complete = true;

                    esp_err_t espError = esp_ota_set_boot_partition(_partition_to_load);
                    if(espError != ESP_OK)
                    {
                        printf("\r\nEsp ota set boot partition failed. reason: %d\r\n", espError);
                    }
                    else
                    {
                        esp_restart();
                    }
                }
            }
            else
            {
                //request previous seq number
                printf("\r\nWARM: got out of order sequence number packet, ignored but acked");
            }
        }
        else
        {
            ok = false;
        }
    }

    if(ok)
    {
        memcpy(_rx_buffer, "OK", 2);
        packet_buffer.payload = _rx_buffer;
        packet_buffer.len = 2;
        packet_buffer.tot_len = 2;
        packet_buffer.type = PBUF_RAM;
        packet_buffer.ref = 1;
        udp_sendto(upcb, &packet_buffer, remote_addr, port);
    }
    else
    {
        memcpy(_rx_buffer, "ERROR", 5);
        packet_buffer.payload = _rx_buffer;
        packet_buffer.len = 5;
        packet_buffer.tot_len = 5;
        packet_buffer.type = PBUF_RAM;
        packet_buffer.ref = 1;
        udp_sendto(upcb, &packet_buffer, remote_addr, port);
    }

    pbuf_free(p);
}

bool UpdateManager_Create(void)
{
    pcb = udp_new();
    update_manager_addr.u_addr.ip4.addr = htonl(INADDR_ANY);
    udp_bind(pcb, &update_manager_addr, _UPDATE_MANAGER_PORT);

    udp_recv(pcb, UpdateManager_RxCallback, NULL);

    return true;
}

bool UpdateManager_GetUpdateComplete(void)
{
    return _update_complete;
}

esp_partition_t* UpdateManager_GetNewPartition(void)
{
    return _partition_to_load;
}

//======================================================
//BELOW IS LOWER LAYER UDP/MULTICAST STUFF
// struct udp_pcb* pcb;
// ip_addr_t multicast_addr;
// struct pbuf packet_buffer;

//void udp_multicast_init()
//{
//  pcb = udp_new();
//  multicast_addr.u_addr.ip4.addr = inet_addr(BROADCAST_GROUP_ADDR);
//  udp_connect(pcb, &multicast_addr, BROADCAST_PORT);
//}

//void udp_broadcast(uint16_t port, uint8_t* buffer, uint16_t buffer_length)
//{
    // packet_buffer.next = NULL;
    // packet_buffer.payload = buffer;
    // packet_buffer.len = buffer_length;
    // packet_buffer.tot_len = buffer_length;
    // packet_buffer.flags = 0;
    // packet_buffer.ref = 1;
    // packet_buffer.type = PBUF_RAM;
    // err_t res = udp_send(pcb, &packet_buffer);
    // if(res < 0)
    // {
    //  Pin_SetOutput(DEBUG_PIN3_MASK);
    // }
//}