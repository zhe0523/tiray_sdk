#include "tiray_sdk.h"

#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>
#include <pthread.h>
#endif

#define BAR0_SIZE 4096u
#define DMA_TABLE_BASE 0x100u
#define DMA_TABLE_STRIDE 0x8u
#define DMA_TABLE_ENTRIES 69u

struct tiray_pcie_receiver {
    tiray_pcie_config_t config;
#ifndef _WIN32
    char bar0_resource_storage[PATH_MAX];
    int event_fd;
    int c2h_fd;
    pthread_t worker;
    int running;
    int worker_started;
    tiray_pcie_frame_callback_t callback;
    void* callback_user_data;
#endif
};

#ifndef _WIN32
static void* pcie_worker(void* argument);
#endif

static uint32_t read_le32_pcie(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

void tiray_pcie_default_config(tiray_pcie_config_t* config) {
    if (config == NULL) return;
    config->event_device = "/dev/idma0_event_0";
    config->c2h_device = "/dev/idma0_c2h_0";
    config->bar0_resource = NULL;
    config->wait_timeout_ms = 1000u;
    config->fallback_rows = 7680u;
    config->fallback_columns = 3072u;
}

#ifndef _WIN32
static int find_bar0_resource(char* output, size_t capacity) {
    DIR* directory = opendir("/sys/bus/pci/devices");
    if (directory == NULL) return -1;
    struct dirent* entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char vendor_path[PATH_MAX], device_path[PATH_MAX], resource_path[PATH_MAX];
        (void)snprintf(vendor_path, sizeof(vendor_path), "/sys/bus/pci/devices/%s/vendor", entry->d_name);
        (void)snprintf(device_path, sizeof(device_path), "/sys/bus/pci/devices/%s/device", entry->d_name);
        (void)snprintf(resource_path, sizeof(resource_path), "/sys/bus/pci/devices/%s/resource0", entry->d_name);
        FILE* vendor = fopen(vendor_path, "r");
        FILE* device = fopen(device_path, "r");
        unsigned int vendor_id = 0u, device_id = 0u;
        if (vendor != NULL && fscanf(vendor, "%x", &vendor_id) != 1) vendor_id = 0u;
        if (device != NULL && fscanf(device, "%x", &device_id) != 1) device_id = 0u;
        if (vendor != NULL) fclose(vendor);
        if (device != NULL) fclose(device);
        if (vendor_id == 0x1b4du && device_id == 0x6667u && access(resource_path, R_OK) == 0) {
            (void)snprintf(output, capacity, "%s", resource_path);
            closedir(directory);
            return 0;
        }
    }
    closedir(directory);
    return -1;
}
#endif

tiray_pcie_receiver_t* tiray_pcie_create(const tiray_pcie_config_t* config) {
    tiray_pcie_config_t defaults;
    tiray_pcie_default_config(&defaults);
    if (config != NULL) defaults = *config;
    if (defaults.event_device == NULL || defaults.c2h_device == NULL) return NULL;
    tiray_pcie_receiver_t* receiver = (tiray_pcie_receiver_t*)calloc(1u, sizeof(*receiver));
    if (receiver == NULL) return NULL;
    receiver->config = defaults;
#ifndef _WIN32
    if (defaults.bar0_resource == NULL) {
        if (find_bar0_resource(receiver->bar0_resource_storage, sizeof(receiver->bar0_resource_storage)) != 0) {
            free(receiver);
            return NULL;
        }
        receiver->config.bar0_resource = receiver->bar0_resource_storage;
    }
    receiver->event_fd = -1;
    receiver->c2h_fd = -1;
#endif
    return receiver;
}

void tiray_pcie_close(tiray_pcie_receiver_t* receiver) {
    if (receiver == NULL) return;
#ifndef _WIN32
    tiray_pcie_stop(receiver);
    if (receiver->event_fd >= 0) close(receiver->event_fd);
    if (receiver->c2h_fd >= 0) close(receiver->c2h_fd);
    receiver->event_fd = -1;
    receiver->c2h_fd = -1;
#endif
}

