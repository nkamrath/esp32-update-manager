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
#define _BUFFER_LENGTH   1024

#define _UPDATE_MANAGER_DEFAULT__PORT 54322
#define _UPDATE_MANAGER_DEFAULT__AUTO_RESTART true
#define _UPDATE_MANAGER_DEFAULT__RESTART_DELAY_MS 0

#define _UPDATE_MANAGER_DEFAULT_OPTIONS { \
    .udp_port = _UPDATE_MANAGER_DEFAULT__PORT, \
    .auto_restart = _UPDATE_MANAGER_DEFAULT__AUTO_RESTART, \
    .restart_delay_ms = _UPDATE_MANAGER_DEFAULT__RESTART_DELAY_MS \
}

#define _UPDATE_MANAGER_PACKET_MARKER "UPD8"

#define _PARTITION1 "ota_0"
#define _PARTITION2 "ota_1"

/***********************
 * Private Types
 ***********************/
/** @brief update manager packet types**/
typedef enum
{
    UPDATE_PACKET_TYPE__METADATA,
    UPDATE_PACKET_TYPE__IMAGE_DATA,
}update_packet_type_t;

/** @brief structure for an update packet header.  This is packed **/
typedef struct
{
    uint32_t marker;            /**< Marker which helps identify this packet as an update manager packet **/
    uint32_t sequence_number;   /**< Sequence number for this packet.  For image metadata, this should always be 0 **/
    uint32_t packet_type;       /**< Specifies the type of this packet **/
    uint32_t payload_length;    /**< Length of the payload for this packet **/
    uint8_t* payload;           /**< Payload data **/
}__attribute__((packed)) update_header_t;

/** @brief structure for an update metadata packet.  This is packed **/
typedef struct
{
    uint32_t image_size_bytes;  /**< Image size in bytes. **/
    uint32_t num_packets;       /**< number of packets to expect over the course of the update process **/
    uint32_t image_checksum;    /**< checksum of the entire image **/
}__attribute__((packed)) update_metadata_t;

/** @brief structure for an update data packet.  Contains new image binary data.  This is packed **/
typedef struct
{
    uint32_t chunk_size_bytes;  /**< size of the image binary contained within this packet **/
    uint8_t* image_chunk;       /**< The binary image data of this packet **/
}__attribute__((packed)) update_image_data_t;

/***********************
 * Private Variables
 ***********************/

//UDP socket variables
struct udp_pcb* pcb;
ip_addr_t update_manager_addr;
struct pbuf packet_buffer;

//
uint8_t _buffer[_BUFFER_LENGTH];

//variables for image update process
uint32_t _last_sequence_number = 0;
uint32_t _image_size_bytes = 0;
uint32_t _num_update_packets = 0;
bool _received_metadata = false;
const esp_partition_t* _partition_to_load;
esp_ota_handle_t _ota_handle;

//update manager private options
update_manager_options_t _options = _UPDATE_MANAGER_DEFAULT_OPTIONS;
update_manager_state_t _state = UPDATE_MANAGER_STATE__IDLE;

//This loop is entered when the device receives an update begin packet from the server.
//loop handles all tx/rx traffic, updating, setting new partition, restarting, etc.

