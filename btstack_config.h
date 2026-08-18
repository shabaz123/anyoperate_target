#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

//
// BLE support itself is supplied by linking pico_btstack_ble.
//
#ifndef ENABLE_BLE
#error Please link to pico_btstack_ble
#endif

//
// We are a BLE peripheral / GATT server.
//
#define ENABLE_LE_PERIPHERAL

//
// Useful while developing.
//
#define ENABLE_LOG_ERROR
//#define ENABLE_LOG_INFO
#define ENABLE_PRINTF_HEXDUMP

//
// Buffers / connection limits.
//
#define HCI_OUTGOING_PRE_BUFFER_SIZE 4
#define HCI_ACL_PAYLOAD_SIZE (255 + 4)
#define HCI_ACL_CHUNK_SIZE_ALIGNMENT 4

#define MAX_NR_HCI_CONNECTIONS 1
#define MAX_NR_GATT_CLIENTS 0
#define MAX_NR_SM_LOOKUP_ENTRIES 3
#define MAX_NR_WHITELIST_ENTRIES 16
#define MAX_NR_LE_DEVICE_DB_ENTRIES 16

//
// Limit controller buffering to avoid overrunning the
// CYW43 shared SPI bus.
//
#define MAX_NR_CONTROLLER_ACL_BUFFERS 3
#define MAX_NR_CONTROLLER_SCO_PACKETS 3

//
// Controller -> host flow control.
//
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL

#define HCI_HOST_ACL_PACKET_LEN (255 + 4)
#define HCI_HOST_ACL_PACKET_NUM 3

#define HCI_HOST_SCO_PACKET_LEN 120
#define HCI_HOST_SCO_PACKET_NUM 3

//
// Persistent BT/LE database sizing.
//
#define NVM_NUM_DEVICE_DB_ENTRIES 16
#define NVM_NUM_LINK_KEYS 16

//
// We aren't giving BTstack malloc(), so use a fixed-size ATT DB.
//
#define MAX_ATT_DB_SIZE 512

//
// Pico SDK / embedded HAL configuration.
//
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_ASSERT

//
// Software crypto used by BLE security support.
//
#define ENABLE_SOFTWARE_AES128
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

#endif
