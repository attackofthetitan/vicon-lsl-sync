#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    if (argc > 1 && std::filesystem::path(argv[1]).filename() == "session-stop.xdf") {
        std::cout << "waiting for Stop" << std::endl;
        std::string command;
        if (!std::getline(std::cin, command) || !command.empty()) return 1;
        std::cout << "Stop received" << std::endl;
        return 0;
    }
    const std::string payload(96, 'x');
    for (int line = 0; line < 900; ++line) {
        std::cout << line << ':' << payload << '\n';
    }
    std::cout << "cwd=" << std::filesystem::current_path().generic_string() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return 0;
}
