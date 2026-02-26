This program utilizes ESP_Now to establish a connection between devices in a network. There is one master device (who also behaves as a client) and many client devices. 
The master unicasts to each and assigns properties to the clients as they come online and their state changes via button press.  This topology was chosen over
all devices being permitted to broadcast to all their states as a means of decreasing traffic/collisions and increasing reliability. 

The device begins SEARCHING. where it broadcasts to all. The properties that are shared in every conversation are in the btnData struct. Currently, they are isSearching, 
isMaster, id, and lastIdAssigned - which is the latest id the master assigned to the most recent new device. 

If the device searches for too long it will timeout and become the master. When the device is in the MASTER state - it listens to devices who are broadcasting and whose 
data isSearching is true. When this is the case, the master stores the mac in a vector of known peers, configures the peer for future unicasts, assigns an id to the device
and responds to the device. When a SEARCHING device hears from the master, its isSearching goes false, its state turns to OPERATION, it sets its id to the id received
and then it adds the master's mac address and configures the master as a peer. 

The tricky part of the program lies in the election process of the master happens to fall offline. The master periodically broadcasts a heartbeat - which is simply its
btnData (which again, is shared in EVERY communication). When OPERATION  devices hear from the master, they record  the lastHeartbeatms. If too much time has elapsed since
the lastHeartbeatms - the devices will pause for a time calculated as a function of their id. This delay is critical since all online devices will timeout roughly at the 
same time. So how would they know who gets elected? The lowest id will have the shortest delay (since the id value is used as a factor in the delay calculation). After
this delay,  the device starts SEARCHING. Since this device starts SEARCHING, if the master happens to come right back online, the SEARCHING device will transfer states
to SEARCHING, however, if no master is found, this first device that starts searching will timeout before all other devices and will be assigned MASTER and the cycle 
continues. 

This new master currently reassigns ids as it has no list of knownPeers (aside from the master) and configures all SEARCHING devices as they return back online. If 
this election happens so quickly that those devices currently in delays after timeouts -thus still in OPERATION - check to see that when they receive a signaL from the
master that not only is the isMaster true, but  that the transmitterAddr matches the masterAddr they have on record. If it does not - they share their info with the 
new master so they can be reconfigured and so they can add the new master as peer - all before entering SEARCHING. 

The main issue currently is reassigning an id every time a new master appears. Since the delay is based on the id number,  as these numbers grow, so does the pause before 
election. 