#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>

#include "../common/protocol.h"
#include "../common/marshalling.h"

struct Account{
    uint32_t aid;
    std::string name;
    std::string password;
    Currency currency;
    double balance;
};

struct ReqID{
    uint32_t rid;
    uint32_t ip;
    uint16_t port;

    bool operator==(const ReqID& o) const{
        return rid == o.rid && ip == o.ip && port == o.port;
    }
};

struct ReqIDHash{
    size_t operator()(const ReqID& k) const{
        return (size_t)k.rid ^ ((size_t)k.ip << 1) ^ ((size_t)k.port << 16);
    }
};

struct MonitorClient{
    sockaddr_in addr{};
    std::chrono::steady_clock::time_point expiry;
};

ByteWriter build_reply_header(Status status, uint32_t rid);

void handle_open_account(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, uint32_t& next_account, std::unordered_map<uint32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache, std::vector<MonitorClient>& monitor_list);
void handle_close_account(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, std::unordered_map<uint32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache, std::vector<MonitorClient>& monitor_list);
void handle_deposit_or_withdrawal(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, Opcode opcode, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, std::unordered_map<uint32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache, std::vector<MonitorClient>& monitor_list);
void handle_monitor(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, std::vector<MonitorClient>& monitor_list, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache);
void prune_expired_monitors(std::vector<MonitorClient>& monitor_list);
void notify_monitors(int serverSocket, std::vector<MonitorClient>& monitor_list, Opcode opcode, const Account& acc, const std::string& msg);


int main(int argc, char** argv){
    if(argc < 2){
        std::cerr << "Error: Port number required\n";
        return 1;
    }
    int port = std::stoi(argv[1]);

    int serverSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if(serverSocket < 0){
        perror("socket");
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons((uint16_t)port);

    if(bind(serverSocket, (sockaddr*)&serverAddress, sizeof(serverAddress)) < 0){
        perror("bind");
        return 1;
    }

    std::cout << "Listening on port " << port << "\n";

    uint32_t next_account = 1;
    std::unordered_map<uint32_t, Account> accounts;

    //reply cache for at-most-once semantic
    std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash> reply_cache;
    std::vector<MonitorClient> monitor_list;
    
    while(1){
        uint8_t buf[2048];
        sockaddr_in cli{};
        socklen_t clen = sizeof(cli);

        ssize_t r = recvfrom(serverSocket, buf, sizeof(buf), 0, (sockaddr*)&cli, &clen);
        if(r < 0){
            perror("recvfrom");
            continue;
        }

        try{
            ByteReader br(buf, (size_t)r);
            uint8_t version = br.u8();
            uint8_t opcode = br.u8();
            uint8_t semantic = br.u8();
            (void)br.u8();
            uint32_t rid = br.u32();

            ReqID req_id{rid, cli.sin_addr.s_addr, cli.sin_port};

            if(version != version_number){
                ByteWriter bw = build_reply_header(Status::ERROR, rid);
                bw.str_with_len("Version mismatch");
                if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
                    reply_cache.emplace(req_id, bw.buffer);
                }
                ssize_t bytes_sent = sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&cli, clen);
                if(bytes_sent < 0){
                    perror("sendto");
                }
                continue;
                }

            if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
                auto reply = reply_cache.find(req_id);
                if(reply != reply_cache.end()){
                    std::cout << "duplicate request: " << rid << ". resending cached reply \n";
                    ssize_t bytes_sent = sendto(serverSocket, reply->second.data(), reply->second.size(), 0, (sockaddr*)&cli, clen);
                    if(bytes_sent < 0){
                        perror("sendto");
                    }
                    continue; 
                }
            }

            switch(static_cast<Opcode>(opcode)){
                case Opcode::OPEN_ACCOUNT:
                    handle_open_account(serverSocket, br, rid, semantic, req_id, cli, clen, next_account, accounts, reply_cache, monitor_list);
                    break;
                case Opcode::CLOSE_ACCOUNT:
                    handle_close_account(serverSocket, br, rid, semantic, req_id, cli, clen, accounts, reply_cache, monitor_list);
                    break;
                case Opcode::DEPOSIT:
                case Opcode::WITHDRAW:
                    handle_deposit_or_withdrawal(serverSocket, br, rid, semantic, static_cast<Opcode>(opcode), req_id, cli, clen, accounts, reply_cache, monitor_list);
                    break;
                case Opcode::MONITOR:
                    handle_monitor(serverSocket, br, rid, semantic, req_id, cli, clen, monitor_list, reply_cache);
                    break;
                default:
                    std::cerr << "unsupported opcode: " << (int)opcode << "\n";
                    break;
            }
        }catch(const std::exception& e){
            std::cerr << "error: " << e.what() << "\n";
        }
    }
    close(serverSocket);
    return 0;
}

