#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "models.h"
#include "monitor_service.h"
#include "../common/marshalling.h"

class ServerApp {
public:
    explicit ServerApp(uint16_t port);
    ~ServerApp();

    void run();

private:
    int serverSocket_{-1};
    uint32_t next_account_{1};

    std::unordered_map<uint32_t, Account> accounts_;
    std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash> reply_cache_;
    MonitorService monitor_service_;

    void handle_open_account(ByteReader& br,
                             uint32_t rid,
                             uint8_t semantic,
                             const ReqID& req_id,
                             const sockaddr_in& cli,
                             socklen_t clen);

    void handle_close_account(ByteReader& br,
                              uint32_t rid,
                              uint8_t semantic,
                              const ReqID& req_id,
                              const sockaddr_in& cli,
                              socklen_t clen);

    void handle_deposit_or_withdrawal(ByteReader& br,
                                      uint32_t rid,
                                      uint8_t semantic,
                                      Opcode opcode,
                                      const ReqID& req_id,
                                      const sockaddr_in& cli,
                                      socklen_t clen);

    void handle_monitor(ByteReader& br,
                        uint32_t rid,
                        uint8_t semantic,
                        const ReqID& req_id,
                        const sockaddr_in& cli,
                        socklen_t clen);

    void handle_view_account(ByteReader& br,
                             uint32_t rid,
                             uint8_t semantic,
                             const ReqID& req_id,
                             const sockaddr_in& cli,
                             socklen_t clen);

    void handle_transfer(ByteReader& br,
                         uint32_t rid,
                         uint8_t semantic,
                         const ReqID& req_id,
                         const sockaddr_in& cli,
                         socklen_t clen);

    void cache_reply_if_needed(uint8_t semantic,
                               const ReqID& req_id,
                               const std::vector<uint8_t>& reply);

    void send_buffer_to_client(const std::vector<uint8_t>& buffer,
                               const sockaddr_in& cli,
                               socklen_t clen);
};