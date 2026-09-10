#ifndef TIRAY_SDK_HPP
#define TIRAY_SDK_HPP

#include "tiray_sdk.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace tiray {

class AsyncJob {
public:
    AsyncJob() = default;
    ~AsyncJob() { reset(); }
    AsyncJob(const AsyncJob&) = delete;
    AsyncJob& operator=(const AsyncJob&) = delete;
    AsyncJob(AsyncJob&& other) noexcept : job_(other.job_), context_(other.context_) {
        other.job_ = nullptr; other.context_ = nullptr;
    }
    AsyncJob& operator=(AsyncJob&& other) noexcept {
        if (this != &other) {
            reset(); job_ = other.job_; context_ = other.context_;
            other.job_ = nullptr; other.context_ = nullptr;
        }
        return *this;
    }
    tiray_status_t wait() { return job_ != nullptr ? tiray_async_job_wait(job_) : TIRAY_STATUS_INVALID_ARGUMENT; }
    void reset() {
        if (job_ != nullptr) tiray_async_job_destroy(job_);
        delete context_; job_ = nullptr; context_ = nullptr;
    }
    bool valid() const { return job_ != nullptr; }

private:
    struct CallbackContext {
        std::function<void(tiray_status_t)> callback;
    };
    static void callback_trampoline(tiray_status_t status, void* user_data) {
        CallbackContext* context = static_cast<CallbackContext*>(user_data);
        if (context != nullptr && context->callback != nullptr) context->callback(status);
    }
    tiray_async_job_t* job_ = nullptr;
    CallbackContext* context_ = nullptr;
    friend class Sdk;
};

class Sdk {
public:
    explicit Sdk(const tiray_sdk_config_t* config = nullptr)
        : handle_(tiray_sdk_create(config)) {}
    ~Sdk() { tiray_sdk_destroy(handle_); }
    Sdk(const Sdk&) = delete;
    Sdk& operator=(const Sdk&) = delete;
    Sdk(Sdk&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    Sdk& operator=(Sdk&& other) noexcept {
        if (this != &other) {
            tiray_sdk_destroy(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    tiray_status_t open() { return tiray_sdk_open(handle_); }
    void close() { tiray_sdk_close(handle_); }
    bool is_open() const { return tiray_sdk_is_open(handle_) != 0; }
    tiray_status_t ping() { return tiray_ping(handle_); }
    tiray_status_t reboot() { return tiray_reboot(handle_); }
    tiray_status_t status(tiray_device_status_t& value) { return tiray_get_status(handle_, &value); }
    tiray_status_t dynamic_status(tiray_dynamic_status_t& value) { return tiray_query_dynamic(handle_, &value); }
    tiray_status_t start_static() { return tiray_start_static_capture(handle_); }
    tiray_status_t start_dynamic() { return tiray_start_dynamic(handle_); }
    tiray_status_t stop_dynamic() { return tiray_stop_dynamic(handle_); }
    tiray_status_t offset_begin(uint32_t total_frames, uint32_t valid_frames, uint8_t mode) {
        return tiray_cal_offset_begin(handle_, total_frames, valid_frames, mode);
    }
    tiray_status_t offset_capture() { return tiray_cal_offset_capture(handle_); }
    tiray_status_t offset_build() { return tiray_cal_offset_build(handle_); }
    tiray_status_t offset_cancel() { return tiray_cal_offset_cancel(handle_); }
    tiray_status_t gain_begin(const std::vector<uint32_t>& levels,
                              uint32_t frames_per_level, float defect_threshold) {
        return tiray_cal_gain_begin(handle_, levels.data(), levels.size(),
                                    frames_per_level, defect_threshold);
    }
    tiray_status_t gain_capture(uint32_t level) { return tiray_cal_gain_capture(handle_, level); }
    tiray_status_t gain_build() { return tiray_cal_gain_build(handle_); }
    tiray_status_t gain_cancel() { return tiray_cal_gain_cancel(handle_); }
    tiray_status_t calibration_status(tiray_cal_status_t& value) {
        return tiray_cal_status(handle_, &value);
    }
    tiray_status_t upload_config(uint32_t template_kind, uint32_t image_addr,
                                 uint32_t rows, uint32_t columns, uint32_t package_count) {
        return tiray_img_upload_config(handle_, template_kind, image_addr, rows, columns, package_count);
    }
    tiray_status_t upload_start() { return tiray_img_upload_start(handle_); }
    tiray_status_t upload_status(tiray_image_upload_status_t& value) {
        return tiray_img_upload_query(handle_, &value);
    }
    tiray_status_t get_config(uint16_t group, std::vector<tiray_config_item_t>& items) {
        items.resize(256);
        size_t count = 0;
        const tiray_status_t result = tiray_get_config_group(handle_, group, items.data(), items.size(), &count);
        if (result == TIRAY_STATUS_OK) items.resize(count); else items.clear();
        return result;
    }
    tiray_status_t set_config(uint16_t group, const std::vector<tiray_config_item_t>& items) {
        return tiray_set_config_group(handle_, group, items.data(), items.size());
    }
    tiray_status_t get_static_config(tiray_static_config_t& value) { return tiray_get_static_config(handle_, &value); }
    tiray_status_t set_static_config(const tiray_static_config_t& value) { return tiray_set_static_config(handle_, &value); }
    tiray_status_t get_dynamic_config(tiray_dynamic_config_t& value) { return tiray_get_dynamic_config(handle_, &value); }
    tiray_status_t set_dynamic_config(const tiray_dynamic_config_t& value) { return tiray_set_dynamic_config(handle_, &value); }
    AsyncJob ping_async(std::function<void(tiray_status_t)> callback) {
        AsyncJob result;
        result.context_ = new AsyncJob::CallbackContext{std::move(callback)};
        const tiray_status_t status = tiray_sdk_ping_async(
            handle_, AsyncJob::callback_trampoline,
            result.context_, &result.job_);
        if (status != TIRAY_STATUS_OK) {
            delete result.context_; result.context_ = nullptr;
        }
        return result;
    }
    tiray_sdk_t* native_handle() { return handle_; }
    const tiray_sdk_t* native_handle() const { return handle_; }

private:
    tiray_sdk_t* handle_;
};

class PcieReceiver {
public:
    explicit PcieReceiver(const tiray_pcie_config_t* config = nullptr)
        : handle_(tiray_pcie_create(config)) {}
    ~PcieReceiver() { tiray_pcie_destroy(handle_); }
    PcieReceiver(const PcieReceiver&) = delete;
    PcieReceiver& operator=(const PcieReceiver&) = delete;
    PcieReceiver(PcieReceiver&& other) noexcept : handle_(other.handle_) { other.handle_ = nullptr; }
    PcieReceiver& operator=(PcieReceiver&& other) noexcept {
        if (this != &other) {
            tiray_pcie_destroy(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    tiray_status_t open() { return tiray_pcie_open(handle_); }
    void close() { tiray_pcie_close(handle_); }
    tiray_status_t wait_frame(tiray_image_frame_t& frame) { return tiray_pcie_wait_frame(handle_, &frame); }
    tiray_status_t start(tiray_pcie_frame_callback_t callback, void* user_data = nullptr) {
        return tiray_pcie_start(handle_, callback, user_data);
    }
    void stop() { tiray_pcie_stop(handle_); }
    bool is_running() const { return tiray_pcie_is_running(handle_) != 0; }
    tiray_pcie_receiver_t* native_handle() { return handle_; }

private:
    tiray_pcie_receiver_t* handle_;
};

} // namespace tiray

#endif
