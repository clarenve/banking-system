/*
client.cpp implements the udp banking client
- provides cli interface for user
- marshals requests and sends them to server
- handles timeout and retries
- prints replies or error messages
- supports experiment modes to simulate request loss
*/

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

//request id counter (unique per request)
static uint32_t next_rid = 1;

//enable/disable request loss simulation for deposit/withdraw
static bool experiment_deposit_withdraw = false;

//enable request loss simulation for view account
static bool experiment_view = false;

void handle_open_account(int serverSocket, sockaddr_in& srv, Semantics semantic);
void handle_close_account(int serverSocket, sockaddr_in& srv, Semantics semantic);
void handle_deposit_or_withdrawal(int serverSocket, sockaddr_in& srv, Semantics semantic, Opcode opcode);
void handle_monitor(int serverSocket, sockaddr_in& srv, Semantics semantic);
void handle_view_account(int serverSocket, sockaddr_in& srv, Semantics semantic);
void handle_transfer(int serverSocket, sockaddr_in& srv, Semantics semantic);

/*
main client loop
- parses input arguments (server ip, port, semantic)
- sets up udp socket
- repeatedly prompts user for operations
*/
int main(int argc, char** argv){
    if(argc < 4){
        std::cerr << "Usage: client_app <server_ip> <server_port> <semantic> [-e-deposit_withdraw] [-e-view]\n";
        return 1;
    }
    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    Semantics semantic = std::stoi(argv[3]) == 1 ? Semantics::AT_MOST_ONCE : Semantics::AT_LEAST_ONCE;

    for(int i = 4; i < argc; i++){
        std::string arg = argv[i];
        if(arg == "-e-deposit_withdraw"){
            experiment_deposit_withdraw = true;
        }else if(arg == "-e-view"){
            experiment_view = true;
        }else{
            std::cerr << "Unknown argument: " << arg << "\n";
            std::cerr << "Usage: client_app <server_ip> <server_port> <semantic> [-e-deposit_withdraw] [-e-view]\n";
            return 1;
        }
    }

    int serverSocket = ::socket(AF_INET, SOCK_DGRAM, 0);
    if(serverSocket < 0){
        perror("socket");
        return 1;
    }

    //set 1 second timeout so client can retry if reply is lost
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

    //cli interface for user to select operations
    while(1){
        int choice;
        std::cout << "===== Banking System =====\n";
        std::cout << "1. Open Account\n";
        std::cout << "2. Close Account\n";
        std::cout << "3. Deposit\n";
        std::cout << "4. Withdraw\n";
        std::cout << "5. Monitor\n";
        std::cout << "6. View Account Info\n";
        std::cout << "7. Transfer Money\n";
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
            case 6:
                handle_view_account(serverSocket, srv, semantic);
                continue;
            case 7:
                handle_transfer(serverSocket, srv, semantic);
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

/*
send open account request
- collects user input
- sends request to server
- retries on timeout
- prints result or error
*/
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

    //retry up to 3 times if no reply is received
    //same rid is reused so server can detect duplicates
    for(int i = 1; i <= 3; i++){
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
            std::cout << msg << ". AccountID = " << aid << "\n";
            return;

        }catch(const std::exception& e){
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    std::cout << "No reply from server\n";
}

/*
send close account request
- verifies ownership using name + password
- removes account if valid
*/
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

    //retry up to 3 times if no reply is received
    //same rid is reused so server can detect duplicates
    for(int i = 1; i <= 3; i++){
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

            uint32_t closed_aid = br.u32();
            std::string msg = br.str_u16len();
            std::cout << msg << ". Account aid = " << closed_aid << "\n";
            return;

        }catch(const std::exception& e){
            std::cerr << "Error: " << e.what() << "\n";
        }
    }
    std::cout << "No reply from server\n";
}

