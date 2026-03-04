/*
  On startup the device broadcasts that it is searching for a master.
  If no one responds within the allotted time, the searching device
  becomes the master.

  All online devices check incoming messages for the searching flag. If
  a received device is searching and this device is a master, it should
  respond with its master info.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_err.h>
#include <array>
#include <vector>
#include <cstdint>
#include <deque>
#include "btn.h"

// Broadcast MAC (alias provided by btn.h)

MacAddr recvAddr = {0, 0, 0, 0, 0, 0};
MacAddr masterAddr = {0, 0, 0, 0, 0, 0};
MacAddr myMacAddr = {0, 0, 0, 0, 0, 0};

btnData_t myData, recvData;
newMember_t newMemberData; // data structure for new member messages
heartBeat_t heartBeatData; // data structure for heartbeat messages

RxMode currentMode = SEARCHING;

std::vector<PeerInfo_t> knownPeers; // list of known peers
std::deque<MacAddr> masterQueue; //store the list to be manipulated

unsigned long searchStart = 0;
unsigned long searchBroadcastStart = 0;
unsigned long lastRecvHeartbeat = 0;
uint8_t lastAssignedId = 0;
uint8_t nextMasterIdUp = 0;
int offsetNewSearchWait = 0;

volatile bool readyForMsg = true; 
volatile msgTypes pendingMsg = MSG_NONE; // type of pending message to process in loop, set in ISR/callback

// Shared peer info globals (types declared in btn.h)
PeerInfo_t masterPeerInfo;                              // master peer info
PeerInfo_t broadcastPeerInfo = {broadcastAddr, myData}; // broadcast peer info
PeerInfo_t recvPeer;                                   // received peer info in callback

peerMacsIds_t peerMacsIdsData; // data structure for peer mac and id collection messages

MacAddr currentQueue[30] = {}; //stores list to be sent

void onReceive(const uint8_t *mac, const uint8_t *data, int len)
{
 if(readyForMsg){
  switch (data[0]) { // check the msg type
    case MSG_HEARTBEAT:
      pendingMsg = MSG_HEARTBEAT;
      memcpy(masterPeerInfo.macAddr.data(), mac, masterPeerInfo.macAddr.size()); // copy mac to recvPeer
      memcpy(&heartBeatData, data, sizeof(heartBeatData)); // copy data to recvData
      readyForMsg = false; // wait to process before accepting another message
      break;

    case MSG_NEW_MEMBER:
      memcpy(&newMemberData, data, sizeof(newMemberData));
      pendingMsg = MSG_NEW_MEMBER;
      readyForMsg = false; // wait to process before accepting another message
      break;

    case MSG_UPDATE:
      pendingMsg  = MSG_UPDATE;
      readyForMsg = false; // wait to process before accepting another message
      break;

    case MSG_BTN:
      memcpy(&recvPeer.data, data, sizeof(recvPeer.data));
      memcpy(recvPeer.macAddr.data(), mac, recvPeer.macAddr.size());
      pendingMsg = MSG_BTN;
      readyForMsg = false; // wait to process before accepting another message
      break;

    case MSG_PEER_MACS_IDS:
      memcpy(&peerMacsIdsData, data, sizeof(peerMacsIdsData));
      pendingMsg = MSG_PEER_MACS_IDS;
      readyForMsg = false; // wait to process before accepting another message
      break;

    case MSG_CURRENT_QUEUE:
      memcpy(&currentQueue, data, sizeof(currentQueue));
      pendingMsg = MSG_CURRENT_QUEUE;
      readyForMsg = false;
      break;
      
    default:
      break;
  }
  }
}

void setup()
{
  initGPIO();

  Serial.begin(115200);
  delay(100);
  Serial.println("Booting...");

  WiFi.mode(WIFI_STA);

  WiFi.macAddress(myMacAddr.data());  //store my mac address

  // Force channel (must match on both boards)
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while (true)
      delay(1000);
  }

  esp_now_register_recv_cb(onReceive);

  // Add broadcast peer
  addBroadcastPeer();
  searchStart = millis();
  searchBroadcastStart = millis();
}

void loop()
{
  /*WE RECEIVED A MESSAGE THAT NEEDS HANDLED */

  switch (pendingMsg) {
    case MSG_HEARTBEAT:
      pendingMsg = MSG_NONE;
      handleHeartBeat(); // handle heartbeat and master election logic in separate function to keep things organized
      readyForMsg = true; 
      break;

    case MSG_NEW_MEMBER:
      pendingMsg = MSG_NONE;
      readyForMsg = true; 
      break;

    case MSG_UPDATE:
      pendingMsg = MSG_NONE;
      readyForMsg = true; 
      break;

    case MSG_BTN:
      pendingMsg = MSG_NONE;    
      handleBtnData();
      readyForMsg = true; 
      break;

    case MSG_PEER_MACS_IDS:
      pendingMsg = MSG_NONE;    
      readyForMsg = true; 
      break;

    default://msgNone or unrecognized msg type
    break;
  }
    /* END OF MESSAGE HANDLING - THIS IS WHAT HAPPENS WHEN NO MESSAGES ARE PENDING*/
    
    switch (currentMode) {
      case SEARCHING: {  
          blink(SEARCHING_BLINK_DURATION);
          if (checkTimeout(searchStart)) break;        // haven't heard a heartbeat, I'm the new master (transition handled elsewhere)
        }
      break;
      case OPERATION:{
        blink(OPERATION_BLINK_DURATION);
      // If master connection is lost
        if (!listenHeartbeat())  break;  
      //check if button was pressed - if so, send UPDATE flag to master
        sendButtonPress(); 
        
      // Normal operation
      break;
      }
    case MASTER:{
        heartBeat(); // sending the heartbeat allows searching devices to either find a master or be elected - similarly, when a master falls offline a missed heartbeat triggers new election
        digitalWrite(LED_PIN, LOW);
        if(checkMasterButtonPress()){
          handleButtonPress(myMacAddr);//add/remove from queue
          sendCurrentQueue(broadcastAddr);//tell everyone to update their list
          assignLedColor();//choose color
        }
        break;
     }
    } 
  delay(10);
  }

