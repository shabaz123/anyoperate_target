#pragma once

#include <cstddef>
#include <cstdint>

#include "btstack.h"
#include "config_protocol.h"

class BleTransport : public ProtocolTransport
{
public:
    BleTransport();

    bool init();

    int readChar() override;

    void write(
        const char* data,
        size_t length
    ) override;

    bool connected() const
    {
        return connectionHandle_ !=
               HCI_CON_HANDLE_INVALID;
    }

private:
    static BleTransport* instance_;

    static constexpr size_t RxBufferSize = 512;
    static constexpr size_t TxBufferSize = 2048;

    uint8_t rxBuffer_[RxBufferSize];
    size_t rxRead_ = 0;
    size_t rxWrite_ = 0;

    uint8_t txBuffer_[TxBufferSize];
    size_t txRead_ = 0;
    size_t txWrite_ = 0;

    hci_con_handle_t connectionHandle_ =
        HCI_CON_HANDLE_INVALID;

    bool notificationsEnabled_ = false;
    bool sendRequested_ = false;

    uint16_t commandValueHandle_ = 0;
    uint16_t responseValueHandle_ = 0;
    uint16_t responseCccdHandle_ = 0;

    btstack_packet_callback_registration_t hciEventCallback_{};
    btstack_context_callback_registration_t sendCallback_{};

    static void packetHandler(
        uint8_t packetType,
        uint16_t channel,
        uint8_t* packet,
        uint16_t size
    );

    static int attWriteCallback(
        hci_con_handle_t connectionHandle,
        uint16_t attributeHandle,
        uint16_t transactionMode,
        uint16_t offset,
        uint8_t* buffer,
        uint16_t bufferSize
    );

    static void canSendNow(void* context);

    void handlePacket(
        uint8_t packetType,
        uint16_t channel,
        uint8_t* packet,
        uint16_t size
    );

    int handleWrite(
        hci_con_handle_t connectionHandle,
        uint16_t attributeHandle,
        uint16_t transactionMode,
        uint16_t offset,
        uint8_t* buffer,
        uint16_t bufferSize
    );

    void enqueueRx(
        const uint8_t* data,
        size_t length
    );

    bool enqueueTx(
        const uint8_t* data,
        size_t length
    );

    void requestSend();
    void sendNextChunk();

    size_t txAvailable() const;
};
