#include "tiray_sdk.hpp"

#include <iostream>
#include <mutex>
#include <vector>

struct CustomerFrameStore {
    std::mutex mutex;
    std::vector<uint8_t> latest_frame;
    tiray_image_frame_t metadata{};
};

static void on_frame(const tiray_image_frame_t* frame, void* user_data) {
    if (frame == nullptr) return;
    CustomerFrameStore* store = static_cast<CustomerFrameStore*>(user_data);
    if (store != nullptr) {
        std::lock_guard<std::mutex> lock(store->mutex);
        store->latest_frame.assign(frame->data, frame->data + frame->data_length);
        store->metadata = *frame;
        store->metadata.data = store->latest_frame.data();
        store->metadata.data_capacity = store->latest_frame.size();
    }
    std::cout << "image id=" << frame->image_id
              << " type=" << frame->image_type
              << " size=" << frame->rows << 'x' << frame->columns
              << " bytes=" << frame->data_length << '\n';
    // 客户程序应在这里把 frame->data 复制到自己的图像队列。
}

int main() {
    tiray_sdk_config_t config;
    tiray_sdk_default_config(&config);
    config.rs422_device = "/dev/ttyWCH0";

    tiray::Sdk sdk(&config);
    tiray_status_t result = sdk.open();
    if (result != TIRAY_STATUS_OK) {
        std::cerr << "open failed: " << tiray_status_string(result) << '\n';
        return 1;
    }

    tiray::PcieReceiver pcie;
    result = pcie.open();
    if (result != TIRAY_STATUS_OK) {
        std::cerr << "pcie open failed: " << tiray_status_string(result) << '\n';
        return 2;
    }
    CustomerFrameStore frame_store;
    result = pcie.start(on_frame, &frame_store);
    if (result != TIRAY_STATUS_OK) {
        std::cerr << "pcie start failed: " << tiray_status_string(result) << '\n';
        return 3;
    }

    tiray_dynamic_config_t dynamic_config{};
    if (sdk.get_dynamic_config(dynamic_config) == TIRAY_STATUS_OK) {
        std::cout << "dynamic cycle=" << dynamic_config.cycle << '\n';
    }
    result = sdk.start_static();
    if (result != TIRAY_STATUS_OK)
        std::cerr << "static capture failed: " << tiray_status_string(result) << '\n';

    pcie.stop();
    sdk.close();
    return result == TIRAY_STATUS_OK ? 0 : 4;
}
