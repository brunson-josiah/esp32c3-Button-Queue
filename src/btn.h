#ifndef BTN_H
#define BTN_H

#include <cstdint>
#include <array>
#include <vector>
#include <deque>
#include <algorithm>//use for find and erase in the deque

#define BUTTON_PIN 5
#define GREEN_PIN 0
#define RED_PIN 1
#define LED_PIN 8
#define SEARCH_TIMEOUT 2000 //how long to wait while searching before becoming master
#define SEARCH_BROADCAST_INTERVAL 250
#define BASE_WAIT 100           // base wait time before retrying search after losing master
#define SLOT_WAIT 30            // additional wait time per id slot before retrying search after losing master
#define MAX_JITTER_WAIT 20      // max additional random wait time to add jitter to search retries
#define OPERATION_BLINK_DURATION 1000
#define SEARCHING_BLINK_DURATION 200
#define HEARTBEAT_INTERVAL 1000
#define ID_REQUEST_INTERVAL 1000

using MacAddr = std::array<uint8_t, 6>;
constexpr MacAddr broadcastAddr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Benefit of storing multiple flags in a single integer:
// - Mask, compare, and combine with bitwise operators
// - Can represent multiple boolean states compactly
enum BtnFlags : uint16_t {
  IS_SEARCHING      = 1 << 0,
  IS_HEARTBEAT      = 1 << 1,
  UPDATE_NEEDED     = 1 << 2,
  ID_REQUESTED      = 1 << 3,
  ID_ASSIGNED   = 1 << 4,
  IS_OPERATING      = 1 << 5,
  IS_MASTER         = 1 << 6,
  HAND_RAISED         = 1 << 7,
  POTTY_PRESSED       = 1 << 8
};

enum msgTypes : uint8_t {
  MSG_NONE        = 0,
  MSG_NEW_MEMBER      = 1 << 0,
  MSG_HEARTBEAT      = 1 << 1,
  MSG_BTN         = 1 << 2,
  MSG_UPDATE         = 1 << 3,
  MSG_PEER_MACS_IDS = 1 << 4,
  MSG_CURRENT_QUEUE = 1 << 5
};

// Device receive modes
enum RxMode {
  SEARCHING,
  OPERATION,
  MASTER
};

//msg types
struct __attribute__((packed)) btnData_t {
  msgTypes msgType = MSG_BTN;
  uint16_t flags = IS_SEARCHING;
  uint8_t  id = 0;
  uint8_t  lastMasterAssignedId = 0;
  uint8_t  assignedId = 0;       // id assigned to the device by the master
 //using recursive variadic templates to set and clear flags with any number of arguments
// 1. Base Case (Stopper)
void setFlags() {} 
// 2. Recursive Step
// "T" = The type of the first argument
// "Args" = The type of the remaining arguments (The Parameter Pack)
template<typename T, typename ... Args>
void setFlags(T firstArg, Args... otherArgs) {
    // Use the name you chose for the first item
    flags |= static_cast<uint16_t>(firstArg);   
    // Use the name you chose for the pack
    setFlags(otherArgs...); 
}

// 3. Same logic for clearing
void clearFlags() {}

template<typename T, typename ... Args>
void clearFlags(T firstArg, Args... otherArgs) {
    flags &= ~static_cast<uint16_t>(firstArg);
    clearFlags(otherArgs...);
}

  bool hasFlag(uint16_t flag) const {
    return (flags & flag) != 0;
  }
};

struct __attribute__((packed)) PeerInfo_t {
  MacAddr macAddr;
  btnData_t data;
};

struct __attribute__((packed)) newMember_t {
  msgTypes msgType = MSG_NEW_MEMBER;
  PeerInfo_t peerInfo;
};

struct __attribute__((packed)) heartBeat_t{
  msgTypes msgType = MSG_HEARTBEAT;
  uint8_t queueSize = 0;
  MacAddr macs[30];
};

//msg type just to collect the mac addresses and their associated ids for new members
struct __attribute__((packed)) peerMacsIds_t{
  msgTypes msgType = MSG_PEER_MACS_IDS;
  MacAddr macAddr;
  uint8_t id;
};



void newPeerConfig(const PeerInfo_t &peerInfo);
void blink(int duration = 500, int pin = LED_PIN);
void heartBeat();
void updatePeerData(const PeerInfo_t &peerInfo, int index);
void addBroadcastPeer();
int jitter();
int getPeerIndex(const PeerInfo_t &peerInfo);
// Added missing prototypes implemented in btn.cpp
bool checkTimeout(unsigned long startTime);
bool listenHeartbeat();
void handleHeartBeat();
void handleBtnData();
bool handleButtonPress(const MacAddr& mac);
void sendCurrentQueue(const MacAddr &mac); 
void sendButtonPress(); 
void assignLedColor();
void initGPIO();
bool checkMasterButtonPress();
int offsetNewSearchWait();

// Shared global state (defined in main.cpp)
extern btnData_t myData;
extern btnData_t recvData;
extern newMember_t newMemberData;
extern heartBeat_t heartBeatData;
extern std::vector<PeerInfo_t> knownPeers;
extern RxMode currentMode;
extern PeerInfo_t masterPeerInfo;
extern PeerInfo_t broadcastPeerInfo;
extern PeerInfo_t recvPeer;
extern std::deque<MacAddr> masterQueue; 
extern MacAddr currentQueue[30];
extern MacAddr myMacAddr;

extern unsigned long searchStart;
extern unsigned long lastRecvHeartbeat;
extern uint8_t lastAssignedId;
extern uint8_t nextMasterIdUp;
extern int offsetNewSearchWait;
extern volatile bool readyForMsg;
extern volatile msgTypes pendingMsg;
#endif
