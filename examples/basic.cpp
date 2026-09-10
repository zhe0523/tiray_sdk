#include "tiray_sdk.hpp"

#include <iostream>

int main() {
    tiray_sdk_config_t config;
    tiray_sdk_default_config(&config);
    tiray::Sdk sdk(&config);
    const tiray_status_t open_status = sdk.open();
    if (open_status != TIRAY_STATUS_OK) {
        std::cerr << "RS422 open failed: " << static_cast<int>(open_status) << '\n';
        return 1;
    }
    const tiray_status_t ping_status = sdk.ping();
    std::cout << "PING status=" << static_cast<int>(ping_status) << '\n';
    return ping_status == TIRAY_STATUS_OK ? 0 : 2;
}
