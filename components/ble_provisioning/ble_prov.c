#include <string.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_bt.h>

#include <protocomm.h>
#include <protocomm_ble.h>
#include <protocomm_security0.h>
#include <protocomm_security1.h>
#include <wifi_provisioning/wifi_config.h>

#include "ble_prov.h"

static const char *TAG = "ble_prov";
static const char *ssid_prefix = "CMJ-";

extern wifi_prov_config_handlers_t wifi_prov_handlers;

struct ble_prov_data {
    protocomm_t *pc;                      /*!< Protocomm handler */
    int security;                         /*!< Type of security to use with protocomm */
    const protocomm_security_pop_t *pop;  /*!< Pointer to proof of possession */
    esp_timer_handle_t timer;             /*!< Handle to timer */
    wifi_prov_sta_state_t wifi_state;
    wifi_prov_sta_fail_reason_t wifi_disconnect_reason;
    RingbufHandle_t receive_rb;
    RingbufHandle_t send_rb;
};


static struct ble_prov_data *g_prov;

RingbufHandle_t ble_prov_get_receive_rb()
{
    return g_prov->receive_rb;
}

RingbufHandle_t ble_prov_get_send_rb()
{
    return g_prov->send_rb;
}


static esp_err_t ble_prov_start_service(void)
{
    g_prov->pc = protocomm_new();
    if (g_prov->pc == NULL) {
        ESP_LOGE(TAG, "Failed to create new protocomm instance");
        return ESP_FAIL;
    }

    protocomm_ble_name_uuid_t nu_lookup_table[] = {
        {"prov-session",    0xFF51},
        {"prov-config",     0xFF52},
        {"proto-ver",       0xFF53},
        {"custom-data",     0xFF54},
    };

    protocomm_ble_config_t config = {
        .service_uuid = {
            /* LSB <---------------------------------------
             * ---------------------------------------> MSB */
            0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
            0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
        },
        .nu_lookup_count = sizeof(nu_lookup_table) / sizeof(nu_lookup_table[0]),
        .nu_lookup = nu_lookup_table
    };
    uint64_t mac;
    esp_efuse_mac_get_default((uint8_t *)&mac);
    snprintf(config.device_name, sizeof(config.device_name), "%s%06llX", ssid_prefix, mac);

    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err) {
        ESP_LOGE(TAG, "bt_controller_mem_release failed %d", err);
        if (err != ESP_ERR_INVALID_STATE) {
            return err;
        }
    }

    if (protocomm_ble_start(g_prov->pc, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start BLE provisioning");
        return ESP_FAIL;
    }

    protocomm_set_version(g_prov->pc, "proto-ver", "V0.1");

    if (g_prov->security == 0) {
        protocomm_set_security(g_prov->pc, "prov-session", &protocomm_security0, NULL);
    } else if (g_prov->security == 1) {
        protocomm_set_security(g_prov->pc, "prov-session", &protocomm_security1, g_prov->pop);
    }

    if (protocomm_add_endpoint(g_prov->pc, "prov-config",
                               wifi_prov_config_data_handler,
                               (void *) &wifi_prov_handlers) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set provisioning endpoint");
        protocomm_ble_stop(g_prov->pc);
        return ESP_FAIL;
    }

    if (protocomm_add_endpoint(g_prov->pc, "custom-data",
                               ble_prov_custom_data_handler,
                               (void *) g_prov) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set custom provisioning endpoint");
        protocomm_ble_stop(g_prov->pc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Provisioning started with BLE devname : '%s'", config.device_name);
    return ESP_OK;
}

static void ble_prov_stop_service(void)
{
    protocomm_remove_endpoint(g_prov->pc, "custom-data");
    protocomm_remove_endpoint(g_prov->pc, "prov-config");
    protocomm_unset_security(g_prov->pc, "prov-session");
    protocomm_unset_version(g_prov->pc, "proto-ver");
    protocomm_ble_stop(g_prov->pc);
    protocomm_delete(g_prov->pc);

    esp_bt_mem_release(ESP_BT_MODE_BTDM);
}

/* Task spawned by timer callback */
static void stop_prov_task(void *arg)
{
    ESP_LOGI(TAG, "Stopping provisioning");
    ble_prov_stop_service();

    esp_timer_handle_t timer = g_prov->timer;
    esp_timer_delete(timer);
    g_prov->timer = NULL;

    /* Free provisioning process data */
    free(g_prov);
    g_prov = NULL;
    ESP_LOGI(TAG, "Provisioning stopped");

    vTaskDelete(NULL);
}

/* Callback to be invoked by timer */
static void _stop_prov_cb(void *arg)
{
    xTaskCreate(&stop_prov_task, "stop_prov", 2048, NULL, tskIDLE_PRIORITY, NULL);
}


esp_err_t ble_prov_event_handler(void *ctx, system_event_t *event)
{
    /* For accessing reason codes in case of disconnection */
    system_event_info_t *info = &event->event_info;

    /* If pointer to provisioning application data is NULL
     * then provisioning is not running, therefore return without
     * error */
    if (!g_prov) {
        return ESP_OK;
    }

    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI(TAG, "STA Start");
            /* Once configuration is received through protocomm,
             * device is started as station. Once station starts,
             * wait for connection to establish with configured
             * host SSID and password */
            g_prov->wifi_state = WIFI_PROV_STA_CONNECTING;
            break;

        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "STA Got IP");
            /* Station got IP. That means configuration is successful.
             * Schedule timer to stop provisioning app after 30 seconds. */
            g_prov->wifi_state = WIFI_PROV_STA_CONNECTED;
            if (g_prov && g_prov->timer) {
                esp_timer_start_once(g_prov->timer, 30000 * 1000U);
            }
            break;

        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGE(TAG, "STA Disconnected");
            /* Station couldn't connect to configured host SSID */
            g_prov->wifi_state = WIFI_PROV_STA_DISCONNECTED;
            ESP_LOGE(TAG, "Disconnect reason : %d", info->disconnected.reason);

            /* Set code corresponding to the reason for disconnection */
            switch (info->disconnected.reason) {
                case WIFI_REASON_AUTH_EXPIRE:
                case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                case WIFI_REASON_BEACON_TIMEOUT:
                case WIFI_REASON_AUTH_FAIL:
                case WIFI_REASON_ASSOC_FAIL:
                case WIFI_REASON_HANDSHAKE_TIMEOUT:
                    ESP_LOGI(TAG, "STA Auth Error");
                    g_prov->wifi_disconnect_reason = WIFI_PROV_STA_AUTH_ERROR;
                    break;
                case WIFI_REASON_NO_AP_FOUND:
                    ESP_LOGI(TAG, "STA AP Not found");
                    g_prov->wifi_disconnect_reason = WIFI_PROV_STA_AP_NOT_FOUND;
                    break;
                default:
                    /* If none of the expected reasons,
                     * retry connecting to host SSID */
                    g_prov->wifi_state = WIFI_PROV_STA_CONNECTING;
                    esp_wifi_connect();
            }
            break;

        default:
            break;
    }
    return ESP_OK;
}

