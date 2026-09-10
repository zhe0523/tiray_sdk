#include "tiray_sdk.hpp"

#include <chrono>
#include <cstdint>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct Options {
    std::string port = "/dev/ttyWCH0";
    bool dynamic = false;
    bool offset = false;
    bool gain = false;
    bool upload_query = false;
    uint16_t config_group = 0;
    bool config_read = false;
    std::string save_path;
};

bool check(tiray_status_t status, const char* operation);
bool test_pcie(const Options& options);

void usage(const char* program) {
    std::cout << "用法: " << program << " [选项]\n"
              << "  --port PATH          RS422串口，默认 /dev/ttyWCH0\n"
              << "  PCIe                 程序启动后自动监听，无需命令\n"
              << "  --dynamic            启动Dynamic，查询一次后停止\n"
              << "  --offset             执行2帧静态暗场模板流程\n"
              << "  --gain               执行5000/10000/20000亮场模板流程\n"
              << "  --upload-query       查询模板上传状态\n"
              << "  --config GROUP       读取配置组并打印\n"
              << "  --save PATH          保存PCIe原始图像\n"
              << "  --help               显示帮助\n";
}

void interactive_help() {
    std::cout << "交互命令:\n"
              << "  help                         显示命令\n"
              << "  ping                         测试RS422\n"
              << "  status                       读取设备状态\n"
              << "  static                       手动上图一次\n"
              << "  dynamic_start/query/stop     Dynamic启停和查询\n"
              << "  config_get GROUP             读取配置组\n"
              << "  config_set GROUP ID=VALUE.. 下发配置项并保存\n"
              << "  offset_begin T V MODE        开始暗场模板\n"
              << "  offset_capture/build/cancel  暗场流程操作\n"
              << "  gain_begin LEVELS F T        开始亮场，如 5000,10000,20000 2 0.3\n"
              << "  gain_capture LEVEL           采集亮场灰度级\n"
              << "  gain_build/cancel            生成或取消亮场\n"
              << "  cal_status                   查询模板进度\n"
              << "  upload_query                 查询模板上传\n"
              << "  reboot                       重启设备\n"
              << "  quit                         退出\n";
}

class PcieMonitor {
public:
    explicit PcieMonitor(const std::string& save_path) : save_path_(save_path) {}
    ~PcieMonitor() { stop(); }
    void start() {
        if (receiver_.open() != TIRAY_STATUS_OK) {
            std::cerr << "[WARN] PCIe自动监听未启动，请确认event/c2h设备和BAR0路径\n";
            return;
        }
        running_ = true;
        worker_ = std::thread(&PcieMonitor::run, this);
        std::cout << "[PASS] PCIe自动监听已启动\n";
    }
    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }
private:
    void run() {
        std::vector<uint8_t> image(3072u * 7680u * 2u);
        while (running_) {
            tiray_image_frame_t frame{};
            frame.data = image.data();
            frame.data_capacity = image.size();
            const tiray_status_t status = receiver_.wait_frame(frame);
            if (status == TIRAY_STATUS_TIMEOUT) continue;
            if (status != TIRAY_STATUS_OK) {
                if (running_) std::cerr << "[WARN] PCIe接收失败 status=" << static_cast<int>(status)
                                         << " rows=" << frame.rows << " columns=" << frame.columns
                                         << " buffer_capacity=" << frame.data_capacity << '\n';
                continue;
            }
            std::cout << "[PCIe] image_id=" << frame.image_id
                      << " type=" << frame.image_type
                      << " size=" << frame.rows << 'x' << frame.columns
                      << " bytes=" << frame.data_length << " final_addr=0x"
                      << std::hex << frame.final_image_address << std::dec << '\n';
            if (!save_path_.empty()) {
                std::ofstream output(save_path_, std::ios::binary);
                if (output) output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(frame.data_length));
            }
        }
    }
    tiray::PcieReceiver receiver_;
    std::string save_path_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

bool parse_number(const std::string& text, uint32_t& value) {
    try { size_t end = 0; value = static_cast<uint32_t>(std::stoul(text, &end, 0)); return end == text.size(); }
    catch (...) { return false; }
}

void print_status(tiray::Sdk& sdk) {
    tiray_device_status_t value{};
    if (check(sdk.status(value), "读取设备状态"))
        std::cout << "  work_mode=" << value.work_mode << " work_state=" << value.work_state
                  << " last_error=" << value.last_error << " frame_count=" << value.frame_count << '\n';
}

void print_config(tiray::Sdk& sdk, uint32_t group) {
    std::vector<tiray_config_item_t> items;
    if (check(sdk.get_config(static_cast<uint16_t>(group), items), "读取配置组"))
        for (const auto& item : items)
            std::cout << "  item=0x" << std::hex << item.item_id << " value=0x" << item.value << std::dec << '\n';
}

