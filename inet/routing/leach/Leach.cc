

#include "inet/common/IProtocolRegistrationListener.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/routing/leach/Leach.h"
#include "inet/physicallayer/contract/packetlevel/SignalTag_m.h"
#include "inet/networklayer/common/L3AddressResolver.h"

#include <list>
#include <vector>
#include <algorithm>
#include <time.h>
#include <functional>
#include <iostream>
#include <fstream>

using namespace std;
using namespace power;

namespace inet {

    // objects for storing node metadata in vector array
/*
    struct nodeObject {
        Ipv4Address nodeAddr;
        Ipv4Address CHAddr;
        double energy;
    };
    */

    // object for storing packet fingerprint in vector array
    struct packetLogEntry {
        string fingerprint;
    };

    struct eventLogEntry {
        string srcNodeName;
        string destNodeName;
        double time;
        string packet;
        string type;
        C residualCapacity;
        string state;
    };

    struct nodePositionEntry {
        string nodeName;
        double posX;
        double posY;
    };

    struct nodeWeightObject {
        string nodeName;
        int weight;
    };

    vector<Ipv4Address> CHlist;
    vector<packetLogEntry> packetLog;
    vector<eventLogEntry> eventLog;
    vector<nodePositionEntry> nodePositionList;
    vector<nodeWeightObject> nodeWeightList;

    double threshold;
    double randNo;
    Ipv4Address idealCH;

    Register_Enum(inet::Leach, (Leach::ch, Leach::nch));
    Define_Module(Leach);

    Leach::ForwardEntry::~ForwardEntry()
    {
        if (this->event!=nullptr) delete this->event;
        if (this->hello!=nullptr) delete this->hello;
    }

    Leach::Leach()
    {
    }

    Leach::~Leach()
    {
        stop();
       // Dispose of dynamically allocated the objects
        delete event;

    }

    void Leach::initialize(int stage)
    {
        RoutingProtocolBase::initialize(stage);

        //reads from omnetpp.ini
        if (stage == INITSTAGE_LOCAL)
        {
            sequencenumber = 0;
            host = getContainingNode(this);
            ift = getModuleFromPar<IInterfaceTable>(par("interfaceTableModule"), this);

            clusterHeadPercentage = par("clusterHeadPercentage");
            numNodes = par("numNodes");

            dataPktSent = 0;
            dataPktReceived = 0;
            dataPktReceivedVerf = 0;
            controlPktSent = 0;
            controlPktReceived = 0;
            bsPktSent = 0;

            dataPktSendDelay = dblrand(0)*10;
//            dataPktSendDelay = par("dataPktSendDelay");
            CHPktSendDelay = par("CHPktSendDelay");
            roundDuration = dblrand(0)*10;
//            roundDuration = par("roundDuration");

            TDMADelayCounter = 1;

            helloInterval = par("helloInterval");
            event = new cMessage("event");

            WATCH(threshold);

            roundDuration = dblrand(0)*10;
            round = 0;
            weight = 0;
            wasCH = false;

            vector<nodeMemoryObject> nodeMemory(numNodes);                  // Localized NCH node memory with CH data
            vector<TDMAScheduleEntry> nodeCHMemory(numNodes);               // Localized CH memory
            vector<TDMAScheduleEntry> extractedTDMASchedule(numNodes);      // Localized NCH node memory with extracted TDMA data

        }
        else if (stage == INITSTAGE_ROUTING_PROTOCOLS)
        {
            registerService(Protocol::manet, nullptr, gate("ipIn"));
            registerProtocol(Protocol::manet, gate("ipOut"), nullptr);
        }
    }

    void Leach::start()
    {
        // Add node position to a vector array
        addToNodePosList();

        // Search the 802154 interface
        int  num_802154 = 0;
        InterfaceEntry *ie;
        InterfaceEntry *i_face;
        const char *name;
        broadcastDelay = &par("broadcastDelay");
        for (int i = 0; i < ift->getNumInterfaces(); i++)
        {
            ie = ift->getInterface(i);
            name = ie->getInterfaceName();
            if (strstr(name, "wlan") != nullptr)
            {
                i_face = ie;
                num_802154++;
                interfaceId = i;
            }
        }

        // One enabled network interface (in total)
        if (num_802154 == 1)
            interface80211ptr = i_face;
        else
            throw cRuntimeError("DSDV has found %i 80211 interfaces", num_802154);
        CHK(interface80211ptr->getProtocolData<Ipv4InterfaceData>())->joinMulticastGroup(Ipv4Address::LL_MANET_ROUTERS);

        // schedules a random periodic event
        event->setKind(SELF);
        scheduleAt(simTime() + uniform(0.0, par("maxVariance").doubleValue()), event);
    }

