#include "tiray_sdk.hpp"

#include <iostream>
#include <vector>

int main() {
    tiray_sdk_config_t config;
    tiray_sdk_default_config(&config);
    tiray::Sdk sdk(&config);
    if (sdk.open() != TIRAY_STATUS_OK) {
        std::cerr << "RS422 open failed\n";
        return 1;
    }

    auto job = sdk.ping_async(
        [](tiray_status_t status) {
            std::cout << "PING status=" << static_cast<int>(status) << '\n';
        });
    return static_cast<int>(job.wait());
}
