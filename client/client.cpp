#include <iostream>
#include <string>
#include <cstring>
#include <limits>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "../common/protocol.h"
#include "../common/marshalling.h"

static uint32_t next_rid = 1;

void handle_open_account(int serverSocket, sockaddr_in& srv, Semantics semantic);
void handle_close_account(int serverSocket, sockaddr_in& srv, Semantics semantic);

int main(int argc, char** argv){
    if(argc < 4){
        std::cerr << "Usage: client <server_ip> <server_port> <semantic>\n";
        return 1;
    }
    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    Semantics semantic = std::stoi(argv[3]) == 1 ? Semantics::AT_MOST_ONCE : Semantics::AT_LEAST_ONCE;

    int serverSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if(serverSocket < 0){
        perror("socket");
        return 1;
    }

    timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in srv{};
    srv.sin_family = AF_INET;
    srv.sin_port = htons((uint16_t)port);
    if(inet_pton(AF_INET, ip.c_str(), &srv.sin_addr) != 1){
        std::cerr << "Bad server IP\n";
        close(serverSocket);
        return 1;
    }

    while(1){
        int choice;
        std::cout << "===== Banking System =====\n";
        std::cout << "1. Open Account\n";
        std::cout << "2. Close Account\n";
        std::cout << "3. Deposit\n";
        std::cout << "4. Withdraw\n";
        std::cout << "5. Monitor\n";
        std::cout << "0. Exit\n";
        std::cout << "Enter choice: ";
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        switch(choice){
            case 1:
                handle_open_account(serverSocket, srv, semantic);
                continue;
            case 2:
                handle_close_account(serverSocket, srv, semantic);
                continue;
            case 3:
                std::cout << "Service not implemented yet\n";
                continue;
            case 4:
                std::cout << "Service not implemented yet\n";
                continue;
            case 5:
                std::cout << "Service not implemented yet\n";
                continue;
            case 0:
                std::cout << "Exiting\n";
                close(serverSocket);
                return 0;
            default:
                std::cout << "Invalid choice\n";
                continue;
                
        }
    }
}

void handle_open_account(int serverSocket, sockaddr_in& srv, Semantics semantic){
    std::string name, password;
    int currency_in_int = {};
    double balance = {};

    std::cout << "Name: ";
    std::getline(std::cin, name);

    std::cout << "\nPassword: ";
    std::getline(std::cin, password);

    std::cout << "\nCurrency (0 = SGD, 1 = RM): ";
    std::cin >> currency_in_int;

    std::cout << "\nInitial balance: ";
    std::cin >> balance;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    ByteWriter bw;
    bw.u8(version_number);
    bw.u8((uint8_t)Opcode::OPEN_ACCOUNT);
    bw.u8((uint8_t)semantic);
    bw.u8(0);
    uint32_t current_rid = next_rid;
    bw.u32(next_rid++);

    bw.str_with_len(name);
    bw.str_with_len(password);
    bw.u8((uint8_t)currency_in_int);
    bw.u64(double_to_u64(balance));

    uint8_t reply_buffer[2048];

    int attempts = semantic == Semantics::AT_LEAST_ONCE ? 3 : 1;

    for(int i = 1; i <= attempts; i++){
        ssize_t bytes_sent = sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&srv, sizeof(srv));

        if(bytes_sent < 0){
            perror("sendto");
            continue;
        }

        sockaddr_in from{};
        socklen_t flen = sizeof(from);

        ssize_t bytes_received = recvfrom(serverSocket, reply_buffer, sizeof(reply_buffer), 0, (sockaddr*)&from, &flen);

        if(bytes_received < 0){
            std::cout << "Timeout on attempt " << i << "\n";
            continue;
        }

        try{
            ByteReader br(reply_buffer, (size_t)bytes_received);

            uint8_t version = br.u8();
            Status status = (Status)br.u8();
            (void)br.u16();
            uint32_t reply_rid = br.u32();
            uint32_t aid = br.u32();
            std::string msg = br.str_u16len();

            if(version != version_number){
                std::cout << "Wrong version number\n";
                continue;
            }else if(reply_rid != current_rid){
                std::cout << "Wrong rid";
                continue;
            }

            if(status == Status::SUCCESS){
                std::cout << msg << "AccountID = " << aid << "\n";
            }
            
            return;

        }catch(const std::exception& e){
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    std::cout << "No reply from server\n";
}

void handle_close_account(int serverSocket, sockaddr_in& srv, Semantics semantic){
    std::string name, password;
    uint32_t aid = {};

    std::cout << "Enter name: ";
    std::getline(std::cin, name);
    
    std::cout << "Enter password: ";
    std::getline(std::cin, password);

    std::cout << "Enter Account ID: ";
    std::cin >> aid;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    ByteWriter bw;
    bw.u8(version_number);
    bw.u8((uint8_t)Opcode::CLOSE_ACCOUNT);
    bw.u8((uint8_t)semantic);
    bw.u8(0);
    uint32_t current_rid = next_rid++;
    bw.u32(current_rid);
    bw.str_with_len(name);
    bw.str_with_len(password);
    bw.u32(aid);

    uint8_t reply_buffer[2048];
    int attempts = semantic == Semantics::AT_LEAST_ONCE ? 3 : 1;

    for(int i = 1; i <= attempts; i++){
        ssize_t bytes_sent = sendto(serverSocket, bw.buffer.data(), bw.buffer.size(), 0, (sockaddr*)&srv, sizeof(srv));
        if(bytes_sent < 0){
            perror("sendto");
            continue;
        }

        sockaddr_in from{};
        socklen_t flen = sizeof(from);

        ssize_t bytes_received = recvfrom(serverSocket, reply_buffer, sizeof(reply_buffer),0 , (sockaddr*)&from, &flen);
        if(bytes_received < 0){
            std::cout << "Timeout on attempt " << i << "\n";
            continue;
        }

        try{
            ByteReader br(reply_buffer, (size_t) bytes_received);
            uint8_t version = br.u8();
            Status status = (Status)br.u8();
            (void)br.u16();
            uint32_t reply_rid = br.u32();
            uint32_t aid = br.u32();
            std::string msg = br.str_u16len();

            if(version != version_number){
                std::cout << "Wrong version number\n";
                continue;
            }else if(reply_rid != current_rid){
                std::cout << "Wrong rid";
                continue;
            }

            if(status == Status::ERROR){
                std::cout << "Request failed. Error: " << msg << "\n";
                return;
            }else{
                std::cout << "Request successful. Account aid=" << aid << "has been closed\n";
                return;
            }
        }catch(const std::exception& e){
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    std::cout << "No reply from server\n";

}