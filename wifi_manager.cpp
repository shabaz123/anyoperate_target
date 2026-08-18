#include "wifi_manager.h"

#include <cstring>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"

#include "flash_volume_store.h"


WifiManager::WifiManager()
{
    copyText(
        ssid,
        sizeof(ssid),
        "MySSID"
    );

    copyText(
        secret,
        sizeof(secret),
        "FactorySecret"
    );

    copyText(
        state,
        sizeof(state),
        "Not initialised"
    );

    copyText(
        ipAddress,
        sizeof(ipAddress),
        "0.0.0.0"
    );

    copyText(
        storageState,
        sizeof(storageState),
        "Not checked"
    );
}


void WifiManager::copyText(
    char* destination,
    size_t capacity,
    const char* source
)
{
    if (capacity == 0)
    {
        return;
    }

    strncpy(
        destination,
        source,
        capacity - 1
    );

    destination[capacity - 1] =
        '\0';
}


void WifiManager::setState(
    const char* text
)
{
    copyText(
        state,
        sizeof(state),
        text
    );
}


void WifiManager::setStorageState(
    const char* text
)
{
    copyText(
        storageState,
        sizeof(storageState),
        text
    );
}


void WifiManager::clearIp()
{
    copyText(
        ipAddress,
        sizeof(ipAddress),
        "0.0.0.0"
    );
}


void WifiManager::loadConfiguration()
{
    if (!FlashVolumeStore::layoutSafe())
    {
        setStorageState("Layout unsafe");
        return;
    }

    StoredSettings saved{};
    size_t savedLength = 0;

    const bool ok =
        FlashVolumeStore::read(
            FlashVolumeStore::Volume::Settings,
            StorageVersion,
            &saved,
            sizeof(saved),
            savedLength
        );

    if (!ok ||
        savedLength != sizeof(saved))
    {
        setStorageState("Defaults");
        return;
    }

    saved.ssid[sizeof(saved.ssid) - 1] =
        '\0';

    saved.secret[sizeof(saved.secret) - 1] =
        '\0';

    copyText(
        ssid,
        sizeof(ssid),
        saved.ssid
    );

    copyText(
        secret,
        sizeof(secret),
        saved.secret
    );

    setStorageState("Loaded");
}


bool WifiManager::saveConfiguration()
{
    if (!FlashVolumeStore::layoutSafe())
    {
        setStorageState("Layout unsafe");
        return false;
    }

    StoredSettings saved{};

    copyText(
        saved.ssid,
        sizeof(saved.ssid),
        ssid
    );

    copyText(
        saved.secret,
        sizeof(saved.secret),
        secret
    );

    const bool ok =
        FlashVolumeStore::write(
            FlashVolumeStore::Volume::Settings,
            StorageVersion,
            &saved,
            sizeof(saved)
        );

    setStorageState(
        ok ? "Saved" : "Save failed"
    );

    return ok;
}


bool WifiManager::stationHasIPv4() const
{
    const ip4_addr_t* address =
        netif_ip4_addr(
            &cyw43_state.netif[CYW43_ITF_STA]
        );

    return address != nullptr &&
           ip4_addr_get_u32(address) != 0;
}


void WifiManager::captureIp()
{
    ip4addr_ntoa_r(
        netif_ip4_addr(
            &cyw43_state.netif[CYW43_ITF_STA]
        ),
        ipAddress,
        sizeof(ipAddress)
    );
}


bool WifiManager::startJoin()
{
    clearIp();
    setState("Connecting");

    if (ssid[0] == '\0')
    {
        setState("SSID empty");

        connectState_ =
            ConnectState::Idle;

        return false;
    }

    const int result =
        cyw43_arch_wifi_connect_async(
            ssid,
            secret,
            CYW43_AUTH_WPA2_MIXED_PSK
        );

    if (result != 0)
    {
        setState("Join start failed");

        connectState_ =
            ConnectState::Idle;

        return false;
    }

    stateDeadline_ =
        make_timeout_time_ms(
            ConnectTimeoutMs
        );

    connectState_ =
        ConnectState::Joining;

    return true;
}


void WifiManager::start()
{
    cyw43_arch_enable_sta_mode();

    (void)startJoin();
}


bool WifiManager::requestApply()
{
    setState("Apply pending");

    stateDeadline_ =
        make_timeout_time_ms(
            ApplyGraceMs
        );

    connectState_ =
        ConnectState::ApplyDelay;

    return true;
}


void WifiManager::service()
{
    struct netif* staNetif =
        &cyw43_state.netif[CYW43_ITF_STA];

    switch (connectState_)
    {
        case ConnectState::Idle:
        {
            return;
        }

        case ConnectState::ApplyDelay:
        {
            if (!time_reached(
                    stateDeadline_))
            {
                return;
            }

            setState("Saving");

            if (!saveConfiguration())
            {
                setState("Save failed");

                connectState_ =
                    ConnectState::Idle;

                return;
            }

            setState("Applying");
            clearIp();

            cyw43_arch_disable_sta_mode();

            stateDeadline_ =
                make_timeout_time_ms(
                    LeaveSettleMs
                );

            connectState_ =
                ConnectState::StaDisabled;

            return;
        }

        case ConnectState::StaDisabled:
        {
            if (!time_reached(
                    stateDeadline_))
            {
                return;
            }

            cyw43_arch_enable_sta_mode();

            (void)startJoin();

            return;
        }

        case ConnectState::Joining:
        {
            const int linkStatus =
                cyw43_wifi_link_status(
                    &cyw43_state,
                    CYW43_ITF_STA
                );

            if (linkStatus ==
                CYW43_LINK_BADAUTH)
            {
                setState("Bad password");

                connectState_ =
                    ConnectState::Idle;

                return;
            }

            if (linkStatus ==
                CYW43_LINK_NONET)
            {
                setState("SSID not found");

                connectState_ =
                    ConnectState::Idle;

                return;
            }

            if (linkStatus ==
                CYW43_LINK_FAIL)
            {
                setState("Connect failed");

                connectState_ =
                    ConnectState::Idle;

                return;
            }

            if (netif_is_link_up(
                    staNetif))
            {
                setState("Getting IP");

                connectState_ =
                    ConnectState::WaitingForDhcp;

                return;
            }

            if (time_reached(
                    stateDeadline_))
            {
                setState("Connect timeout");

                connectState_ =
                    ConnectState::Idle;

                return;
            }

            return;
        }

        case ConnectState::WaitingForDhcp:
        {
            if (!netif_is_link_up(
                    staNetif))
            {
                setState("Link lost");
                clearIp();

                connectState_ =
                    ConnectState::Idle;

                return;
            }

            if (stationHasIPv4())
            {
                captureIp();
                setState("Connected");

                connectState_ =
                    ConnectState::Idle;

                return;
            }

            if (time_reached(
                    stateDeadline_))
            {
                setState("DHCP timeout");
                clearIp();

                connectState_ =
                    ConnectState::Idle;

                return;
            }

            return;
        }
    }
}
