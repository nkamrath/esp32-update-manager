ESP32 Update Manager <i class="icon-download"></i>
====================
[TOC]

###<i class="icon-bookmark"></i>About

The Update Manager (UM) is a module designed for the ESP32 which handles over the air firmware application updates.  The UM can be configured with a number of different behaviors that are described throughout this document.

###<i class="icon-bookmark"></i>Basic Usage

At its most simplistic use,  the UM creates a UDP socket through LWIP which registers a single callback function to handle all UDP packets required for application updates.  It then runs the course of its update process asynchronously with other tasks until the update is complete.  At this point it will switch the boot partition and restart the device.

####<i class="icon-folder-open">Project Organization Tips
The UM is intended to be used as a submodule within a larger project that requires over the air update capabilities.  It is suggested that a directory structure similar to what is shown below is used.
```
src
├── lib
│   └── esp32-update-manager
│       ├── docs
│       │   └── ESP32-Update-Manager-Design.md
│       ├── README.md
│       └── src
│           ├── update_manager.c
│           └── update_manager.h
├── LICENSE
├── main
│   ├── main.c
│   ├── update_manager.c -> ../lib/esp32-update-manager/src/update_manager.c
│   └── update_manager.h -> ../lib/esp32-update-manager/src/update_manager.h
└── Makefile
```
In the above example, **src** is the root source code directory for the project.  **lib** is then a generic library folder where git submodules are stored.  This folder could be deeper with more levels of organization, but that is left up to the implementer.  Then, in a folder such as **main**, which represents the main source specific to this project, symlinks can be created to reference the library files in the UM submodule.  Again, the directory structure of **main** could be completely different, but the same concepts would still apply.  This example is simply to serve as an example and the designer should feel free to include the UM in any way that works best for his/her project.

###<i class="icon-bookmark"></i>Advanced Usage

The UM can be configured in a variety of different ways by passing in an option struct on UM creation.  If no options are desired (default behavior), the user can pass NULL into the creation function.  This section describes the different configuration options available and their functionality.  For more information on any of the described features, please see the header file code comments or doxygen documentation.

####<i class="icon-bookmark">Changing the UM's UDP Port
The UM options struct contains a field for specifying the port to use.  By default, this is set to **54322**.

####<i class="icon-refresh">Changing the UM's Auto Restart Behavior
The UM options struct contains a field for specifying the auto restart behavior of the UM.  If this values is set to true, the UM will automatically handle resetting the device upon new firmware update completion.  In order to allow an app to initiate the update restart to switch application images, set this value to false.  An app can then periodically poll the UM's state, or register a callback with the UM to be notified when a new firmware image has been successfully received (described later).  By default, this is set to **true**.

####<i class="icon-pause">Changing the UM's Restart Delay Time
The UM options struct contains a field for specifying the amount of time in milliseconds to delay between a restart being triggered through the UM, and the restart call actually happening.  This gives apps and other code a window of time to prepare for the restart if necessary.  If auto restart is enabled, this delay state will automatically be entered upon new firmware update completion.  Apps can see this state by polling the UM's state, or by registering a callback with the UM to be notified when a firmware image update restart has been initiated.  By default, this value is set to **0**.

####<i class="icon-stop">Cancel Update Restart
A firmware image update restart can be cancelled at any time through the UM's interface.  An app can simply call the cancel function which will cancel any update in progress as well as any future update's auto restart capability.  This essentially sets the UM's options for auto restart to false.  In order for an update restart to occur after this call, a call to the UM to change the firmware and restart must be made.

####<i class="icon-play">Firmware Application Switch
It is important to note that the firmware application to use will not actually be changed until the UM is instructed to do so.  What this means is that if a new image is received, but not automatically selected by the UM through auto restart, or auto restart is cancelled for any reason, the new image will not be run on restart even if it has been fully loaded to a partition in flash.  If auto restart is not enabled or an app cancels an auto update, the switch functionality must manually be called to change which application partition should be run on next restart.

####<i class="icon-bookmark">Registering Callbacks with the UM
Callback functions can be registered with the UM when an app wants to be notified of a pending firmware change restart.  This can be useful if an app wants to ensure that it is in a good state for restart or could want to cancel the restart for some reason and initiate it manually at a later time.  Whatever the reason may be, the UM supports this type of callback registration.

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

###Extras
Esp erase flash command:
```
python ../../../esp-idf/components/esptool_py/esptool/esptool.py --chip esp32 --port /dev/ttyUSB1 --baud 115200 erase_flash
```