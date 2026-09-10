/*
 * tiray_host —— TiRay 43108 SDK 维护上位机。
 *
 * 定位：维护和 SDK 回归测试工具。只调用 tiray_sdk 公共 C/C++ API，
 * 不实现也不拼接任何协议细节。研发自有协议栈工具（pa_host /
 * pa_controller）不在本仓库，仍按原方式独立维护。
 *
 * 构建：-DTIRAY_SDK_BUILD_HOST=ON，目标名 tiray_host。
 */

#include "tiray_sdk.hpp"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::string port = "/dev/ttyWCH0";
    bool one_shot = false;
    bool internal_profile = false;
    bool pcie = false;
    std::string pcie_save_path;
};

bool check(tiray_status_t status, const char* operation) {
    if (status == TIRAY_STATUS_OK) {
        std::cout << "[PASS] " << operation << '\n';
        return true;
    }
    std::cerr << "[FAIL] " << operation << ": "
              << tiray_status_string(status)
              << " (" << static_cast<int>(status) << ")\n";
    return false;
}

const char* work_mode_name(uint32_t mode) {
    switch (mode) {
        case TIRAY_WORK_MODE_IDLE: return "IDLE";
        case TIRAY_WORK_MODE_AED: return "AED";
        case TIRAY_WORK_MODE_SYNC_OUT: return "SYNC_OUT";
        case TIRAY_WORK_MODE_SYNC_IN: return "SYNC_IN";
        case TIRAY_WORK_MODE_PREP: return "PREP";
        case TIRAY_WORK_MODE_CONTINUOUS: return "CONTINUOUS";
        case TIRAY_WORK_MODE_INNER: return "INNER";
        case TIRAY_WORK_MODE_FREE_SYNC: return "FREE_SYNC";
        case TIRAY_WORK_MODE_DDR: return "DDR";
        default: return "UNKNOWN";
    }
}

