/*
server_app.h defines the main server class
- handles request processing and dispatch
- maintains account data and reply cache
- applies invocation semantics
- integrates monitor service and experiment modes
*/

#pragma once
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "models.h"
#include "monitor_service.h"
#include "../common/marshalling.h"

//main udp banking server
class ServerApp {
public:
    explicit ServerApp(uint16_t port, bool experiment_deposit_withdraw = false, bool experiment_view = false);
    ~ServerApp();

    void run();

private:

    //udp socket for communication
    int serverSocket_{-1};
    uint32_t next_account_{1};

    //experiment flags for simulating reply loss
    bool experiment_deposit_withdraw_{false};
    bool experiment_view_{false};

    //for tracking which request ids have already had their first reply dropped
    std::unordered_set<uint32_t> dropped_deposit_withdraw_reply_rids_;
    std::unordered_set<uint32_t> dropped_view_reply_rids_;

    //stores all accounts in memory
    std::unordered_map<uint32_t, Account> accounts_;

    //reply cache for at-most-once semantics
    std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash> reply_cache_;

    //monitor service for callback updates
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