/*
send deposit or withdraw request
- updates account balance on server
- non-idempotent: repeated execution changes balance again
*/
void handle_deposit_or_withdrawal(int serverSocket, sockaddr_in& srv, Semantics semantic, Opcode opcode){
    std::string name, password;
    uint32_t aid = {};
    int currency_in_int = {};
    Currency currency{};
    double amount = {};

    std::cout << "Enter name: ";
    std::getline(std::cin, name);

    std::cout << "\nEnter password: ";
    std::getline(std::cin, password);

    std::cout << "\nEnter Account ID: ";
    std::cin >> aid;

    std::cout << "\nEnter currency (0 = SGD, 1 = RM): ";
    std::cin >> currency_in_int;

    currency = (currency_in_int == 0) ? Currency::SGD : Currency::RM;

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

    //retry up to 3 times if no reply is received
    //same rid is reused so server can detect duplicates
    for(int i = 1; i <= 3; i++){

        //experiment mode: drop first request to simulate request loss
        if((i == 1) && experiment_deposit_withdraw){
            std::cout << "[experiment] dropping first deposit/withdraw request\n";
            continue;
        }

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
                std::cout << msg << ". New balance: " << balance << "\n";
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

/*
send monitor request
- registers client for callback updates
- waits for updates during given interval
*/
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

    //retry up to 3 times if no reply is received
    //same rid is reused so server can detect duplicates
    for(int i = 1; i <= 3; i++){
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
                std::cout << "Monitor request failed. Error: " << msg << "\n";
                return;
            }

            std::string msg = br.str_u16len();
            std::cout << msg << "\n";
            std::cout << "Waiting for callback updates...\n";

            auto end_time = std::chrono::steady_clock::now() + std::chrono::seconds(interval_in_seconds);

            //receive callback updates from server until interval expires
            while(std::chrono::steady_clock::now() < end_time){
                sockaddr_in update_from{};
                socklen_t update_len = sizeof(update_from);

                ssize_t update_bytes = recvfrom(serverSocket, reply_buffer, sizeof(reply_buffer), 0,
                                                (sockaddr*)&update_from, &update_len);

                if(update_bytes < 0){
                    continue;
                }

                try{
                    ByteReader update_br(reply_buffer, static_cast<size_t>(update_bytes));

                    uint8_t update_version = update_br.u8();
                    Opcode update_opcode = static_cast<Opcode>(update_br.u8());
                    (void)update_br.u16();

                    if(update_version != version_number){
                        std::cout << "Ignoring callback with wrong version\n";
                        continue;
                    }

                    std::string account_name = update_br.str_u16len();
                    uint32_t account_aid = update_br.u32();
                    Currency account_currency = static_cast<Currency>(update_br.u8());
                    double account_balance = u64_to_double(update_br.u64());
                    std::string update_msg = update_br.str_u16len();

                    std::cout << "\n=== CALLBACK UPDATE ===\n";
                    std::cout << "Operation: " << static_cast<int>(update_opcode) << "\n";
                    std::cout << "Name: " << account_name << "\n";
                    std::cout << "Account ID: " << account_aid << "\n";
                    std::cout << "Currency: " << (account_currency == Currency::SGD ? "SGD" : "RM") << "\n";
                    std::cout << "Balance: " << account_balance << "\n";
                    std::cout << "Message: " << update_msg << "\n";
                    std::cout << "=======================\n";

                }catch(const std::exception& e){
                    std::cerr << "Callback parse error: " << e.what() << "\n";
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

/*
send view account request
- retrieves account info (currency, balance)
- idempotent: repeated execution does not modify state
*/
void handle_view_account(int serverSocket, sockaddr_in& srv, Semantics semantic){
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
    bw.u8(static_cast<uint8_t>(Opcode::VIEW_ACCOUNT));
    bw.u8(static_cast<uint8_t>(semantic));
    bw.u8(0);
    uint32_t current_rid = next_rid++;
    bw.u32(current_rid);

    bw.str_with_len(name);
    bw.str_with_len(password);
    bw.u32(aid);

    uint8_t reply_buffer[2048];

    //retry up to 3 times if no reply is received
    //same rid is reused so server can detect duplicates
    for(int i = 1; i <= 3; i++){

        //experiment mode: drop first request to simulate request loss
        if((i == 1) && experiment_view){
            std::cout << "[experiment] dropping first view account request\n";
            continue;
        }

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
                Currency currency = static_cast<Currency>(br.u8());
                double balance = u64_to_double(br.u64());
                std::string msg = br.str_u16len();

                std::cout << "\n" << msg << "\n";
                std::cout << "Currency: " << (currency == Currency::SGD ? "SGD" : "RM") << "\n";
                std::cout << "Balance: " << balance << "\n\n";
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

/*
send transfer request
- moves funds between accounts
- non-idempotent: repeated execution duplicates transfer
*/
void handle_transfer(int serverSocket, sockaddr_in& srv, Semantics semantic){
    std::string sender_name, sender_password;
    std::string recipient_name;
    uint32_t sender_aid = {};
    uint32_t recipient_aid = {};
    double amount = {};

    std::cout << "Enter sender name: ";
    std::getline(std::cin, sender_name);

    std::cout << "Enter sender password: ";
    std::getline(std::cin, sender_password);

    std::cout << "Enter sender Account ID: ";
    std::cin >> sender_aid;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    std::cout << "Enter recipient name: ";
    std::getline(std::cin, recipient_name);

    std::cout << "Enter recipient Account ID: ";
    std::cin >> recipient_aid;

    std::cout << "Enter transfer amount: ";
    std::cin >> amount;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    ByteWriter bw;
    bw.u8(version_number);
    bw.u8(static_cast<uint8_t>(Opcode::TRANSFER));
    bw.u8(static_cast<uint8_t>(semantic));
    bw.u8(0);
    uint32_t current_rid = next_rid++;
    bw.u32(current_rid);

    bw.str_with_len(sender_name);
    bw.str_with_len(sender_password);
    bw.u32(sender_aid);
    bw.str_with_len(recipient_name);
    bw.u32(recipient_aid);
    bw.u64(double_to_u64(amount));

    uint8_t reply_buffer[2048];

    //retry up to 3 times if no reply is received
    //same rid is reused so server can detect duplicates
    for(int i = 1; i <= 3; i++){
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
                double sender_balance = u64_to_double(br.u64());
                std::string msg = br.str_u16len();

                std::cout << msg << "\n";
                std::cout << "Sender new balance: " << sender_balance << "\n";
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