#ifndef TIRAY_PROTOCOL_INTERNAL_H
#define TIRAY_PROTOCOL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "tiray_sdk.h"

#define TIRAY_PROTOCOL_MAGIC 0x55AAu
#define TIRAY_PROTOCOL_VERSION 1u
#define TIRAY_PROTOCOL_HEADER_SIZE 16u
#define TIRAY_PROTOCOL_MAX_PAYLOAD 2048u
#define TIRAY_PROTOCOL_MAX_FRAME (TIRAY_PROTOCOL_HEADER_SIZE + TIRAY_PROTOCOL_MAX_PAYLOAD + 2u)

typedef enum tiray_message_type {
    TIRAY_MSG_REQUEST = 0x01,
    TIRAY_MSG_ACK = 0x02,
    TIRAY_MSG_DONE = 0x03,
    TIRAY_MSG_ERROR = 0x04,
    TIRAY_MSG_EVENT = 0x05,
} tiray_message_type_t;

typedef enum tiray_command {
    TIRAY_CMD_HELLO = 0x0001, TIRAY_CMD_PING = 0x0002, TIRAY_CMD_STATUS = 0x0003,
    TIRAY_CMD_VERSION = 0x0004, TIRAY_CMD_REBOOT = 0x0005,
    TIRAY_CMD_GET_CONFIG_GROUP = 0x0103, TIRAY_CMD_SET_CONFIG_GROUP = 0x0104,
    TIRAY_CMD_DUMP_REGS = 0x0105, TIRAY_CMD_WRITE_REG = 0x0106,
    TIRAY_CMD_START_STATIC_CAPTURE = 0x0200, TIRAY_CMD_START_DYNAMIC = 0x0210,
    TIRAY_CMD_STOP_DYNAMIC = 0x0211, TIRAY_CMD_QUERY_DYNAMIC = 0x0212,
    TIRAY_CMD_CAL_OFFSET_BEGIN = 0x0300, TIRAY_CMD_CAL_OFFSET_CAPTURE = 0x0301,
    TIRAY_CMD_CAL_OFFSET_BUILD = 0x0302, TIRAY_CMD_CAL_OFFSET_CANCEL = 0x0303,
    TIRAY_CMD_CAL_GAIN_BEGIN = 0x0304, TIRAY_CMD_CAL_GAIN_CAPTURE = 0x0305,
    TIRAY_CMD_CAL_GAIN_BUILD = 0x0306, TIRAY_CMD_CAL_GAIN_CANCEL = 0x0307,
    TIRAY_CMD_CAL_STATUS = 0x0308, TIRAY_CMD_IMG_UPLOAD_CONFIG = 0x0500,
    TIRAY_CMD_IMG_UPLOAD_START = 0x0501, TIRAY_CMD_IMG_UPLOAD_QUERY = 0x0502,
} tiray_command_t;

typedef struct tiray_frame {
    uint8_t version;
    uint8_t header_size;
    uint8_t message_type;
    uint8_t flags;
    uint16_t command;
    uint32_t sequence;
    const uint8_t* payload;
    uint16_t payload_length;
} tiray_frame_t;

typedef struct tiray_response {
    uint8_t message_type;
    uint16_t command;
    uint32_t sequence;
    uint8_t* payload;
    size_t payload_capacity;
    size_t payload_length;
    uint32_t device_error;
} tiray_response_t;

uint16_t tiray_protocol_crc16(const uint8_t* data, size_t length);
tiray_status_t tiray_protocol_encode(const tiray_frame_t* frame, uint8_t* output,
                                      size_t output_capacity, size_t* output_length);
tiray_status_t tiray_protocol_decode(const uint8_t* input, size_t input_length,
                                      tiray_frame_t* frame);
tiray_status_t tiray_sdk_request(tiray_sdk_t* sdk, uint16_t command,
                                  const uint8_t* payload, size_t payload_length,
                                  tiray_response_t* response);
tiray_status_t tiray_sdk_request_async_internal(tiray_sdk_t* sdk, uint16_t command,
                                                 const uint8_t* payload, size_t payload_length,
                                                 tiray_async_callback_t callback, void* user_data,
                                                 tiray_async_job_t** job);

#endif
