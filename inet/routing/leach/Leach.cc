//
// Copyright (C) 2008
// DSDV simple example for INET (add-on)
// Version 2.0
// Diogo Antio & Pedro Menezes
// Instituto Superior Tecnico
// Lisboa - Portugal
// This version and newer version can be found at http://dsdv.8rf.com
// This code was written while assisting the course "Redes moveis e sem fios" http://comp.ist.utl.pt/ec-cm
// Autorization to use and modify this code not needed :P
// The authors hope it will be useful to help understand how
// INET and OMNET++ works(more specifically INET 20061020 and omnet++ 3.3).
// Also we hope it will help in the developing of routing protocols using INET.
//

#include "inet/common/IProtocolRegistrationListener.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/ProtocolTag_m.h"
#include "inet/linklayer/common/InterfaceTag_m.h"
#include "inet/networklayer/common/L3AddressTag_m.h"
#include "inet/routing/leach/Leach.h"
#include "inet/physicallayer/contract/packetlevel/SignalTag_m.h"
#include <list>
#include <vector>
#include <algorithm>
using namespace std;

namespace inet {

    int subIntervalTotalVar = 0;
    int nodeCounter = 0;
    int subIntervalCounter = 0;
    int intervalCounter = 1;

    struct nodeObject {
        Ipv4Address nodeAddr;
        Ipv4Address CHAddr;
        double energy;
    };

    vector<nodeObject> nodeList;
    vector<Ipv4Address> CHlist;
    double threshold;
    double randNo;
    Ipv4Address idealCH;

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

    // Bootstraps program per node
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

            dataPktSendDelay = par("dataPktSendDelay");
            CHPktSendDelay = par("CHPktSendDelay");
            roundDuration = par("roundDuration");

            helloInterval = par("helloInterval");
            event = new cMessage("event");

            WATCH(threshold);
            WATCH(randNo);
            WATCH(nodeCounter);
            WATCH(subIntervalCounter);
            WATCH(intervalCounter);

