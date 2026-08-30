# NimBLE Advertisement Receiver

This is a generic advertisement processor for ESP-IDF using the NimBLE / Apache Newt libraries. 

What needs to be configured:
- `CONFIG_BT_ENABLED=y` - Bluetooth Enabled
- `CONFIG_BT_NIMBLE_ENABLED=y` - Nimble Enabled
- `CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n` - Bluetooth 5.0 disabled

If a BLE ADV sender is needed, see [this BLE one-shot repo](https://github.com/jlmurdoch/nimble_adv_one_shot) or [Apache Newt's example for iBeacon](https://mynewt.apache.org/latest/tutorials/ble/ibeacon.html).

## iBeacon
This example also parses basic Apple iBeacon data, using manufacturer data and tears it apart.

Example reference of setting these is in [ESP-IDF's NimBLE repo](https://github.com/espressif/esp-nimble/blob/master/nimble/host/src/ble_ibeacon.c).

| Example Data | Size     | Type     | Description          |
|--------------|----------|----------|----------------------|
| `02`         | 1 byte   | BLE ADV  | Header size in Bytes |
| `01 06`      | 2 bytes  | BLE ADV  | 0x1 = Flags, 0x6 = General Discoverable Mode, BR/EDR Not Supported |
| `1A`         | 1 byte   | BLE ADV  | Advertisement length (26 bytes) |
| `FF`         | 1 byte   | BLE ADV  | Custom Manufacturer Data |
| `4C 00`      | 2 bytes  | Mfg Data | Apple 16-bit ID - 004C |
| `02`         | 1 byte   | Mfg Data | iBeacon Type |
| `15`         | 1 byte   | Mfg Data | iBeacon Size |
| `AA 11 ..`   | 16 bytes | iBeacon  | 128-bit UUID |
| `00 00`      | 2 bytes  | iBeacon  | Major ID |
| `00 00`      | 2 bytes  | iBeacon  | Minor ID |
| `C5`         | 1 byte   | iBeacon  | Tx Power at 1 metre (for distance calculation - i.e -59 dBm) |

## BTHome
This example also parses [BTHome](https://bthome.io) data, using the `Service Data, 16-bit UUID` field.

| Example Data | Size     | Type     | Description          |
|--------------|----------|----------|----------------------|
| `02`         | 1 byte   | BLE ADV  | Header size in Bytes |
| `01 06`      | 2 bytes  | BLE ADV  | 0x1 = Flags, 0x6 = General Discoverable Mode, BR/EDR Not Supported |
| `0E`         | 1 byte   | BLE ADV  | Advertisement length (14 bytes) |
| `16`         | 1 byte   | BLE ADV  | Service Data, 16-bit UUID |
| `D2 FC`      | 2 bytes  | Svc Data | BTHome 16-bit ID - FCD2 |
| `44`         | 1 byte   | Svc Data | Version, Triggered / Scheduled, Encrypted |
| `00 0B`      | 2 bytes  | BTHome   | 0x00 = Packet ID, 0x0B = #11 |
| `01 64`      | 2 bytes  | BTHome   | 0x01 = Battery, 0x0B = 100% |
| `...`        | ? bytes  | BTHome   | Further data fields: i.e. 0x02 = Temperature, 0x03 = Humidity, ...|