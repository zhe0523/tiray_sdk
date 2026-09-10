#include "tiray_protocol.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    const uint8_t payload[] = {0x00u, 0x30u, 0x04u, 0x00u, 0x0cu, 0x00u, 0x00u, 0x00u};
    tiray_frame_t request = {
        TIRAY_PROTOCOL_VERSION, TIRAY_PROTOCOL_HEADER_SIZE, TIRAY_MSG_REQUEST, 0u,
        TIRAY_CMD_CAL_OFFSET_BEGIN, 42u, payload, (uint16_t)sizeof(payload)
    };
    uint8_t encoded[TIRAY_PROTOCOL_MAX_FRAME];
    size_t encoded_length = 0u;
    if (tiray_protocol_encode(&request, encoded, sizeof(encoded), &encoded_length) != TIRAY_STATUS_OK) return 1;
    tiray_frame_t decoded;
    if (tiray_protocol_decode(encoded, encoded_length, &decoded) != TIRAY_STATUS_OK) return 2;
    if (decoded.command != request.command || decoded.sequence != request.sequence ||
        decoded.payload_length != request.payload_length || memcmp(decoded.payload, payload, sizeof(payload)) != 0) return 3;
    encoded[encoded_length - 1u] ^= 1u;
    if (tiray_protocol_decode(encoded, encoded_length, &decoded) != TIRAY_STATUS_CRC_ERROR) return 4;
    puts("tiray_protocol_tests passed");
    return 0;
}
