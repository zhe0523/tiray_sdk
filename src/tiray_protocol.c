#include "tiray_protocol.h"

#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <termios.h>
#include <unistd.h>
#endif

struct tiray_sdk {
    tiray_sdk_config_t config;
    uint32_t next_sequence;
#ifndef _WIN32
    int fd;
    pthread_mutex_t mutex;
#endif
};

static uint16_t read_le16(const uint8_t* p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8u);
}

static uint32_t read_le32(const uint8_t* p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8u)
        | ((uint32_t)p[2] << 16u)
        | ((uint32_t)p[3] << 24u);
}

static void write_le16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static void write_le32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

uint16_t tiray_protocol_crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xffffu;
    if (data == NULL && length != 0u) return 0u;
    for (size_t i = 0u; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8u;
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u
                ? (uint16_t)((crc << 1u) ^ 0x1021u)
                : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

tiray_status_t tiray_protocol_encode(const tiray_frame_t* frame,
                                      uint8_t* output,
                                      size_t output_capacity,
                                      size_t* output_length) {
    if (frame == NULL || output == NULL || output_length == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
    if (frame->version != TIRAY_PROTOCOL_VERSION || frame->header_size != TIRAY_PROTOCOL_HEADER_SIZE ||
        frame->payload_length > TIRAY_PROTOCOL_MAX_PAYLOAD ||
        (frame->payload == NULL && frame->payload_length != 0u)) return TIRAY_STATUS_PROTOCOL_ERROR;
    const size_t total = TIRAY_PROTOCOL_HEADER_SIZE + frame->payload_length + 2u;
    if (output_capacity < total) return TIRAY_STATUS_INVALID_ARGUMENT;
    write_le16(output, TIRAY_PROTOCOL_MAGIC);
    output[2] = frame->version;
    output[3] = frame->header_size;
    output[4] = frame->message_type;
    output[5] = frame->flags;
    write_le16(output + 6u, frame->command);
    write_le32(output + 8u, frame->sequence);
    write_le16(output + 12u, frame->payload_length);
    write_le16(output + 14u, 0u);
    if (frame->payload_length != 0u) memcpy(output + TIRAY_PROTOCOL_HEADER_SIZE, frame->payload, frame->payload_length);
    write_le16(output + total - 2u, tiray_protocol_crc16(output, total - 2u));
    *output_length = total;
    return TIRAY_STATUS_OK;
}

tiray_status_t tiray_protocol_decode(const uint8_t* input,
                                      size_t input_length,
                                      tiray_frame_t* frame) {
    if (input == NULL || frame == NULL || input_length < TIRAY_PROTOCOL_HEADER_SIZE + 2u) return TIRAY_STATUS_INVALID_ARGUMENT;
    if (read_le16(input) != TIRAY_PROTOCOL_MAGIC || input[2] != TIRAY_PROTOCOL_VERSION || input[3] != TIRAY_PROTOCOL_HEADER_SIZE) return TIRAY_STATUS_PROTOCOL_ERROR;
    const uint16_t payload_length = read_le16(input + 12u);
    const size_t total = TIRAY_PROTOCOL_HEADER_SIZE + payload_length + 2u;
    if (payload_length > TIRAY_PROTOCOL_MAX_PAYLOAD || total != input_length || read_le16(input + 14u) != 0u) return TIRAY_STATUS_PROTOCOL_ERROR;
    if (read_le16(input + total - 2u) != tiray_protocol_crc16(input, total - 2u)) return TIRAY_STATUS_CRC_ERROR;
    frame->version = input[2];
    frame->header_size = input[3];
    frame->message_type = input[4];
    frame->flags = input[5];
    frame->command = read_le16(input + 6u);
    frame->sequence = read_le32(input + 8u);
    frame->payload = input + TIRAY_PROTOCOL_HEADER_SIZE;
    frame->payload_length = payload_length;
    return TIRAY_STATUS_OK;
}

void tiray_sdk_default_config(tiray_sdk_config_t* config) {
    if (config == NULL) return;
    config->rs422_device = NULL;
    config->rs422_baudrate = 115200u;
    config->retry.response_timeout_ms = 500u;
    config->retry.max_retries = 2u;
    config->profile = TIRAY_SDK_PROFILE_EXTERNAL;
}

const char* tiray_status_string(tiray_status_t status) {
    switch (status) {
        case TIRAY_STATUS_OK: return "成功";
        case TIRAY_STATUS_INVALID_ARGUMENT: return "参数或缓冲区错误";
        case TIRAY_STATUS_NOT_OPEN: return "设备未打开";
        case TIRAY_STATUS_BUSY: return "设备忙";
        case TIRAY_STATUS_TIMEOUT: return "响应超时";
        case TIRAY_STATUS_PROTOCOL_ERROR: return "协议或设备元数据错误";
        case TIRAY_STATUS_CRC_ERROR: return "CRC校验失败";
        case TIRAY_STATUS_IO_ERROR: return "串口或PCIe读写失败";
        case TIRAY_STATUS_DEVICE_ERROR: return "下位机拒绝执行";
        case TIRAY_STATUS_INTERNAL_ERROR: return "SDK内部错误";
        default: return "未知状态";
    }
}

static tiray_status_t validate_config(const tiray_sdk_config_t* config) {
    if (config == NULL || config->rs422_baudrate == 0u ||
        config->retry.response_timeout_ms == 0u || config->retry.max_retries > 10u ||
        (config->profile != TIRAY_SDK_PROFILE_EXTERNAL && config->profile != TIRAY_SDK_PROFILE_INTERNAL)) {
        return TIRAY_STATUS_INVALID_ARGUMENT;
    }
#ifndef TIRAY_SDK_INTERNAL_BUILD
    if (config->profile == TIRAY_SDK_PROFILE_INTERNAL) return TIRAY_STATUS_INVALID_ARGUMENT;
#endif
    return TIRAY_STATUS_OK;
}

tiray_sdk_t* tiray_sdk_create(const tiray_sdk_config_t* config) {
    tiray_sdk_config_t defaults;
    tiray_sdk_default_config(&defaults);
    if (config != NULL) defaults = *config;
    if (validate_config(&defaults) != TIRAY_STATUS_OK) return NULL;
    tiray_sdk_t* sdk = (tiray_sdk_t*)calloc(1u, sizeof(*sdk));
    if (sdk == NULL) return NULL;
    sdk->config = defaults;
    sdk->next_sequence = 1u;
#ifndef _WIN32
    sdk->fd = -1;
    if (pthread_mutex_init(&sdk->mutex, NULL) != 0) {
        free(sdk);
        return NULL;
    }
#endif
    return sdk;
}

void tiray_sdk_destroy(tiray_sdk_t* sdk) {
    if (sdk == NULL) return;
    tiray_sdk_close(sdk);
#ifndef _WIN32
    pthread_mutex_destroy(&sdk->mutex);
#endif
    free(sdk);
}

#ifndef _WIN32
static speed_t baud_to_termios(uint32_t baudrate) {
    switch (baudrate) {
        case 1200u: return B1200;
        case 2400u: return B2400;
        case 4800u: return B4800;
        case 9600u: return B9600;
        case 19200u: return B19200;
        case 38400u: return B38400;
        case 57600u: return B57600;
        case 115200u: return B115200;
#ifdef B230400
        case 230400u: return B230400;
#endif
        default: return (speed_t)0;
    }
}
#endif

tiray_status_t tiray_sdk_open(tiray_sdk_t* sdk) {
    if (sdk == NULL || sdk->config.rs422_device == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
#ifdef _WIN32
    return TIRAY_STATUS_IO_ERROR;
#else
    pthread_mutex_lock(&sdk->mutex);
    if (sdk->fd >= 0) {
        pthread_mutex_unlock(&sdk->mutex);
        return TIRAY_STATUS_OK;
    }
    const speed_t speed = baud_to_termios(sdk->config.rs422_baudrate);
    if (speed == 0) {
        pthread_mutex_unlock(&sdk->mutex);
        return TIRAY_STATUS_INVALID_ARGUMENT;
    }
    sdk->fd = open(sdk->config.rs422_device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (sdk->fd < 0) {
        pthread_mutex_unlock(&sdk->mutex);
        return TIRAY_STATUS_IO_ERROR;
    }
    struct termios tio;
    if (tcgetattr(sdk->fd, &tio) != 0) {
        close(sdk->fd); sdk->fd = -1; pthread_mutex_unlock(&sdk->mutex); return TIRAY_STATUS_IO_ERROR;
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
    if (tcsetattr(sdk->fd, TCSANOW, &tio) != 0) {
        close(sdk->fd); sdk->fd = -1; pthread_mutex_unlock(&sdk->mutex); return TIRAY_STATUS_IO_ERROR;
    }
    pthread_mutex_unlock(&sdk->mutex);
    return TIRAY_STATUS_OK;
#endif
}

void tiray_sdk_close(tiray_sdk_t* sdk) {
    if (sdk == NULL) return;
#ifndef _WIN32
    pthread_mutex_lock(&sdk->mutex);
    if (sdk->fd >= 0) close(sdk->fd);
    sdk->fd = -1;
    pthread_mutex_unlock(&sdk->mutex);
#endif
}

int tiray_sdk_is_open(const tiray_sdk_t* sdk) {
#ifdef _WIN32
    (void)sdk; return 0;
#else
    return sdk != NULL && sdk->fd >= 0;
#endif
}

void tiray_response_reset(tiray_response_t* response) {
    if (response == NULL) return;
    response->message_type = 0u;
    response->command = 0u;
    response->sequence = 0u;
    response->payload_length = 0u;
    response->device_error = 0u;
}

#ifndef _WIN32
static tiray_status_t write_all(int fd, const uint8_t* data, size_t length) {
    size_t offset = 0u;
    while (offset < length) {
        const ssize_t written = write(fd, data + offset, length - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return TIRAY_STATUS_IO_ERROR;
        offset += (size_t)written;
    }
    return TIRAY_STATUS_OK;
}

static tiray_status_t read_response(int fd, uint32_t timeout_ms, uint32_t sequence,
                                    uint16_t command, tiray_response_t* response) {
    uint8_t buffer[TIRAY_PROTOCOL_MAX_FRAME];
    size_t length = 0u;
    for (;;) {
        struct pollfd pfd = {fd, POLLIN, 0};
        const int ready = poll(&pfd, 1, (int)timeout_ms);
        if (ready == 0) return TIRAY_STATUS_TIMEOUT;
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return TIRAY_STATUS_IO_ERROR;
        const ssize_t got = read(fd, buffer + length, sizeof(buffer) - length);
        if (got <= 0) return TIRAY_STATUS_IO_ERROR;
        length += (size_t)got;
        size_t start = 0u;
        while (length - start >= TIRAY_PROTOCOL_HEADER_SIZE + 2u) {
            while (start + 1u < length && !(buffer[start] == 0xAAu && buffer[start + 1u] == 0x55u)) ++start;
            if (length - start < TIRAY_PROTOCOL_HEADER_SIZE + 2u) break;
            const uint16_t payload_length = read_le16(buffer + start + 12u);
            const size_t frame_length = TIRAY_PROTOCOL_HEADER_SIZE + payload_length + 2u;
            if (payload_length > TIRAY_PROTOCOL_MAX_PAYLOAD || frame_length > sizeof(buffer)) { ++start; continue; }
            if (length - start < frame_length) break;
            tiray_frame_t frame;
            const tiray_status_t status = tiray_protocol_decode(buffer + start, frame_length, &frame);
            if (status != TIRAY_STATUS_OK) { ++start; continue; }
            if (frame.sequence != sequence || frame.command != command) { start += frame_length; continue; }
            response->message_type = frame.message_type;
            response->command = frame.command;
            response->sequence = frame.sequence;
            response->payload_length = frame.payload_length;
            if (response->payload != NULL && response->payload_capacity >= frame.payload_length)
                memcpy(response->payload, frame.payload, frame.payload_length);
            else if (frame.payload_length != 0u)
                return TIRAY_STATUS_INVALID_ARGUMENT;
            if (frame.message_type == TIRAY_MSG_ERROR) return TIRAY_STATUS_DEVICE_ERROR;
            return TIRAY_STATUS_OK;
        }
        if (start != 0u) { memmove(buffer, buffer + start, length - start); length -= start; }
        if (length == sizeof(buffer)) return TIRAY_STATUS_PROTOCOL_ERROR;
    }
}
#endif

tiray_status_t tiray_sdk_request(tiray_sdk_t* sdk,
                                  uint16_t command,
                                  const uint8_t* payload,
                                  size_t payload_length,
                                  tiray_response_t* response) {
    if (sdk == NULL || response == NULL || payload_length > TIRAY_PROTOCOL_MAX_PAYLOAD ||
        (payload == NULL && payload_length != 0u)) return TIRAY_STATUS_INVALID_ARGUMENT;
    tiray_response_reset(response);
#ifdef _WIN32
    (void)command; (void)payload; (void)payload_length;
    return TIRAY_STATUS_IO_ERROR;
#else
    pthread_mutex_lock(&sdk->mutex);
    if (sdk->fd < 0) { pthread_mutex_unlock(&sdk->mutex); return TIRAY_STATUS_NOT_OPEN; }
    tiray_frame_t frame = {TIRAY_PROTOCOL_VERSION, TIRAY_PROTOCOL_HEADER_SIZE, TIRAY_MSG_REQUEST, 0u,
                           command, sdk->next_sequence, payload, (uint16_t)payload_length};
    uint8_t encoded[TIRAY_PROTOCOL_MAX_FRAME];
    size_t encoded_length = 0u;
    tiray_status_t status = tiray_protocol_encode(&frame, encoded, sizeof(encoded), &encoded_length);
    if (status != TIRAY_STATUS_OK) { pthread_mutex_unlock(&sdk->mutex); return status; }
    const uint32_t sequence = frame.sequence;
    sdk->next_sequence = sdk->next_sequence == UINT32_MAX ? 1u : sdk->next_sequence + 1u;
    for (uint32_t attempt = 0u; attempt <= sdk->config.retry.max_retries; ++attempt) {
        status = write_all(sdk->fd, encoded, encoded_length);
        if (status != TIRAY_STATUS_OK) break;
        status = read_response(sdk->fd, sdk->config.retry.response_timeout_ms, sequence, command, response);
        if (status != TIRAY_STATUS_TIMEOUT) break;
    }
    pthread_mutex_unlock(&sdk->mutex);
    return status;
#endif
}

static void append_le16_local(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
}

static void append_le32_local(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
    data[2] = (uint8_t)(value >> 16u);
    data[3] = (uint8_t)(value >> 24u);
}

static size_t append_tlv_u32_local(uint8_t* payload, size_t pos, uint16_t type, uint32_t value) {
    append_le16_local(payload + pos, type);
    append_le16_local(payload + pos + 2u, 4u);
    append_le32_local(payload + pos + 4u, value);
    return pos + 8u;
}

static tiray_status_t command_with_payload(tiray_sdk_t* sdk, uint16_t command,
                                           const uint8_t* payload, size_t payload_length) {
    uint8_t response_payload[TIRAY_PROTOCOL_MAX_PAYLOAD];
    tiray_response_t response = {0u, 0u, 0u, response_payload, sizeof(response_payload), 0u, 0u};
    const tiray_status_t status = tiray_sdk_request(sdk, command, payload, payload_length, &response);
    if (status != TIRAY_STATUS_OK) return status;
    return response.message_type == TIRAY_MSG_DONE ? TIRAY_STATUS_OK : TIRAY_STATUS_PROTOCOL_ERROR;
}

static int find_tlv_u32(const uint8_t* payload, size_t length, uint16_t type, uint32_t* value) {
    size_t pos = 0u;
    while (pos + 4u <= length) {
        const uint16_t current_type = read_le16(payload + pos);
        const uint16_t item_length = read_le16(payload + pos + 2u);
        pos += 4u;
        if (pos + item_length > length) return -1;
        if (current_type == type) {
            if (item_length != 4u) return -1;
            *value = read_le32(payload + pos);
            return 1;
        }
        pos += item_length;
    }
    return pos == length ? 0 : -1;
}

static tiray_status_t simple_command(tiray_sdk_t* sdk, uint16_t command) {
    uint8_t response_payload[TIRAY_PROTOCOL_MAX_PAYLOAD];
    tiray_response_t response = {0u, 0u, 0u, response_payload, sizeof(response_payload), 0u, 0u};
    const tiray_status_t status = tiray_sdk_request(sdk, command, NULL, 0u, &response);
    if (status != TIRAY_STATUS_OK) return status;
    return response.message_type == TIRAY_MSG_DONE ? TIRAY_STATUS_OK : TIRAY_STATUS_PROTOCOL_ERROR;
}

tiray_status_t tiray_ping(tiray_sdk_t* sdk) { return simple_command(sdk, TIRAY_CMD_PING); }
tiray_status_t tiray_reboot(tiray_sdk_t* sdk) { return simple_command(sdk, TIRAY_CMD_REBOOT); }
tiray_status_t tiray_start_static_capture(tiray_sdk_t* sdk) { return simple_command(sdk, TIRAY_CMD_START_STATIC_CAPTURE); }
tiray_status_t tiray_start_dynamic(tiray_sdk_t* sdk) { return simple_command(sdk, TIRAY_CMD_START_DYNAMIC); }
tiray_status_t tiray_stop_dynamic(tiray_sdk_t* sdk) { return simple_command(sdk, TIRAY_CMD_STOP_DYNAMIC); }

tiray_status_t tiray_get_status(tiray_sdk_t* sdk, tiray_device_status_t* status) {
    if (status == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
    uint8_t response_payload[TIRAY_PROTOCOL_MAX_PAYLOAD];
    tiray_response_t response = {0u, 0u, 0u, response_payload, sizeof(response_payload), 0u, 0u};
    tiray_status_t result = tiray_sdk_request(sdk, TIRAY_CMD_STATUS, NULL, 0u, &response);
    if (result != TIRAY_STATUS_OK) return result;
    if (response.message_type != TIRAY_MSG_DONE) return TIRAY_STATUS_PROTOCOL_ERROR;
    memset(status, 0, sizeof(*status));
    const uint16_t ids[] = {0x0200u, 0x0201u, 0x0202u, 0x0204u, 0x0205u, 0x0206u,
                            0x0207u, 0x0209u, 0x020Au, 0x020Bu, 0x020Cu};
    uint32_t* values[] = {&status->work_mode, &status->work_state, &status->last_error,
                          &status->capture_id, &status->frame_count, &status->output_addr,
                          &status->offset_addr, &status->write_state, &status->write_end,
                          &status->correction_state, &status->correction_end};
    for (size_t i = 0u; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        const int found = find_tlv_u32(response_payload, response.payload_length, ids[i], values[i]);
        if (found < 0) return TIRAY_STATUS_PROTOCOL_ERROR;
    }
    return TIRAY_STATUS_OK;
}

tiray_status_t tiray_query_dynamic(tiray_sdk_t* sdk, tiray_dynamic_status_t* status) {
    if (status == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
    uint8_t response_payload[TIRAY_PROTOCOL_MAX_PAYLOAD];
    tiray_response_t response = {0u, 0u, 0u, response_payload, sizeof(response_payload), 0u, 0u};
    tiray_status_t result = tiray_sdk_request(sdk, TIRAY_CMD_QUERY_DYNAMIC, NULL, 0u, &response);
    if (result != TIRAY_STATUS_OK) return result;
    if (response.message_type != TIRAY_MSG_DONE) return TIRAY_STATUS_PROTOCOL_ERROR;
    memset(status, 0, sizeof(*status));
    const uint16_t ids[] = {0x2003u, 0x2007u, 0x2008u, 0x2004u, 0x2005u};
    uint32_t* values[] = {&status->state, &status->end, &status->debug_out,
                          &status->final_image_addr, &status->frame_count};
    for (size_t i = 0u; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        const int found = find_tlv_u32(response_payload, response.payload_length, ids[i], values[i]);
        if (found < 0) return TIRAY_STATUS_PROTOCOL_ERROR;
    }
    return TIRAY_STATUS_OK;
}

tiray_status_t tiray_get_config_group(tiray_sdk_t* sdk, uint16_t group_id,
                                       tiray_config_item_t* items, size_t item_capacity,
                                       size_t* item_count) {
    if (sdk == NULL || item_count == NULL || (items == NULL && item_capacity != 0u)) return TIRAY_STATUS_INVALID_ARGUMENT;
    if (sdk->config.profile == TIRAY_SDK_PROFILE_EXTERNAL && group_id != 1u && group_id != 5u)
        return TIRAY_STATUS_DEVICE_ERROR;
    uint8_t request_payload[2];
    append_le16_local(request_payload, group_id);
    uint8_t response_payload[TIRAY_PROTOCOL_MAX_PAYLOAD];
    tiray_response_t response = {0u, 0u, 0u, response_payload, sizeof(response_payload), 0u, 0u};
    tiray_status_t result = tiray_sdk_request(sdk, TIRAY_CMD_GET_CONFIG_GROUP, request_payload, sizeof(request_payload), &response);
    if (result != TIRAY_STATUS_OK) return result;
    if (response.message_type != TIRAY_MSG_DONE) return TIRAY_STATUS_PROTOCOL_ERROR;
    size_t count = 0u;
    size_t pos = 0u;
    while (pos + 8u <= response.payload_length) {
        const uint16_t item_id = read_le16(response_payload + pos);
        const uint16_t length = read_le16(response_payload + pos + 2u);
        if (length != 4u) return TIRAY_STATUS_PROTOCOL_ERROR;
        if (count >= item_capacity) return TIRAY_STATUS_INVALID_ARGUMENT;
        items[count].item_id = item_id;
        items[count].value = read_le32(response_payload + pos + 4u);
        ++count;
        pos += 8u;
    }
    if (pos != response.payload_length) return TIRAY_STATUS_PROTOCOL_ERROR;
    *item_count = count;
    return TIRAY_STATUS_OK;
}

tiray_status_t tiray_set_config_group(tiray_sdk_t* sdk, uint16_t group_id,
                                       const tiray_config_item_t* items, size_t item_count) {
    if (sdk == NULL || items == NULL || item_count == 0u || item_count > 255u) return TIRAY_STATUS_INVALID_ARGUMENT;
    if (sdk->config.profile == TIRAY_SDK_PROFILE_EXTERNAL && group_id != 1u && group_id != 5u)
        return TIRAY_STATUS_DEVICE_ERROR;
    uint8_t payload[TIRAY_PROTOCOL_MAX_PAYLOAD];
    if (2u + item_count * 8u > sizeof(payload)) return TIRAY_STATUS_INVALID_ARGUMENT;
    append_le16_local(payload, group_id);
    for (size_t i = 0u; i < item_count; ++i) {
        append_le16_local(payload + 2u + i * 8u, items[i].item_id);
        append_le16_local(payload + 4u + i * 8u, 4u);
        append_le32_local(payload + 6u + i * 8u, items[i].value);
    }
    uint8_t response_payload[TIRAY_PROTOCOL_MAX_PAYLOAD];
    tiray_response_t response = {0u, 0u, 0u, response_payload, sizeof(response_payload), 0u, 0u};
    tiray_status_t result = tiray_sdk_request(sdk, TIRAY_CMD_SET_CONFIG_GROUP, payload, 2u + item_count * 8u, &response);
    if (result != TIRAY_STATUS_OK) return result;
    return response.message_type == TIRAY_MSG_DONE ? TIRAY_STATUS_OK : TIRAY_STATUS_PROTOCOL_ERROR;
}

static uint32_t config_item_value(const tiray_config_item_t* items, size_t count, uint16_t id, int* found) {
    for (size_t i = 0u; i < count; ++i) if (items[i].item_id == id) { *found = 1; return items[i].value; }
    *found = 0; return 0u;
}

tiray_status_t tiray_get_static_config(tiray_sdk_t* sdk, tiray_static_config_t* config) {
    if (config == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
    tiray_config_item_t items[8]; size_t count = 0u;
    tiray_status_t result = tiray_get_config_group(sdk, 1u, items, 8u, &count);
    if (result != TIRAY_STATUS_OK) return result;
    int found = 0; config->idle_clean_interval_ms = config_item_value(items, count, 0x1000u, &found);
    config->exposure_window_ms = config_item_value(items, count, 0x1001u, &found);
    config->dark_window_ms = config_item_value(items, count, 0x1002u, &found);
    return TIRAY_STATUS_OK;
}

tiray_status_t tiray_set_static_config(tiray_sdk_t* sdk, const tiray_static_config_t* config) {
    if (config == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
    const tiray_config_item_t items[] = {{0x1000u, config->idle_clean_interval_ms},
                                         {0x1001u, config->exposure_window_ms},
                                         {0x1002u, config->dark_window_ms}};
    return tiray_set_config_group(sdk, 1u, items, 3u);
}

tiray_status_t tiray_get_dynamic_config(tiray_sdk_t* sdk, tiray_dynamic_config_t* config) {
    if (config == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
    tiray_config_item_t items[32]; size_t count = 0u;
    tiray_status_t result = tiray_get_config_group(sdk, 5u, items, 32u, &count);
    if (result != TIRAY_STATUS_OK) return result;
    memset(config, 0, sizeof(*config));
    int found = 0;
    config->cycle = config_item_value(items, count, 0x0600u, &found);
    config->image_start_addr = config_item_value(items, count, 0x0601u, &found);
    config->image_end_addr = config_item_value(items, count, 0x0602u, &found);
    config->start_timeout_ms = config_item_value(items, count, 0x0603u, &found);
    config->state_poll_interval_ms = config_item_value(items, count, 0x0604u, &found);
    config->stop_timeout_ms = config_item_value(items, count, 0x0605u, &found);
    for (uint32_t i = 0u; i < 10u; ++i) {
        config->step_high[i] = config_item_value(items, count, (uint16_t)(0x2100u + i * 0x10u), &found);
        config->step_low[i] = config_item_value(items, count, (uint16_t)(0x2101u + i * 0x10u), &found);
    }
    return TIRAY_STATUS_OK;
}

tiray_status_t tiray_set_dynamic_config(tiray_sdk_t* sdk, const tiray_dynamic_config_t* config) {
    if (config == NULL || config->cycle == 0u || config->image_start_addr > config->image_end_addr)
        return TIRAY_STATUS_INVALID_ARGUMENT;
    tiray_config_item_t items[26]; size_t count = 0u;
    items[count++] = (tiray_config_item_t){0x0600u, config->cycle};
    items[count++] = (tiray_config_item_t){0x0601u, config->image_start_addr};
    items[count++] = (tiray_config_item_t){0x0602u, config->image_end_addr};
    items[count++] = (tiray_config_item_t){0x0603u, config->start_timeout_ms};
    items[count++] = (tiray_config_item_t){0x0604u, config->state_poll_interval_ms};
    items[count++] = (tiray_config_item_t){0x0605u, config->stop_timeout_ms};
    for (uint32_t i = 0u; i < 10u; ++i) {
        items[count++] = (tiray_config_item_t){(uint16_t)(0x2100u + i * 0x10u), config->step_high[i]};
        items[count++] = (tiray_config_item_t){(uint16_t)(0x2101u + i * 0x10u), config->step_low[i]};
    }
    return tiray_set_config_group(sdk, 5u, items, count);
}

tiray_status_t tiray_cal_offset_begin(tiray_sdk_t* sdk, uint32_t total_frames,
                                      uint32_t valid_frames, uint8_t mode) {
    if (total_frames == 0u || valid_frames == 0u || valid_frames > total_frames || mode > 1u)
        return TIRAY_STATUS_INVALID_ARGUMENT;
    uint8_t payload[24]; size_t pos = 0u;
    pos = append_tlv_u32_local(payload, pos, 0x3000u, total_frames);
    pos = append_tlv_u32_local(payload, pos, 0x3001u, valid_frames);
    pos = append_tlv_u32_local(payload, pos, 0x3002u, mode);
    return command_with_payload(sdk, TIRAY_CMD_CAL_OFFSET_BEGIN, payload, pos);
}

tiray_status_t tiray_cal_offset_capture(tiray_sdk_t* sdk) {
    return command_with_payload(sdk, TIRAY_CMD_CAL_OFFSET_CAPTURE, NULL, 0u);
}
tiray_status_t tiray_cal_offset_build(tiray_sdk_t* sdk) {
    return command_with_payload(sdk, TIRAY_CMD_CAL_OFFSET_BUILD, NULL, 0u);
}
tiray_status_t tiray_cal_offset_cancel(tiray_sdk_t* sdk) {
    return command_with_payload(sdk, TIRAY_CMD_CAL_OFFSET_CANCEL, NULL, 0u);
}

tiray_status_t tiray_cal_gain_begin(tiray_sdk_t* sdk, const uint32_t* levels,
                                    size_t level_count, uint32_t frames_per_level,
                                    float defect_threshold) {
    if (levels == NULL || level_count == 0u || level_count > 16u || frames_per_level == 0u)
        return TIRAY_STATUS_INVALID_ARGUMENT;
    uint8_t payload[TIRAY_PROTOCOL_MAX_PAYLOAD]; size_t pos = 0u;
    uint32_t threshold_bits = 0u; memcpy(&threshold_bits, &defect_threshold, sizeof(threshold_bits));
    pos = append_tlv_u32_local(payload, pos, 0x3100u, (uint32_t)level_count);
    pos = append_tlv_u32_local(payload, pos, 0x3101u, frames_per_level);
    pos = append_tlv_u32_local(payload, pos, 0x3102u, threshold_bits);
    append_le16_local(payload + pos, 0x3110u); append_le16_local(payload + pos + 2u, (uint16_t)(level_count * 4u));
    pos += 4u;
    for (size_t i = 0u; i < level_count; ++i) { append_le32_local(payload + pos, levels[i]); pos += 4u; }
    return command_with_payload(sdk, TIRAY_CMD_CAL_GAIN_BEGIN, payload, pos);
}

tiray_status_t tiray_cal_gain_capture(tiray_sdk_t* sdk, uint32_t level) {
    uint8_t payload[8]; const size_t pos = append_tlv_u32_local(payload, 0u, 0x3120u, level);
    return command_with_payload(sdk, TIRAY_CMD_CAL_GAIN_CAPTURE, payload, pos);
}
tiray_status_t tiray_cal_gain_build(tiray_sdk_t* sdk) { return command_with_payload(sdk, TIRAY_CMD_CAL_GAIN_BUILD, NULL, 0u); }
tiray_status_t tiray_cal_gain_cancel(tiray_sdk_t* sdk) { return command_with_payload(sdk, TIRAY_CMD_CAL_GAIN_CANCEL, NULL, 0u); }

tiray_status_t tiray_cal_status(tiray_sdk_t* sdk, tiray_cal_status_t* status) {
    if (status == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
    uint8_t data[TIRAY_PROTOCOL_MAX_PAYLOAD];
    tiray_response_t response = {0u, 0u, 0u, data, sizeof(data), 0u, 0u};
    tiray_status_t result = tiray_sdk_request(sdk, TIRAY_CMD_CAL_STATUS, NULL, 0u, &response);
    if (result != TIRAY_STATUS_OK) return result;
    if (response.message_type != TIRAY_MSG_DONE) return TIRAY_STATUS_PROTOCOL_ERROR;
    memset(status, 0, sizeof(*status));
    uint32_t* values[] = {&status->task_id, &status->task_kind, &status->task_state, &status->last_error,
                          &status->progress_current, &status->progress_total, &status->gain_level,
                          &status->level_count, &status->levels_ready, &status->frames_per_level,
                          NULL, &status->bad_pixel_count};
    const uint16_t ids[] = {0x3200u,0x3201u,0x3202u,0x3203u,0x3204u,0x3205u,0x3206u,
                            0x3211u,0x3212u,0x3213u,0x3214u,0x3215u};
    for (size_t i = 0u; i < 12u; ++i) {
        uint32_t value = 0u; const int found = find_tlv_u32(data, response.payload_length, ids[i], &value);
        if (found < 0) return TIRAY_STATUS_PROTOCOL_ERROR;
        if (found > 0) { if (values[i] != NULL) *values[i] = value; else memcpy(&status->defect_threshold, &value, sizeof(value)); }
    }
    return TIRAY_STATUS_OK;
}

tiray_status_t tiray_img_upload_config(tiray_sdk_t* sdk, uint32_t template_kind,
                                       uint32_t image_addr, uint32_t rows, uint32_t columns,
                                       uint32_t package_count) {
    uint8_t payload[40]; size_t pos = 0u;
    pos = append_tlv_u32_local(payload, pos, 0x5000u, template_kind);
    pos = append_tlv_u32_local(payload, pos, 0x5001u, image_addr);
    pos = append_tlv_u32_local(payload, pos, 0x5002u, rows);
    pos = append_tlv_u32_local(payload, pos, 0x5003u, columns);
    pos = append_tlv_u32_local(payload, pos, 0x5004u, package_count);
    return command_with_payload(sdk, TIRAY_CMD_IMG_UPLOAD_CONFIG, payload, pos);
}
tiray_status_t tiray_img_upload_start(tiray_sdk_t* sdk) { return command_with_payload(sdk, TIRAY_CMD_IMG_UPLOAD_START, NULL, 0u); }

tiray_status_t tiray_img_upload_query(tiray_sdk_t* sdk, tiray_image_upload_status_t* status) {
    if (status == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
    uint8_t data[TIRAY_PROTOCOL_MAX_PAYLOAD];
    tiray_response_t response = {0u, 0u, 0u, data, sizeof(data), 0u, 0u};
    tiray_status_t result = tiray_sdk_request(sdk, TIRAY_CMD_IMG_UPLOAD_QUERY, NULL, 0u, &response);
    if (result != TIRAY_STATUS_OK) return result;
    if (response.message_type != TIRAY_MSG_DONE) return TIRAY_STATUS_PROTOCOL_ERROR;
    memset(status, 0, sizeof(*status));
    (void)find_tlv_u32(data, response.payload_length, 0x5005u, &status->state);
    (void)find_tlv_u32(data, response.payload_length, 0x5006u, &status->end);
    (void)find_tlv_u32(data, response.payload_length, 0x5007u, &status->debug_out);
    return TIRAY_STATUS_OK;
}