esp_err_t ble_prov_get_wifi_state(wifi_prov_sta_state_t *state)
{
    if (g_prov == NULL || state == NULL) {
        return ESP_FAIL;
    }

    *state = g_prov->wifi_state;
    return ESP_OK;
}

esp_err_t ble_prov_get_wifi_disconnect_reason(wifi_prov_sta_fail_reason_t *reason)
{
    if (g_prov == NULL || reason == NULL) {
        return ESP_FAIL;
    }

    if (g_prov->wifi_state != WIFI_PROV_STA_DISCONNECTED) {
        return ESP_FAIL;
    }

    *reason = g_prov->wifi_disconnect_reason;
    return ESP_OK;
}

esp_err_t ble_prov_is_provisioned(bool *provisioned)
{
    *provisioned = false;

#ifdef CONFIG_RESET_PROVISIONED
    nvs_flash_erase();
#endif

    if (nvs_flash_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS");
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init wifi");
        return ESP_FAIL;
    }

    /* Get WiFi Station configuration */
    wifi_config_t wifi_cfg;
    if (esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_cfg) != ESP_OK) {
        return ESP_FAIL;
    }

    if (strlen((const char *) wifi_cfg.sta.ssid)) {
        *provisioned = true;
        ESP_LOGI(TAG, "Found ssid %s", (const char *) wifi_cfg.sta.ssid);
        ESP_LOGI(TAG, "Found password %s", (const char *) wifi_cfg.sta.password);
    }
    return ESP_OK;
}

