#include <stdio.h>

#include "esp_err.h"
#include "nvs_flash.h"
#include "host/ble_hs.h" // ble_hs_adv_parse_fields
#include "host/util/util.h" // ble_hs_util_ensure_addr
#include "nimble/nimble_port.h" // nimble_port_run
#include "nimble/nimble_port_freertos.h" // nimble_port_freertos_init

void nimble_host_task(void *param)
{
    // This runs until nimble_port_stop() is issued 
    nimble_port_run();

    // If stopped, deinitialise
    nimble_port_freertos_deinit();
}

// Callback for startup or reset of stack to ensure there is a device
static void nimble_sync_callback(void) {
    // Ensure there is one Bluetooth device
    int ret = ble_hs_util_ensure_addr(0);
    assert(ret == 0);
}

static void ibeacon_parse(const uint8_t *data, uint8_t len) {
    // iBeacon Header is just an 0xFF (Manufacturers data)
    // 02       - header size
    // 01 06    - flags 
    // 1A       - 26 bytes 
    // FF       - mfg_data
    
    // 4C 00 - 004C (Apple)
    if ((data[0] == 0x4C) && (data[1] == 0x00)) {
        // 02 - ibeacon
        // 15 - 21 bytes 
        if ((data[2] == 0x02) && (data[3] == 0x15)) {
            // AA 11 BB 22 CC 33 DD 44 EE 55 FF 66 00 77 11 88 - UUID
            // 00 00 - 16-bit major id
            // 00 00 - 16-bit minor id 
            // C5    - Tx Power 
            printf("\tUUID: ");
            for (int i = 4; i < 20; i++) {
                printf("%02x ", data[i]);
            }
            printf("\n");
            printf("\tMajor ID: %02x %02x\n", data[20], data[21]);
            printf("\tMinor ID: %02x %02x\n", data[22], data[23]);
            printf("\tTx Power: %d dBm\n", (int8_t)data[24]);
        }
    }
}

static int adv_gap_event_callback(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_gap_disc_desc *ar = &event->disc;
        struct ble_hs_adv_fields adv_fields;

        // Limit to just very local devices i.e. -45dBm > -0.1dBm
        if (ar->rssi < -45) {
            return 0;
        }

        if (ble_hs_adv_parse_fields(&adv_fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }

        printf("Advertisement from: %02X:%02X:%02X:%02X:%02X:%02X\n",
          ar->addr.val[5], ar->addr.val[4], ar->addr.val[3],
          ar->addr.val[2], ar->addr.val[1], ar->addr.val[0]
        );

        printf("- RSSI: %d dBm\n", ar->rssi);

        printf("- Raw Data: ");
        for (int i = 0; i < ar->length_data; i++) {
            printf("%02X ", ar->data[i]);
        }
        printf("\n");

        printf("- Parsed Data:\n");

        if (adv_fields.num_uuids16 > 0) {
            printf("  * 16--bit UUID(s) present\n");
        } 

        if (adv_fields.num_uuids32 > 0) {
            printf("  * 32-bit UUID(s) present\n");
        }

        if (adv_fields.num_uuids128 > 0) {
            printf("  * 128-bit UUID(s) present\n");
        }

        if (adv_fields.svc_data_uuid16 != NULL) {
            printf("  * Service Data - 16-bit UUID present\n");
        }

        if (adv_fields.svc_data_uuid32 != NULL) {
            printf("  * Service Data - 32-bit UUID present\n");
        }

        if (adv_fields.svc_data_uuid128 != NULL) {
            printf("  * Service Data - 128-bit UUID present\n");
        }

        if (adv_fields.uri != NULL) {
            printf("  * URI present\n");
        }

        if (adv_fields.mfg_data != NULL) {
            printf("  * Manufacturer Data present: ");
            for (int i = 0; i < adv_fields.mfg_data_len; i++) {
                printf("%02X ", adv_fields.mfg_data[i]);
            }
            printf("\n");
            ibeacon_parse(adv_fields.mfg_data, adv_fields.mfg_data_len);
        }
    }

    return 0;
}

void adv_discover(void) {
    // Configuration for scanning for GAP messages
    struct ble_gap_disc_params disc_params = { 
        // .filter_duplicates = 1, // Filter out duplicates
        .passive = 1 // Scan passively
    };

    // Scan passively (so no address needed) and forever 
    int ret = ble_gap_disc(0, BLE_HS_FOREVER, &disc_params, adv_gap_event_callback, NULL);
    if (ret != 0) { return; }
}

void app_main(void)
{
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // For PHY calibration data
    esp_err_t ret = nvs_flash_init();
    if  (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialise BLE host stack
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        printf("Could not initialise NimBLE: %d\n", ret);
        return;
    }

    // This is not a professional solution, so use FIFO with bond details
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    // What do to at startup or after a reset
    ble_hs_cfg.sync_cb = nimble_sync_callback;

    // Start NimBLE host stack in FreeRTOS task
    nimble_port_freertos_init(nimble_host_task);

    // Start scanning
    adv_discover();
}
