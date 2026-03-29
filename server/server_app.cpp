#include "server_app.h"

#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "../common/protocol.h"
#include "../common/marshalling.h"
#include "reply_utils.h"

ServerApp::ServerApp(uint16_t port){
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

void ServerApp::cache_reply_if_needed(uint8_t semantic,
                                      const ReqID& req_id,
                                      const std::vector<uint8_t>& reply){
    if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
        reply_cache_.emplace(req_id, reply);
    }
}

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
            uint8_t version = br.u8();
            uint8_t opcode = br.u8();
            uint8_t semantic = br.u8();
            (void)br.u8();
            uint32_t rid = br.u32();

            ReqID req_id{rid, cli.sin_addr.s_addr, cli.sin_port};

            if(version != version_number){
                ByteWriter bw = build_reply_header(Status::ERROR, rid);
                bw.str_with_len("Version mismatch");
                cache_reply_if_needed(semantic, req_id, bw.buffer);
                send_buffer_to_client(bw.buffer, cli, clen);
                continue;
            }

            if(static_cast<Semantics>(semantic) == Semantics::AT_MOST_ONCE){
                auto reply = reply_cache_.find(req_id);
                if(reply != reply_cache_.end()){
                    std::cout << "duplicate request: " << rid
                              << ". resending cached reply\n";
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

void ServerApp::handle_open_account(ByteReader& br,
                                    uint32_t rid,
                                    uint8_t semantic,
                                    const ReqID& req_id,
                                    const sockaddr_in& cli,
                                    socklen_t clen){
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    Currency currency = static_cast<Currency>(br.u8());
    double balance = u64_to_double(br.u64());

    Status status = Status::SUCCESS;
    std::string msg;

    if(name.empty()){
        status = Status::ERROR;
        msg = "Name cannot be empty";
    }else if(password.empty()){
        status = Status::ERROR;
        msg = "Password cannot be empty";
    }else if(balance < 0){
        status = Status::ERROR;
        msg = "Initial balance cannot be negative";
    }else{
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

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}

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

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}

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
    }

    ByteWriter bw = build_reply_header(status, rid);
    if(status == Status::SUCCESS){
        bw.u64(double_to_u64(acc->second.balance));
    }
    bw.str_with_len(msg);

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}

void ServerApp::handle_monitor(ByteReader& br,
                               uint32_t rid,
                               uint8_t semantic,
                               const ReqID& req_id,
                               const sockaddr_in& cli,
                               socklen_t clen){
    uint32_t interval_in_seconds = br.u32();

    monitor_service_.prune_expired_monitors();

    if(interval_in_seconds < 10){
        ByteWriter bw = build_reply_header(Status::ERROR, rid);
        bw.str_with_len("Monitor interval must be at least 10 seconds");
        cache_reply_if_needed(semantic, req_id, bw.buffer);
        send_buffer_to_client(bw.buffer, cli, clen);
        return;
    }

    monitor_service_.register_or_refresh(cli, interval_in_seconds);

    ByteWriter bw = build_reply_header(Status::SUCCESS, rid);
    bw.str_with_len("Monitor registration successful");

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}

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

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}

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
        bw.u64(double_to_u64(recipient->second.balance));
    }
    bw.str_with_len(msg);

    cache_reply_if_needed(semantic, req_id, bw.buffer);
    send_buffer_to_client(bw.buffer, cli, clen);
}