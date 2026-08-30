# NimBLE Advertisement Receiver

This is a generic advertisement processor for ESP-IDF using the NimBLE / Apache Newt libraries. 

What needs to be configured:
- `CONFIG_BT_ENABLED=y` - Bluetooth Enabled
- `CONFIG_BT_NIMBLE_ENABLED=y` - Nimble Enabled
- `CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n` - Bluetooth 5.0 disabled

This example also parses basic Apple iBeacon data, using manufacturer data and tears it apart. Comments in code.