#include "tiray_sdk.hpp"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <vector>

struct CustomerFrameStore {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<uint8_t> latest_frame;
    tiray_image_frame_t metadata{};
    bool got_frame = false;
};

static void on_frame(const tiray_image_frame_t* frame, void* user_data) {
    if (frame == nullptr || frame->data == nullptr) return;
    auto* store = static_cast<CustomerFrameStore*>(user_data);
    {
        std::lock_guard<std::mutex> lock(store->mutex);
        store->latest_frame.assign(frame->data, frame->data + frame->data_length);
        store->metadata = *frame;
        store->metadata.data = store->latest_frame.data();
        store->metadata.data_capacity = store->latest_frame.size();
        store->got_frame = true;
    }
    store->cv.notify_one();
    std::cout << "frame id=" << frame->image_id
              << " type=" << frame->image_type
              << " size=" << frame->rows << 'x' << frame->columns
              << " bytes=" << frame->data_length << '\n';
}

static int fail(const char* step, tiray_status_t status) {
    std::cerr << step << ": " << tiray_status_string(status) << '\n';
    return 1;
}

int main(int argc, char** argv) {
    tiray_sdk_config_t config;
    tiray_sdk_default_config(&config);
    config.rs422_device = argc > 1 ? argv[1] : "/dev/ttyWCH0";

    tiray::Sdk sdk(&config);
    tiray_status_t result = sdk.open();
    if (result != TIRAY_STATUS_OK) return fail("rs422 open", result);

    result = sdk.ping();
    if (result != TIRAY_STATUS_OK) return fail("ping", result);

    tiray_device_status_t device_status{};
    result = sdk.status(device_status);
    if (result != TIRAY_STATUS_OK) return fail("status", result);
    std::cout << "work_state=" << device_status.work_state
              << " last_error=" << device_status.last_error << '\n';

    tiray::PcieReceiver pcie;
    result = pcie.open();
    if (result != TIRAY_STATUS_OK) return fail("pcie open", result);

    CustomerFrameStore store;
    result = pcie.start(on_frame, &store);
    if (result != TIRAY_STATUS_OK) return fail("pcie start", result);

    result = sdk.start_static();
    if (result != TIRAY_STATUS_OK) {
        pcie.stop();
        return fail("start_static", result);
    }

    {
        std::unique_lock<std::mutex> lock(store.mutex);
        if (!store.cv.wait_for(lock, std::chrono::seconds(5), [&] { return store.got_frame; })) {
            std::cerr << "timed out waiting for image (is PCIe connected?)\n";
            pcie.stop();
            sdk.close();
            return 2;
        }
        std::cout << "copied " << store.latest_frame.size() << " bytes into customer buffer\n";
    }

    pcie.stop();
    sdk.close();
    return 0;
}
