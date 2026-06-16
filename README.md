# ESP32-C3 Student Help Queue

This project is a working prototype of a classroom queue system built with ESP32-C3 boards and ESP-NOW. Students press a button to join or leave a help queue, while RGB LEDs provide immediate status feedback. The network uses heartbeat monitoring and automatic master failover so the queue can continue operating if the current master device drops offline.

## Demo

![Short demo](buttonQueueExample.gif)

[Full demo video](https://photos.app.goo.gl/GJdPcA1xx7kngdMS9)

## Purpose

The purpose of this project is to monitor and manage a live queue of students who have requested assistance. It is designed to replace traditional hand-raising, which can be cumbersome for a student who is trying to keep their hand raised while continuing to write or work.

Unlike hand-raising, this system removes ambiguity about which student asked for help first. Each student interacts with the queue through a single large button with an embedded RGB LED.

When the button is pressed, the student is added to the queue. If the student is at the front of the queue, the button turns green. Students who are in the queue but waiting receive red LED feedback. If the student is not currently in the queue, the LED is off.

## System Overview

The buttons form a wireless network using ESP-NOW. The network uses a star-like master/client structure. One device acts as the master, while the remaining devices operate as clients. The master is responsible for maintaining the queue and sending queue updates to the rest of the network.

Each device begins in `SEARCHING` mode. In this mode, the device listens for a heartbeat from an existing master. If it receives a heartbeat, it registers the master as a peer and moves into `OPERATION` mode.

If no heartbeat is received within the timeout period, the device promotes itself to `MASTER` and begins broadcasting heartbeat messages.

## Device Modes

The program uses three main device modes:

* `SEARCHING`: The device is looking for an existing master.
* `OPERATION`: The device has found a master and operates as a client.
* `MASTER`: The device maintains the queue and broadcasts heartbeat/queue information.

The main loop checks for pending ESP-NOW messages, handles the message based on its type, and then performs behavior based on the current device mode.

## Heartbeat and Master Failover

The master periodically broadcasts a heartbeat message. This heartbeat contains the current queue size and an array of MAC addresses representing the current queue.

Clients use this heartbeat to confirm that the master is still online. If a client misses several heartbeats, it assumes the master may have dropped offline and returns to `SEARCHING` mode.

If a searching device does not hear from a master after the timeout period, it promotes itself to `MASTER`. The timeout includes a randomized/staggered offset so that multiple devices are less likely to promote themselves at exactly the same time.

If two devices temporarily claim to be master and one receives a heartbeat from the other, they compare MAC addresses. The device with the lower-priority MAC address steps down into `OPERATION` mode, while the higher-priority MAC address remains `MASTER`.

This gives the system basic automatic master failover without requiring manual intervention.

## Queue Behavior

The master stores the active queue as a `std::deque` of MAC addresses.

When a client button is pressed, the client sends a unicast message to the master with an update flag. The master checks whether that device's MAC address is already in the queue.

If the device is already in the queue, the master removes it. If the device is not in the queue, the master adds it to the back of the queue.

After changing the queue, the master sends the updated queue directly to the requesting device and then broadcasts the updated queue to the rest of the network. The queue update is stored in the heartbeat data structure so clients can resynchronize when they receive later heartbeat messages.

## LED Feedback

Each device checks its own MAC address against the current queue.

* If the device is not in the queue, its RGB LED is off.
* If the device is first in the queue, the LED is green.
* If the device is in the queue but not first, the LED is red.

The built-in LED is also used for basic mode feedback while the device is searching, operating, or is the master.

## Concepts Demonstrated

This project demonstrates several programming and embedded-systems concepts:

* Embedded C++ programming
* Modular source/header organization
* Structs and enums
* Bit flags and Boolean logic
* Packed message structures
* Arrays and `std::array`
* `std::vector` for peer tracking
* `std::deque` for queue management
* Callback-driven programming
* State-machine design
* ESP-NOW wireless communication
* Unicast and broadcast message handling
* Heartbeat monitoring
* Automatic master failover
* Hardware/software debugging

## Current Status

The project is functional as a working prototype. It demonstrates button input, RGB LED status feedback, ESP-NOW communication, queue updates, heartbeat monitoring, and automatic master failover.

The code still needs cleanup and refactoring because the network architecture changed several times during development. Some structs, flags, message types, and ID-assignment logic are partially implemented or left over from earlier design plans.

## Known Limitations

If a device is in the queue and falls offline, the queue may be delayed when that device reaches the front. A future version should allow the master to detect when the front-of-queue device is no longer online and remove it from the queue automatically.

Broadcast messages do not have the same acknowledgement/retry behavior as unicast messages. The system partially addresses this by allowing devices to resynchronize from later heartbeat messages, but stronger queue synchronization would improve reliability.

The current hardware prototype also appears to be sensitive to antenna placement. The ESP32-C3 Super Mini communicates more reliably when it is not inserted directly into a breadboard. The devices can generally maintain communication with the initial master, but master election is more reliable when the boards are positioned with better antenna clearance.

## Future Improvements

Planned improvements include:

* Refactor unused structs, flags, and earlier design artifacts
* Strengthen queue synchronization after master failover
* Add detection for offline devices that are currently in the queue
* Add deep-sleep support for devices that are not currently in the queue
* Wake sleeping devices using a hardware interrupt from the button
* Add a low-power flag so devices with low battery are not eligible to become master
* Improve enclosure, wiring, and antenna placement for classroom use
* Add clearer serial debug output for failover and queue events
