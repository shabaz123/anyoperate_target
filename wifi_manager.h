#pragma once

#include <cstddef>
#include <cstdint>

#include "pico/stdlib.h"

class WifiManager
{
public:
    static constexpr size_t SsidCapacity = 33;
    static constexpr size_t SecretCapacity = 64;
    static constexpr size_t StateCapacity = 32;
    static constexpr size_t IpCapacity = 16;
    static constexpr size_t StorageStateCapacity = 32;

    WifiManager();

    //
    // Loads saved Wi-Fi settings from flash. If no valid saved record
    // exists, the compiled defaults remain in ssid/secret.
    //
    void loadConfiguration();

    //
    // Called once after CYW43 has been initialized.
    //
    void start();

    //
    // Request APPLY WIFI. The actual save/reconnect is performed later
    // from service(), keeping ProtocolSession non-blocking.
    //
    bool requestApply();

    //
    // Call frequently from main().
    //
    void service();

    char ssid[SsidCapacity];
    char secret[SecretCapacity];

    char state[StateCapacity];
    char ipAddress[IpCapacity];
    char storageState[StorageStateCapacity];

private:
    static constexpr uint32_t ConnectTimeoutMs = 30000;
    static constexpr uint32_t ApplyGraceMs = 250;
    static constexpr uint32_t LeaveSettleMs = 100;
    static constexpr uint16_t StorageVersion = 1;

    struct StoredSettings
    {
        char ssid[SsidCapacity];
        char secret[SecretCapacity];
    };

    enum class ConnectState
    {
        Idle,
        ApplyDelay,
        StaDisabled,
        Joining,
        WaitingForDhcp
    };

    ConnectState connectState_ =
        ConnectState::Idle;

    absolute_time_t stateDeadline_;

    static void copyText(
        char* destination,
        size_t capacity,
        const char* source
    );

    void setState(const char* text);
    void setStorageState(const char* text);
    void clearIp();

    bool saveConfiguration();

    bool stationHasIPv4() const;
    void captureIp();

    bool startJoin();
};
