#include "tiray_sdk.h"
#include "tiray_protocol.h"

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <pthread.h>
#endif

struct tiray_async_job {
    tiray_sdk_t* sdk;
    uint16_t command;
    uint8_t* payload;
    size_t payload_length;
    tiray_async_callback_t callback;
    void* user_data;
    tiray_response_t response;
    uint8_t response_payload[TIRAY_PROTOCOL_MAX_PAYLOAD];
    tiray_status_t result;
#ifndef _WIN32
    pthread_t thread;
    int started;
    int joined;
#endif
};

#ifndef _WIN32
static void* async_worker(void* argument) {
    tiray_async_job_t* job = (tiray_async_job_t*)argument;
    job->response.payload = job->response_payload;
    job->response.payload_capacity = sizeof(job->response_payload);
    job->result = tiray_sdk_request(job->sdk, job->command, job->payload,
                                    job->payload_length, &job->response);
    if (job->callback != NULL) job->callback(job->result, job->user_data);
    return NULL;
}
#endif

tiray_status_t tiray_sdk_request_async_internal(tiray_sdk_t* sdk, uint16_t command,
                                       const uint8_t* payload, size_t payload_length,
                                       tiray_async_callback_t callback, void* user_data,
                                       tiray_async_job_t** job_out) {
    if (sdk == NULL || job_out == NULL || payload_length > TIRAY_PROTOCOL_MAX_PAYLOAD ||
        (payload == NULL && payload_length != 0u)) return TIRAY_STATUS_INVALID_ARGUMENT;
#ifdef _WIN32
    (void)command; (void)payload; (void)payload_length; (void)callback; (void)user_data;
    return TIRAY_STATUS_IO_ERROR;
#else
    tiray_async_job_t* job = (tiray_async_job_t*)calloc(1u, sizeof(*job));
    if (job == NULL) return TIRAY_STATUS_INTERNAL_ERROR;
    job->sdk = sdk;
    job->command = command;
    job->payload_length = payload_length;
    job->callback = callback;
    job->user_data = user_data;
    if (payload_length != 0u) {
        job->payload = (uint8_t*)malloc(payload_length);
        if (job->payload == NULL) { free(job); return TIRAY_STATUS_INTERNAL_ERROR; }
        memcpy(job->payload, payload, payload_length);
    }
    if (pthread_create(&job->thread, NULL, async_worker, job) != 0) {
        free(job->payload); free(job); return TIRAY_STATUS_INTERNAL_ERROR;
    }
    job->started = 1;
    *job_out = job;
    return TIRAY_STATUS_OK;
#endif
}

tiray_status_t tiray_sdk_ping_async(tiray_sdk_t* sdk, tiray_async_callback_t callback,
                                    void* user_data, tiray_async_job_t** job) {
    return tiray_sdk_request_async_internal(sdk, TIRAY_CMD_PING, NULL, 0u,
                                            callback, user_data, job);
}

tiray_status_t tiray_async_job_wait(tiray_async_job_t* job) {
    if (job == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
#ifdef _WIN32
    return TIRAY_STATUS_IO_ERROR;
#else
    if (job->started && !job->joined) {
        if (pthread_join(job->thread, NULL) != 0) return TIRAY_STATUS_INTERNAL_ERROR;
        job->joined = 1;
    }
    return job->result;
#endif
}

void tiray_async_job_destroy(tiray_async_job_t* job) {
    if (job == NULL) return;
#ifndef _WIN32
    (void)tiray_async_job_wait(job);
#endif
    free(job->payload);
    free(job);
}
