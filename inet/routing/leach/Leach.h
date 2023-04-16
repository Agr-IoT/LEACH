

#ifndef __INET_DSDV_H
#define __INET_DSDV_H

#include <list>
#include <map>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>

#include "inet/common/INETDefs.h"
#include "inet/common/packet/Packet.h"
#include "inet/networklayer/contract/IInterfaceTable.h"
#include "inet/networklayer/contract/ipv4/Ipv4Address.h"
#include "inet/networklayer/ipv4/IIpv4RoutingTable.h"
#include "inet/networklayer/ipv4/Ipv4Header_m.h"
#include "inet/networklayer/ipv4/Ipv4InterfaceData.h"
#include "inet/networklayer/ipv4/Ipv4RoutingTable.h"
#include "inet/routing/base/RoutingProtocolBase.h"
#include "inet/routing/leach/LeachPkts_m.h"
using namespace std;


namespace inet {

/**
 * DSDV protocol implementation.
 */
class INET_API Leach : public RoutingProtocolBase
{

  private:
    struct ForwardEntry
    {
        cMessage *event = nullptr;
        Packet *hello = nullptr;

        ForwardEntry() {}
        ~ForwardEntry();
    };

    bool isForwardHello = false;
    cMessage *event = nullptr;
    cPar *broadcastDelay = nullptr;
    std::list<ForwardEntry *> *forwardList  = nullptr;
    InterfaceEntry *interface80211ptr = nullptr;
    int interfaceId = -1;
    unsigned int sequencenumber = 0;
    simtime_t routeLifetime;
    cModule *host = nullptr;

    Ipv4Address idealCH;

    cMessage *event = nullptr;

    simsignal_t subIntervalTot;

    int dataPktSent;
    int dataPktReceived;

    int controlPktSent;
    int controlPktReceived;
    int bsPktSent;
    int round;
    int weight;

  protected:
    simtime_t helloInterval;
    IInterfaceTable *ift = nullptr;
    IIpv4RoutingTable *rt = nullptr;

    simtime_t dataPktSendDelay;
    simtime_t CHPktSendDelay;
    simtime_t roundDuration;

    int numNodes = 0;
    double clusterHeadPercentage = 0.0;
    int dataPktReceivedVerf;


  public:
    Leach();
    ~Leach();
    enum LeachState { nch, ch }; // written in lower caps as it conflicts with enum in LeachPkts.msg file

    LeachState leachState;
    double TDMADelayCounter;

    // object for storing CH data per node in vector array
    struct nodeMemoryObject {
        Ipv4Address nodeAddr;
        Ipv4Address CHAddr;
        double energy;
    };

    struct TDMAScheduleEntry {
        Ipv4Address nodeAddress;
        double TDMAdelay;
    };

    vector<nodeMemoryObject> nodeMemory;
    vector<TDMAScheduleEntry> nodeCHMemory;
    vector<TDMAScheduleEntry> extractedTDMASchedule;
    bool checkFingerprint(string fingerprint);
    bool wasCH;

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleMessageWhenUp(cMessage *msg) override;

    void handleSelfMessage(cMessage *msg);
    void processMessage (cMessage *msg);

    // lifecycle
    virtual void handleStartOperation(LifecycleOperation *operation) override { start(); }
    virtual void handleStopOperation(LifecycleOperation *operation) override;
    virtual void handleCrashOperation(LifecycleOperation *operation) override;
    void start();
    void stop();
    virtual void refreshDisplay() const override;
    void finish();
    enum SelfMsgKinds { SELF = 1, DATA2CH, DATA2BS };

    double generateThresholdValue(int subInterval);

    void sendDataToCH(Ipv4Address nodeAddr, Ipv4Address CHAddr, double TDMAslot);
    void sendDataToBS(Ipv4Address CHAddr, string fingerprint);
    void sendAckToCH(Ipv4Address nodeAddr, Ipv4Address CHAddr);
    void sendSchToNCH(Ipv4Address selfAddr);

    void addToNodeMemory(Ipv4Address nodeAddr, Ipv4Address CHAddr, double energy);
    void addToNodeCHMemory(Ipv4Address NCHAddr);
    bool isCHAddedInMemory(Ipv4Address CHAddr);
    bool isNCHAddedInCHMemory(Ipv4Address NCHAddr);
    void generateTDMASchedule();

    virtual void setLeachState(LeachState ls);

    Ipv4Address getIdealCH(Ipv4Address nodeAddr);
    string resolveFingerprint(Ipv4Address nodeAddr,Ipv4Address CHAddr);

    void addToPacketLog(string fingerprint);
    void addToEventLog(Ipv4Address srcAddr, Ipv4Address destAddr, string packet, string type);
    void addToNodePosList();
    void addToNodeWeightList();

    void generateEventLogCSV();
    void generateNodePosCSV();
    void generatePacketLogCSV();
};


} // namespace inet

#endif // ifndef __INET_DSDV_H

