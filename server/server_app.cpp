/* 
server_app.cpp implements the udp banking server
- receives requests from clients using udp
- unmarshals request data and dispatches to handlers
- applies at-least-once or at-most-once semantics
- stores account data in memory
- caches replies for duplicate detection
- supports experiment modes to simulate message loss
- sends replies back to clients
*/

#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "server_app.h"
#include "../common/protocol.h"
#include "../common/marshalling.h"
#include "reply_utils.h"

/*
initialize udp socket and bind to given port
- server listens on all interfaces
- experiment flags control whether reply loss is simulated
*/
ServerApp::ServerApp(uint16_t port, bool experiment_deposit_withdraw, bool experiment_view) 
            : experiment_deposit_withdraw_(experiment_deposit_withdraw), 
            experiment_view_(experiment_view){
    serverSocket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if(serverSocket_ < 0){
        perror("socket");
        throw std::runtime_error("failed to create socket");
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(port);

    if(bind(serverSocket_, (sockaddr*)&serverAddress, sizeof(serverAddress)) < 0){
        perror("bind");
        close(serverSocket_);
        throw std::runtime_error("failed to bind socket");
    }

    std::cout << "Listening on port " << port << "\n";
}

ServerApp::~ServerApp(){
    if(serverSocket_ >= 0){
        close(serverSocket_);
    }
}

/*
store reply in cache only for at-most-once semantics
- allows server to resend reply if client retries
- prevents re-execution of the same request
*/
void ServerApp::cache_reply_if_needed(uint8_t semantic,
                                      const ReqID& req_id,
                                      const std::vector<uint8_t>& reply){
    if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
        reply_cache_.emplace(req_id, reply);
    }
}

/*
send reply buffer to client via udp
- no guarantee of delivery (udp)
*/
void ServerApp::send_buffer_to_client(const std::vector<uint8_t>& buffer,
                                      const sockaddr_in& cli,
                                      socklen_t clen){
    ssize_t bytes_sent = sendto(
        serverSocket_,
        buffer.data(),
        buffer.size(),
        0,
        (const sockaddr*)&cli,
        clen
    );

    if(bytes_sent < 0){
        perror("sendto");
    }
}

/*
main server loop
- receive request from client
- read request header fields
- check for duplicate requests (at-most-once)
- dispatch to correct handler based on opcode
*/
void ServerApp::run(){
    while(true){
        uint8_t buf[2048];
        sockaddr_in cli{};
        socklen_t clen = sizeof(cli);

        ssize_t r = recvfrom(serverSocket_, buf, sizeof(buf), 0, (sockaddr*)&cli, &clen);
        if(r < 0){
            perror("recvfrom");
            continue;
        }

        try{
            ByteReader br(buf, (size_t)r);

            // read header fields in protocol order: version, opcode, semantic, reserved, rid
            uint8_t version = br.u8();
            uint8_t opcode = br.u8();
            uint8_t semantic = br.u8();
            (void)br.u8();
            uint32_t rid = br.u32();

            // build request hash using rid + client ip + port for duplicate detection
            ReqID req_id{rid, cli.sin_addr.s_addr, cli.sin_port};

            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

            std::cout << "[server] received request from " << ip << ":" << ntohs(cli.sin_port)
                      << "\nrid: " << rid
                      << "\nopcode: " << static_cast<int>(opcode)
                      << "\nsemantic: " << static_cast<int>(semantic)
                      << "\n\n";

            if(version != version_number){
                ByteWriter bw = build_reply_header(Status::ERROR, rid);
                bw.str_with_len("Version mismatch");
                cache_reply_if_needed(semantic, req_id, bw.buffer);
                send_buffer_to_client(bw.buffer, cli, clen);
                continue;
            }
            
            // if at-most-once semantic and request already processed, resend cached reply
            if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
                auto reply = reply_cache_.find(req_id);
                if(reply != reply_cache_.end()){
                    std::cout << "[server] duplicate request"
                              << "\nrid: " << rid
                              << "\naction: resend cached reply"
                              << "\n\n";
                    send_buffer_to_client(reply->second, cli, clen);
                    continue;
                }
            }

            switch(static_cast<Opcode>(opcode)){
                case Opcode::OPEN_ACCOUNT:
                    handle_open_account(br, rid, semantic, req_id, cli, clen);
                    break;

                case Opcode::CLOSE_ACCOUNT:
                    handle_close_account(br, rid, semantic, req_id, cli, clen);
                    break;

                case Opcode::DEPOSIT:
                case Opcode::WITHDRAW:
                    handle_deposit_or_withdrawal(br, rid, semantic,
                                                 static_cast<Opcode>(opcode),
                                                 req_id, cli, clen);
                    break;

                case Opcode::MONITOR:
                    handle_monitor(br, rid, semantic, req_id, cli, clen);
                    break;

                case Opcode::VIEW_ACCOUNT:
                    handle_view_account(br, rid, semantic, req_id, cli, clen);
                    break;

                case Opcode::TRANSFER:
                    handle_transfer(br, rid, semantic, req_id, cli, clen);
                    break;

                default:
                    std::cerr << "unsupported opcode: " << (int)opcode << "\n";
                    break;
            }

        }catch(const std::exception& e){
            std::cerr << "error: " << e.what() << "\n";
        }
    }
}

