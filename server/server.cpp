#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

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

void handle_open_account(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, uint32_t& next_account, std::unordered_map<int32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache);

void handle_close_account(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, std::unordered_map<int32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache);

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
    std::unordered_map<int32_t, Account> accounts;

    //reply cache needed for at-most-once
    std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash> reply_cache;
    
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
            (void)br.u8(); // padding/can use in the future
            uint32_t rid = br.u32();

            ReqID req_id{rid, cli.sin_addr.s_addr, cli.sin_port};

            if(version != version_number){
                std::cerr << "version not supported. current version: " << version_number << "\n";
                continue;
            }

            if((Semantics)semantic == Semantics::AT_MOST_ONCE){
                auto reply = reply_cache.find(req_id);
                if(reply != reply_cache.end()){
                    std::cout << "duplicate request: " << rid << ". resending cached reply \n";
                    sendto(serverSocket, reply->second.data(), reply->second.size(), 0, (sockaddr*)&cli, clen);
                    continue; 
                }
            }

            switch((Opcode)opcode){
                case Opcode::OPEN_ACCOUNT:
                    handle_open_account(serverSocket, br, rid, semantic, req_id, cli, clen, next_account, accounts, reply_cache);
                    break;
                case Opcode::CLOSE_ACCOUNT:
                    handle_close_account(serverSocket, br, rid, semantic, req_id, cli, clen, accounts, reply_cache);
                case Opcode::DEPOSIT:
                case Opcode::WITHDRAW:
                case Opcode::MONITOR:
                    std::cerr << "opcode not implemented yet: " << (int)opcode << "\n";
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

void handle_open_account(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, uint32_t& next_account, std::unordered_map<int32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache){
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    Currency currency = (Currency)br.u8();
    double balance = u64_to_double(br.u64());

    std::cout << "OPEN_ACCOUNT rid = " << rid
              << " name = " << name
              << " currency: " << (int)currency
              << " balance = " << balance << "\n";

    Account acc{};
    acc.aid = next_account++;
    acc.name = name;
    acc.password = password;
    acc.currency = currency;
    acc.balance = balance;

    accounts[acc.aid] = acc;

    ByteWriter bw;
    bw.u8(version_number);
    bw.u8((uint8_t)Status::SUCCESS);
    bw.u16(0);
    bw.u32(rid);
    bw.u32(acc.aid);
    bw.str_with_len("Account created");

    if((Semantics)semantic == Semantics::AT_MOST_ONCE){
        reply_cache.emplace(req_id, bw.buffer);
    }

    std::cout << "Account created. aid = " << acc.aid << "\n";
    sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&cli, clen);
}

void handle_close_account(int serverSocket, ByteReader& br, uint32_t rid, uint8_t semantic, const ReqID& req_id, sockaddr_in& cli, socklen_t clen, std::unordered_map<int32_t, Account>& accounts, std::unordered_map<ReqID, std::vector<uint8_t>, ReqIDHash>& reply_cache){
    std::string name = br.str_u16len();
    std::string password = br.str_u16len();
    uint32_t aid = br.u32();

    ByteWriter bw;
    bw.u8(version_number);
    bw.u8((uint8_t)Status::SUCCESS);
    bw.u16(0);
    bw.u32(rid);
    bw.u32(aid);

    auto user = accounts.find(aid);

    if(user == accounts.end()){
        std::cerr << "Account with aid= " << aid << " does not exist";
        bw.buffer[1] = (uint8_t)Status::ERROR;
        bw.str_with_len("Account does not exist");
    }else if(name != user->second.name){
        std::cerr << "Account does not belong to " << name << "\n";
        bw.buffer[1] = (uint8_t)Status::ERROR;
        bw.str_with_len("Account does not belong to this user");
    }else if(password != user->second.password){
        std::cerr << "Wrong password";
        bw.buffer[1] = (uint8_t)Status::ERROR;
        bw.str_with_len("Wrong password");        
    }else{
        accounts.erase(user);
        bw.str_with_len("Account successfully closed");
    }
    
    if((Semantics)semantic == Semantics::AT_MOST_ONCE){
        reply_cache.emplace(req_id, bw.buffer);
    }

    sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&cli, clen);
}
