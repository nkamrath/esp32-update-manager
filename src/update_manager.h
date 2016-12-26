#ifndef UPDATE_MANAGER_H
#define UPDATE_MANAGER_H

#include "esp_partition.h"

#include <stdbool.h>

/** @brief Struct for specifying options to the update manager on creation **/
typedef struct{
	uint16_t udp_port;				/**< specifies the UDP port that the update manager should use **/
	bool auto_restart;				/**< indicates if the update manager should auto restart on new image recv **/
	uint32_t restart_delay_ms;		/**< the amount of time in ms to wait after restart is triggered before initiating the restart**/
}update_manager_options_t;

typedef enum{
	UPDATE_MANAGER_STATE__IDLE,
	UPDATE_MANAGER_STATE__UPDATING,
	UPDATE_MANAGER_STATE__NEW_IMAGE_READY,
	UPDATE_MANAGER_STATE__NEW_IMAGE_SELECTED,
	UPDATE_MANAGER_STATE__PRE_RESTART_DELAY,
	UPDATE_MANAGER_STATE__ERROR
}update_manager_state_t;

bool UpdateManager_Create(update_manager_options_t* options);
update_manager_state_t UpdateManager_GetState(void);
const esp_partition_t* UpdateManager_GetNewPartition(void);
bool UpdateManager_SelectNewBootPartition(void);
bool UpdateManager_InitiateRestart(void);

#endif