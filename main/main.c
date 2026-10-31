#include <stdio.h>

#include "esp_err.h"
#include "nvs_flash.h"
#include "host/ble_hs.h" // ble_hs_adv_parse_fields
#include "host/util/util.h" // ble_hs_util_ensure_addr
#include "nimble/nimble_port.h" // nimble_port_run
#include "nimble/nimble_port_freertos.h" // nimble_port_freertos_init

#define BLE_UUID16_APPLE  0x004C
#define BLE_UUID16_BTHOME 0xFCD2

#define BTHOME_PACKET_ID    0x00
#define BTHOME_BATTERY      0x01
#define BTHOME_ILLUMINANCE  0x05
#define BTHOME_VOLTAGE_A    0x0C
#define BTHOME_POWER        0x10
#define BTHOME_MOTION       0x21
#define BTHOME_WINDOW       0x2D
#define BTHOME_BUTTON       0x3A
#define BTHOME_ROTATION     0x3F
#define BTHOME_CURRENT_B    0x5D

int16_t unpack_int16(uint8_t first, uint8_t second) {
    uint16_t bits = ((uint16_t)first << 8) | (uint16_t)second;
    if (bits <= INT16_MAX) 
    {
        return (int16_t)bits;
    }
    else
    {
        return (int16_t)((int32_t)bits - 65536);
    }
}

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

