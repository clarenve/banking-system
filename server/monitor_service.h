#pragma once

#include <vector>
#include <string>

#include "models.h"

class MonitorService {
public:
    void prune_expired_monitors();
    void register_or_refresh(const sockaddr_in& cli, uint32_t interval_in_seconds);
    void notify_monitors(int serverSocket, Opcode opcode, const Account& acc, const std::string& msg);

private:
    std::vector<MonitorClient> monitor_list_;
};