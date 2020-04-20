#pragma once

#include "esp_event.h"
#include <freertos/ringbuf.h>

#include "protocomm_security.h"
#include <wifi_provisioning/wifi_config.h>

esp_err_t ble_prov_get_wifi_state(wifi_prov_sta_state_t *state);


esp_err_t ble_prov_get_wifi_disconnect_reason(wifi_prov_sta_fail_reason_t *reason);

esp_err_t ble_prov_event_handler(void *ctx, system_event_t *event);


esp_err_t ble_prov_is_provisioned(bool *provisioned);

esp_err_t ble_prov_configure_sta(wifi_config_t *wifi_cfg);

RingbufHandle_t ble_prov_get_receive_rb();
RingbufHandle_t ble_prov_get_send_rb();
esp_err_t ble_provisioning_start(int security,
                                 const protocomm_security_pop_t *pop,
                                 RingbufHandle_t send_rb,
                                 RingbufHandle_t receive_rb);

esp_err_t ble_prov_custom_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                       uint8_t **outbuf, ssize_t *outlen, void *priv_data);
