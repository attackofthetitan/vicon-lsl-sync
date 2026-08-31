#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

int main() {
    const std::string payload(96, 'x');
    for (int line = 0; line < 900; ++line) {
        std::cout << line << ':' << payload << '\n';
    }
    std::cout << "cwd=" << std::filesystem::current_path().generic_string() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return 0;
}
