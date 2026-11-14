#include "network.h"
#include <algorithm>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#endif

std::vector<std::string> network_urls(int port) {
    std::vector<std::string> addresses;
    auto add=[&](const sockaddr* address){
        if(!address || address->sa_family!=AF_INET)return;
        char buffer[64]{};
        auto* value=&reinterpret_cast<const sockaddr_in*>(address)->sin_addr;
#ifdef _WIN32
        if(!InetNtopA(AF_INET,const_cast<in_addr*>(value),buffer,sizeof(buffer)))return;
#else
        if(!inet_ntop(AF_INET,value,buffer,sizeof(buffer)))return;
#endif
        std::string ip(buffer);
        if(ip.rfind("127.",0)!=0 && ip!="0.0.0.0")addresses.push_back("http://"+ip+":"+std::to_string(port));
    };
#ifdef _WIN32
    WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)!=0)return {};
    ULONG size=16000;std::vector<unsigned char> bytes(size);
    auto* adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());
    auto result=GetAdaptersAddresses(AF_INET,0,nullptr,adapters,&size);
    if(result==ERROR_BUFFER_OVERFLOW){bytes.resize(size);adapters=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());result=GetAdaptersAddresses(AF_INET,0,nullptr,adapters,&size);}
    if(result==NO_ERROR)for(auto* adapter=adapters;adapter;adapter=adapter->Next)
        if(adapter->OperStatus==IfOperStatusUp)for(auto* u=adapter->FirstUnicastAddress;u;u=u->Next)add(u->Address.lpSockaddr);
    WSACleanup();
#else
    ifaddrs* interfaces=nullptr;
    if(getifaddrs(&interfaces)==0){for(auto* i=interfaces;i;i=i->ifa_next)if(i->ifa_flags&IFF_UP)add(i->ifa_addr);freeifaddrs(interfaces);}
#endif
    std::sort(addresses.begin(),addresses.end());addresses.erase(std::unique(addresses.begin(),addresses.end()),addresses.end());
    return addresses;
}
