#include <iostream>
#include <string>

#include "server_app.h"

int main(int argc, char** argv){
    if(argc < 2){
        std::cerr << "Error: Port number required\n";
        return 1;
    }

    try{
        int port = std::stoi(argv[1]);
        ServerApp app(static_cast<uint16_t>(port));
        app.run();
    }catch(const std::exception& e){
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}