void tiray_pcie_destroy(tiray_pcie_receiver_t* receiver) {
    if (receiver == NULL) return;
    tiray_pcie_close(receiver);
    free(receiver);
}

tiray_status_t tiray_pcie_open(tiray_pcie_receiver_t* receiver) {
    if (receiver == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
#ifdef _WIN32
    return TIRAY_STATUS_IO_ERROR;
#else
    if (receiver->event_fd >= 0 && receiver->c2h_fd >= 0) return TIRAY_STATUS_OK;
    receiver->event_fd = open(receiver->config.event_device, O_RDONLY | O_NONBLOCK);
    if (receiver->event_fd < 0) return TIRAY_STATUS_IO_ERROR;
    receiver->c2h_fd = open(receiver->config.c2h_device, O_RDONLY);
    if (receiver->c2h_fd < 0) { tiray_pcie_close(receiver); return TIRAY_STATUS_IO_ERROR; }
    return TIRAY_STATUS_OK;
#endif
}

static tiray_status_t read_bar0(tiray_pcie_receiver_t* receiver, tiray_image_frame_t* frame,
                                uint64_t* c2h_offset) {
#ifdef _WIN32
    (void)receiver; (void)frame; (void)c2h_offset; return TIRAY_STATUS_IO_ERROR;
#else
    int fd = open(receiver->config.bar0_resource, O_RDONLY);
    if (fd < 0) return TIRAY_STATUS_IO_ERROR;
    const uint8_t* base = (const uint8_t*)mmap(NULL, BAR0_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (base == MAP_FAILED) return TIRAY_STATUS_IO_ERROR;
    const uint32_t block_size = read_le32_pcie(base + 0x08u);
    frame->rows = read_le32_pcie(base + 0x0cu);
    frame->columns = read_le32_pcie(base + 0x10u);
    frame->image_id = read_le32_pcie(base + 0x14u);
    frame->image_type = read_le32_pcie(base + 0x18u);
    frame->final_image_address = (uint64_t)read_le32_pcie(base + 0x1cu) |
                                 ((uint64_t)read_le32_pcie(base + 0x20u) << 32u);
    if (frame->rows == 0u || frame->columns == 0u ||
        frame->rows == UINT32_MAX || frame->columns == UINT32_MAX) {
        frame->rows = receiver->config.fallback_rows;
        frame->columns = receiver->config.fallback_columns;
    }
    if (block_size == 0u || frame->rows == 0u || frame->columns == 0u ||
        frame->rows > 16384u || frame->columns > 16384u) {
        munmap((void*)base, BAR0_SIZE); return TIRAY_STATUS_PROTOCOL_ERROR;
    }
    int matched = 0;
    for (uint32_t index = 0u; index < DMA_TABLE_ENTRIES; ++index) {
        const uint32_t offset = DMA_TABLE_BASE + index * DMA_TABLE_STRIDE;
        const uint64_t dma = (uint64_t)read_le32_pcie(base + offset) |
                             ((uint64_t)read_le32_pcie(base + offset + 4u) << 32u);
        if (frame->final_image_address >= dma && frame->final_image_address - dma < block_size) {
            *c2h_offset = (uint64_t)index * block_size + frame->final_image_address - dma;
            matched = 1; break;
        }
    }
    munmap((void*)base, BAR0_SIZE);
    return matched ? TIRAY_STATUS_OK : TIRAY_STATUS_PROTOCOL_ERROR;
#endif
}

tiray_status_t tiray_pcie_wait_frame(tiray_pcie_receiver_t* receiver, tiray_image_frame_t* frame) {
    if (receiver == NULL || frame == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
#ifdef _WIN32
    return TIRAY_STATUS_IO_ERROR;
#else
    if (receiver->event_fd < 0 || receiver->c2h_fd < 0) return TIRAY_STATUS_NOT_OPEN;
    struct pollfd pfd = {receiver->event_fd, POLLIN, 0};
    const int timeout = receiver->config.wait_timeout_ms > 0u ? (int)receiver->config.wait_timeout_ms : -1;
    if (poll(&pfd, 1, timeout) == 0) return TIRAY_STATUS_TIMEOUT;
    if ((pfd.revents & POLLIN) == 0) return TIRAY_STATUS_IO_ERROR;
    uint32_t event_value = 0u;
    if (read(receiver->event_fd, &event_value, sizeof(event_value)) != (ssize_t)sizeof(event_value)) return TIRAY_STATUS_IO_ERROR;
    (void)event_value;
    uint64_t c2h_offset = 0u;
    tiray_status_t status = read_bar0(receiver, frame, &c2h_offset);
    if (status != TIRAY_STATUS_OK) return status;
    const uint64_t image_bytes = (uint64_t)frame->rows * frame->columns * 2u;
    if (image_bytes == 0u || image_bytes > SIZE_MAX || frame->data == NULL || frame->data_capacity < image_bytes) return TIRAY_STATUS_INVALID_ARGUMENT;
    if (lseek(receiver->c2h_fd, (off_t)c2h_offset, SEEK_SET) < 0) return TIRAY_STATUS_IO_ERROR;
    size_t total = 0u;
    while (total < (size_t)image_bytes) {
        const ssize_t got = read(receiver->c2h_fd, frame->data + total, (size_t)image_bytes - total);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return TIRAY_STATUS_IO_ERROR;
        total += (size_t)got;
    }
    frame->data_length = total;
    return TIRAY_STATUS_OK;
#endif
}

#ifndef _WIN32
static void* pcie_worker(void* argument) {
    tiray_pcie_receiver_t* receiver = (tiray_pcie_receiver_t*)argument;
    uint8_t* data = (uint8_t*)malloc(3072u * 7680u * 2u);
    if (data == NULL) { receiver->running = 0; return NULL; }
    while (receiver->running) {
        tiray_image_frame_t frame;
        memset(&frame, 0, sizeof(frame));
        frame.data = data;
        frame.data_capacity = 3072u * 7680u * 2u;
        const tiray_status_t status = tiray_pcie_wait_frame(receiver, &frame);
        if (status == TIRAY_STATUS_OK && receiver->running && receiver->callback != NULL)
            receiver->callback(&frame, receiver->callback_user_data);
    }
    free(data);
    return NULL;
}
#endif

tiray_status_t tiray_pcie_start(tiray_pcie_receiver_t* receiver,
                                tiray_pcie_frame_callback_t callback, void* user_data) {
    if (receiver == NULL || callback == NULL) return TIRAY_STATUS_INVALID_ARGUMENT;
#ifdef _WIN32
    (void)user_data; return TIRAY_STATUS_IO_ERROR;
#else
    if (receiver->event_fd < 0 || receiver->c2h_fd < 0) return TIRAY_STATUS_NOT_OPEN;
    if (receiver->running) return TIRAY_STATUS_BUSY;
    receiver->callback = callback;
    receiver->callback_user_data = user_data;
    receiver->running = 1;
    if (pthread_create(&receiver->worker, NULL, pcie_worker, receiver) != 0) {
        receiver->running = 0; receiver->callback = NULL; return TIRAY_STATUS_INTERNAL_ERROR;
    }
    receiver->worker_started = 1;
    return TIRAY_STATUS_OK;
#endif
}

void tiray_pcie_stop(tiray_pcie_receiver_t* receiver) {
    if (receiver == NULL) return;
#ifndef _WIN32
    receiver->running = 0;
    if (receiver->worker_started) {
        (void)pthread_join(receiver->worker, NULL);
        receiver->worker_started = 0;
    }
    receiver->callback = NULL;
    receiver->callback_user_data = NULL;
#endif
}

int tiray_pcie_is_running(const tiray_pcie_receiver_t* receiver) {
#ifdef _WIN32
    (void)receiver; return 0;
#else
    return receiver != NULL && receiver->running != 0;
#endif
}
