#include <iostream>
#include <string>

#include "server_app.h"

int main(int argc, char** argv){
    if(argc < 2){
        std::cerr << "Usage: server_app <port> [-e-deposit_withdraw] [-e-view]\n";
        return 1;
    }

    bool experiment_flag_deposit_withdraw = false;
    bool experiment_flag_view = false;

    try{
        int port = std::stoi(argv[1]);
        for(int i = 2; i < argc; i++){
            std::string arg = argv[i];
            if(arg == "-e-deposit_withdraw"){
                experiment_flag_deposit_withdraw = true;
            }else if(arg == "-e-view"){
                experiment_flag_view = true;
            }else{
                std::cerr << "Unknown argument: " << arg << "\n";
                std::cerr << "Usage: server_app <port> [-e-deposit_withdraw] [-e-view]\n";
            }
        }
        ServerApp app(static_cast<uint16_t>(port), experiment_flag_deposit_withdraw, experiment_flag_view);
        app.run();
    }catch(const std::exception& e){
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}