/*
handle open account request
- validates input fields
- creates new account if valid
- sends generated account id to client
*/
void ServerApp::handle_open_account(ByteReader& br,
                                    uint32_t rid,
                                    uint8_t semantic,
                                    const ReqID& req_id,
                                    const sockaddr_in& cli,
                                    socklen_t clen){
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    int currency_in_int = br.u8();
    double balance = u64_to_double(br.u64());

    Status status = Status::SUCCESS;
    std::string msg;

    // validate name, password, and initial balance
    if(name.empty()){
        status = Status::ERROR;
        msg = "Name cannot be empty";
    }else if(password.empty()){
        status = Status::ERROR;
        msg = "Password cannot be empty";
    }else if(currency_in_int != static_cast<uint8_t>(Currency::SGD) && currency_in_int != static_cast<uint8_t>(Currency::RM)){
        status = Status::ERROR;
        msg = "Invalid currency";
    }else if(balance < 0){
        status = Status::ERROR;
        msg = "Initial balance cannot be negative";
    }else{
        Currency currency = static_cast<Currency>(currency_in_int);
        Account acc{};
        acc.aid = next_account_++;
        acc.name = name;
        acc.password = password;
        acc.currency = currency;
        acc.balance = balance;

        accounts_.emplace(acc.aid, acc);
        monitor_service_.notify_monitors(serverSocket_, Opcode::OPEN_ACCOUNT, acc, "Account opened");
        msg = "Account created";
    }

    ByteWriter bw = build_reply_header(status, rid);

    if(status == Status::SUCCESS){
        uint32_t created_aid = next_account_ - 1;
        bw.u32(created_aid);
    }

    bw.str_with_len(msg);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

    std::cout << "[server] sending reply to " << ip << ":" << ntohs(cli.sin_port)
            << "\nrid: " << rid
            << "\nopcode: " << static_cast<int>(Opcode::OPEN_ACCOUNT)
            << "\nstatus: " << (status == Status::SUCCESS ? "SUCCESS" : "ERROR")
            << "\nmessage: " << msg;
    if(status == Status::SUCCESS){
        std::cout << "\naid: " << next_account_ - 1;
    }
    std::cout << "\n\n";

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}

