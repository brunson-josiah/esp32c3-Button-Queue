#include <Arduino.h>
#include <esp_now.h>
#include <esp_err.h>
#include <array>
#include <vector>
#include <cstdint>
#include "btn.h"

void heartBeat()
{
  static unsigned long lastHeartBeat = millis();
  if ((millis() - lastHeartBeat) >= HEARTBEAT_INTERVAL) {
    myData.setFlags(IS_HEARTBEAT);
    //heartBeatData.latestId = myData.lastMasterAssignedId;
    //send heartBeat_t to all 
    esp_err_t sendStatus = esp_now_send(broadcastAddr.data(), (uint8_t *)&heartBeatData, sizeof(heartBeatData));
    Serial.printf("heartbeat broadcast, send status = %d (%s)\n", (int)sendStatus, esp_err_to_name(sendStatus));
    printKnownPeers();
    myData.clearFlags(IS_HEARTBEAT);
    lastHeartBeat = millis();
  }
}

// check to see if the macAddr is already stored in knownPeers
int getPeerIndex(const PeerInfo_t &peerInfo)
{
  for (size_t i = 0; i < knownPeers.size(); ++i) {
    if (knownPeers[i].macAddr == peerInfo.macAddr) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// assign an id to a peer (does NOT update knownPeers list)
void assignId(PeerInfo_t &peerInfo, int index)
{
  /*== PEER IS ALREADY IN LIST ==*/
  if (index >= 0) {
    // device in list
    if (knownPeers[index].data.id > 0) {
      Serial.printf("Peer is in list and has existing id, assigning existing id %u\n", knownPeers[index].data.id);
      myData.assignedId = knownPeers[index].data.id;
      peerInfo.data.id = knownPeers[index].data.id;
      peerInfo.data.lastMasterAssignedId = knownPeers[index].data.lastMasterAssignedId;
    } else {
      // assign new id
      myData.lastMasterAssignedId++;
      heartBeatData.latestId = myData.lastMasterAssignedId;//update heartbeat data so new master can share the most recent id assignment in its heartbeat
      myData.assignedId = myData.lastMasterAssignedId;
      peerInfo.data.lastMasterAssignedId = myData.lastMasterAssignedId;
      peerInfo.data.id = myData.assignedId;
      knownPeers[index].data = peerInfo.data;
    }
  } 
  else {
  /*== PEER IS NOT IN LIST ==*/   
   //device already has an id
    if (peerInfo.data.id > 0) {
      Serial.printf("Peer has existing id, assigning same id %u\n", peerInfo.data.id);
      myData.assignedId = peerInfo.data.id;
    } else {
      //device needs an id
      myData.lastMasterAssignedId++;
      heartBeatData.latestId = myData.lastMasterAssignedId;//update heartbeat data so new master can share the most recent id assignment in its heartbeat
      myData.assignedId = myData.lastMasterAssignedId;
      peerInfo.data.id = myData.assignedId;
      peerInfo.data.lastMasterAssignedId = myData.lastMasterAssignedId;

      Serial.printf("Peer has no existing id and not in list, assigning new id %u\n", myData.assignedId);
    }
  }
  myData.setFlags(ID_ASSIGNED);
}

bool checkTimeout(unsigned long startTime)
{//if we miss 3 heartbeats while searching, we become master and send heartbeat
  if ((millis() - startTime) > HEARTBEAT_INTERVAL * 3) {
    // Timeout - become master
    myData.setFlags(IS_MASTER, IS_HEARTBEAT);
    myData.clearFlags(IS_SEARCHING);
    currentMode = MASTER;
    Serial.println("No master found - becoming MASTER");

    // broadcast a heartbeat to all
    esp_err_t sendStatus = esp_now_send(broadcastAddr.data(), (uint8_t *)&heartBeatData, sizeof(heartBeatData));
    myData.clearFlags(IS_HEARTBEAT);
    Serial.printf("broadcasting new master, send status = %d (%s)\n", (int)sendStatus, esp_err_to_name(sendStatus));
    return true;
  }
  return false;
}

// check if peer is in list, assign the received peer to that index, otherwise, add it with newpeerconfig
void updatePeerData(PeerInfo_t &peerInfo, int index)
{
  if (index >= 0) {
    knownPeers[index] = peerInfo;
    Serial.println("Peer data found and updated successfully");
  } else {
    Serial.println("Peer not found for data update - going to configure new peer");
    newPeerConfig(peerInfo);
  }
}

// returns false if failed to receive heartbeat after 3 attempts and causes the device to begin searching
bool listenHeartbeat()
{
  if ((millis() - lastRecvHeartbeat) > (HEARTBEAT_INTERVAL * 3)) {
    Serial.println("Connection to master lost, going back to SEARCHING");
    offsetNewSearchWait = BASE_WAIT + (myData.id * SLOT_WAIT) + jitter();
    myData.setFlags(IS_SEARCHING);
    myData.clearFlags(IS_MASTER);
    delay(offsetNewSearchWait); // stagger retries based on last assigned id
    searchStart = millis();
    currentMode = SEARCHING;
    return false;
  }
  return true;
}

// add new peer and push to knownPeers if successful
void newPeerConfig(const PeerInfo_t &peerInfo)
{
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, peerInfo.macAddr.data(), peerInfo.macAddr.size());
  peer.channel = 1;
  peer.encrypt = false;

  esp_err_t addStatus = esp_now_add_peer(&peer);
  Serial.printf("add peer = %d (%s)\n", (int)addStatus, esp_err_to_name(addStatus));

  if (addStatus == ESP_OK) {
    Serial.println("Peer added successfully");
    knownPeers.push_back(peerInfo);
  } else if (addStatus == ESP_ERR_ESPNOW_EXIST) {
    // already existed in ESP-NOW; sync local list
    Serial.println("Peer already existed in ESP-NOW; syncing local list");
    knownPeers.push_back(peerInfo);
  } else {
    Serial.println("Peer add failed; will retry later");
    //timer to retry should be writtne at some point
  }
}


void blink(int duration, int pin)
{
  static unsigned long startBlink = millis();
  static bool ledState = false;
  if ((millis() - startBlink) > duration) {
    ledState = !ledState;
    digitalWrite(pin, ledState ? HIGH : LOW);
    startBlink = millis();
  }
}

int jitter()
{
  return random(0, MAX_JITTER_WAIT);
}

void addBroadcastPeer()
{
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddr.data(), broadcastAddr.size());
  peer.channel = 1;
  peer.encrypt = false;

  esp_err_t addStatus = esp_now_add_peer(&peer);
  Serial.printf("add broadcast peer = %d (%s)\n", (int)addStatus, esp_err_to_name(addStatus));
}