void interactive_shell(tiray::Sdk& sdk, const Options& options) {
    interactive_help();
    std::string line;
    while (std::cout << "sdk> " && std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command;
        input >> command;
        if (command.empty()) continue;
        if (command == "quit" || command == "exit") break;
        if (command == "help") { interactive_help(); continue; }
        if (command == "ping") { check(sdk.ping(), "PING"); continue; }
        if (command == "status") { print_status(sdk); continue; }
        if (command == "static") { check(sdk.start_static(), "手动上图"); continue; }
        if (command == "dynamic_start") { check(sdk.start_dynamic(), "启动Dynamic"); continue; }
        if (command == "dynamic_stop") { check(sdk.stop_dynamic(), "停止Dynamic"); continue; }
        if (command == "dynamic_query") {
            tiray_dynamic_status_t value{};
            if (check(sdk.dynamic_status(value), "查询Dynamic"))
                std::cout << "  state=" << value.state << " end=" << value.end << " frames=" << value.frame_count << '\n';
            continue;
        }
        if (command == "config_get") {
            uint32_t group = 0; std::string group_text;
            if (!(input >> group_text) || !parse_number(group_text, group)) { std::cout << "参数错误\n"; continue; }
            print_config(sdk, group); continue;
        }
        if (command == "config_set") {
            uint32_t group = 0; std::string group_text;
            if (!(input >> group_text) || !parse_number(group_text, group)) { std::cout << "参数错误\n"; continue; }
            std::vector<tiray_config_item_t> items; std::string pair;
            while (input >> pair) {
                const size_t equal = pair.find('='); uint32_t id = 0, value = 0;
                if (equal == std::string::npos || !parse_number(pair.substr(0, equal), id) || !parse_number(pair.substr(equal + 1), value)) { items.clear(); break; }
                items.push_back({static_cast<uint16_t>(id), value});
            }
            if (items.empty()) { std::cout << "至少需要一个 ID=VALUE\n"; continue; }
            check(sdk.set_config(static_cast<uint16_t>(group), items), "下发配置组"); continue;
        }
        if (command == "offset_begin") {
            uint32_t total = 0, valid = 0, mode = 0; std::string a, b, c;
            if (!(input >> a >> b >> c) || !parse_number(a, total) || !parse_number(b, valid) || !parse_number(c, mode)) { std::cout << "参数错误\n"; continue; }
            check(sdk.offset_begin(total, valid, static_cast<uint8_t>(mode)), "暗场开始"); continue;
        }
        if (command == "offset_capture") { check(sdk.offset_capture(), "暗场采集"); continue; }
        if (command == "offset_build") { check(sdk.offset_build(), "暗场生成"); continue; }
        if (command == "offset_cancel") { check(sdk.offset_cancel(), "取消暗场"); continue; }
        if (command == "gain_begin") {
            std::string levels_text, frames_text, threshold_text;
            if (!(input >> levels_text >> frames_text >> threshold_text)) { std::cout << "参数错误\n"; continue; }
            std::vector<uint32_t> levels; std::stringstream level_stream(levels_text); std::string level_text; uint32_t level;
            while (std::getline(level_stream, level_text, ',')) { if (!parse_number(level_text, level)) { levels.clear(); break; } levels.push_back(level); }
            uint32_t frames = 0; float threshold = 0.0f;
            try { frames = static_cast<uint32_t>(std::stoul(frames_text)); threshold = std::stof(threshold_text); } catch (...) { levels.clear(); }
            if (levels.empty()) { std::cout << "参数错误\n"; continue; }
            check(sdk.gain_begin(levels, frames, threshold), "亮场开始"); continue;
        }
        if (command == "gain_capture") { std::string text; uint32_t level = 0; if (!(input >> text) || !parse_number(text, level)) { std::cout << "参数错误\n"; continue; } check(sdk.gain_capture(level), "亮场采集"); continue; }
        if (command == "gain_build") { check(sdk.gain_build(), "亮场生成"); continue; }
        if (command == "gain_cancel") { check(sdk.gain_cancel(), "取消亮场"); continue; }
        if (command == "cal_status") { tiray_cal_status_t value{}; if (check(sdk.calibration_status(value), "读取模板状态")) std::cout << "  state=" << value.task_state << " progress=" << value.progress_current << '/' << value.progress_total << " error=" << value.last_error << '\n'; continue; }
        if (command == "upload_query") { tiray_image_upload_status_t value{}; if (check(sdk.upload_status(value), "查询模板上传")) std::cout << "  state=" << value.state << " end=" << value.end << " debug_out=" << value.debug_out << '\n'; continue; }
        if (command == "reboot") { check(sdk.reboot(), "重启设备"); continue; }
        std::cout << "未知命令，输入 help 查看\n";
    }
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") { usage(argv[0]); return false; }
        if (arg == "--dynamic") options.dynamic = true;
        else if (arg == "--offset") options.offset = true;
        else if (arg == "--gain") options.gain = true;
        else if (arg == "--upload-query") options.upload_query = true;
        else if (arg == "--port" && i + 1 < argc) options.port = argv[++i];
        else if (arg == "--save" && i + 1 < argc) options.save_path = argv[++i];
        else if (arg == "--config" && i + 1 < argc) {
            options.config_read = true;
            options.config_group = static_cast<uint16_t>(std::stoul(argv[++i], nullptr, 0));
        } else {
            std::cerr << "未知或缺少参数: " << arg << '\n';
            usage(argv[0]); return false;
        }
    }
    return true;
}

