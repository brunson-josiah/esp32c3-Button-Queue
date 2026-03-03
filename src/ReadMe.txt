/********** PURPOSE ***************/

The purpose of this program is to monitor and manage a live queue of students who have requested assistance. It is designed to replace traditional "hand raising"
which can be cumbersome for a student who is trying to keep their hand raised and continue writing or working. Unlike hand raising, there is no ambiguity 
between which student asked for help first. 

The students interface with the queue through a single large button with an RGB led embedded into it. When the button is pressed, the student is added to the queue.
If the student is in the  front of the queue, the button will be green. Students that are in the queue but waiting will having blinking red buttons - 
with the frequency being a function of their position in the queue. 

/**********  PROGRAM CONCEPT  **************/

The buttons form a wirless network for communication. This program utilizes ESP_Now to create this network. There is one master device/button (who also may behave 
as a client) and the remaining buttons are clients. The master is responsible for managing the queue and the network. All devices begin in SEARCHING mode, where
they remain idle, listening for a heartbeat from the master. The heartbeat contains the current queue, as an array of mac addresses, and the number of devices 
in the queue. When a searching device hears the heartbeat, it moves into OPERATION mode. If 3 heartbeats are missed (currently 1.5 seconds), the SEARCHING device
promotes itself MASTER. 

The trickiest part of this network is maintaining management and allowing the queue to persist should the master fall offline for any reason. If the master falls
offline, the devices in OPERATION recognize after 3 missed heartbeats. If the current queue is populated, the first device in the queue is elected, if
not, devices in OPERAITON wait for a randomly designated amount of time and then go into SEARCHING. This delay is vital so that a herd of devices arent all
timing out of SEARCHING at once. Since all devices should record the queue after each heartbeat,  this new MASTER should already contain a copy of the last queue
and restore it. Should the master receive a heartbeat, it will alert that multiple masters are detected and it will fall into SEARCHING to retry a proper election.  

When a device in OPERATION is pressed, it unicasts a message to the master containing a flag that the button was pressed. If the device's mac is currently in the 
queue, that device is popped from the master's deque. If the device is not in the queue, it is pushed to the back. Any alteration of the queue will result in an
immediate unicast to the requesting device, followed by a broadcast from the master. However, since there is no acknowledgement/built-in retries for broadcasted 
messasges, should a device miss the update it should resync upon the next heartbeat. 

As devices receive the queue updates from the master they find their position in the list and adjust their own leds accordingly. 


/********* IMPROVEMENT/DEVELOPMENT NOTES **********/
Buttons not in the queue should go into a deep sleep - awakened by a hardware interrupt attached to the button. This may be slow as the device will need to wake
SEARCHING since a master couldve fallen offline when the device went into sleep. While the device is SEARCHING, the led should blink a unique color for feedback. 

I may attach a flag for low remaining power devices such that they CANNOT be elected MASTER, since the master will be by far the most power hungry role.