#include <string.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_vfs_dev.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "app_manager.h"
#include "openvent.pb-c.h"
static const char *TAG = "APP_MANAGER";

#define MEM_CHECK(mem) if (mem == NULL) { ESP_LOGE(TAG, "Memory exhaused"); return ESP_ERR_NO_MEM; }
#define MEM_CHECK_ACT(mem, act) if (mem == NULL) { ESP_LOGE(TAG, "Memory exhaused"); act; }


typedef struct {
    bool run;
    RingbufHandle_t input_rb;
    RingbufHandle_t output_rb;
    char *access_key;
    app_manager_event_handler event_handler;
    void *ctx;
} app_manager_data;

static app_manager_data *g_manager;

esp_err_t app_manager_response(VentResponse *resp)
{
    size_t outlen = vent_response__get_packed_size(resp);
    uint8_t *outbuf = (uint8_t *) malloc(outlen);
    if (outbuf == NULL) {
        ESP_LOGE(TAG, "Memory exhaused");
        return ESP_FAIL;
    }
    vent_response__pack(resp, outbuf);
    if (xRingbufferSend(g_manager->output_rb, outbuf, outlen, 10000 / portTICK_RATE_MS) != pdPASS) {
        ESP_LOGE(TAG, "Error response data");
        free(outbuf);
        return ESP_FAIL;
    }
    free(outbuf);
    return ESP_OK;
}

esp_err_t app_manager_file_handle(void **ctx, VentRequest *req, VentResponse *resp)
{
    FileData *file_data = req->write_firmware_request;
    FILE *file = *ctx;
    ESP_LOGI(TAG, "Ctx = %x", (int)*ctx);

    resp->status = STATUS__Fail;

    if (file_data == NULL) {
        return app_manager_response(resp);
    }
    if (file_data->offset == 0 && file_data->file_name) {

        if (file) {
            fclose(file);
            file = NULL;
            *ctx = NULL;
        }
        ESP_LOGI(TAG, "Opening file %s", file_data->file_name);
        file = fopen(file_data->file_name, "w");
        if (file == NULL) {
            ESP_LOGE(TAG, "Error opening file %s", file_data->file_name);
            return app_manager_response(resp);
        }
        *ctx = file;
    }
    if (file_data->data.len > 0 && file) {
        ESP_LOGI(TAG, "Writing %d/%d, memfree=%d", file_data->offset + file_data->data.len, file_data->file_size, esp_get_free_heap_size());
        fwrite(file_data->data.data, sizeof(uint8_t), file_data->data.len, file);
        if (file_data->offset + file_data->data.len >= file_data->file_size) {
            fclose(file);
            file = NULL;
            *ctx = NULL;
            ESP_LOGI(TAG, "Write file finish %s", file_data->file_name);
        }
    }
    resp->status = STATUS__Success;
    return app_manager_response(resp);
}

static esp_err_t _app_process_data(VentRequest *req)
{
    VentResponse resp = VENT_RESPONSE__INIT;
    resp.status = STATUS__Fail;
    if (strcmp(req->access_key, g_manager->access_key) != 0) {
        resp.status = STATUS__InvalidAccessKey;
        return app_manager_response(&resp);
    }
    if (g_manager->event_handler) {
        if (g_manager->event_handler(&g_manager->ctx, req, &resp) != ESP_OK) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

static void _app_manager_task(void *pv)
{
    uint8_t *data;
    size_t data_size;
    while (g_manager->run) {
        data = xRingbufferReceive(g_manager->input_rb, &data_size, 1000 / portTICK_RATE_MS);
        if (data == NULL) {
            continue;
        }
        ESP_LOGI(TAG, "Receiving %d bytes", data_size);
        VentRequest *req = vent_request__unpack(NULL, data_size, data);
        vRingbufferReturnItem(g_manager->input_rb, data);

        if (req == NULL) {
            ESP_LOGE(TAG, "Error unpack data");
            continue;
        }
        _app_process_data(req);
        vent_request__free_unpacked(req, NULL);

    }
    vTaskDelete(NULL);
}


esp_err_t app_manager_init(app_manager_cfg_t *config)
{
    g_manager = calloc(1, sizeof(app_manager_data));
    MEM_CHECK_ACT(g_manager, goto _app_manager_init_fail);
    g_manager->input_rb = xRingbufferCreate(config->input_rb_size, RINGBUF_TYPE_NOSPLIT);
    MEM_CHECK_ACT(g_manager->input_rb, goto _app_manager_init_fail);
    g_manager->output_rb = xRingbufferCreate(config->output_rb_size, RINGBUF_TYPE_NOSPLIT);
    MEM_CHECK_ACT(g_manager->output_rb, goto _app_manager_init_fail);

    g_manager->run = true;
    g_manager->event_handler = config->event_handler;
    g_manager->access_key = strdup(config->access_key);
    if (xTaskCreate(_app_manager_task, "manager_task", 4 * 1024, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "error creating manager task");
        goto _app_manager_init_fail;
    }
    return ESP_OK;

_app_manager_init_fail:
    if (g_manager && g_manager->input_rb) {
        vRingbufferDelete(g_manager->input_rb);
    }
    if (g_manager && g_manager->output_rb) {
        vRingbufferDelete(g_manager->output_rb);
    }
    free(g_manager);
    return ESP_FAIL;
}

RingbufHandle_t app_manager_get_input_rb()
{
    return g_manager->input_rb;
}

RingbufHandle_t app_manager_get_output_rb()
{
    return g_manager->output_rb;
}
