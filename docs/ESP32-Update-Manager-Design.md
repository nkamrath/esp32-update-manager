ESP32 Update Manager <i class="icon-download"></i>
====================

[TOC]

###<i class="icon-bookmark"></i>About
The update manager is a module designed for the ESP32 which handles over the air firmware application updates.  The Update Manager cna be configured with a number of different behaviors that are described throught this document.  

###<i class="icon-bookmark"></i>Basic Usage
At its most simplistic use, it creates a UDP socket through LWIP which regisers a single callback function to handle all UDP packets required for application updates.  It then runs the course of its update process asynchronously with other tasks until the update is complete.  At this point it will switch the boot partition and restart the device.

###<i class="icon-bookmark"></i>Advanced Usage


The Update Manager can be configured to work as described below.

- Change Update Manager UDP port:  Change the UPDATE_MANAGER_UDP_PORT define in update_manager.h
- Change Update Manager to wait for app to signal restart
	- By default, the Update Manager will automatically restart the device upon completing application update.  If it is desired that an application manually trigger the restart, set the auto_restart flag in the configuration struct to false.  This will cause the Update Manager to wait until UpdateManager_Restart() is called.  Apps can get the state of the Update Manager by calling UpdateManager_GetState().  
- If an application would like to initiate a restart of the device to the new firmware, but would like for the switch to occur after a delay, an app can set the restart_delay_us parameter in the Update Manager configuration struct.  This will cause the Update Manager to go into the PRE_RESTART_DELAY state and wait the amount of time specified before restarting.  If an app would like to cancel a restart during the PRE_RESTART_DELAY period, it can issue a call to UpdateManager_HaltPending which will cancel the restart and force the update manager back to the state NEW_IMAGE_READY.
- If an application would like to receive a callback notification when a restart is about to occur, it can register a call back handler function with UpdateManager_RestartCallback(update_manager_callback_t*) which can alert the application that a restart is about to happen

###<i class="icon-bookmark"></i>Example Code

```
update_manager_configs_t
{
	bool auto_restart;  //the update manager will automatically trigger a device restart on completion if this is true
	uint32_t restart_delay_us; //the update manager will delay this many microseconds after 
}

update_manager_state_t
{
	UPDATE_MANAGER_STATE__IDLE,
	UPDATE_MANAGER_STATE__UPDATING,
	UPDATE_MANAGER_STATE__NEW_IMAGE_READY,
	UPDATE_MANAGER_STATE__PRE_RESTART_DELAY
	UPDATE_MANAGER_STATE__ERROR
}

typedef update_manager_callback_t void(*callback)(void* arg);
```