//assigns the latest id given out and updates the list of peers
bool assignUpdateLastMasterAssigned(PeerInfo_t &peerInfo)
{
  if (peerInfo.data.lastMasterAssignedId > myData.lastMasterAssignedId) {
    myData.lastMasterAssignedId = peerInfo.data.lastMasterAssignedId;
    updatePeerData(peerInfo, getPeerIndex(peerInfo));//add changes to your list
    return true;
  }
  else{
    peerInfo.data.lastMasterAssignedId = myData.lastMasterAssignedId;
    updatePeerData(peerInfo, getPeerIndex(peerInfo));//add changes to your list
    return false;
  }
}


//the heartbeat is how devices know master exists. Searching devices must pair with the master
//by recording the mac and adding to the list of peers - maybe the heartbeat will include
//the current queue list, so devices operating can sync their lists as well
void handleHeartBeat(){
   switch (currentMode) {
      case SEARCHING: {
          lastRecvHeartbeat = millis();
          // add master as peer - functions check if master is already added
          Serial.printf("Master found, no id yet - going into op mode and requesting id\n");
          myData.clearFlags(IS_SEARCHING, IS_MASTER); // stop searching/master
          myData.setFlags(IS_OPERATING, ID_REQUESTED);             // set operating flag
          
          //register master as peer
          esp_now_peer_info_t peer = {};
          memcpy(peer.peer_addr, masterPeerInfo.macAddr.data(), masterPeerInfo.macAddr.size());
          peer.channel = 1;
          peer.encrypt = false;
          esp_err_t addStatus = esp_now_add_peer(&peer);
          Serial.printf("add master as peer = %d (%s)\n", (int)addStatus, esp_err_to_name(addStatus));
          currentMode = OPERATION;
          break;
      }
      case OPERATION:{
        lastRecvHeartbeat = millis();
        if(heartBeatData.latestId > myData.lastMasterAssignedId){
        myData.lastMasterAssignedId = heartBeatData.latestId;
        }
        Serial.printf("Received heartbeat in OPERATION mode, my lastMasterAssignedId: %u\n", myData.lastMasterAssignedId);
        printKnownPeers();
        break;
      }
      case MASTER:{
        Serial.printf("Received heartbeat in MASTER mode - stepping down\n");
        offsetNewSearchWait = BASE_WAIT + (myData.id * SLOT_WAIT) + jitter();
        delay(offsetNewSearchWait); // add delay and jitter to avoid collisions with other devices also stepping down
        searchStart = millis();
        myData.clearFlags(IS_MASTER);
        myData.setFlags(IS_SEARCHING);
        currentMode = SEARCHING;
        break;
      }
   }
}