    void Leach::stop()
    {
        cancelEvent(event);
        nodeMemory.clear();
        nodeCHMemory.clear();
        extractedTDMASchedule.clear();

        TDMADelayCounter = 1;
    }

    void Leach::handleMessageWhenUp(cMessage *msg)
    {
        // if node is sending message
        if (msg->isSelfMessage())
        {
            double randNo = dblrand(0);
            threshold = generateThresholdValue(round);

            if (randNo < threshold && wasCH == false) {
                weight++;
                setLeachState(ch);               // Set state for GUI visualization
                wasCH = true;
                handleSelfMessage(msg);
            }

            round += 1;
            int intervalLength = 1.0/clusterHeadPercentage;
            if (fmod(round,intervalLength) == 0) {
                wasCH = false;
                nodeMemory.clear();
                nodeCHMemory.clear();
                extractedTDMASchedule.clear();
                TDMADelayCounter = 1;
            }

            // schedule another self message every time new one is received by node
            event->setKind(SELF);
            scheduleAt(simTime() + roundDuration, event);
        }
        // if node is receiving message
        else if (check_and_cast<Packet *>(msg)->getTag<PacketProtocolTag>()->getProtocol() == &Protocol::manet)
        {
            processMessage(msg);

        } else {
            throw cRuntimeError("Message not supported %s", msg->getName());
        }
    }

