#include "squirrel.hpp"
#include <iostream>
#include <chrono>
#include <thread>
int main() {
    Squirrel::Server server(8080);
    server.setStaticDir("public");
    std::thread serverThread([&]() {
        server.start();
    });
    std::cout << "server running at http://localhost:8080/" << std::endl;
    std::cout << "press ctrl+c to end" << std::endl;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "stopping server..." << std::endl;
    server.stop();
    serverThread.join();
    std::cout << "server stopped.. bai" << std::endl;
    return 0;
}