/*
handle close account request
- checks ownership and password
- removes account if valid
*/
void ServerApp::handle_close_account(ByteReader& br,
                                     uint32_t rid,
                                     uint8_t semantic,
                                     const ReqID& req_id,
                                     const sockaddr_in& cli,
                                     socklen_t clen){
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    uint32_t aid = br.u32();

    Status status = Status::SUCCESS;
    std::string msg;

    auto acc = accounts_.find(aid);
    Account closed_account{};

    if(acc == accounts_.end()){
        status = Status::ERROR;
        msg = "Account does not exist";
    }else if(acc->second.name != name){
        status = Status::ERROR;
        msg = "Account does not belong to this user";
    }else if(acc->second.password != password){
        status = Status::ERROR;
        msg = "Wrong password";
    }else{
        closed_account = acc->second;
        accounts_.erase(acc);
        msg = "Account successfully closed";
        monitor_service_.notify_monitors(serverSocket_, Opcode::CLOSE_ACCOUNT, closed_account, "Account closed");
    }

    ByteWriter bw = build_reply_header(status, rid);
    if(status == Status::SUCCESS){
        bw.u32(aid);
    }
    bw.str_with_len(msg);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

    std::cout << "[server] sending reply to " << ip << ":" << ntohs(cli.sin_port)
            << "\nrid: " << rid
            << "\nopcode: " << static_cast<int>(Opcode::CLOSE_ACCOUNT)
            << "\nstatus: " << (status == Status::SUCCESS ? "SUCCESS" : "ERROR")
            << "\nmessage: " << msg
            << "\n\n";

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}
/*
handle deposit and withdrawal requests
- authenticate user and check balance conditions
- update account balance
*/
void ServerApp::handle_deposit_or_withdrawal(ByteReader& br,
                                             uint32_t rid,
                                             uint8_t semantic,
                                             Opcode opcode,
                                             const ReqID& req_id,
                                             const sockaddr_in& cli,
                                             socklen_t clen){
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    uint32_t aid = br.u32();
    Currency currency = static_cast<Currency>(br.u8());
    double amount = u64_to_double(br.u64());

    Status status = Status::SUCCESS;
    std::string msg;

    auto acc = accounts_.find(aid);

    // check account existence, ownership, credentials, currency, and amount
    if(acc == accounts_.end()){
        status = Status::ERROR;
        msg = "Account does not exist";
    }else if(acc->second.name != name){
        status = Status::ERROR;
        msg = "Account does not belong to this user";
    }else if(acc->second.password != password){
        status = Status::ERROR;
        msg = "Wrong password";
    }else if(acc->second.currency != currency){
        status = Status::ERROR;
        msg = "Account not opened with this currency";
    }else if(amount <= 0){
        status = Status::ERROR;
        msg = "Amount must be more than 0";
    }else if(opcode == Opcode::WITHDRAW && acc->second.balance < amount){
        status = Status::ERROR;
        msg = "Insufficient balance";
    }else{
        if(opcode == Opcode::WITHDRAW){
            acc->second.balance -= amount;
            msg = "Withdrawal successful";
            monitor_service_.notify_monitors(serverSocket_, Opcode::WITHDRAW, acc->second, msg);
        }else{
            acc->second.balance += amount;
            msg = "Deposit successful";
            monitor_service_.notify_monitors(serverSocket_, Opcode::DEPOSIT, acc->second, msg);
        }
        std::cout << "[server] operation executed"
        << "\nrid: " << rid
        << "\nopcode: " << static_cast<int>(opcode)
        << "\nname: " << name
        << "\naid: " << aid
        << "\nnew_balance: " << acc->second.balance
        << "\n\n";
    }

    ByteWriter bw = build_reply_header(status, rid);
    if(status == Status::SUCCESS){
        bw.u64(double_to_u64(acc->second.balance));
    }
    bw.str_with_len(msg);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

    std::cout << "[server] sending reply to " << ip << ":" << ntohs(cli.sin_port)
            << "\nrid: " << rid
            << "\nopcode: " << static_cast<int>(opcode)
            << "\nstatus: " << (status == Status::SUCCESS ? "SUCCESS" : "ERROR")
            << "\nmessage: " << msg
            << "\n\n";

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    // experiment mode: drop first reply for this rid to simulate reply loss
    // reply is already cached so at-most-once can resend it later
    if(experiment_deposit_withdraw_ && !dropped_deposit_withdraw_reply_rids_.count(rid)){
            dropped_deposit_withdraw_reply_rids_.insert(rid);
            std::cout << "[experiment] dropping reply"
                      << "\nrid: " << rid
                      << "\nopcode: " << static_cast<int>(opcode)
                      << "\ntype: deposit/withdraw"
                      << "\n\n";
            return;
    }
    send_buffer_to_client(bw.buffer, cli, clen);
}

/*
handle monitor registration
- client subscribes for updates on all accounts for a chosen interval
- server sends callback updates during interval
*/
void ServerApp::handle_monitor(ByteReader& br,
                               uint32_t rid,
                               uint8_t semantic,
                               const ReqID& req_id,
                               const sockaddr_in& cli,
                               socklen_t clen){
    uint32_t interval_in_seconds = br.u32();

    // remove expired monitor clients before registering new ones
    monitor_service_.prune_expired_monitors();

    if(interval_in_seconds < 10){
        ByteWriter bw = build_reply_header(Status::ERROR, rid);
        bw.str_with_len("Monitor interval must be at least 10 seconds");
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

        std::cout << "[server] sending reply to " << ip << ":" << ntohs(cli.sin_port)
                << "\nrid: " << rid
                << "\nopcode: " << static_cast<int>(Opcode::MONITOR)
                << "\nstatus: ERROR"
                << "\nmessage: Monitor interval must be at least 10 seconds"
                << "\n\n";
        cache_reply_if_needed(semantic, req_id, bw.buffer);
        send_buffer_to_client(bw.buffer, cli, clen);
        return;
    }

    monitor_service_.register_or_refresh(cli, interval_in_seconds);

    ByteWriter bw = build_reply_header(Status::SUCCESS, rid);
    bw.str_with_len("Monitor registration successful");
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

    std::cout << "[server] sending reply to " << ip << ":" << ntohs(cli.sin_port)
            << "\nrid: " << rid
            << "\nopcode: " << static_cast<int>(Opcode::MONITOR)
            << "\nstatus: SUCCESS"
            << "\nmessage: Monitor registration successful"
            << "\n\n";

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}

