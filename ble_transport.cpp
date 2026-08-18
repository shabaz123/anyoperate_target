#include "ble_transport.h"

#include <algorithm>
#include <cstring>

#include "easyconfig.h"

BleTransport* BleTransport::instance_ = nullptr;


BleTransport::BleTransport()
{
    instance_ = this;
}


bool BleTransport::init()
{
    // CYW43 is initialized once by main().
    l2cap_init();
    sm_init();

    att_server_init(
        profile_data,
        nullptr,
        &BleTransport::attWriteCallback
    );

    //
    // Query generated GATT DB for our service/handles.
    //
    uint16_t serviceStart = 0;
    uint16_t serviceEnd = 0xffff;

    static const uint8_t serviceUuid[] =
    {
        0x6E, 0x40, 0x00, 0x01,
        0xB5, 0xA3,
        0xF3, 0x93,
        0xE0, 0xA9,
        0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E
    };

    static const uint8_t commandUuid[] =
    {
        0x6E, 0x40, 0x00, 0x02,
        0xB5, 0xA3,
        0xF3, 0x93,
        0xE0, 0xA9,
        0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E
    };

    static const uint8_t responseUuid[] =
    {
        0x6E, 0x40, 0x00, 0x03,
        0xB5, 0xA3,
        0xF3, 0x93,
        0xE0, 0xA9,
        0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E
    };

    if (!gatt_server_get_handle_range_for_service_with_uuid128(
            serviceUuid,
            &serviceStart,
            &serviceEnd))
    {
        return false;
    }

    commandValueHandle_ =
        gatt_server_get_value_handle_for_characteristic_with_uuid128(
            serviceStart,
            serviceEnd,
            commandUuid
        );

    responseValueHandle_ =
        gatt_server_get_value_handle_for_characteristic_with_uuid128(
            serviceStart,
            serviceEnd,
            responseUuid
        );

    responseCccdHandle_ =
        gatt_server_get_client_configuration_handle_for_characteristic_with_uuid128(
            serviceStart,
            serviceEnd,
            responseUuid
        );

    hciEventCallback_.callback =
        &BleTransport::packetHandler;

    hci_add_event_handler(
        &hciEventCallback_
    );

    att_server_register_packet_handler(
        &BleTransport::packetHandler
    );

    //
    // Advertising.
    //
    static constexpr char deviceName[] = "EasyConfig";

    static constexpr size_t advDataSize =
        3 +                         // Flags AD structure
        2 + sizeof(deviceName) - 1; // Name AD structure

    uint8_t advData[advDataSize];
    size_t advPos = 0;

    advData[advPos++] = 0x02;
    advData[advPos++] = BLUETOOTH_DATA_TYPE_FLAGS;
    advData[advPos++] = 0x06;

    advData[advPos++] = sizeof(deviceName);
    advData[advPos++] = BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME;

    memcpy(
        &advData[advPos],
        deviceName,
        sizeof(deviceName) - 1
    );

    advPos += sizeof(deviceName) - 1;

    bd_addr_t nullAddress;
    memset(nullAddress, 0, sizeof(nullAddress));

    gap_advertisements_set_params(
        0x0030,
        0x0060,
        0,
        0,
        nullAddress,
        0x07,
        0x00
    );

    gap_advertisements_set_data(
        sizeof(advData),
        advData
    );

    hci_power_control(HCI_POWER_ON);

    return true;
}

void BleTransport::packetHandler(
    uint8_t packetType,
    uint16_t channel,
    uint8_t* packet,
    uint16_t size
)
{
    if (instance_)
    {
        instance_->handlePacket(
            packetType,
            channel,
            packet,
            size
        );
    }
}