    void Leach::handleSelfMessage(cMessage *msg)
    {
        if (msg == event)
        {
            if (event->getKind() == SELF) {
                auto ctrlPkt = makeShared<LeachControlPkt>();

                // Filling the LeachControlPkt fields
                ctrlPkt->setPacketType(CH);
                Ipv4Address source = (interface80211ptr->getProtocolData<Ipv4InterfaceData>()->getIPAddress());
                ctrlPkt->setChunkLength(b(128)); ///size of Hello message in bits
                ctrlPkt->setSrcAddress(source);

                //new control info for LeachControlPkt
                auto packet = new Packet("LEACHControlPkt", ctrlPkt);
                auto addressReq = packet->addTag<L3AddressReq>();
                addressReq->setDestAddress(Ipv4Address(255, 255, 255, 255)); //let's try the limited broadcast 255.255.255.255 but multicast goes from 224.0.0.0 to 239.255.255.255
                addressReq->setSrcAddress(source); //let's try the limited broadcast
                packet->addTag<InterfaceReq>()->setInterfaceId(interface80211ptr->getInterfaceId());
                packet->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
                packet->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv4);

                //broadcast to other nodes the hello message
                send(packet, "ipOut");
                addToEventLog(source, Ipv4Address(255, 255, 255, 255), "CTRL", "SENT");
                controlPktSent += 1;
                packet = nullptr;
                ctrlPkt = nullptr;

                bubble("Sending new enrolment message");
            }
        }
        else
        {
            delete msg;
        }
    }

    void Leach::processMessage(cMessage *msg) {
        Ipv4Address selfAddr = (interface80211ptr->getProtocolData<Ipv4InterfaceData>()->getIPAddress());
        auto receivedCtrlPkt = staticPtrCast<LeachControlPkt>(check_and_cast<Packet *>(msg)->peekData<LeachControlPkt>()->dupShared());
//        auto receivedCtrlPkt = !isForwardHello ? staticPtrCast<LeachControlPkt>(check_and_cast<Packet *>(msg)->peekData<LeachControlPkt>()->dupShared()) : nullptr;
        Packet * receivedPkt = check_and_cast<Packet *>(msg);
        auto& leachControlPkt = receivedPkt->popAtFront<LeachControlPkt>();

        auto packetType = leachControlPkt->getPacketType();

        // filter packet based on type and run specific functions
        if (msg->arrivedOn("ipIn")) {
           // first broadcast from CH to NCH nodes
           if (packetType == 1) {                      //  && leachState == nch
               controlPktReceived += 1;
               Ipv4Address CHAddr = receivedCtrlPkt->getSrcAddress();
               addToEventLog(CHAddr, selfAddr, "CTRL", "REC");

               auto signalPowerInd = receivedPkt->getTag<SignalPowerInd>();
               double rxPower = signalPowerInd->getPower().get();

               addToNodeMemory(selfAddr, CHAddr, rxPower);
               sendAckToCH(selfAddr, CHAddr);

           // ACK packet from NCH node to CH
           } else if (packetType == 2 && leachState == ch) {
               Ipv4Address nodeAddr= receivedCtrlPkt->getSrcAddress();
               addToEventLog(nodeAddr, selfAddr, "ACK", "REC");

               addToNodeCHMemory(nodeAddr);
               if (nodeCHMemory.size() > 2) {
                   sendSchToNCH(selfAddr);
               }

           // TDMA schedule from CH to NCH
           } else if (packetType == 3) {               //  && leachState == nch
               Ipv4Address CHAddr = receivedCtrlPkt->getSrcAddress();
               addToEventLog(CHAddr, selfAddr, "SCH", "REC");

               int scheduleArraySize = receivedCtrlPkt->getScheduleArraySize();
               // Traverses through schedule array in packets and sets values into a vector in local node memory
               for (int counter = 0; counter < scheduleArraySize; counter++) {
                   ScheduleEntry tempScheduleEntry = receivedCtrlPkt->getSchedule(counter);
                   TDMAScheduleEntry extractedTDMAScheduleEntry;
                   extractedTDMAScheduleEntry.nodeAddress = tempScheduleEntry.getNodeAddress();
                   extractedTDMAScheduleEntry.TDMAdelay = tempScheduleEntry.getTDMAdelay();
                   extractedTDMASchedule.push_back(extractedTDMAScheduleEntry);
               }

               // Finds TDMA slot for self by traversing through earlier generated vector
               double receivedTDMADelay = -1;
               for (auto& it : extractedTDMASchedule) {
                   if (it.nodeAddress == selfAddr) {
                       receivedTDMADelay = it.TDMAdelay;
                       break;
                   }
               }

               if (receivedTDMADelay > -1) {                        // Only sends data to CH if self address is included in schedule
                   sendDataToCH(selfAddr,CHAddr,receivedTDMADelay);
               }
           // Data packet from NCH to CH
           } else if (packetType == 4) {
               Ipv4Address NCHAddr = receivedCtrlPkt->getSrcAddress();
               addToEventLog(NCHAddr, selfAddr, "DATA", "REC");
               string fingerprint = receivedCtrlPkt->getFingerprint();

               if (checkFingerprint(fingerprint)) {
                   dataPktReceivedVerf += 1;
               }
               dataPktReceived += 1;
               sendDataToBS(selfAddr,fingerprint);

           // BS packet from CH to BS - deleted in the case of standard nodes
           } else if (packetType == 5) {
               delete msg;
           }
        } else {
            throw cRuntimeError("Message arrived on unknown gate %s", msg->getArrivalGate()->getName());
        }
    }

    // Runs during node shutdown events
    void Leach::handleStopOperation(LifecycleOperation *operation) {
        cancelEvent(event);
    }


    // Runs during node crash events
    void Leach::handleCrashOperation(LifecycleOperation *operation) {
        cancelEvent(event);
    }

    double Leach::Leach::generateThresholdValue(int round) {
        int intervalLength = 1.0/clusterHeadPercentage;
        double threshold = (clusterHeadPercentage/(1-clusterHeadPercentage*(fmod(round,intervalLength))));
        if (threshold == 1) {
            round = 0;
        }
        return threshold;
    }

    void Leach::addToNodeMemory(Ipv4Address nodeAddr, Ipv4Address CHAddr, double energy){
        if (!isCHAddedInMemory(CHAddr)) {
            nodeMemoryObject node;
            node.nodeAddr = nodeAddr;
            node.CHAddr = CHAddr;
            node.energy = energy;
            nodeMemory.push_back(node);
        }
    }

    void Leach::addToNodeCHMemory(Ipv4Address NCHAddr) {
        if (!isNCHAddedInCHMemory(NCHAddr)) {
            TDMAScheduleEntry scheduleEntry;
            scheduleEntry.nodeAddress = NCHAddr;
            scheduleEntry.TDMAdelay = TDMADelayCounter;
            nodeCHMemory.push_back(scheduleEntry);
            TDMADelayCounter++;                             // Counter increases and sets slots for NCH transmission time
        }
    }

    bool Leach::isCHAddedInMemory(Ipv4Address CHAddr) {
        for (auto& it : nodeMemory) {
            if (it.CHAddr == CHAddr) {
                return true;
            }
        }
        return false;
    }

    bool Leach::isNCHAddedInCHMemory(Ipv4Address NCHAddr){
        for (auto& it : nodeCHMemory) {
            if (it.nodeAddress == NCHAddr) {
                return true;
            }
        }
        return false;
    }

    void Leach::generateTDMASchedule() {
        for (auto& it : nodeCHMemory) {
            ScheduleEntry scheduleEntry;
            scheduleEntry.setNodeAddress(it.nodeAddress);
            scheduleEntry.setTDMAdelay(it.TDMAdelay);

        }
    }

    void Leach::setLeachState(LeachState ls) {
        leachState = ls;
    }

    void Leach::sendAckToCH(Ipv4Address nodeAddr, Ipv4Address CHAddr) {
        auto ackPkt = makeShared<LeachAckPkt>();
        ackPkt->setPacketType(ACK);
        ackPkt->setChunkLength(b(128)); ///size of Hello message in bits
        ackPkt->setSrcAddress(nodeAddr);

        auto ackPacket = new Packet("LeachAckPkt",ackPkt);
        auto addressReq = ackPacket->addTag<L3AddressReq>();

        addressReq->setDestAddress(getIdealCH(nodeAddr));
        addressReq->setSrcAddress(nodeAddr);
        ackPacket->addTag<InterfaceReq>()->setInterfaceId(interface80211ptr->getInterfaceId());
        ackPacket->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
        ackPacket->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv4);

        send(ackPacket, "ipOut");
        addToEventLog(nodeAddr, getIdealCH(nodeAddr), "ACK", "SENT");
    }

    void Leach::sendSchToNCH(Ipv4Address selfAddr) {
        auto schedulePkt = makeShared<LeachSchedulePkt>();
        schedulePkt->setPacketType(SCH);
        schedulePkt->setChunkLength(b(128)); ///size of Hello message in bits
        schedulePkt->setSrcAddress(selfAddr);

        for (auto& it : nodeCHMemory) {
            ScheduleEntry scheduleEntry;
            scheduleEntry.setNodeAddress(it.nodeAddress);
            scheduleEntry.setTDMAdelay(it.TDMAdelay);
            schedulePkt->insertSchedule(scheduleEntry);
        }

        auto schedulePacket = new Packet("LeachSchedulePkt", schedulePkt);
        auto scheduleReq = schedulePacket->addTag<L3AddressReq>();

        scheduleReq->setDestAddress(Ipv4Address(255, 255, 255, 255));
        scheduleReq->setSrcAddress(selfAddr);
        schedulePacket->addTag<InterfaceReq>()->setInterfaceId(interface80211ptr->getInterfaceId());
        schedulePacket->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
        schedulePacket->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv4);

        send(schedulePacket, "ipOut");
        addToEventLog(selfAddr, Ipv4Address(255, 255, 255, 255), "SCH", "SENT");
    }

    void Leach::sendDataToCH (Ipv4Address nodeAddr, Ipv4Address CHAddr, double TDMAslot) {
        auto dataPkt = makeShared<LeachDataPkt>();
        dataPkt->setPacketType(DATA);
        double temperature = (double) rand()/RAND_MAX;
        double humidity = (double) rand()/RAND_MAX;
        string fingerprint = resolveFingerprint(nodeAddr, getIdealCH(nodeAddr));

        dataPkt->setChunkLength(b(128));
        dataPkt->setTemperature(temperature);
        dataPkt->setHumidity(humidity);
        dataPkt->setSrcAddress(nodeAddr);
        dataPkt->setFingerprint(fingerprint.c_str());
        addToPacketLog(fingerprint);

        auto dataPacket = new Packet("LEACHDataPkt", dataPkt);
        auto addressReq = dataPacket->addTag<L3AddressReq>();
//        addressReq->setDestAddress(destAddr);

        addressReq->setDestAddress(getIdealCH(nodeAddr));
        addressReq->setSrcAddress(nodeAddr);
        dataPacket->addTag<InterfaceReq>()->setInterfaceId(interface80211ptr->getInterfaceId());
        dataPacket->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
        dataPacket->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv4);

        sendDelayed(dataPacket, TDMAslot, "ipOut");
