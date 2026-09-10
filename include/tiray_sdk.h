#ifndef TIRAY_SDK_H
#define TIRAY_SDK_H

/*
 * TiRay 43108 SDK 公共接口第一阶段定义。
 *
 * 该头文件只使用 C99 基础类型，不依赖 Qt、串口库或平台 GUI 框架。
 * 后续 C++/Qt 封装只能建立在这些稳定接口之上。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(TIRAY_SDK_BUILD)
#  define TIRAY_SDK_API __declspec(dllexport)
#elif defined(_WIN32)
#  define TIRAY_SDK_API __declspec(dllimport)
#else
#  define TIRAY_SDK_API __attribute__((visibility("default")))
#endif

typedef enum tiray_status {
    TIRAY_STATUS_OK = 0,
    TIRAY_STATUS_INVALID_ARGUMENT = 1,
    TIRAY_STATUS_NOT_OPEN = 2,
    TIRAY_STATUS_BUSY = 3,
    TIRAY_STATUS_TIMEOUT = 4,
    TIRAY_STATUS_PROTOCOL_ERROR = 5,
    TIRAY_STATUS_CRC_ERROR = 6,
    TIRAY_STATUS_IO_ERROR = 7,
    TIRAY_STATUS_DEVICE_ERROR = 8,
    TIRAY_STATUS_INTERNAL_ERROR = 9,
} tiray_status_t;

typedef enum tiray_sdk_profile {
    TIRAY_SDK_PROFILE_EXTERNAL = 0,
    TIRAY_SDK_PROFILE_INTERNAL = 1,
} tiray_sdk_profile_t;

typedef struct tiray_retry_config {
    uint32_t response_timeout_ms; /* 默认 500 */
    uint32_t max_retries;         /* 默认 2 */
} tiray_retry_config_t;

typedef struct tiray_sdk_config {
    const char* rs422_device;
    uint32_t rs422_baudrate; /* 默认 115200 */
    tiray_retry_config_t retry;
    tiray_sdk_profile_t profile; /* 默认对外版；内部版允许访问全部配置组 */
} tiray_sdk_config_t;

typedef struct tiray_sdk tiray_sdk_t;
typedef struct tiray_async_job tiray_async_job_t;

typedef void (*tiray_async_callback_t)(tiray_status_t status,
                                       void* user_data);

typedef struct tiray_config_item {
    uint16_t item_id;
    uint32_t value;
} tiray_config_item_t;

typedef struct tiray_device_status {
    uint32_t work_mode;
    uint32_t work_state;
    uint32_t last_error;
    uint32_t capture_id;
    uint32_t frame_count;
    uint32_t output_addr;
    uint32_t offset_addr;
    uint32_t write_state;
    uint32_t write_end;
    uint32_t correction_state;
    uint32_t correction_end;
} tiray_device_status_t;

typedef struct tiray_dynamic_status {
    uint32_t state;
    uint32_t end;
    uint32_t debug_out;
    uint32_t final_image_addr;
    uint32_t frame_count;
} tiray_dynamic_status_t;

typedef struct tiray_static_config {
    uint32_t idle_clean_interval_ms;
    uint32_t exposure_window_ms;
    uint32_t dark_window_ms;
} tiray_static_config_t;

typedef struct tiray_dynamic_config {
    uint32_t cycle;
    uint32_t image_start_addr;
    uint32_t image_end_addr;
    uint32_t start_timeout_ms;
    uint32_t state_poll_interval_ms;
    uint32_t stop_timeout_ms;
    uint32_t step_high[10];
    uint32_t step_low[10];
} tiray_dynamic_config_t;

typedef struct tiray_cal_status {
    uint32_t task_id;
    uint32_t task_kind;
    uint32_t task_state;
    uint32_t last_error;
    uint32_t progress_current;
    uint32_t progress_total;
    uint32_t gain_level;
    uint32_t level_count;
    uint32_t levels_ready;
    uint32_t frames_per_level;
    float defect_threshold;
    uint32_t bad_pixel_count;
} tiray_cal_status_t;

typedef struct tiray_image_upload_status {
    uint32_t state;
    uint32_t end;
    uint32_t debug_out;
} tiray_image_upload_status_t;

typedef struct tiray_pcie_config {
    const char* event_device;
    const char* c2h_device;
    const char* bar0_resource;
    uint32_t wait_timeout_ms;
    uint32_t fallback_rows;
    uint32_t fallback_columns;
} tiray_pcie_config_t;

typedef struct tiray_image_frame {
    uint8_t* data;
    size_t data_capacity;
    size_t data_length;
    uint32_t rows;
    uint32_t columns;
    uint32_t image_id;
    uint32_t image_type; /* 0=正常图片，1=模板上传 */
    uint64_t final_image_address;
} tiray_image_frame_t;

typedef struct tiray_pcie_receiver tiray_pcie_receiver_t;
typedef void (*tiray_pcie_frame_callback_t)(const tiray_image_frame_t* frame, void* user_data);

