#include <Arduino.h>
#include <esp_now.h>
#include <esp_err.h>
#include <array>
#include <vector>
#include <cstdint>
#include <algorithm> 
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

bool checkTimeout(unsigned long startTime)
{//if we miss 3 heartbeats while searching, we become master and send heartbeat
  if ((millis() - startTime) > (HEARTBEAT_INTERVAL * 3 + heartbeatTimeout)) {
    // Timeout - become master
    myData.setFlags(IS_MASTER, IS_HEARTBEAT);
    myData.clearFlags(IS_SEARCHING);
    currentMode = MASTER;
    Serial.println("No master found - becoming MASTER");
    heartbeatTimeout = offsetNewSearchWait();
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
  if ((millis() - lastRecvHeartbeat) > (HEARTBEAT_INTERVAL * 3 + heartbeatTimeout)) {
    Serial.println("Connection to master lost, going back to SEARCHING");
    myData.setFlags(IS_SEARCHING);
    currentMode = SEARCHING;
    myData.clearFlags(IS_MASTER);
    heartbeatTimeout = offsetNewSearchWait();
     // stagger retries based on last assigned id
    searchStart = millis();
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
  return int(esp_random() % MAX_JITTER_WAIT);
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
        assignLedColor();
        Serial.printf("Received heartbeat in OPERATION mode, my lastMasterAssignedId: %u\n", myData.lastMasterAssignedId);
        break;
      }
      case MASTER:{
        Serial.printf("Received heartbeat in MASTER mode - stepping down\n");
        
        if(masterPeerInfo.macAddr > myMacAddr){
        Serial.printf("Stepping down ....");
        myData.clearFlags(IS_MASTER);
        myData.setFlags(IS_OPERATING);
        currentMode = OPERATION;
        heartbeatTimeout = offsetNewSearchWait(); // add delay and jitter to avoid collisions with other devices also stepping down
        searchStart = millis();
        }
        else {
          Serial.printf("I have larger MAC, asserting dominance"); 
        }
        
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
          updatePeerData(masterPeerInfo, index);
          myData.id = masterPeerInfo.data.assignedId;//store your assigned id in your data struct so you know your id for future reference and can share it with others when needed
          //send
          myData.lastMasterAssignedId = masterPeerInfo.data.lastMasterAssignedId;//sync your lastMasterAssignedId with the master's so you know if your id is outdated in the future
          myData.clearFlags(ID_ASSIGNED);
        }
    break;
      }
    case MASTER:{

  //device's button has been pressed and requests an update
        if(recvPeer.data.hasFlag(UPDATE_NEEDED)){
          Serial.println("Received button press - list is being updated...");
          recvPeer.data.clearFlags(UPDATE_NEEDED);
          handleButtonPress(recvPeer.macAddr);//adds or removes item from list                  
          sendCurrentQueue(recvPeer.macAddr);//unicasts direct to requesting clien
          delay(10);//in esp32 this is non blocking for the wifi
          sendCurrentQueue(broadcastAddr); 
          assignLedColor();
        }
    break;
      }
  }
}



//sends the full peer list to the new member requesting id so they can sync their list with the most up to date info right after they receive their id assignment
void sendCurrentQueue(const MacAddr &mac){
  //will need to think about gaurds/segmenting/chunking if list grows over 30
  size_t listSize = masterQueue.size()<=30 ? masterQueue.size() : 30;  
  heartBeatData.queueSize = listSize; 
  //fill the buffer
  for(int i = 0; i<(int)listSize; i++ ){
    heartBeatData.macs[i] = masterQueue[i];
  }
      esp_err_t sendStatus = esp_now_send(mac.data(), (uint8_t *)&heartBeatData, sizeof(heartBeatData));
      Serial.printf("Sending current queue, send status = %d (%s)\n", 
                    (int)sendStatus, esp_err_to_name(sendStatus));
}

void updateQueue(){
  //fill current queue with the heartbeat queue
  for(int i=0; i<heartBeatData.queueSize; i++){
    currentQueue[i] = heartBeatData.macs[i];
  }
  

}
//for all clients
void sendButtonPress(){
  static bool lastState = true; 
  bool currentState = digitalRead(BUTTON_PIN);
    delay(20);//may need nonblocking debounce but i dont think so 
  if(!currentState && lastState){
    //button pressed - falling edge 
    //send update - this will toggle on the master's side depending on if theyre in the queue
    myData.setFlags(UPDATE_NEEDED);
    esp_err_t sendStatus = esp_now_send(masterPeerInfo.macAddr.data(), (uint8_t *)&myData, sizeof(myData));
    Serial.printf("Sent update request, send status = %d (%s)\n", (int)sendStatus, esp_err_to_name(sendStatus));
    myData.clearFlags(UPDATE_NEEDED);
  }
  lastState = currentState;
}

//for only the master since he can auto adjust queue
bool checkMasterButtonPress(){
    static bool lastState = true; 
    bool justPressed = false;
    bool currentState = digitalRead(BUTTON_PIN);
    delay(20);//may need nonblocking debounce but i dont think so 
  if(!currentState && lastState){
    //button pressed - falling edge 
    justPressed = true;
  }

  lastState = currentState;
  return justPressed;
}

void assignLedColor(){
  //check where i am in the queue
  int position = -1; 
  for(int i = 0; i<heartBeatData.queueSize; i++){
    if(myMacAddr == heartBeatData.macs[i]){
      position = i;
      break;
    }
  }
  static int lastPosition = -2;
  if(position == lastPosition){
    return;//prevents us from spamming the pins on every heartbeat
  }
  lastPosition = position; 

  switch (position)
  {
  case -1:
    //not found in queue, shut off led
    digitalWrite(GREEN_PIN,0);
    digitalWrite(RED_PIN,0);
    Serial.println("Out of queue, LEDS OFF");
    break;
  
  case 0://next up
    digitalWrite(GREEN_PIN,1);
    digitalWrite(RED_PIN,0);
    Serial.println("Next up in queue, LED GREEN");

  break;

  default://in queue but not next
    digitalWrite(RED_PIN,1);
    digitalWrite(GREEN_PIN,0);
    Serial.println("Waiting in queue, LEDS RED");

    break;
  }
}

//returns false if the device has been kicked from the list
bool handleButtonPress(const MacAddr& mac){
  auto it = std::find(masterQueue.begin(), masterQueue.end(), mac);
//find returns the "smart" pointer to the location of mac - since deque elements are in non contiguous chunks of memory. 
  if(it != masterQueue.end()){//device is in list! .end is the address AFTER the last index
    masterQueue.erase(it);//remove the device
    return false; 
  }
  else{
    masterQueue.push_back(mac);
    return true; 
  }
}

void initGPIO(){
  pinMode(GREEN_PIN,OUTPUT);
  pinMode(RED_PIN,OUTPUT);
  pinMode(LED_PIN,OUTPUT);
  pinMode(BUTTON_PIN,INPUT_PULLUP); 

  digitalWrite(LED_PIN,1);//turn built in off
  digitalWrite(RED_PIN,0);
  digitalWrite(GREEN_PIN,0);
}

int offsetNewSearchWait(){
  int integer = esp_random() % 40;//this random function generates a 32bit integer, so the mod confines the max
  return int(BASE_WAIT+ integer*SLOT_WAIT +jitter());
}