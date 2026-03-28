#include <iostream>
#include <string>
#include <cstring>
#include <limits>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <chrono>

#include "../common/protocol.h"
#include "../common/marshalling.h"

static uint32_t next_rid = 1;

void handle_open_account(int serverSocket, sockaddr_in& srv, Semantics semantic);
void handle_close_account(int serverSocket, sockaddr_in& srv, Semantics semantic);
void handle_deposit_or_withdrawal(int serverSocket, sockaddr_in& srv, Semantics semantic, Opcode opcode);
void handle_monitor(int serverSocket, sockaddr_in& srv, Semantics semantic);

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
                handle_deposit_or_withdrawal(serverSocket, srv, semantic, Opcode::DEPOSIT);
                continue;
            case 4:
                handle_deposit_or_withdrawal(serverSocket, srv, semantic, Opcode::WITHDRAW);
                continue;
            case 5:
                handle_monitor(serverSocket, srv, semantic);
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
    uint32_t current_rid = next_rid++;
    bw.u32(current_rid);

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
            Status status = static_cast<Status>(br.u8());
            (void)br.u16();
            uint32_t reply_rid = br.u32();

            if(version != version_number){
                std::string msg = br.str_u16len();
                std::cout << "Error: " << msg << " Please update client to latest version\n";
                return;
            }else if(reply_rid != current_rid){
                std::cout << "Wrong rid received. Retrying...\n";
                continue;
            }

            if(status == Status::ERROR){
                std::string msg = br.str_u16len();
                std::cout << "Request failed. Error: " << msg << "\n";
                return;
            }

            uint32_t aid = br.u32();
            std::string msg = br.str_u16len();
            std::cout << msg << "AccountID = " << aid << "\n";
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
            Status status = static_cast<Status>(br.u8());
            (void)br.u16();
            uint32_t reply_rid = br.u32();

            if(version != version_number){
                std::string msg = br.str_u16len();
                std::cout << "Error: " << msg << " Please update client to latest version\n";
                return;
            }else if(reply_rid != current_rid){
                std::cout << "Wrong rid received. Retrying...\n";
                continue;
            }
            if(status == Status::ERROR){
                std::string msg = br.str_u16len();
                std::cout << "Request failed. Error: " << msg << "\n";
                return;
            }

            uint32_t aid = br.u32();
            std::string msg = br.str_u16len();
            std::cout << "Request successful. Account aid=" << aid << " has been closed\n";
            return;

        }catch(const std::exception& e){
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    std::cout << "No reply from server\n";
}

void handle_deposit_or_withdrawal(int serverSocket, sockaddr_in& srv, Semantics semantic, Opcode opcode){
    std::string name, password;
    uint32_t aid = {};
    int currency_in_int = {};
    Currency currency;
    double amount = {};

    std::cout << "Enter name: ";
    std::getline(std::cin, name);

    std::cout << "\nEnter password: ";
    std::getline(std::cin, password);

    std::cout << "\nEnter Account ID: ";
    std::cin >> aid;

    std::cout << "\n Enter currency (0 = SGD, 1 = RM): ";
    std::cin >> currency_in_int;

    currency_in_int == 0 ? currency = Currency::SGD : currency = Currency::RM;

    std::cout << "\nEnter amount: ";
    std::cin >> amount;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    ByteWriter bw;
    bw.u8(version_number);
    bw.u8(static_cast<uint8_t>(opcode));
    bw.u8(static_cast<uint8_t>(semantic));
    bw.u8(0);
    uint32_t current_rid = next_rid++;
    bw.u32(current_rid);

    bw.str_with_len(name);
    bw.str_with_len(password);
    bw.u32(aid);
    bw.u8(static_cast<uint8_t>(currency));
    bw.u64(double_to_u64(amount));

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
            ByteReader br(reply_buffer, static_cast<size_t>(bytes_received));
            
            uint8_t version = br.u8();
            Status status = static_cast<Status>(br.u8());
            (void)br.u16();
            uint32_t reply_rid = br.u32();

            if(reply_rid != current_rid){
                std::cout << "Wrong rid received. Retrying...\n";
                continue;
            }

            if(version != version_number){
                std::string msg = br.str_u16len();
                std::cout << "Error: " << msg << " Please update client to latest version\n";
                return;
            }

            if(status == Status::SUCCESS){
                double balance = u64_to_double(br.u64());
                std::string msg = br.str_u16len();
                std::cout << msg << "New balance: " << balance << "\n";
                return;
            }else{
                std::string msg = br.str_u16len();
                std::cout << "Error: " << msg << "\n";
                return;
            }
            
        }catch(const std::exception& e){
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    std::cout << "No reply from server\n";
}

void handle_monitor(int serverSocket, sockaddr_in& srv, Semantics semantic){
    uint32_t interval_in_seconds = {};

    std::cout << "Enter monitor interval in seconds (minimum 10 seconds): ";
    std::cin >> interval_in_seconds;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    ByteWriter bw;
    bw.u8(version_number);
    bw.u8(static_cast<uint8_t>(Opcode::MONITOR));
    bw.u8(static_cast<uint8_t>(semantic));
    bw.u8(0);
    uint32_t current_rid = next_rid++;
    bw.u32(current_rid);
    bw.u32(interval_in_seconds);

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
            ByteReader br(reply_buffer, static_cast<size_t>(bytes_received));
            
            uint8_t version = br.u8();
            Status status = static_cast<Status>(br.u8());
            (void)br.u16();
            uint32_t reply_rid = br.u32();

            if(reply_rid != current_rid){
                std::cout << "Wrong rid received. Retrying...\n";
                continue;
            }

            if(version != version_number){
                std::string msg = br.str_u16len();
                std::cout << "Error: " << msg << " Please update client to latest version\n";
                return;
            }

            if(status == Status::ERROR){
                std::string msg = br.str_u16len();
                std::cout << "Error: " << msg << "\n";
                return;
            }else{
                std::string msg = br.str_u16len();
                std::cout << msg << "\n";

                auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(interval_in_seconds);
    
                while(std::chrono::steady_clock::now() < end_time){
                    sockaddr_in callback_from {};
                    socklen_t callback_len = sizeof(callback_from);

                    ssize_t callback_bytes = recvfrom(serverSocket, reply_buffer, sizeof(reply_buffer), 0, (sockaddr*)&callback_from, &callback_len);
                    if(callback_bytes < 0){
                        continue;
                    }
                    
                    try{
                        ByteReader callback_reader(reply_buffer, static_cast<size_t>(callback_bytes));

                        uint8_t callback_version = callback_reader.u8();
                    Opcode callback_opcode = static_cast<Opcode>(callback_reader.u8());
                    (void)callback_reader.u16();

                    if(callback_version != version_number){
                        continue;
                    }

                    std::string name = callback_reader.str_u16len();
                    uint32_t aid = callback_reader.u32();
                    Currency currency = static_cast<Currency>(callback_reader.u8());
                    double balance = u64_to_double(callback_reader.u64());
                    std::string callback_msg = callback_reader.str_u16len();

                    std::cout << "[MONITOR] "
                              << "opcode=" << static_cast<int>(callback_opcode)
                              << " name=" << name
                              << " aid=" << aid
                              << " currency=" << static_cast<int>(currency)
                              << " balance=" << balance
                              << " msg=" << callback_msg << "\n";
                    }catch(const std::exception& e){
                        std::cerr << "Monitor callback error: " << e.what() << "\n";
                    }
                
                }
            }
            std::cout << "Monitor interval expired\n";
            return;
            
        }catch(const std::exception& e){
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    std::cout << "No reply from server\n";

}