            controlPktSendSignal = registerSignal("controlPktSendSignal");
            dataPktSendSignal = registerSignal("dataPktSendSignal");
            bsPktSendSignal = registerSignal("bsPktSendSignal");
            subIntervalTot = registerSignal("subIntervalTot");
        }
        else if (stage == INITSTAGE_ROUTING_PROTOCOLS)
        {
            registerService(Protocol::manet, nullptr, gate("ipIn"));
            registerProtocol(Protocol::manet, gate("ipOut"), nullptr);
        }
    }

    void Leach::start()
    {
        /* Search the 80211 interface */
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

        // schedules a random periodic event: the hello message broadcast from DSDV module
        scheduleAt(simTime() + uniform(0.0, par("maxVariance").doubleValue()), event);
    }

    void Leach::stop()
    {
        cancelEvent(event);
        nodeList.clear();
        CHlist.clear();

    }

    void Leach::handleMessageWhenUp(cMessage *msg)
    {
        Ipv4Address nodeAddr = (interface80211ptr->getProtocolData<Ipv4InterfaceData>()->getIPAddress());
        // if node is sending message
        if (msg->isSelfMessage())
        {
            randNo = (double) rand()/RAND_MAX;
            threshold = generateThresholdValue(subIntervalCounter);

            if (randNo < threshold && !isNodeCH(nodeAddr)) {
                CHlist.push_back(nodeAddr);
                handleSelfMessage(msg);
            }

            nodeCounter += 1;
            if (subIntervalCounter <= (1/clusterHeadPercentage)) {
                if (nodeCounter > (numNodes-1)) {
                    nodeCounter = 0;
                    subIntervalCounter += 1;
                    subIntervalTotalVar += 1;
                    emit(subIntervalTot, subIntervalTotalVar);
                }
            } else {
                subIntervalCounter = 0;
                intervalCounter += 1;
                nodeList.clear();
                CHlist.clear();
            }

            // schedule another self message every time new one is received by node
            scheduleAt(simTime() + roundDuration, event);
        }
        // if node is receiving message
        else if (check_and_cast<Packet *>(msg)->getTag<PacketProtocolTag>()->getProtocol() == &Protocol::manet)
        {
            auto receivedCtrlPkt = !isForwardHello ? staticPtrCast<LeachControlPkt>(check_and_cast<Packet *>(msg)->peekData<LeachControlPkt>()->dupShared()) : nullptr;

            Packet * receivedPkt = check_and_cast<Packet *>(msg);
            auto& leachControlPkt = receivedPkt->popAtFront<LeachControlPkt>();

            auto packetType = leachControlPkt->getPacketType();

            // filter packet based on type and run specific functions
            if (msg->arrivedOn("ipIn")) {
               // packet from CH to NCH nodes
               if (packetType == 1 && !isNodeCH(nodeAddr)) {
                   Ipv4Address srcAddr = receivedCtrlPkt->getSrcAddress();

                   auto signalPowerInd = receivedPkt->getTag<SignalPowerInd>();
                   double rxPower = signalPowerInd->getPower().get();

                   addToNodeList(nodeAddr, srcAddr, rxPower);
                   sendData2CH(srcAddr,nodeAddr);

               // packet from NCH node to CH
               } else if (packetType == 2 && isNodeCH(nodeAddr)) {
                   sendData2BS(nodeAddr);

               // packet from CH to BS
               } else if (packetType == 3) {
                   delete msg;
               }
            } else {
                throw cRuntimeError("Message arrived on unknown gate %s", msg->getArrivalGate()->getName());
            }

        } else {
            throw cRuntimeError("Message not supported %s", msg->getName());
        }
    }

    
    void Leach::handleSelfMessage(cMessage *msg)
    {
        if (msg == event)
        {
            auto ctrlPkt = makeShared<LeachControlPkt>();

           // Filling the LeachControlPkt fields
            ctrlPkt->setPacketType(CH);
            Ipv4Address source = (interface80211ptr->getProtocolData<Ipv4InterfaceData>()->getIPAddress());
            ctrlPkt->setChunkLength(b(128)); //size of Hello message in bits
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
            packet = nullptr;
            ctrlPkt = nullptr;

            emit(controlPktSendSignal,packet);
            bubble("Sending new enrolment message");
        }
        else
        {
            delete msg;
        }
    }

    double Leach::generateThresholdValue(int subInterval) {
        double threshold = (clusterHeadPercentage/1-clusterHeadPercentage*(fmod(subInterval,(1.0/clusterHeadPercentage))));
        if (threshold == 0) {
            subIntervalCounter = 0;
        }
        return threshold;
    }

    void Leach::addToNodeList(Ipv4Address nodeAddr, Ipv4Address CHAddr, double energy){
        nodeObject node;
        node.nodeAddr = nodeAddr;
        node.CHAddr = CHAddr;
        node.energy = energy;
        nodeList.push_back(node);
    }

    void Leach::sendData2CH (Ipv4Address destAddr,Ipv4Address nodeAddr) {
        auto dataPkt = makeShared<LeachDataPkt>();
        dataPkt->setPacketType(NCH);
        double temperature = (double) rand()/RAND_MAX;
        double humidity = (double) rand()/RAND_MAX;

        dataPkt->setChunkLength(b(128));
        dataPkt->setTemperature(temperature);
        dataPkt->setHumidity(humidity);
        dataPkt->setSrcAddress(nodeAddr);

        auto dataPacket = new Packet("LEACHDataPkt", dataPkt);
        auto addressReq = dataPacket->addTag<L3AddressReq>();
//        addressReq->setDestAddress(destAddr);

        addressReq->setDestAddress(getIdealCH(nodeAddr));
        addressReq->setSrcAddress(nodeAddr);
        dataPacket->addTag<InterfaceReq>()->setInterfaceId(interface80211ptr->getInterfaceId());
        dataPacket->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
        dataPacket->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv4);

        sendDelayed(dataPacket, dataPktSendDelay, "ipOut");
        emit(dataPktSendSignal, dataPacket);
        dataPacket = nullptr;
        dataPkt = nullptr;

    }

    void Leach::sendData2BS(Ipv4Address CHAddr) {
        auto bsPkt = makeShared<LeachBSPkt>();
        bsPkt->setPacketType(CH);

        bsPkt->setChunkLength(b(128));
        bsPkt->setCHAddr(CHAddr);

        auto bsPacket = new Packet("LEACHBsPkt", bsPkt);
        auto addressReq = bsPacket->addTag<L3AddressReq>();

        addressReq->setDestAddress(Ipv4Address(10,0,0,1));
        addressReq->setSrcAddress(CHAddr);
        bsPacket->addTag<InterfaceReq>()->setInterfaceId(interface80211ptr->getInterfaceId());
        bsPacket->addTag<PacketProtocolTag>()->setProtocol(&Protocol::manet);
        bsPacket->addTag<DispatchProtocolReq>()->setProtocol(&Protocol::ipv4);

        sendDelayed(bsPacket, CHPktSendDelay, "ipOut");
//        send(bsPacket, "ipOut");
        bsPacket = nullptr;
        bsPkt = nullptr;
    }

    bool Leach::isNodeCH(Ipv4Address nodeAddr) {
        if (std::find(CHlist.begin(),CHlist.end(),nodeAddr) != CHlist.end()) {
            return true;
        }
        return false;
    }


    Ipv4Address Leach::getIdealCH(Ipv4Address nodeAddr) {
        Ipv4Address tempIdealCHAddr;
        double tempRxPower = 0.0;
        for (auto& it : nodeList) {

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

}