const char* work_state_name(uint32_t state) {
    switch (state) {
        case TIRAY_WORK_STATE_STOPPED: return "STOPPED";
        case TIRAY_WORK_STATE_IDLE_WAIT: return "IDLE_WAIT";
        case TIRAY_WORK_STATE_IDLE_CLEANING: return "IDLE_CLEANING";
        case TIRAY_WORK_STATE_EXPOSURE_WINDOW: return "EXPOSURE_WINDOW";
        case TIRAY_WORK_STATE_BRIGHT_CAPTURE: return "BRIGHT_CAPTURE";
        case TIRAY_WORK_STATE_DARK_WINDOW: return "DARK_WINDOW";
        case TIRAY_WORK_STATE_DARK_CAPTURE: return "DARK_CAPTURE";
        case TIRAY_WORK_STATE_DYNAMIC_STARTING: return "DYNAMIC_STARTING";
        case TIRAY_WORK_STATE_DYNAMIC_RUNNING: return "DYNAMIC_RUNNING";
        case TIRAY_WORK_STATE_DYNAMIC_STOPPING: return "DYNAMIC_STOPPING";
        case TIRAY_WORK_STATE_DYNAMIC_COMPLETED: return "DYNAMIC_COMPLETED";
        case TIRAY_WORK_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

const char* cal_state_name(uint32_t state) {
    switch (state) {
        case 0: return "空闲";
        case 1: return "运行";
        case 2: return "停止中";
        case 3: return "成功";
        case 4: return "失败";
        case 5: return "取消";
        default: return "未知";
    }
}

std::string hex32(uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

bool parse_u32(const std::string& text, uint32_t& value) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 0);
    if (errno != 0 || end == nullptr || *end != '\0' || parsed > UINT32_MAX) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens;
}

void usage(const char* program) {
    std::cout
        << "tiray_host —— TiRay 43108 SDK 维护上位机（只调用 SDK 公共 API）\n\n"
        << "用法: " << program << " [选项] [命令 [参数...]]\n"
        << "不带命令时进入交互模式。\n\n"
        << "选项:\n"
        << "  --port PATH        RS422 串口，默认 /dev/ttyWCH0\n"
        << "  --baud RATE        波特率，默认 115200（固件侧协商）\n"
        << "  --timeout MS       响应超时毫秒，默认 500；重试默认 2 次，重试超时再加倍\n"
        << "  --internal         使用内部版配置（需以 -DTIRAY_SDK_INTERNAL_BUILD=ON 构建 SDK）\n"
        << "  --pcie             可选：启动 PCIe 帧监听线程并打印/保存图像元数据；\n"
        << "                     与 --save PATH 组合保存原始帧，仅维护诊断用，\n"
        << "                     生产图像接收请直接使用 SDK PCIe 接口\n"
        << "  --help             显示帮助\n\n"
        << "命令:\n"
        << "  ping                                    测试 RS422 通信\n"
        << "  status                                  读取设备状态（工作模式/状态/错误/帧计数）\n"
        << "  static                                  触发一次静态采集命令（下图由 PCIe 完成）\n"
        << "  dynamic_start / dynamic_query / dynamic_stop\n"
        << "  config_get GROUP                        读取配置组并打印（对外版仅组 1/5）\n"
        << "  config_set GROUP ID=VALUE [ID=VALUE...] 下发配置项（十六进制或十进制）\n"
        << "  static_cfg_get / static_cfg_set IDLE_MS EXPOSURE_MS DARK_MS\n"
        << "  dynamic_cfg_get / dynamic_cfg_set 参数组见 dynamic_cfg_set 无参提示，\n"
        << "                                      可省略尾部参数保留原值，'.' 表示保留对应位置原值；\n"
        << "                                      step 高/低电平请继续用 config_set GROUP 5\n"
        << "  offset_begin TOTAL VALID MODE           开始暗场模板（MODE 0=静态 1=动态）\n"
        << "  offset_capture / offset_build / offset_cancel\n"
        << "  gain_begin L1,L2,... FRAMES THRESHOLD   开始亮场模板，如 5000,10000,20000 2 0.300000（省略=0.3）\n"
        << "  gain_capture LEVEL / gain_build / gain_cancel\n"
        << "  cal_status                              查询模板任务状态和进度（需轮询至成功/失败）\n"
        << "  upload_config KIND ADDR ROWS COLS PKGS  配置模板上传（KIND 0=暗场 1=亮场）\n"
        << "  upload_start / upload_query\n"
        << "  reboot                                  重启设备（长命令，需加大超时或加重试）\n"
        << "  help / quit\n\n"
        << "示例:\n"
        << "  sudo " << program << " --port /dev/ttyWCH0                # 交互模式（需串口权限）\n"
        << "  sudo " << program << " --port /dev/ttyWCH0 ping status      # 一次性回归检查，失败即非零退出\n"
        << "  sudo " << program << " --port /dev/ttyWCH0 --pcie --save /tmp/frame.raw\n";
}

void interactive_help() {
    std::cout << "交互命令（输入 help 再次显示）:\n"
              << "  ping / status / reboot\n"
              << "  static\n"
              << "  dynamic_start / dynamic_query / dynamic_stop\n"
              << "  config_get GROUP\n"
              << "  config_set GROUP ID=VALUE [ID=VALUE...]\n"
              << "  static_cfg_get / static_cfg_set IDLE_MS EXPOSURE_MS DARK_MS\n"
              << "  dynamic_cfg_get / dynamic_cfg_set（不带参数查看提示）\n"
              << "  offset_begin TOTAL VALID MODE / offset_capture / offset_build / offset_cancel\n"
              << "  gain_begin L1,L2,... FRAMES THRESHOLD / gain_capture LEVEL / gain_build / gain_cancel\n"
              << "  cal_status\n"
              << "  upload_config KIND ADDR ROWS COLS PKGS / upload_start / upload_query\n"
              << "  quit\n";
}

class PcieMonitor {
public:
    explicit PcieMonitor(std::string save_path) : save_path_(std::move(save_path)) {}
    ~PcieMonitor() { stop(); }
    PcieMonitor(const PcieMonitor&) = delete;
    PcieMonitor& operator=(const PcieMonitor&) = delete;

    bool start() {
        if (receiver_.open() != TIRAY_STATUS_OK) {
            std::cerr << "[WARN] PCIe 监听未启动：请确认 event/c2h 设备节点和 BAR0 自动发现结果。\n";
            return false;
        }
        running_ = true;
        worker_ = std::thread(&PcieMonitor::run, this);
        std::cout << "[PASS] PCIe 帧监听已启动（回调线程）。\n";
        return true;
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

private:
    void run() {
        /* 回调在 SDK 线程；frame->data 返回后失效，保存必须在回调内完成。 */
        receiver_.start(
            [](const tiray_image_frame_t* frame, void* user_data) {
                auto* self = static_cast<PcieMonitor*>(user_data);
                self->on_frame(frame);
            },
            this);
    }

    void on_frame(const tiray_image_frame_t* frame) {
        ++frame_count_;
        std::cout << "[PCIe] frame=" << frame_count_
                  << " id=" << frame->image_id
                  << " type=" << (frame->image_type == 1 ? "模板上传" : "正常图像")
                  << " rows=" << frame->rows
                  << " cols=" << frame->columns
                  << " addr=" << hex32(static_cast<uint32_t>(frame->final_image_address))
                  << " bytes=" << frame->data_length << '\n';
        if (!save_path_.empty() && frame->data != nullptr) {
            std::ofstream out(save_path_, std::ios::binary | std::ios::trunc);
            if (out) {
                out.write(reinterpret_cast<const char*>(frame->data),
                          static_cast<std::streamsize>(frame->data_length));
            } else {
                std::cerr << "[WARN] 无法写入 " << save_path_ << '\n';
            }
        }
    }

    tiray::PcieReceiver receiver_;
    std::string save_path_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frame_count_{0};
};

bool parse_config_item(const std::string& text, tiray_config_item_t& item) {
    const std::size_t eq = text.find('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= text.size()) return false;
    uint32_t id = 0;
    uint32_t value = 0;
    if (!parse_u32(text.substr(0, eq), id) || id > UINT16_MAX) return false;
    if (!parse_u32(text.substr(eq + 1), value)) return false;
    item.item_id = static_cast<uint16_t>(id);
    item.value = value;
    return true;
}

void print_status_line(const tiray_device_status_t& status) {
    std::cout << "work_mode=" << status.work_mode << "(" << work_mode_name(status.work_mode) << ")"
              << " work_state=" << status.work_state << "(" << work_state_name(status.work_state) << ")"
              << " last_error=" << hex32(status.last_error)
              << " capture_id=" << status.capture_id
              << " frame_count=" << status.frame_count << '\n'
              << "output_addr=" << hex32(status.output_addr)
              << " offset_addr=" << hex32(status.offset_addr)
              << " write_state=" << status.write_state
              << " write_end=" << status.write_end
              << " correction_state=" << status.correction_state
              << " correction_end=" << status.correction_end << '\n';
}

void print_dynamic_status(const tiray_dynamic_status_t& status) {
    std::cout << "state=" << status.state << "(" << work_state_name(status.state) << ")"
              << " end=" << status.end
              << " debug_out=" << hex32(status.debug_out)
              << " final_image_addr=" << hex32(status.final_image_addr)
              << " frame_count=" << status.frame_count << '\n';
}

void print_cal_status(const tiray_cal_status_t& status) {
    std::cout << "task_id=" << status.task_id
              << " kind=" << status.task_kind
              << " state=" << status.task_state << "(" << cal_state_name(status.task_state) << ")"
              << " last_error=" << hex32(status.last_error)
              << " progress=" << status.progress_current << "/" << status.progress_total << '\n'
              << "gain_level=" << status.gain_level
              << " level_count=" << status.level_count
              << " levels_ready=" << status.levels_ready
              << " frames_per_level=" << status.frames_per_level
              << " defect_threshold=" << status.defect_threshold
              << " bad_pixel_count=" << status.bad_pixel_count << '\n';
}

void print_static_config(const tiray_static_config_t& config) {
    std::cout << "idle_clean_interval_ms=" << config.idle_clean_interval_ms
              << " exposure_window_ms=" << config.exposure_window_ms
              << " dark_window_ms=" << config.dark_window_ms << '\n';
}

void print_dynamic_config(const tiray_dynamic_config_t& config) {
    std::cout << "cycle=" << config.cycle
              << " image_start_addr=" << hex32(config.image_start_addr)
              << " image_end_addr=" << hex32(config.image_end_addr) << '\n'
              << "start_timeout_ms=" << config.start_timeout_ms
              << " state_poll_interval_ms=" << config.state_poll_interval_ms
              << " stop_timeout_ms=" << config.stop_timeout_ms << '\n';
    for (int i = 0; i < 10; ++i) {
        std::cout << "step[" << i << "] high=" << hex32(config.step_high[i])
                  << " low=" << hex32(config.step_low[i]) << '\n';
    }
}

/* 交互式与一次性模式共用的命令执行；返回 false 表示该条命令失败。 */
class CommandRunner {
public:
    explicit CommandRunner(tiray::Sdk& sdk) : sdk_(sdk) {}

    bool run(const std::vector<std::string>& args) {
        if (args.empty()) return true;
        const std::string& command = args[0];
        if (command == "ping") return check(sdk_.ping(), "ping");
        if (command == "status") return cmd_status();
        if (command == "static") return check(sdk_.start_static(), "start_static_capture");
        if (command == "dynamic_start") return check(sdk_.start_dynamic(), "start_dynamic");
        if (command == "dynamic_query") return cmd_dynamic_query();
        if (command == "dynamic_stop") return check(sdk_.stop_dynamic(), "stop_dynamic");
        if (command == "config_get") return cmd_config_get(args);
        if (command == "config_set") return cmd_config_set(args);
        if (command == "static_cfg_get") return cmd_static_cfg_get();
        if (command == "static_cfg_set") return cmd_static_cfg_set(args);
        if (command == "dynamic_cfg_get") return cmd_dynamic_cfg_get();
        if (command == "dynamic_cfg_set") return cmd_dynamic_cfg_set(args);
        if (command == "offset_begin") return cmd_offset_begin(args);
        if (command == "offset_capture") return check(sdk_.offset_capture(), "cal_offset_capture");
        if (command == "offset_build") return check(sdk_.offset_build(), "cal_offset_build");
        if (command == "offset_cancel") return check(sdk_.offset_cancel(), "cal_offset_cancel");
        if (command == "gain_begin") return cmd_gain_begin(args);
        if (command == "gain_capture") return cmd_gain_capture(args);
        if (command == "gain_build") return check(sdk_.gain_build(), "cal_gain_build");
        if (command == "gain_cancel") return check(sdk_.gain_cancel(), "cal_gain_cancel");
        if (command == "cal_status") return cmd_cal_status();
        if (command == "upload_config") return cmd_upload_config(args);
        if (command == "upload_start") return check(sdk_.upload_start(), "img_upload_start");
        if (command == "upload_query") return cmd_upload_query();
        if (command == "reboot") return check(sdk_.reboot(), "reboot");
        std::cerr << "未知命令: " << command << "（help 查看帮助）\n";
        return false;
    }

private:
    bool cmd_status() {
        tiray_device_status_t status{};
        if (!check(sdk_.status(status), "get_status")) return false;
        print_status_line(status);
        const uint32_t device_error = sdk_.last_device_error();
        if (device_error != 0) std::cout << "最近 ERROR 帧 TLV 0x0002: " << hex32(device_error) << '\n';
        return true;
    }

    bool cmd_dynamic_query() {
        tiray_dynamic_status_t status{};
        if (!check(sdk_.dynamic_status(status), "query_dynamic")) return false;
        print_dynamic_status(status);
        return true;
    }

    bool cmd_config_get(const std::vector<std::string>& args) {
        uint32_t group = 0;
        if (args.size() != 2 || !parse_u32(args[1], group) || group > UINT16_MAX) {
            std::cerr << "用法: config_get GROUP（十六进制或十进制）\n";
            return false;
        }
        std::vector<tiray_config_item_t> items;
        if (!check(sdk_.get_config(static_cast<uint16_t>(group), items), "get_config_group")) {
            std::cerr << "提示：对外版仅允许组 1 和 5；组 2~4 需内部版 SDK。\n";
            return false;
        }
        std::cout << "组 " << hex32(group) << " 共 " << items.size() << " 项:\n";
        for (const auto& item : items) {
            std::cout << "  " << hex32(item.item_id) << " = " << hex32(item.value) << '\n';
        }
        return true;
    }

    bool cmd_config_set(const std::vector<std::string>& args) {
        uint32_t group = 0;
        if (args.size() < 3 || !parse_u32(args[1], group) || group > UINT16_MAX) {
            std::cerr << "用法: config_set GROUP ID=VALUE [ID=VALUE...]\n";
            return false;
        }
        std::vector<tiray_config_item_t> items;
        items.reserve(args.size() - 2);
        for (std::size_t i = 2; i < args.size(); ++i) {
            tiray_config_item_t item{};
            if (!parse_config_item(args[i], item)) {
                std::cerr << "配置项格式错误: " << args[i] << "（应为 ID=VALUE）\n";
                return false;
            }
            items.push_back(item);
        }
        return check(sdk_.set_config(static_cast<uint16_t>(group), items), "set_config_group");
    }

    bool cmd_static_cfg_get() {
        tiray_static_config_t config{};
        if (!check(sdk_.get_static_config(config), "get_static_config")) return false;
        print_static_config(config);
        return true;
    }

    bool cmd_static_cfg_set(const std::vector<std::string>& args) {
        tiray_static_config_t config{};
        if (args.size() != 4 || !parse_u32(args[1], config.idle_clean_interval_ms) ||
            !parse_u32(args[2], config.exposure_window_ms) ||
            !parse_u32(args[3], config.dark_window_ms)) {
            std::cerr << "用法: static_cfg_set IDLE_MS EXPOSURE_MS DARK_MS\n";
            return false;
        }
        return check(sdk_.set_static_config(config), "set_static_config");
    }

    bool cmd_dynamic_cfg_get() {
        tiray_dynamic_config_t config{};
        if (!check(sdk_.get_dynamic_config(config), "get_dynamic_config")) return false;
        print_dynamic_config(config);
        return true;
    }

    bool cmd_dynamic_cfg_set(const std::vector<std::string>& args) {
        tiray_dynamic_config_t config{};
        if (args.size() < 2) {
            std::cerr << "用法: dynamic_cfg_set CYCLE [START_ADDR END_ADDR START_TIMEOUT_MS"
                      << " POLL_MS STOP_TIMEOUT_MS]\n"
                      << "省略的尾部参数沿用设备当前值；任一位置可用 '.' 保留原值；\n"
                      << "step 高/低电平请用 config_set 5 0x21xx=... 逐项设置。\n";
            return false;
        }
        if (!check(sdk_.get_dynamic_config(config), "get_dynamic_config")) return false;
        const std::vector<uint32_t*> fields = {
            &config.cycle, &config.image_start_addr, &config.image_end_addr,
            &config.start_timeout_ms, &config.state_poll_interval_ms, &config.stop_timeout_ms,
        };
        for (std::size_t i = 1; i < args.size() && i - 1 < fields.size(); ++i) {
            if (args[i] == ".") continue;
            if (!parse_u32(args[i], *fields[i - 1])) {
                std::cerr << "参数格式错误: " << args[i] << '\n';
                return false;
            }
        }
        if (config.cycle == 0) {
            std::cerr << "cycle 必须 > 0（按实际采集帧数设置）。\n";
            return false;
        }
        return check(sdk_.set_dynamic_config(config), "set_dynamic_config");
    }

    bool cmd_offset_begin(const std::vector<std::string>& args) {
        uint32_t total = 0;
        uint32_t valid = 0;
        uint32_t mode = 0;
        if (args.size() != 4 || !parse_u32(args[1], total) || !parse_u32(args[2], valid) ||
            !parse_u32(args[3], mode)) {
            std::cerr << "用法: offset_begin TOTAL VALID MODE（MODE 0=静态 1=动态）\n";
            return false;
        }
        if (valid == 0 || valid > total) {
            std::cerr << "需满足 0 < valid_frames <= total_frames。\n";
            return false;
        }
        return check(sdk_.offset_begin(total, valid, static_cast<uint8_t>(mode)), "cal_offset_begin");
    }

    bool cmd_gain_begin(const std::vector<std::string>& args) {
        if (args.size() != 3 && args.size() != 4) {
            std::cerr << "用法: gain_begin L1,L2,... FRAMES_PER_LEVEL [DEFECT_THRESHOLD]\n";
            return false;
        }
        std::vector<uint32_t> levels;
        std::istringstream stream(args[1]);
        std::string level_text;
        while (std::getline(stream, level_text, ',')) {
            uint32_t level = 0;
            if (level_text.empty() || !parse_u32(level_text, level)) {
                std::cerr << "灰度级格式错误: " << args[1] << '\n';
                return false;
            }
            levels.push_back(level);
        }
        if (levels.empty() || levels.size() > 16) {
            std::cerr << "灰度级数量需为 1~16。\n";
            return false;
        }
        uint32_t frames = 0;
        if (!parse_u32(args[2], frames) || frames == 0) {
            std::cerr << "frames_per_level 必须 > 0。\n";
            return false;
        }
        float threshold = 0.3f;
        if (args.size() == 4) {
            errno = 0;
            char* end = nullptr;
            const float parsed = std::strtof(args[3].c_str(), &end);
            if (errno != 0 || end == nullptr || *end != '\0') {
                std::cerr << "defect_threshold 格式错误: " << args[3] << '\n';
                return false;
            }
            threshold = parsed;
        }
        return check(sdk_.gain_begin(levels, frames, threshold), "cal_gain_begin");
    }

    bool cmd_gain_capture(const std::vector<std::string>& args) {
        uint32_t level = 0;
        if (args.size() != 2 || !parse_u32(args[1], level)) {
            std::cerr << "用法: gain_capture LEVEL（须与 gain_begin 的灰度级一致）\n";
            return false;
        }
        return check(sdk_.gain_capture(level), "cal_gain_capture");
    }

    bool cmd_cal_status() {
        tiray_cal_status_t status{};
        if (!check(sdk_.calibration_status(status), "cal_status")) return false;
        print_cal_status(status);
        if (status.task_state == 1) std::cout << "提示：任务运行中，请继续轮询 cal_status。\n";
        return true;
    }

    bool cmd_upload_config(const std::vector<std::string>& args) {
        uint32_t kind = 0;
        uint32_t addr = 0;
        uint32_t rows = 0;
        uint32_t columns = 0;
        uint32_t packages = 0;
        if (args.size() != 6 || !parse_u32(args[1], kind) || !parse_u32(args[2], addr) ||
            !parse_u32(args[3], rows) || !parse_u32(args[4], columns) ||
            !parse_u32(args[5], packages)) {
            std::cerr << "用法: upload_config KIND ADDR ROWS COLS PKGS（KIND 0=暗场 1=亮场）\n";
            return false;
        }
        return check(sdk_.upload_config(kind, addr, rows, columns, packages), "img_upload_config");
    }

    bool cmd_upload_query() {
        tiray_image_upload_status_t status{};
        if (!check(sdk_.upload_status(status), "img_upload_query")) return false;
        std::cout << "state=" << status.state
                  << " end=" << status.end
                  << " debug_out=" << hex32(status.debug_out) << '\n';
        return true;
    }

    tiray::Sdk& sdk_;
};

int run_interactive(tiray::Sdk& sdk) {
    std::cout << "进入交互模式；help 查看命令，quit 退出。\n";
    CommandRunner runner(sdk);
    std::string line;
    while (true) {
        std::cout << "tiray> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) continue;
        if (tokens[0] == "quit" || tokens[0] == "exit") break;
        if (tokens[0] == "help") {
            interactive_help();
            continue;
        }
        runner.run(tokens); /* 单条命令失败不退出交互循环 */
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    uint32_t baudrate = 115200;
    uint32_t timeout_ms = 500;
    std::vector<std::string> command_args;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " 需要一个参数。\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return 0;
        } else if (arg == "--port") {
            options.port = need_value("--port");
        } else if (arg == "--baud") {
            if (!parse_u32(need_value("--baud"), baudrate)) {
                std::cerr << "--baud 需要十进制数值。\n";
                return 2;
            }
        } else if (arg == "--timeout") {
            if (!parse_u32(need_value("--timeout"), timeout_ms) || timeout_ms == 0) {
                std::cerr << "--timeout 需要 >0 的毫秒数。\n";
                return 2;
            }
        } else if (arg == "--internal") {
            options.internal_profile = true;
        } else if (arg == "--pcie") {
            options.pcie = true;
        } else if (arg == "--save") {
            options.pcie = true;
            options.pcie_save_path = need_value("--save");
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "未知选项: " << arg << '\n';
            usage(argv[0]);
            return 2;
        } else {
            command_args.assign(argv + i, argv + argc);
            options.one_shot = true;
            break;
        }
    }

    tiray_sdk_config_t config;
    tiray_sdk_default_config(&config);
    config.rs422_device = options.port.c_str();
    config.rs422_baudrate = baudrate;
    config.retry.response_timeout_ms = timeout_ms;
    if (options.internal_profile) {
        config.profile = TIRAY_SDK_PROFILE_INTERNAL;
        std::cout << "[INFO] 使用内部版配置（组 2~4 可读，需内部版 SDK 构建）。\n";
    }

    tiray::Sdk sdk(&config);
    const tiray_status_t open_status = sdk.open();
    if (open_status != TIRAY_STATUS_OK) {
        std::cerr << "[FAIL] 打开 " << options.port << " 失败: "
                  << tiray_status_string(open_status)
                  << "（请确认串口存在和权限，通常需要 root 或 dialout 组）\n";
        return 1;
    }
    std::cout << "[PASS] 已打开 " << options.port << '\n';

    std::unique_ptr<PcieMonitor> monitor;
    if (options.pcie) {
        monitor = std::make_unique<PcieMonitor>(options.pcie_save_path);
        monitor->start();
    }

    int exit_code = 0;
    if (options.one_shot) {
        /* 一次性回归模式：任何一条命令失败即返回非零。 */
        CommandRunner runner(sdk);
        exit_code = runner.run(command_args) ? 0 : 1;
    } else {
        exit_code = run_interactive(sdk);
    }

    monitor.reset();
    sdk.close();
    return exit_code;
}