ByteWriter build_reply_header(Status status, uint32_t rid){
    ByteWriter bw;
    bw.u8(version_number);
    bw.u8(static_cast<uint8_t>(status));
    bw.u16(0);
    bw.u32(rid);
    return bw;
}

void handle_open_account(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, uint32_t& next_account, std::unordered_map<uint32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache, std::vector<MonitorClient>& monitor_list){    
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    Currency currency = static_cast<Currency>(br.u8());
    double balance = u64_to_double(br.u64());

    std::cout << "OPEN_ACCOUNT rid = " << rid
              << " name = " << name
              << " currency: " << static_cast<int>(currency)
              << " balance = " << balance << "\n";

    Status status = Status::SUCCESS;
    std::string msg;
    ByteWriter bw;

    if(name.empty()){
        std::cerr << "Name cannot be empty\n";
        status = Status::ERROR;
        msg = "Name cannot be empty";
    }else if(password.empty()){
        std::cerr << "Password cannot be empty\n";
        status = Status::ERROR;
        msg = "Password cannot be empty";
    }else if(balance < 0){
        std::cerr << "Initial balance cannot be negative\n";
        status = Status::ERROR;
        msg = "Initial balance cannot be negative";
    }
    if(status == Status::SUCCESS){
        Account acc{};
        acc.aid = next_account++;
        acc.name = name;
        acc.password = password;
        acc.currency = currency;
        acc.balance = balance;
        accounts[acc.aid] = acc;
        notify_monitors(serverSocket, monitor_list, Opcode::OPEN_ACCOUNT, acc, "Account opened");

        bw = build_reply_header(status, rid);
        bw.u32(acc.aid);
        msg = "Account created";
        bw.str_with_len(msg);
        std::cout << "Account created. aid=" << acc.aid << "\n";
    }else{
        bw = build_reply_header(status, rid);
        bw.str_with_len(msg);
    }

    if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
        reply_cache.emplace(req_id, bw.buffer);
    }

    ssize_t bytes_sent = sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&cli, clen);
    if(bytes_sent < 0){
        perror("sendto");
    }
}

void handle_close_account(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, std::unordered_map<uint32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache, std::vector<MonitorClient>& monitor_list){
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    uint32_t aid = br.u32();

    Status status = Status::SUCCESS;
    std::string msg;

    auto user = accounts.find(aid);
    Account closed_acc;

    if(user == accounts.end()){
        std::cerr << "Account with aid= " << aid << " does not exist\n";
        status = Status::ERROR;
        msg = "Account does not exist";
    }else if(name != user->second.name){
        std::cerr << "Account does not belong to " << name << "\n";
        status = Status::ERROR;
        msg = "Account does not belong to this user";
    }else if(password != user->second.password){
        std::cerr << "Wrong password\n";
        status = Status::ERROR;
        msg = "Wrong password";        
    }else{
        closed_acc = user->second;
        accounts.erase(user);
        msg = "Account successfully closed";
        notify_monitors(serverSocket, monitor_list, Opcode::CLOSE_ACCOUNT, closed_acc, "Account closed");
    }

    ByteWriter bw = build_reply_header(status, rid);
    if(status == Status::SUCCESS){
        bw.u32(aid);
    }
    bw.str_with_len(msg);
    
    if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
        reply_cache.emplace(req_id, bw.buffer);
    }

    ssize_t bytes_sent = sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&cli, clen);
    if(bytes_sent < 0){
        perror("sendto");
    }
}

