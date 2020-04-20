#ifndef _APP_MANAGER_H_
#define _APP_MANAGER_H_
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

#include "openvent.pb-c.h"

typedef esp_err_t (*app_manager_event_handler)(void **ctx, VentRequest *req, VentResponse *resp);

typedef struct {
    int input_rb_size;
    int output_rb_size;
    const char *access_key;
    app_manager_event_handler event_handler;
} app_manager_cfg_t;


esp_err_t app_manager_init(app_manager_cfg_t *config);
esp_err_t app_manager_response(VentResponse *resp);
esp_err_t app_manager_file_handle(void **ctx, VentRequest *req, VentResponse *resp);

RingbufHandle_t app_manager_get_input_rb();
RingbufHandle_t app_manager_get_output_rb();

#endif
