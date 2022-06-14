//
// Copyright (C) 2009 by Alfonso Ariza
// Malaga University
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

package inet.routing.leach;

import inet.routing.contract.IManetRouting;

simple Leach like IManetRouting
{
    parameters:
        @display("i=block/routing");
        string interfaceTableModule;   // The path to the InterfaceTable module
        string routingTableModule;
        double helloInterval @unit(s) = default(5s);  // how often hello messages should be sent out
        string netmask = default("255.255.0.0");  // netmask for the ad-hoc network
        double maxVariance = default(1); // This is the maximum of a random value to determine when the first hello message will be sent out
        double routeLifetime @unit(s) = default(5s);  // the expiry time of the routing entries
        bool manetPurgeRoutingTables = default(true);
        volatile double broadcastDelay @unit(s) = default(uniform(0s,0.01s));
        
        double dataPktSendDelay @unit(s) = default(7s);
        double CHPktSendDelay @unit(s) = default(15s);
        double roundDuration @unit(s) = default(30s);
        
        int numNodes = default(10);
        double clusterHeadPercentage = default(0.5);
    gates:
        input ipIn;
        output ipOut;
}