TIRAY_SDK_API void tiray_sdk_default_config(tiray_sdk_config_t* config);
TIRAY_SDK_API const char* tiray_status_string(tiray_status_t status);
TIRAY_SDK_API tiray_sdk_t* tiray_sdk_create(const tiray_sdk_config_t* config);
TIRAY_SDK_API void tiray_sdk_destroy(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_sdk_open(tiray_sdk_t* sdk);
TIRAY_SDK_API void tiray_sdk_close(tiray_sdk_t* sdk);
TIRAY_SDK_API int tiray_sdk_is_open(const tiray_sdk_t* sdk);
/* 异步任务持有 SDK 指针；销毁 SDK 前必须先 wait/destroy 所有任务。 */
TIRAY_SDK_API tiray_status_t tiray_sdk_ping_async(tiray_sdk_t* sdk,
                                                   tiray_async_callback_t callback,
                                                   void* user_data,
                                                   tiray_async_job_t** job);
TIRAY_SDK_API tiray_status_t tiray_async_job_wait(tiray_async_job_t* job);
TIRAY_SDK_API void tiray_async_job_destroy(tiray_async_job_t* job);
TIRAY_SDK_API tiray_status_t tiray_ping(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_reboot(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_get_status(tiray_sdk_t* sdk, tiray_device_status_t* status);
TIRAY_SDK_API tiray_status_t tiray_query_dynamic(tiray_sdk_t* sdk, tiray_dynamic_status_t* status);
TIRAY_SDK_API tiray_status_t tiray_start_static_capture(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_start_dynamic(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_stop_dynamic(tiray_sdk_t* sdk);
/* 模板制作命令均为同步协议请求；长时间计算通过 tiray_cal_status 查询进度。 */
TIRAY_SDK_API tiray_status_t tiray_cal_offset_begin(tiray_sdk_t* sdk,
                                                    uint32_t total_frames,
                                                    uint32_t valid_frames,
                                                    uint8_t mode);
TIRAY_SDK_API tiray_status_t tiray_cal_offset_capture(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_cal_offset_build(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_cal_offset_cancel(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_cal_gain_begin(tiray_sdk_t* sdk,
                                                  const uint32_t* levels,
                                                  size_t level_count,
                                                  uint32_t frames_per_level,
                                                  float defect_threshold);
TIRAY_SDK_API tiray_status_t tiray_cal_gain_capture(tiray_sdk_t* sdk, uint32_t level);
TIRAY_SDK_API tiray_status_t tiray_cal_gain_build(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_cal_gain_cancel(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_cal_status(tiray_sdk_t* sdk, tiray_cal_status_t* status);
TIRAY_SDK_API tiray_status_t tiray_img_upload_config(tiray_sdk_t* sdk,
                                                     uint32_t template_kind,
                                                     uint32_t image_addr,
                                                     uint32_t rows,
                                                     uint32_t columns,
                                                     uint32_t package_count);
TIRAY_SDK_API tiray_status_t tiray_img_upload_start(tiray_sdk_t* sdk);
TIRAY_SDK_API tiray_status_t tiray_img_upload_query(tiray_sdk_t* sdk,
                                                    tiray_image_upload_status_t* status);
TIRAY_SDK_API tiray_status_t tiray_get_config_group(tiray_sdk_t* sdk,
                                                     uint16_t group_id,
                                                     tiray_config_item_t* items,
                                                     size_t item_capacity,
                                                     size_t* item_count);
TIRAY_SDK_API tiray_status_t tiray_set_config_group(tiray_sdk_t* sdk,
                                                     uint16_t group_id,
                                                     const tiray_config_item_t* items,
                                                     size_t item_count);
TIRAY_SDK_API tiray_status_t tiray_get_static_config(tiray_sdk_t* sdk,
                                                      tiray_static_config_t* config);
TIRAY_SDK_API tiray_status_t tiray_set_static_config(tiray_sdk_t* sdk,
                                                      const tiray_static_config_t* config);
TIRAY_SDK_API tiray_status_t tiray_get_dynamic_config(tiray_sdk_t* sdk,
                                                       tiray_dynamic_config_t* config);
TIRAY_SDK_API tiray_status_t tiray_set_dynamic_config(tiray_sdk_t* sdk,
                                                       const tiray_dynamic_config_t* config);
TIRAY_SDK_API void tiray_pcie_default_config(tiray_pcie_config_t* config);
TIRAY_SDK_API tiray_pcie_receiver_t* tiray_pcie_create(const tiray_pcie_config_t* config);
TIRAY_SDK_API void tiray_pcie_destroy(tiray_pcie_receiver_t* receiver);
TIRAY_SDK_API tiray_status_t tiray_pcie_open(tiray_pcie_receiver_t* receiver);
TIRAY_SDK_API void tiray_pcie_close(tiray_pcie_receiver_t* receiver);
TIRAY_SDK_API tiray_status_t tiray_pcie_wait_frame(tiray_pcie_receiver_t* receiver,
                                                    tiray_image_frame_t* frame);
TIRAY_SDK_API tiray_status_t tiray_pcie_start(tiray_pcie_receiver_t* receiver,
                                               tiray_pcie_frame_callback_t callback,
                                               void* user_data);
TIRAY_SDK_API void tiray_pcie_stop(tiray_pcie_receiver_t* receiver);
TIRAY_SDK_API int tiray_pcie_is_running(const tiray_pcie_receiver_t* receiver);

#ifdef __cplusplus
}
#endif

#endif