void UpdateManager_RxCallback(void* arg, struct udp_pcb* upcb, struct pbuf* p, const ip_addr_t* remote_addr, u16_t port)
{
    //printf("update manager callback");
    //printf("payload length: %d", p->len);
    //printf("total length: %d", p->tot_len);
    uint8_t* buffer = (uint8_t*)p->payload;
    bool ok = false;

    if(memcmp(_UPDATE_MANAGER_PACKET_MARKER, buffer, 4) == 0)
    {
        //parse the header
        update_header_t* header = (update_header_t*)buffer;
        if(header->packet_type == UPDATE_PACKET_TYPE__METADATA)
        {
            update_metadata_t* metadata = (update_metadata_t*)(&header->payload);
            //update the internal state of update manager
            _state = UPDATE_MANAGER_STATE__UPDATING;
            //printf("found marker in update packet");
            if(_received_metadata)
            {
                //we already received meta data once, so there was an error somewhere and this is restarting the process
                esp_ota_end(_ota_handle);  //end previous op
                //reset the last sequence number
                _last_sequence_number = 0;
            }
            if(header->sequence_number == 0)
            {
                _image_size_bytes = metadata->image_size_bytes;
                _num_update_packets = metadata->num_packets;

                printf("\r\nNew image size in bytes: %d\r\n", _image_size_bytes);
                printf("\r\nNum update packets required: %d\r\n", _num_update_packets);


                const esp_partition_t* current_boot_partition = esp_ota_get_boot_partition();//esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, _PARTITION1);//esp_ota_get_boot_partition();
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
                        _partition_to_load = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, (const char*)_PARTITION1);
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
        else if(header->packet_type == UPDATE_PACKET_TYPE__IMAGE_DATA)
        {
            update_image_data_t* image_data = (update_image_data_t*) (&header->payload);
            if(header->sequence_number > 0)
            {
                ok = true;
                if(header->sequence_number == _last_sequence_number+1)
                {
                    _last_sequence_number++;
                    uint8_t* new_image_data = (uint8_t*)((uint32_t)&(image_data->image_chunk));
                    esp_err_t write_res = esp_ota_write(_ota_handle, new_image_data, image_data->chunk_size_bytes);
                    if(write_res != ESP_OK)
                    {
                        printf("\r\nesp ota write failed. Reason: %d\r\n", write_res);
                        ok=false; //bad write, error the update
                    }
                    
                    if(_last_sequence_number == _num_update_packets)
                    {
                        esp_ota_end(_ota_handle);
                        _state = UPDATE_MANAGER_STATE__NEW_IMAGE_READY;
                    }
                }
                else
                {
                    //header had previous seq number
                    if(header->sequence_number <= _last_sequence_number)
                    {
                        printf("\r\nWARM: got old sequence number packet, ignored but acked");
                    }
                    else
                    {
                        printf("\r\nERROR: got future sequence number packet, ignored and errored");
                        ok = false;   
                    }
                }
            }
            else
            {
                ok = false;
            }
        }
        else
        {
            //not an update manager packet, no marker
            pbuf_free(p);
            return;
        }
    }

    if(ok)
    {
        memcpy(_buffer, "OK", 2);
        memcpy(&_buffer[2], &_last_sequence_number, 4);  //add 4 byte seq num that we are ok-ing to this packet for python server to check
        packet_buffer.payload = _buffer;
        packet_buffer.len = 6;
        packet_buffer.tot_len = 6;
        packet_buffer.type = PBUF_RAM;
        packet_buffer.ref = 1;
        udp_sendto(upcb, &packet_buffer, remote_addr, port);
    }
    else
    {
        memcpy(_buffer, "ERROR", 5);
        packet_buffer.payload = _buffer;
        packet_buffer.len = 5;
        packet_buffer.tot_len = 5;
        packet_buffer.type = PBUF_RAM;
        packet_buffer.ref = 1;
        udp_sendto(upcb, &packet_buffer, remote_addr, port);
    }

    pbuf_free(p);

    //auto restart needs to start after the final response has been sent otherwise loader app doesn't get last ok
    if(_state == UPDATE_MANAGER_STATE__NEW_IMAGE_READY && _options.auto_restart)
    {
        //auto restart, check for restart delay and handle
        if(_options.restart_delay_ms != 0)
        {
            vTaskDelay(_options.restart_delay_ms / portTICK_RATE_MS);
        }
        //we have to check that the auto_restart flag is still set here in case an app has cancelled the
        //auto restart during the restart_delay period or through callback for some reason.  
        if(_options.auto_restart)
        {
            //switching the boot partition can not occur until inside this final check for auto_restart to
            //detect cancellation which has the desired behavior of maintining the current boot partition for whatever reason
            if(UpdateManager_SelectNewBootPartition())
            {
                if(UpdateManager_InitiateRestart())
                {

                }
                else
                {
                    printf("\r\nRestart failed due to cancellation\r\n");
                }
            }
            else
            {
                printf("\r\nEsp ota set boot partition failed\r\n");
            }
        }
    }

}

bool UpdateManager_Create(update_manager_options_t* options)
{
    pcb = udp_new();
    update_manager_addr.u_addr.ip4.addr = htonl(INADDR_ANY);

    if(options != NULL)
    {
        _options = *options;
    }

    udp_bind(pcb, &update_manager_addr, _options.udp_port);

    udp_recv(pcb, UpdateManager_RxCallback, NULL);

    return true;
}

update_manager_state_t UpdateManager_GetState(void)
{
    return _state;
}

const esp_partition_t* UpdateManager_GetNewPartition(void)
{
    return _partition_to_load;
}

bool UpdateManager_SelectNewBootPartition(void)
{
    if(_state == UPDATE_MANAGER_STATE__NEW_IMAGE_READY)
    {
        esp_err_t espError = esp_ota_set_boot_partition(_partition_to_load);
        if(espError == ESP_OK)
        {
            _state = UPDATE_MANAGER_STATE__NEW_IMAGE_SELECTED;
            return true;  //we successfully set the new boot partition
        }
    }
    return false; //error
}

bool UpdateManager_InitiateRestart(void)
{
    if(_state == UPDATE_MANAGER_STATE__NEW_IMAGE_SELECTED)
    {
        esp_restart();
    }
    return false; //false return value is only in case of failure since success the caller won't see a return because of restart
}