bool check(tiray_status_t status, const char* operation) {
    if (status == TIRAY_STATUS_OK) {
        std::cout << "[PASS] " << operation << '\n';
        return true;
    }
    std::cerr << "[FAIL] " << operation << " status=" << static_cast<int>(status) << '\n';
    return false;
}

bool wait_calibration(tiray::Sdk& sdk, const char* name) {
    for (int i = 0; i < 600; ++i) {
        tiray_cal_status_t status{};
        if (!check(sdk.calibration_status(status), "读取模板状态")) return false;
        std::cout << "  " << name << " state=" << status.task_state
                  << " progress=" << status.progress_current << '/' << status.progress_total
                  << " error=" << status.last_error << '\n';
        if (status.task_state == 3u) return true;
        if (status.task_state == 4u || status.task_state == 5u) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cerr << "[FAIL] " << name << " 等待超时\n";
    return false;
}

bool test_pcie(const Options& options) {
    tiray_pcie_config_t config;
    tiray_pcie_default_config(&config);
    tiray::PcieReceiver receiver(&config);
    if (!check(receiver.open(), "打开PCIe接收器")) return false;
    std::vector<uint8_t> image(3072u * 7680u * 2u);
    tiray_image_frame_t frame{};
    frame.data = image.data();
    frame.data_capacity = image.size();
    if (!check(receiver.wait_frame(frame), "接收PCIe图像")) return false;
    std::cout << "  image_id=" << frame.image_id << " type=" << frame.image_type
              << " size=" << frame.rows << 'x' << frame.columns
              << " bytes=" << frame.data_length << " final_addr=0x"
              << std::hex << frame.final_image_address << std::dec << '\n';
    if (!options.save_path.empty()) {
        std::ofstream output(options.save_path, std::ios::binary);
        if (!output) { std::cerr << "[FAIL] 无法创建图像文件\n"; return false; }
        output.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(frame.data_length));
        std::cout << "[PASS] 图像已保存: " << options.save_path << '\n';
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        usage(argv[0]);
        return 0;
    }
    Options options;
    if (!parse_options(argc, argv, options)) return argc > 1 ? 1 : 0;

    tiray_sdk_config_t config;
    tiray_sdk_default_config(&config);
    config.rs422_device = options.port.c_str();
    tiray::Sdk sdk(&config);
    if (!check(sdk.open(), "打开RS422")) return 2;
    if (!check(sdk.ping(), "PING")) return 3;

    PcieMonitor pcie_monitor(options.save_path);
    pcie_monitor.start();

    tiray_device_status_t device_status{};
    if (!check(sdk.status(device_status), "读取设备状态")) return 4;
    std::cout << "  work_mode=" << device_status.work_mode
              << " work_state=" << device_status.work_state
              << " last_error=" << device_status.last_error
              << " frame_count=" << device_status.frame_count << '\n';

    if (!options.dynamic && !options.offset && !options.gain &&
        !options.upload_query && !options.config_read) {
        interactive_shell(sdk, options);
        return 0;
    }

    if (options.config_read) {
        std::vector<tiray_config_item_t> items;
        if (!check(sdk.get_config(options.config_group, items), "读取配置组")) return 5;
        for (const auto& item : items)
            std::cout << "  item=0x" << std::hex << item.item_id << " value=0x" << item.value << std::dec << '\n';
    }

    if (options.dynamic) {
        if (!check(sdk.start_dynamic(), "启动Dynamic")) return 6;
        tiray_dynamic_status_t dynamic_status{};
        check(sdk.dynamic_status(dynamic_status), "查询Dynamic");
        std::cout << "  state=" << dynamic_status.state << " end=" << dynamic_status.end
                  << " frames=" << dynamic_status.frame_count << '\n';
        if (!check(sdk.stop_dynamic(), "停止Dynamic")) return 7;
    }

    if (options.offset) {
        if (!check(sdk.offset_begin(2u, 2u, 0u), "暗场开始")) return 8;
        if (!check(sdk.offset_capture(), "暗场采集")) return 9;
        if (!wait_calibration(sdk, "暗场")) return 10;
        if (!check(sdk.offset_build(), "暗场生成")) return 11;
    }

    if (options.gain) {
        const std::vector<uint32_t> levels{5000u, 10000u, 20000u};
        if (!check(sdk.gain_begin(levels, 2u, 0.3f), "亮场开始")) return 12;
        for (uint32_t level : levels) {
            if (!check(sdk.gain_capture(level), "亮场采集灰度级")) return 13;
            if (!wait_calibration(sdk, "亮场")) return 14;
        }
        if (!check(sdk.gain_build(), "亮场生成")) return 15;
    }

    if (options.upload_query) {
        tiray_image_upload_status_t upload_status{};
        if (!check(sdk.upload_status(upload_status), "查询模板上传")) return 16;
        std::cout << "  state=" << upload_status.state << " end=" << upload_status.end
                  << " debug_out=" << upload_status.debug_out << '\n';
    }

    std::cout << "测试完成\n";
    return 0;
}
