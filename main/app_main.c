/*
 * This file is subject to the terms of the Apache License. If a copy of
 * the license was not distributed with this file, you can obtain one at:
 *
 *              ./LICENSE
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "esp_vfs_dev.h"
#include "esp_spiffs.h"

#include "ble_prov.h"
#include "app_manager.h"

static const char *TAG = "OPENVENT";


static esp_err_t _app_manager_event_handler(void **ctx, VentRequest *req, VentResponse *resp)
{
    switch ((uint32_t)req->cmd) {
        case COMMAND__DeviceInfoRequest: {
            DeviceInfo info = DEVICE_INFO__INIT;
            info.fw_version = "1.0.0";
            info.hw_version = "1.0.1";
            info.device_model = 1;
            info.device_name = "device_name";
            resp->device_info_response = &info;
            resp->status = STATUS__Success;
            return app_manager_response(resp);
        }
        case COMMAND__WriteFileRequest: {
            esp_err_t ret = app_manager_file_handle(ctx, req, resp);
            FileData *file_data = req->write_file_request;
            if (file_data && file_data->offset == 0) {
                ESP_LOGI(TAG, "Open file");
            }
            if (file_data && file_data->offset + file_data->data.len >= file_data->file_size) {
                ESP_LOGI(TAG, "Close file");
            }
            return ret;
        }
    }
    return app_manager_response(resp);
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);


    app_manager_cfg_t app_man_cfg = {
        .input_rb_size = 8 * 1024,
        .output_rb_size = 2 * 1024,
        .access_key = "0000",
        .event_handler = _app_manager_event_handler,
    };

    app_manager_init(&app_man_cfg);

    const static protocomm_security_pop_t app_pop = {
        .data = (uint8_t *) CONFIG_SECURITY_POP,
        .len = (sizeof(CONFIG_SECURITY_POP) - 1)
    };

    int prov_security = 1;

#ifndef CONFIG_USE_SEC_1
    prov_security = 0;
#endif

    ble_provisioning_start(prov_security, &app_pop,
                           app_manager_get_output_rb(), /* manager output data as ble send data */
                           app_manager_get_input_rb()); /* manager input data as ble receive data */


    // For Logging
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    ESP_LOGI(TAG, "free mem=%d\n", esp_get_free_heap_size());
}