void handleBtnData(){
  switch(currentMode){
    case SEARCHING:
    //as of now, searching devices only listen, they do not send btn Data
    break;

    case OPERATION:{
      //the master has responded to your id request
        if(recvPeer.data.hasFlag(IS_MASTER) && recvPeer.data.hasFlag(ID_ASSIGNED)){
          masterPeerInfo = recvPeer;//store master info in global for easy access
          Serial.println("Received ID assignment from master");
          myData.clearFlags(ID_REQUESTED);//no longer requesting id
          int index = getPeerIndex(masterPeerInfo);
          assignId(masterPeerInfo, index);
          updatePeerData(masterPeerInfo, index);
          myData.id = masterPeerInfo.data.assignedId;//store your assigned id in your data struct so you know your id for future reference and can share it with others when needed
          //send
          myData.lastMasterAssignedId = masterPeerInfo.data.lastMasterAssignedId;//sync your lastMasterAssignedId with the master's so you know if your id is outdated in the future
          myData.clearFlags(ID_ASSIGNED);
        }
    break;
      }
    case MASTER:{

    //device is requesting an id, assign id and update your list
    //after list is updated, broadcast a new member update
        if(recvPeer.data.hasFlag(ID_REQUESTED)){
          Serial.println("Received ID request, assigning ID and sending update");
          recvPeer.data.clearFlags(ID_REQUESTED);
          assignId(recvPeer, getPeerIndex(recvPeer));
          updatePeerData(recvPeer, getPeerIndex(recvPeer));
          myData.setFlags(ID_ASSIGNED);
          
          //unicast to device so it can store your assigned id and know it has been assigned
          esp_err_t sendStatus = esp_now_send(recvPeer.macAddr.data(), (uint8_t *)&myData, sizeof(myData));
          Serial.printf("Sent ID assignment update, send status = %d (%s)\n", (int)sendStatus, esp_err_to_name(sendStatus));
          myData.clearFlags(ID_ASSIGNED);

          //broadcast to all devices so they can update their lists with the new id assignment and lastMasterAssignedId
          newMemberData.peerInfo = recvPeer;
          esp_err_t broadcastStatus = esp_now_send(broadcastAddr.data(), (uint8_t *)&newMemberData, sizeof(newMemberData));
          Serial.printf("Broadcasted new member update, send status = %d (%s)\n", (int)broadcastStatus, esp_err_to_name(broadcastStatus));
        }
    break;
      }
  }
}

void handleNewMember(){
  //if the new member has a more recent lastMasterAssignedId than you, update your list and your lastMasterAssignedId
  if (assignUpdateLastMasterAssigned(newMemberData.peerInfo)) {
    Serial.println("lastMasterAssigned was greater than mine, updated");
    printKnownPeers();
  } else {
    Serial.println("lastMasterAssigned was NOT greater than mine, kept my own lastMasterAssignedId");
    printKnownPeers();
  }
}

void printKnownPeers(){
      for (auto &p : knownPeers) {
      Serial.printf("Known peer: %02X:%02X:%02X:%02X:%02X:%02X\n id: %u, my lastMasterAssignedId: %u \n", 
                    p.macAddr[0], p.macAddr[1], p.macAddr[2], p.macAddr[3], p.macAddr[4], p.macAddr[5], p.data.id,
                    myData.lastMasterAssignedId);
    }
}

//sends the full peer list to the new member requesting id so they can sync their list with the most up to date info right after they receive their id assignment
void sendKnownPeers(MacAddr &mac){
  for (auto &p : knownPeers) {
    peerMacsIds_t peerMacId; 
    peerMacId.macAddr = p.macAddr;
    peerMacId.id = p.data.id;
      esp_err_t sendStatus = esp_now_send(mac.data(), (uint8_t *)&peerMacId, sizeof(peerMacId));
      Serial.printf("Sending known peers --> address %02X:%02X:%02X:%02X:%02X:%02X, id: %u, send status = %d (%s)\n", 
                    peerMacId.macAddr[0], peerMacId.macAddr[1], peerMacId.macAddr[2], peerMacId.macAddr[3], peerMacId.macAddr[4], peerMacId.macAddr[5],
                    peerMacId.id, (int)sendStatus, esp_err_to_name(sendStatus));
    }
}

void handlePeerMacsIds(){
  //when a new device receives its id assignment, it also receives the most up to date list of macs and ids so it can sync its list
  if(recvPeer.data.msgType == MSG_PEER_MACS_IDS){
    peerMacsIds_t *peerMacId = (peerMacsIds_t *)&recvPeer;//cast to the correct struct type
    PeerInfo_t peerInfo;
    peerInfo.macAddr = peerMacId->macAddr;
    peerInfo.data.id = peerMacId->id;
    int index = getPeerIndex(peerInfo);
    if(index >= 0){
      knownPeers[index].data.id = peerMacId->id;//update the id for that mac in your list
      Serial.printf("Updated id for existing peer in list --> address %02X:%02X:%02X:%02X:%02X:%02X, id: %u\n", 
                    peerInfo.macAddr[0], peerInfo.macAddr[1], peerInfo.macAddr[2], peerInfo.macAddr[3], peerInfo.macAddr[4], peerInfo.macAddr[5],
                    knownPeers[index].data.id);
    }
    else{
      Serial.printf("Received mac and id for unknown peer --> address %02X:%02X:%02X:%02X:%02X:%02X, id: %u\n", 
                    peerInfo.macAddr[0], peerInfo.macAddr[1], peerInfo.macAddr[2], peerInfo.macAddr[3], peerInfo.macAddr[4], peerInfo.macAddr[5],
                    peerInfo.data.id);
    }
  }
}