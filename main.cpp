/*********************************************
 * pico_control_demo
 *********************************************/
#include <cstdint>
#include <cstring>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"

#include "config_protocol.h"
#include "usb_stdio_transport.h"
#include "ble_transport.h"
#include "wifi_tcp_transport.h"
#include "wifi_manager.h"
#include "persistent_store.h"


static constexpr uint16_t WIFI_TCP_PORT = 4242;


// ---------------------------------------------------------------------------
// Application configuration
// ---------------------------------------------------------------------------

int16_t gain = -100;
uint16_t sampleRate = 1000;
float threshold = 1.5f;

char deviceName[64] =
    "Alert Unit";

uint8_t calibrationStorage[4095];

HexArray calibration =
{
    .data = calibrationStorage,
    .length = 0,
    .capacity = sizeof(calibrationStorage)
};


// ---------------------------------------------------------------------------
// Runtime status
// ---------------------------------------------------------------------------

char revision[16] = "1.1";
char state[32] = "Running";
float temperature = 23.5f;


// ---------------------------------------------------------------------------
// Wi-Fi manager
// ---------------------------------------------------------------------------

WifiManager wifi;


static bool applicationApplyHandler(
    const char* target
)
{
    if (strcmp(target, "WIFI") == 0)
    {
        return wifi.requestApply();
    }

    if (strcmp(target, "ERASE") == 0)
    {
        return PersistentStore::eraseAll() ==
               PersistentStatus::Ok;
    }

    if (strcmp(target, "FACTORY") == 0)
    {
        return PersistentStore::eraseFactory() ==
               PersistentStatus::Ok;
    }

    if (strcmp(target, "REBOOT") == 0)
    {
        //
        // Schedule a normal watchdog reboot. This call is asynchronous,
        // so ProtocolSession can still send its normal "OK" response
        // after this callback returns.
        //
        watchdog_reboot(
            0,
            0,
            500
        );

        return true;
    }

    return false;
}


// ---------------------------------------------------------------------------
// AnyOperate field table
// ---------------------------------------------------------------------------

Field fields[] =
{
    // Persistent application configuration.
    // Persistent IDs are globally unique and become part of the storage ABI.
    { "gain", "Gain", FieldType::Int16, FieldGroup::Config,
      &gain, 0, true, false, 0x0101 },

    { "sample_rate", "Sample Rate", FieldType::UInt16, FieldGroup::Config,
      &sampleRate, 0, true, false, 0x0102 },

    { "threshold", "Threshold", FieldType::Float, FieldGroup::Config,
      &threshold, 0, true, false, 0x0103 },

    { "device_name", "Device Name", FieldType::String, FieldGroup::Config,
      deviceName, sizeof(deviceName), true, false, 0x0104 },

    { "calibration", "Calibration Data", FieldType::HexArray, FieldGroup::Config,
      &calibration, 0, true, false, 0x0105 },

    // Wi-Fi remains separately persisted in Volume 0 by WifiManager.
    { "wifiSSID", "WiFi SSID", FieldType::String, FieldGroup::Wifi,
      wifi.ssid, sizeof(wifi.ssid), true, false, 0 },

    { "wifiSecret", "WiFi Secret", FieldType::String, FieldGroup::Wifi,
      wifi.secret, sizeof(wifi.secret), true, true, 0 },

    // Runtime/status values are volatile.
    { "revision", "Revision", FieldType::String, FieldGroup::Status,
      revision, sizeof(revision), false, false, 0 },

    { "state", "State", FieldType::String, FieldGroup::Status,
      state, sizeof(state), false, false, 0 },

    { "temperature", "Temperature", FieldType::Float, FieldGroup::Status,
      &temperature, 0, false, false, 0 },

    { "wifiState", "WiFi State", FieldType::String, FieldGroup::Status,
      wifi.state, sizeof(wifi.state), false, false, 0 },

    { "wifiIPAddr", "WiFi IP Address", FieldType::String, FieldGroup::Status,
      wifi.ipAddress, sizeof(wifi.ipAddress), false, false, 0 },

    { "storageState", "Storage State", FieldType::String, FieldGroup::Status,
      wifi.storageState, sizeof(wifi.storageState), false, false, 0 }
};


int main()
{
    stdio_init_all();

    ConfigRegistry registry(
        fields,
        sizeof(fields) / sizeof(fields[0])
    );

    //
    // Stored application values override the compiled defaults when present.
    // Missing entries simply leave the defaults above untouched.
    //
    (void)registry.loadPersistentFields();

    wifi.loadConfiguration();

    const bool cyw43Ok =
        cyw43_arch_init_with_country(
            CYW43_COUNTRY_UK
        ) == 0;

    if (!cyw43Ok)
    {
        strncpy(
            wifi.state,
            "CYW43 init failed",
            sizeof(wifi.state) - 1
        );

        wifi.state[sizeof(wifi.state) - 1] =
            '\0';
    }


    UsbStdioTransport usb;

    ProtocolSession usbSession(
        usb,
        registry,
        applicationApplyHandler
    );


    BleTransport ble;

    if (cyw43Ok)
    {
        (void)ble.init();
    }

    ProtocolSession bleSession(
        ble,
        registry,
        applicationApplyHandler
    );


    WifiTcpTransport wifiTcp(
        WIFI_TCP_PORT
    );

    bool tcpServerOk = false;

    if (cyw43Ok)
    {
        tcpServerOk =
            wifiTcp.init();

        if (!tcpServerOk)
        {
            strncpy(
                wifi.state,
                "TCP server failed",
                sizeof(wifi.state) - 1
            );

            wifi.state[sizeof(wifi.state) - 1] =
                '\0';
        }
    }

    ProtocolSession wifiSession(
        wifiTcp,
        registry,
        applicationApplyHandler
    );


    if (cyw43Ok)
    {
        wifi.start();
    }


    while (true)
    {
        if (cyw43Ok)
        {
            cyw43_arch_poll();
        }

        if (tcpServerOk)
        {
            wifiTcp.poll();
        }

        usbSession.poll();
        bleSession.poll();
        wifiSession.poll();

        if (tcpServerOk)
        {
            wifiTcp.poll();
        }

        if (cyw43Ok)
        {
            wifi.service();
        }

        //
        // Main application work can happen here.
        //
        // temperature = ...
        // state = ...
        //

        tight_loop_contents();
    }
}