void handle_deposit_or_withdrawal(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, Opcode opcode, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, std::unordered_map<uint32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache, std::vector<MonitorClient>& monitor_list){
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    uint32_t aid = br.u32();
    Currency currency = static_cast<Currency>(br.u8());
    double amount = u64_to_double(br.u64());

    Status status = Status::SUCCESS;
    std::string msg;

    auto acc = accounts.find(aid);

    if(acc == accounts.end()){
        std::cerr << "Account with aid= " << aid << " does not exist\n";
        status = Status::ERROR;
        msg = "Account does not exist";
    }else if(acc->second.name != name){
        std::cerr << "Account does not belong to " << name << "\n";
        status = Status::ERROR;
        msg = "Account does not belong to this user";
    }else if(acc->second.password != password){
        std::cerr << "Wrong password \n";
        status = Status::ERROR;
        msg = "Wrong password";
    }else if(acc->second.currency != currency){
        std::cerr << "Account not opened with this currency\n";
        status = Status::ERROR;
        msg = "Account not opened with this currency";
    }else if(amount <= 0){
        std::cerr << "Amount must be more than 0\n";
        status = Status::ERROR;
        msg = "Amount must be more than 0";
    }else if(opcode == Opcode::WITHDRAW && acc->second.balance < amount){
        std::cerr << "Insufficient balance\n";
        status = Status::ERROR;
        msg = "Insufficient balance";
    }else{
        if(opcode == Opcode::WITHDRAW){
            acc->second.balance -= amount;
            msg = "Withdrawal successful";
            notify_monitors(serverSocket, monitor_list, Opcode::WITHDRAW, acc->second, msg);
        }else{
            acc->second.balance += amount;
            msg = "Deposit successful";
            notify_monitors(serverSocket, monitor_list, Opcode::DEPOSIT, acc->second, msg);
        }
    }

    ByteWriter bw = build_reply_header(status, rid);
    if(status == Status::SUCCESS){
        bw.u64(double_to_u64(acc->second.balance));
    }
    bw.str_with_len(msg);

    if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
        reply_cache.emplace(req_id, bw.buffer);
    }

    ssize_t bytes_sent = sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&cli, clen);
    if(bytes_sent < 0){
        perror("sendto");
    }
}

void prune_expired_monitors(std::vector<MonitorClient>& monitor_list){
    auto now = std::chrono::steady_clock::now();
    
    monitor_list.erase(
        std::remove_if(monitor_list.begin(), monitor_list.end(),
        [&now](const MonitorClient& m){
            return m.expiry <= now;
        }), monitor_list.end()
    );
}

void notify_monitors(int serverSocket, std::vector<MonitorClient>& monitor_list, Opcode opcode, const Account& acc, const std::string& msg){
    prune_expired_monitors(monitor_list);

    ByteWriter bw;
    bw.u8(version_number);
    bw.u8(static_cast<uint8_t>(opcode));
    bw.u16(0);
    bw.str_with_len(acc.name);
    bw.u32(acc.aid);
    bw.u8(static_cast<uint8_t>(acc.currency));
    bw.u64(double_to_u64(acc.balance));
    bw.str_with_len(msg);

    for(const auto& m : monitor_list){
        ssize_t bytes_sent = sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&m.addr, sizeof(m.addr));
        if(bytes_sent < 0){
            perror("sendto");
            
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &m.addr.sin_addr, ip, sizeof(ip));
            uint16_t port = ntohs(m.addr.sin_port);

            std::cout << "Failed to send update to client " << ip << ":" << port << "\n";
        }
    }
}

void handle_monitor(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, std::vector<MonitorClient>& monitor_list, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache){
    uint32_t interval_in_seconds = br.u32();
    prune_expired_monitors(monitor_list);

    if(interval_in_seconds < 10){
        ByteWriter bw = build_reply_header(Status::ERROR, rid);
        bw.str_with_len("Monitor interval must be at least 10 seconds");

        if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
            reply_cache.emplace(req_id, bw.buffer);
        }

        ssize_t bytes_sent = sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&cli, clen);
        if(bytes_sent < 0){
            perror("sendto");
        }
        return;
    }

    auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(interval_in_seconds);

    bool registered_client = false;
    for(auto& m : monitor_list){
        if((m.addr.sin_addr.s_addr == cli.sin_addr.s_addr) && (m.addr.sin_port == cli.sin_port)){
            registered_client = true;
            m.expiry = expiry;
            break;
        }
    }

    if(!registered_client){
        MonitorClient m{};
        m.addr = cli;
        m.expiry = expiry;
        monitor_list.push_back(m);
    }

    ByteWriter bw = build_reply_header(Status::SUCCESS, rid);
    bw.str_with_len("Monitor registration successful");

    if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
        reply_cache.emplace(req_id, bw.buffer);
    }

    ssize_t bytes_sent = sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&cli, clen);
    if(bytes_sent < 0){
        perror("sendto");
    }

}