esp_err_t ble_prov_configure_sta(wifi_config_t *wifi_cfg)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi");
        return ESP_FAIL;
    }
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode");
        return ESP_FAIL;
    }
    if (esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi configuration");
        return ESP_FAIL;
    }
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi configuration");
        return ESP_FAIL;
    }
    if (esp_wifi_connect() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect WiFi");
        return ESP_FAIL;
    }

    if (g_prov) {
        g_prov->wifi_state = WIFI_PROV_STA_CONNECTING;
    }
    return ESP_OK;
}

esp_err_t ble_provisioning_start(int security,
        const protocomm_security_pop_t *pop,
        RingbufHandle_t send_rb,
        RingbufHandle_t receive_rb)
{

    if (g_prov) {
        ESP_LOGI(TAG, "Invalid provisioning state");
        return ESP_ERR_INVALID_STATE;
    }

    g_prov = (struct ble_prov_data *) calloc(1, sizeof(struct ble_prov_data));
    if (!g_prov) {
        ESP_LOGI(TAG, "Unable to allocate prov data");
        return ESP_ERR_NO_MEM;
    }

    g_prov->pop = pop;
    g_prov->security = security;

    /* Create timer object as a member of app data */
    esp_timer_create_args_t timer_conf = {
        .callback = _stop_prov_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "stop_ble_tm"
    };
    esp_err_t err = esp_timer_create(&timer_conf, &g_prov->timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer");
        return ESP_FAIL;
    }

    /* Start provisioning service through BLE */
    err = ble_prov_start_service();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Provisioning failed to start");
        return ESP_FAIL;
    }
    g_prov->receive_rb = receive_rb;
    if (g_prov->receive_rb == NULL) {
        ESP_LOGE(TAG, "Exhaused memory");
        return ESP_FAIL;
    }

    g_prov->send_rb = send_rb;
    if (g_prov->send_rb == NULL) {
        ESP_LOGE(TAG, "Exhaused memory");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "BLE Provisioning started");
    return ESP_OK;
}


int ble_prov_custom_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (g_prov->receive_rb == NULL || g_prov->send_rb == NULL) {
        ESP_LOGE(TAG, "No buffer for send/receive data");
        return ESP_FAIL;
    }

    if (xRingbufferSend(g_prov->receive_rb, inbuf, inlen, 10000 / portTICK_RATE_MS) != pdPASS) {
        ESP_LOGE(TAG, "Error receiving data");
        return ESP_FAIL;
    }
    size_t send_size = 0;
    uint8_t *send_data = xRingbufferReceive(g_prov->send_rb, &send_size, 10000 / portTICK_RATE_MS);
    if (send_data == NULL) {
        ESP_LOGE(TAG, "Error get sending data");
        *outlen = 0;
        return ESP_OK;
    }
    *outlen = send_size;
    *outbuf = (uint8_t *) malloc(*outlen);
    if (outbuf == NULL) {
        ESP_LOGE(TAG, "Memory exhaused");
        return ESP_FAIL;
    }
    memcpy(*outbuf, send_data, send_size);
    vRingbufferReturnItem(g_prov->send_rb, send_data);
    return ESP_OK;
}