/*
handle view account request
- returns account info (currency, balance)
- idempotent: repeated execution does not modify state
*/
void ServerApp::handle_view_account(ByteReader& br,
                                    uint32_t rid,
                                    uint8_t semantic,
                                    const ReqID& req_id,
                                    const sockaddr_in& cli,
                                    socklen_t clen){
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    uint32_t aid = br.u32();

    Status status = Status::SUCCESS;
    std::string msg;
    Currency currency = Currency::SGD;
    double balance = 0.0;

    auto acc = accounts_.find(aid);

    if(acc == accounts_.end()){
        status = Status::ERROR;
        msg = "Account does not exist";
    }else if(acc->second.name != name){
        status = Status::ERROR;
        msg = "Account does not belong to this user";
    }else if(acc->second.password != password){
        status = Status::ERROR;
        msg = "Wrong password";
    }else{
        currency = acc->second.currency;
        balance = acc->second.balance;
        msg = "Account info retrieved successfully";
    }

    ByteWriter bw = build_reply_header(status, rid);
    if(status == Status::SUCCESS){
        bw.u8(static_cast<uint8_t>(currency));
        bw.u64(double_to_u64(balance));
    }
    bw.str_with_len(msg);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

    std::cout << "[server] sending reply to " << ip << ":" << ntohs(cli.sin_port)
            << "\nrid: " << rid
            << "\nopcode: " << static_cast<int>(Opcode::VIEW_ACCOUNT)
            << "\nstatus: " << (status == Status::SUCCESS ? "SUCCESS" : "ERROR")
            << "\nmessage: " << msg;

    if(status == Status::SUCCESS){
        std::cout << "\nbalance: " << acc->second.balance;
    }

    std::cout << "\n\n";

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    // experiment mode: drop first reply for this rid
    if(experiment_view_ && !dropped_view_reply_rids_.count(rid)){
            dropped_view_reply_rids_.insert(rid);
            std::cout << "[experiment] dropping first view account reply for rid= " << rid << "\n";
            return;
    }
    send_buffer_to_client(bw.buffer, cli, clen);
}

/*
handle transfer between accounts
- moves funds from sender to recipient
- authenticate user and verify sufficient balance
- non-idempotent: repeated execution duplicates transfer
*/
void ServerApp::handle_transfer(ByteReader& br,
                                uint32_t rid,
                                uint8_t semantic,
                                const ReqID& req_id,
                                const sockaddr_in& cli,
                                socklen_t clen){
    std::string sender_name = br.str_u16len();
    std::string sender_password = br.str_u16len();
    uint32_t sender_aid = br.u32();

    std::string recipient_name = br.str_u16len();
    uint32_t recipient_aid = br.u32();

    double amount = u64_to_double(br.u64());

    Status status = Status::SUCCESS;
    std::string msg;

    auto sender = accounts_.find(sender_aid);
    auto recipient = accounts_.find(recipient_aid);

    if(amount <= 0){
        status = Status::ERROR;
        msg = "Transfer amount must be more than 0";
    }else if(sender == accounts_.end()){
        status = Status::ERROR;
        msg = "Sender account does not exist";
    }else if(recipient == accounts_.end()){
        status = Status::ERROR;
        msg = "Recipient account does not exist";
    }else if(sender->second.name != sender_name){
        status = Status::ERROR;
        msg = "Sender account does not belong to this user";
    }else if(sender->second.password != sender_password){
        status = Status::ERROR;
        msg = "Wrong sender password";
    }else if(recipient->second.name != recipient_name){
        status = Status::ERROR;
        msg = "Recipient name does not match account";
    }else if(sender_aid == recipient_aid){
        status = Status::ERROR;
        msg = "Cannot transfer to the same account";
    }else if(sender->second.currency != recipient->second.currency){
        status = Status::ERROR;
        msg = "Currency mismatch between accounts";
    }else if(sender->second.balance < amount){
        status = Status::ERROR;
        msg = "Insufficient balance";
    }else{
        sender->second.balance -= amount;
        recipient->second.balance += amount;
        msg = "Transfer successful";

        monitor_service_.notify_monitors(serverSocket_,
                                         Opcode::TRANSFER,
                                         sender->second,
                                         "Transfer out successful");

        monitor_service_.notify_monitors(serverSocket_,
                                         Opcode::TRANSFER,
                                         recipient->second,
                                         "Transfer in successful");
    }

    ByteWriter bw = build_reply_header(status, rid);
    if(status == Status::SUCCESS){
        bw.u64(double_to_u64(sender->second.balance));
    }
    bw.str_with_len(msg);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));

    std::cout << "[server] sending reply to " << ip << ":" << ntohs(cli.sin_port)
            << "\nrid: " << rid
            << "\nopcode: " << static_cast<int>(Opcode::TRANSFER)
            << "\nstatus: " << (status == Status::SUCCESS ? "SUCCESS" : "ERROR")
            << "\nmessage: " << msg;

    if(status == Status::SUCCESS){
        std::cout << "\nsender_balance: " << sender->second.balance
                << "\nrecipient_balance: " << recipient->second.balance;
    }

    std::cout << "\n\n";

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}