static void ble_bthome_parse_adv_data(const uint8_t *data, uint8_t data_len) {
    printf("\tRaw service data: ");
    for (int i = 0; i < data_len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");

    // Is it BTHome v2 (0x40 >> 5)?
    // https://bthome.io/format/
    if ((data[2] >> 5) == 0x2) {
        printf("\t");
        for (int i = 3; i < data_len; i++) {
            switch (data[i++]) {
                
            case BTHOME_PACKET_ID: // Packet ID (counter)
                printf("PacketID=%d, ", data[i]);
            break;

            case BTHOME_BATTERY: // Battery (percentage)
                printf("Battery%%=%d, ", data[i]);
            break;

            case BTHOME_ILLUMINANCE: // Illuminance (lux) / uint24_t
                // 0x13, 0x8A, 0x14 = 13460.67 lx
                printf("Illuminance=%0.2f, ", (float)(((uint32_t)data[i]) | ((uint32_t)data[i+1] << 8) | ((uint32_t)data[i+2] << 16)) * 0.01);
                i += 2; // Three-bytes, so increment another two
            break;

            case BTHOME_MOTION: // Motion: Yes/No
                printf("Motion=%d, ", data[i]);
            break;

            case BTHOME_WINDOW: // Window / door: Open/Close
                printf("Window=%d, ", data[i]);
            break;

            case BTHOME_ROTATION: // Rotation (degrees) / int16_t
                // 0x02, 0x0C = 307.4 degrees 
                // See https://sw23.github.io/fp-conv/formats/int16.html
                printf("Rotation=%0.1f, ", unpack_int16(data[i], data[i+1]) * 0.1);
                i++; // Two-bytes, so increment another one
            break;

            case BTHOME_BUTTON: // Button (press)
                printf("Button=%d, ", data[i]);
            break;

            case BTHOME_POWER: // Power (on/off)
                printf("Power=%d, ", data[i]);
            break;

            case BTHOME_VOLTAGE_A: // Voltage 0.000V / uint16_t
                // 0x02, 0x0C = 3.074 volts
                printf("VoltageA=%0.3f, ", (float)(((uint16_t)data[i] << 8) | ((uint16_t)data[i+1])) * 0.001);
                i++; // Two-bytes, so increment another one
            break;

            case BTHOME_CURRENT_B: // Amperes 0.000A / int16_t
                // 0x02, 0xEA = -5.63 Amps
                printf("CurrentB=%0.3f, ", unpack_int16(data[i], data[i+1]) * 0.001);
                i++; // Two-bytes, so increment another one
            break;

            default:
                // Maybe do something here? 
            break;
            }
        }
        printf("\n");
    }
}

static void ble_ibeacon_parse_adv_data(const uint8_t *data, uint8_t data_len) {
    /*
     * An iBeacon Header is custom manufacturer data on a BLE ADV.
     * The setting (not reading) of this data is visible in:
     * https://github.com/espressif/esp-nimble/blob/master/nimble/host/src/ble_ibeacon.c
     *
     * -- BLE ADV
     * 02       - Header size in Bytes
     * 01 06    - Flags LE (0x6 = General Discoverable Mode, BR/EDR Not Supported)
     * 1A       - Advertisement length (26 bytes)
     * FF       - Manufacturer Data (AKA mfg_data)
     * -- Custom Manufacturer Data
     * 4C 00    - Apple (004C, Little Endian)
     * 02       - iBeacon type
     * 15       - iBeacon length (21 bytes) 
     * -- Apple iBeacon Data
     * AA 11 .. - 128-bit UUID
     * 00 00    - Major ID 
     * 00 00    - Minor ID
     * C5       - Tx Power (8-bit signed integer) 
     */

    // Check to see if expected iBeacon indicator (Type 2, 21 bytes)
    if ((data[2] == 0x02) && (data[3] == 0x15)) {
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

static int ble_adv_gap_event_callback(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_gap_disc_desc *ar = &event->disc;
        struct ble_hs_adv_fields adv_fields;

        // Limit to just very local devices (i.e. -45dBm > -0.1dBm)
        if (ar->rssi < -45) {
            return 0;
        }

        // Parse out any fields - flags is expected at a minimum
        if (ble_hs_adv_parse_fields(&adv_fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }

        // Print the Bluetooth address
        printf("Advertisement from: %02X:%02X:%02X:%02X:%02X:%02X\n",
          ar->addr.val[5], ar->addr.val[4], ar->addr.val[3],
          ar->addr.val[2], ar->addr.val[1], ar->addr.val[0]
        );

        // Print RSSI value
        printf("- RSSI: %d dBm\n", ar->rssi);

        // Print the whole packet for debug purposes
        printf("- Raw Data: ");
        for (int i = 0; i < ar->length_data; i++) {
            printf("%02X ", ar->data[i]);
        }
        printf("\n");

        // Now look for other data
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
            if ((adv_fields.svc_data_uuid16[0] | ((uint16_t)adv_fields.svc_data_uuid16[1] << 8)) == BLE_UUID16_BTHOME) {
                ble_bthome_parse_adv_data(adv_fields.svc_data_uuid16, adv_fields.svc_data_uuid16_len);
            }
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
            if ((adv_fields.mfg_data[0] | ((uint16_t)adv_fields.mfg_data[1] << 8)) == BLE_UUID16_APPLE) {
                ble_ibeacon_parse_adv_data(adv_fields.mfg_data, adv_fields.mfg_data_len);
            }
        }
    }

    return 0;
}

// Optimised BTHome specific example
static int bthome_event_callback(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_gap_disc_desc *ar = &event->disc;
        struct ble_hs_adv_fields adv_fields;

        // Parse out any fields - flags is expected at a minimum
        if (ble_hs_adv_parse_fields(&adv_fields, event->disc.data, event->disc.length_data) != 0) {
            return 0;
        }

        if (adv_fields.svc_data_uuid16 != NULL) {
            if ((adv_fields.svc_data_uuid16[0] | ((uint16_t)adv_fields.svc_data_uuid16[1] << 8)) == BLE_UUID16_BTHOME) {
                // Print the Bluetooth address
                printf("Advertisement from: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    ar->addr.val[5], ar->addr.val[4], ar->addr.val[3],
                    ar->addr.val[2], ar->addr.val[1], ar->addr.val[0]
                );
                
                // Print RSSI value
                printf("- RSSI: %d dBm\n", ar->rssi);

                // Print the whole packet for debug purposes
                printf("- Raw Data: ");
                for (int i = 0; i < ar->length_data; i++) {
                    printf("%02X ", ar->data[i]);
                }
                printf("\n");

                ble_bthome_parse_adv_data(adv_fields.svc_data_uuid16, adv_fields.svc_data_uuid16_len);
            }
        }
    }

    return 0;
}

void ble_adv_discover(void) {
    // Configuration for scanning for GAP messages
    struct ble_gap_disc_params disc_params = { 
        .filter_duplicates = 1, // Filter out duplicates - Remove if testing
        .passive = 1 // Scan passively
    };

    // Scan passively (so no address needed) and forever 
    int ret = ble_gap_disc(0, BLE_HS_FOREVER, &disc_params, ble_adv_gap_event_callback, NULL);
    // Example of making it BTHome specific
    // int ret = ble_gap_disc(0, BLE_HS_FOREVER, &disc_params, bthome_event_callback, NULL);
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

    printf("Waiting for stack to start\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Start scanning
    ble_adv_discover();
}
