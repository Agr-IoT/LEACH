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

import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;
import inet.networklayer.contract.ipv4.Ipv4Address;

cplusplus {{
    #include <vector>
    #include "inet/common/INETDefs.h"
    #include "Leach.h"
    #include "inet/networklayer/contract/ipv4/Ipv4Address.h"
    using namespace std;
    using namespace inet;
    
    struct BSObject {
        Ipv4Address nodeAddr;
        Ipv4Address sourceAddr;
        double temperature;
        double humidity;
    }; 
    
    typedef vector<BSObject> testList;
}}

namespace inet;

enum LeachPktType {
    CH = 1;
    NCH = 2;
    BS = 3;
}

class LeachControlPkt extends FieldsChunk {
	Ipv4Address srcAddress;
	
	LeachPktType packetType = static_cast<LeachPktType>(-1); 
}

class LeachDataPkt extends LeachControlPkt {
    double temperature;
	double humidity;
}

//TODO: implement vector to include individual aggregated data from CHs
class LeachBSPkt extends LeachControlPkt {
    Ipv4Address CHAddr;
}	


