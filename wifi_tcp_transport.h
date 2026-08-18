#pragma once

#include <cstddef>
#include <cstdint>

#include "config_protocol.h"

#include "lwip/err.h"
#include "lwip/tcp.h"

class WifiTcpTransport : public ProtocolTransport
{
public:
    explicit WifiTcpTransport(uint16_t port = 4242);

    // CYW43/lwIP must already have been initialised and Wi-Fi connected.
    bool init();

    // Call frequently from main().
    // Flushes queued TCP output. Incoming TCP data is received by lwIP callbacks.
    void poll();

    int readChar() override;

    void write(
        const char* data,
        size_t length
    ) override;

    bool connected() const
    {
        return clientPcb_ != nullptr;
    }

private:
    enum class TelnetState
    {
        Normal,
        Iac,
        Option
    };

    static constexpr size_t RxBufferSize = 2048;
    static constexpr size_t TxBufferSize = 4096;

    uint16_t port_;

    tcp_pcb* listenPcb_ = nullptr;
    tcp_pcb* clientPcb_ = nullptr;

    uint8_t rxBuffer_[RxBufferSize] = {};
    size_t rxRead_ = 0;
    size_t rxWrite_ = 0;

    uint8_t txBuffer_[TxBufferSize] = {};
    size_t txRead_ = 0;
    size_t txWrite_ = 0;

    TelnetState telnetState_ = TelnetState::Normal;
    bool previousWasCR_ = false;

    static err_t acceptCallback(
        void* arg,
        tcp_pcb* newPcb,
        err_t err
    );

    static err_t recvCallback(
        void* arg,
        tcp_pcb* pcb,
        pbuf* p,
        err_t err
    );

    static void errorCallback(
        void* arg,
        err_t err
    );

    err_t handleAccept(
        tcp_pcb* newPcb,
        err_t err
    );

    err_t handleReceive(
        tcp_pcb* pcb,
        pbuf* p,
        err_t err
    );

    void handleError(err_t err);

    bool enqueueRx(
        const uint8_t* data,
        size_t length
    );

    bool enqueueTx(
        const uint8_t* data,
        size_t length
    );

    size_t txQueued() const;

    void resetBuffers();

    bool receiveByte(uint8_t ch);

    // Called only from an lwIP callback, so no cyw43_arch_lwip_begin/end.
    void closeClientFromCallback(tcp_pcb* pcb);
};