void BleTransport::handlePacket(
    uint8_t packetType,
    uint16_t channel,
    uint8_t* packet,
    uint16_t size
)
{
    (void)channel;
    (void)size;

    if (packetType != HCI_EVENT_PACKET)
    {
        return;
    }

    const uint8_t eventType =
        hci_event_packet_get_type(packet);

    switch (eventType)
    {
        case BTSTACK_EVENT_STATE:
        {
            if (btstack_event_state_get_state(packet) ==
                HCI_STATE_WORKING)
            {
                gap_advertisements_enable(1);
            }

            break;
        }

        case HCI_EVENT_LE_META:
        {
            if (hci_event_le_meta_get_subevent_code(packet) ==
                HCI_SUBEVENT_LE_CONNECTION_COMPLETE)
            {
                connectionHandle_ =
                    hci_subevent_le_connection_complete_get_connection_handle(
                        packet
                    );
            }

            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE:
        {
            connectionHandle_ =
                HCI_CON_HANDLE_INVALID;

            notificationsEnabled_ = false;
            sendRequested_ = false;

            rxRead_ = rxWrite_ = 0;
            txRead_ = txWrite_ = 0;

            break;
        }

        default:
            break;
    }
}

int BleTransport::attWriteCallback(
    hci_con_handle_t connectionHandle,
    uint16_t attributeHandle,
    uint16_t transactionMode,
    uint16_t offset,
    uint8_t* buffer,
    uint16_t bufferSize
)
{
    if (!instance_)
    {
        return 0;
    }

    return instance_->handleWrite(
        connectionHandle,
        attributeHandle,
        transactionMode,
        offset,
        buffer,
        bufferSize
    );
}


int BleTransport::handleWrite(
    hci_con_handle_t connectionHandle,
    uint16_t attributeHandle,
    uint16_t transactionMode,
    uint16_t offset,
    uint8_t* buffer,
    uint16_t bufferSize
)
{
    (void)transactionMode;

    if (offset != 0)
    {
        return ATT_ERROR_INVALID_OFFSET;
    }

    if (attributeHandle == commandValueHandle_)
    {
        connectionHandle_ = connectionHandle;

        enqueueRx(
            buffer,
            bufferSize
        );

        return 0;
    }

    if (attributeHandle == responseCccdHandle_)
    {
        if (bufferSize < 2)
        {
            return ATT_ERROR_INVALID_ATTRIBUTE_VALUE_LENGTH;
        }

        notificationsEnabled_ =
            little_endian_read_16(buffer, 0) ==
            GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;

        connectionHandle_ =
            connectionHandle;

        if (notificationsEnabled_)
        {
            requestSend();
        }

        return 0;
    }

    return 0;
}

void BleTransport::enqueueRx(
    const uint8_t* data,
    size_t length
)
{
    for (size_t i = 0; i < length; ++i)
    {
        const size_t next =
            (rxWrite_ + 1) % RxBufferSize;

        if (next == rxRead_)
        {
            //
            // Buffer full.
            // For now drop remaining bytes.
            //
            return;
        }

        rxBuffer_[rxWrite_] = data[i];
        rxWrite_ = next;
    }
}


int BleTransport::readChar()
{
    if (rxRead_ == rxWrite_)
    {
        return -1;
    }

    const uint8_t value =
        rxBuffer_[rxRead_];

    rxRead_ =
        (rxRead_ + 1) % RxBufferSize;

    return value;
}

bool BleTransport::enqueueTx(
    const uint8_t* data,
    size_t length
)
{
    for (size_t i = 0; i < length; ++i)
    {
        const size_t next =
            (txWrite_ + 1) % TxBufferSize;

        if (next == txRead_)
        {
            return false;
        }

        txBuffer_[txWrite_] = data[i];
        txWrite_ = next;
    }

    return true;
}


void BleTransport::write(
    const char* data,
    size_t length
)
{
    if (!connected())
    {
        return;
    }

    if (!enqueueTx(
            reinterpret_cast<const uint8_t*>(data),
            length))
    {
        return;
    }

    requestSend();
}

size_t BleTransport::txAvailable() const
{
    if (txWrite_ >= txRead_)
    {
        return txWrite_ - txRead_;
    }

    return TxBufferSize -
           txRead_ +
           txWrite_;
}


void BleTransport::requestSend()
{
    if (!notificationsEnabled_ ||
        !connected() ||
        txRead_ == txWrite_ ||
        sendRequested_)
    {
        return;
    }

    sendRequested_ = true;

    sendCallback_.callback =
        &BleTransport::canSendNow;

    sendCallback_.context = this;

    att_server_register_can_send_now_callback(
        &sendCallback_,
        connectionHandle_
    );
}


void BleTransport::canSendNow(void* context)
{
    auto* self =
        static_cast<BleTransport*>(context);

    self->sendRequested_ = false;
    self->sendNextChunk();
}


void BleTransport::sendNextChunk()
{
    if (!notificationsEnabled_ ||
        !connected() ||
        txRead_ == txWrite_)
    {
        return;
    }

    //
    // Start conservatively at 20 bytes.
    // We can improve this later using negotiated MTU.
    //
    uint8_t chunk[20];

    size_t count = 0;

    while (count < sizeof(chunk) &&
           txRead_ != txWrite_)
    {
        chunk[count++] =
            txBuffer_[txRead_];

        txRead_ =
            (txRead_ + 1) % TxBufferSize;
    }

    att_server_notify(
        connectionHandle_,
        responseValueHandle_,
        chunk,
        static_cast<uint16_t>(count)
    );

    if (txRead_ != txWrite_)
    {
        requestSend();
    }
}