//        send(dataPacket,"ipOut");
        addToEventLog(nodeAddr, getIdealCH(nodeAddr), "DATA", "SENT");    // self-address is source address.
        dataPktSent += 1;
        dataPacket = nullptr;
        dataPkt = nullptr;

    }

    void Leach::sendDataToBS(Ipv4Address CHAddr, string fingerprint) {
        auto bsPkt = makeShared<LeachBSPkt>();
        bsPkt->setPacketType(BS);

        bsPkt->setChunkLength(b(128));
        bsPkt->setCHAddr(CHAddr);
        bsPkt->setFingerprint(fingerprint.c_str());

        auto bsPacket = new Packet("LEACHBsPkt", bsPkt);
        auto addressReq = bsPacket->addTag<L3AddressReq>();

        addressReq->setDestAddress(Ipv4Address(10,0,0,1));
        addressReq->setSrcAddress(CHAddr);
        bsPacket->addTag<InterfaceReq>()->setInterfaceId(interface80211ptr->getInterfaceId());
        bsPacket->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
        bsPacket->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv4);


//        sendDelayed(bsPacket, TDMADelayCounter, "ipOut");
        send(bsPacket, "ipOut");

        bsPktSent += 1;
        bsPacket = nullptr;
        bsPkt = nullptr;
        setLeachState(nch);     // Set state for GUI visualization
    }

    Ipv4Address Leach::getIdealCH(Ipv4Address nodeAddr) {
        Ipv4Address tempIdealCHAddr;
        double tempRxPower = 0.0;
        for (auto& it : nodeMemory) {
            if (it.nodeAddr == nodeAddr) {
                if (it.energy > tempRxPower) {
                    tempRxPower = it.energy;
                    tempIdealCHAddr = it.CHAddr;
                    continue;
                }
            } else {
                continue;
            }
        }
        return tempIdealCHAddr;
    }

    string Leach::resolveFingerprint(Ipv4Address nodeAddr, Ipv4Address CHAddr) {
        string CHAddrResolved = to_string(CHAddr.getInt());
        string nodeAddrResolved = to_string(nodeAddr.getInt());
        string simTimeResolved = to_string(simTime().dbl());

        hash<string> hashFn;
        size_t hashResolved = hashFn(CHAddrResolved + nodeAddrResolved + simTimeResolved);
        return to_string(hashResolved);
    }

    bool Leach::checkFingerprint(string fingerprint) {
        for (auto& it : packetLog) {
            if (it.fingerprint == fingerprint) {
                return true;
            }
        }
        return false;
    }

    void Leach::addToPacketLog(string fingerprint) {
        packetLogEntry packet;
        packet.fingerprint = fingerprint;
        packetLog.push_back(packet);
    }

    void Leach::addToEventLog(Ipv4Address srcAddr, Ipv4Address destAddr, string packet, string type) {
        const char* srcNodeName = L3AddressResolver().findHostWithAddress(srcAddr)->getFullName();
        const char* destNodeName;
        if (destAddr.isLimitedBroadcastAddress()) {
            destNodeName = "Broadcast";
        } else {
            destNodeName = L3AddressResolver().findHostWithAddress(destAddr)->getFullName();
        }

        ICcEnergyStorage *energyStorageModule = check_and_cast<ICcEnergyStorage *>(host->getSubmodule("energyStorage"));
        C residualCapacity = energyStorageModule->getResidualChargeCapacity();

        eventLogEntry nodeEvent;
        nodeEvent.srcNodeName = srcNodeName;
        nodeEvent.destNodeName = destNodeName;
        nodeEvent.time = simTime().dbl()*1000;
        nodeEvent.packet = packet;
        nodeEvent.type = type;
        nodeEvent.residualCapacity = residualCapacity;
        eventLog.push_back(nodeEvent);
    }

    void Leach::addToNodePosList() {
        IMobility *mobilityModule = check_and_cast<IMobility *>(host->getSubmodule("mobility"));
        Coord pos = mobilityModule->getCurrentPosition();

        nodePositionEntry nodePosition;
        nodePosition.nodeName = host->getFullName();
        nodePosition.posX = pos.getX();
        nodePosition.posY = pos.getY();
        nodePositionList.push_back(nodePosition);
    }

    void Leach::addToNodeWeightList() {
        nodeWeightObject nodeWeight;
        nodeWeight.nodeName = host->getFullName();
        nodeWeight.weight = weight;
        nodeWeightList.push_back(nodeWeight);
    }

    void Leach::generateEventLogCSV() {
        std::ofstream eventLogFile("eventLog.csv");
//        eventLogFile << "Source node,Destination node,Time,Packet,Type" << std::endl;
//        for (auto& it : eventLog) {
//            eventLogFile << it.srcNodeName + "," + it.destNodeName + "," + to_string(it.time) + "," + it.packet + "," + it.type << std::endl;
//        }
        eventLogFile << "Time,Node,Rx-Tx Node,Packet,Type,Charge" << std::endl;
        for (auto& it : eventLog) {
            string resolvedResidualCapacity = it.residualCapacity.str().erase(it.residualCapacity.str().size() - 2, 2); // remove the C unit from end of string
            if (it.type == "SENT") {
                eventLogFile << to_string(it.time) + "," + it.srcNodeName + "," + it.destNodeName + "," + it.packet + "," + it.type + ","
                        + resolvedResidualCapacity << std::endl;
            } else {
                eventLogFile << to_string(it.time) + "," + it.destNodeName + "," + it.srcNodeName + "," + it.packet + "," + it.type + ","
                        + resolvedResidualCapacity << std::endl;
            }
        }
        eventLogFile.close();
    }

    void Leach::generateNodePosCSV() {
        std::ofstream nodePosFile("nodePos.csv");
//        nodePosFile << "Node,X,Y" << std::endl;
//        for (auto& it : nodePositionList) {
//            nodePosFile << it.nodeName + "," + to_string(it.posX) + "," + to_string(it.posY) << std::endl;
//        }
        nodePosFile << "Node,X,Y,weight" << std::endl;
        for (auto& positionIterator : nodePositionList) {
            for (auto& weightIterator : nodeWeightList) {
                if (positionIterator.nodeName == weightIterator.nodeName) {
                    nodePosFile << positionIterator.nodeName + "," + to_string(positionIterator.posX) + "," + to_string(positionIterator.posY) + ","
                            + to_string(weightIterator.weight) << std::endl;
                    break;
                }
            }
        }
        nodePosFile.close();
    }

    void Leach::generatePacketLogCSV() {
        std::ofstream packetLogFile("packetLog.csv");
        packetLogFile << "Data-Sent" << std::endl;
        for (auto& packetLogIterator : packetLog) {
            packetLogFile << packetLogIterator.fingerprint << std::endl;
        }
        packetLogFile.close();
    }

    void Leach::refreshDisplay() const {
        const char *icon;
        switch (leachState) {
            case nch:
                icon = "";
                break;
            case ch:
                icon = "status/green";
                break;
            default:
                throw cRuntimeError("Unknown LEACH status");
        }
        auto& displayString = getDisplayString();
        if (*icon) {
            displayString.setTagArg("i2", 0, icon);
            host->getDisplayString().setTagArg("i2", 0, icon);
        } else {
            displayString.removeTag("i2");
            host->getDisplayString().removeTag("i2");
        }
    }

    void Leach::finish() {
        addToNodeWeightList();
        generateEventLogCSV();
        generateNodePosCSV();
        generatePacketLogCSV();

        EV << "Total control packets sent by CH:                    " << controlPktSent << endl;
        EV << "Total control packets received by NCHs from CH:      " << controlPktReceived << endl;
        EV << "Total data packets sent to CH:                               "<< dataPktSent << endl;
        EV << "Total data packets received by CH from NCHs:                 "<< dataPktReceived << endl;
        EV << "Total data packets received by CH from NCHs verified:        "<< dataPktReceivedVerf << endl;
        EV << "Total BS packets sent by CH:                         " << bsPktSent << endl;

        recordScalar("#dataPktSent", dataPktSent);
        recordScalar("#dataPktReceived", dataPktReceived);
        recordScalar("#dataPktReceivedVerf", dataPktReceivedVerf);
        recordScalar("#controlPktSent", controlPktSent);
        recordScalar("#controlPktReceived", controlPktReceived);
        recordScalar("#bsPktSent", bsPktSent);
    }

}

