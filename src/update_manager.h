#ifndef UPDATE_MANAGER_H
#define UPDATE_MANAGER_H

#include "esp_partition.h"

#include <stdbool.h>

bool UpdateManager_Create(void);
bool UpdateManager_GetUpdateComplete(void);
esp_partition_t* UpdateManager_GetNewPartition(void);

#endif