/*
monitor_service.cpp implements monitor registration and callbacks
- handles adding and refreshing monitor clients
- removes expired monitor clients
- sends updates when account state changes
*/

#include <iostream>
#include <algorithm>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "monitor_service.h"
#include "../common/protocol.h"
#include "../common/marshalling.h"

//remove monitor clients whose subscription interval has expired
void MonitorService::prune_expired_monitors(){
    auto now = std::chrono::steady_clock::now();

    monitor_list_.erase(
        std::remove_if(
            monitor_list_.begin(),
            monitor_list_.end(),
            [&now](const MonitorClient& m){
                return m.expiry <= now;
            }
        ),
        monitor_list_.end()
    );
}

//add new monitor client or refresh existing subscription
void MonitorService::register_or_refresh(const sockaddr_in& cli, uint32_t interval_in_seconds){
    prune_expired_monitors();

    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(interval_in_seconds);

    bool registered_client = false;
    for(auto& m : monitor_list_){
        if((m.addr.sin_addr.s_addr == cli.sin_addr.s_addr) &&
           (m.addr.sin_port == cli.sin_port)){
            registered_client = true;
            m.expiry = expiry;
            break;
        }
    }

    if(!registered_client){
        MonitorClient m{};
        m.addr = cli;
        m.expiry = expiry;
        monitor_list_.push_back(m);
    }
}

//send callback updates to all active monitor clients
void MonitorService::notify_monitors(int serverSocket, Opcode opcode, const Account& acc, const std::string& msg){
    prune_expired_monitors();

    ByteWriter bw;
    bw.u8(version_number);
    bw.u8(static_cast<uint8_t>(opcode));
    bw.u16(0);
    bw.str_with_len(acc.name);
    bw.u32(acc.aid);
    bw.u8(static_cast<uint8_t>(acc.currency));
    bw.u64(double_to_u64(acc.balance));
    bw.str_with_len(msg);

    for(const auto& m : monitor_list_){
        ssize_t bytes_sent = sendto(
            serverSocket,
            bw.buffer.data(),
            bw.buffer.size(),
            0,
            (const sockaddr*)&m.addr,
            sizeof(m.addr)
        );

        if(bytes_sent < 0){
            perror("sendto");

            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &m.addr.sin_addr, ip, sizeof(ip));
            uint16_t port = ntohs(m.addr.sin_port);

            std::cout << "Failed to send update to client "
                      << ip << ":" << port << "\n";
        }
    }
}