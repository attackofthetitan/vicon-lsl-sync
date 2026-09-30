#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char* argv[]) {
    if (const char* survival_file = std::getenv("VICON_LSL_FIXTURE_SURVIVAL_FILE")) {
        // Writes like a recording LabRecorder until nothing reads its output any
        // more, then leaves a file to show it survived that.
        for (int attempt = 0; attempt < 250; ++attempt) {
            std::cout << "still recording" << std::endl;
            std::cerr << "still recording" << std::endl;
            if (!std::cout || !std::cerr) {
                std::ofstream(survival_file) << "survived\n";
                return 0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return 3;
    }
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
