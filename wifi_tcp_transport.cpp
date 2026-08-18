#include "wifi_tcp_transport.h"

#include <algorithm>
#include <cstring>

#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

WifiTcpTransport::WifiTcpTransport(uint16_t port)
    : port_(port)
{
}


bool WifiTcpTransport::init()
{
    cyw43_arch_lwip_begin();

    tcp_pcb* pcb =
        tcp_new_ip_type(IPADDR_TYPE_ANY);

    if (pcb == nullptr)
    {
        cyw43_arch_lwip_end();
        return false;
    }

    const err_t bindResult =
        tcp_bind(
            pcb,
            nullptr,
            port_
        );

    if (bindResult != ERR_OK)
    {
        tcp_close(pcb);
        cyw43_arch_lwip_end();
        return false;
    }

    listenPcb_ =
        tcp_listen_with_backlog(
            pcb,
            1
        );

    if (listenPcb_ == nullptr)
    {
        tcp_close(pcb);
        cyw43_arch_lwip_end();
        return false;
    }

    tcp_arg(
        listenPcb_,
        this
    );

    tcp_accept(
        listenPcb_,
        &WifiTcpTransport::acceptCallback
    );

    cyw43_arch_lwip_end();

    return true;
}


err_t WifiTcpTransport::acceptCallback(
    void* arg,
    tcp_pcb* newPcb,
    err_t err
)
{
    auto* self =
        static_cast<WifiTcpTransport*>(arg);

    if (self == nullptr)
    {
        if (newPcb != nullptr)
        {
            tcp_abort(newPcb);
        }

        return ERR_ABRT;
    }

    return self->handleAccept(
        newPcb,
        err
    );
}


err_t WifiTcpTransport::handleAccept(
    tcp_pcb* newPcb,
    err_t err
)
{
    // This is already running inside an lwIP callback.
    cyw43_arch_lwip_check();

    if (err != ERR_OK ||
        newPcb == nullptr)
    {
        return ERR_VAL;
    }

    // AnyOperate currently models one protocol session per transport.
    // Reject a second simultaneous TCP client.
    if (clientPcb_ != nullptr)
    {
        tcp_abort(newPcb);
        return ERR_ABRT;
    }

    clientPcb_ = newPcb;

    resetBuffers();

    tcp_arg(
        clientPcb_,
        this
    );

    tcp_recv(
        clientPcb_,
        &WifiTcpTransport::recvCallback
    );

    tcp_err(
        clientPcb_,
        &WifiTcpTransport::errorCallback
    );

    // Commands and replies are normally short. Avoid Nagle delaying them.
    tcp_nagle_disable(clientPcb_);

    return ERR_OK;
}


err_t WifiTcpTransport::recvCallback(
    void* arg,
    tcp_pcb* pcb,
    pbuf* p,
    err_t err
)
{
    auto* self =
        static_cast<WifiTcpTransport*>(arg);

    if (self == nullptr)
    {
        if (p != nullptr)
        {
            pbuf_free(p);
        }

        return ERR_VAL;
    }

    return self->handleReceive(
        pcb,
        p,
        err
    );
}


err_t WifiTcpTransport::handleReceive(
    tcp_pcb* pcb,
    pbuf* p,
    err_t err
)
{
    // This is already running inside an lwIP callback.
    cyw43_arch_lwip_check();

    if (p == nullptr)
    {
        // The remote side closed the connection.
        closeClientFromCallback(pcb);
        return ERR_OK;
    }

    if (err != ERR_OK)
    {
        pbuf_free(p);
        return err;
    }

    bool acceptedAll = true;

    for (pbuf* q = p;
         q != nullptr && acceptedAll;
         q = q->next)
    {
        const auto* data =
            static_cast<const uint8_t*>(q->payload);

        for (uint16_t i = 0;
             i < q->len;
             ++i)
        {
            if (!receiveByte(data[i]))
            {
                acceptedAll = false;
                break;
            }
        }
    }

    if (acceptedAll)
    {
        // Tell TCP that the application has accepted the bytes.
        tcp_recved(
            pcb,
            p->tot_len
        );
    }

    pbuf_free(p);

    // If our RX FIFO fills, close rather than silently corrupting a command.
    if (!acceptedAll)
    {
        closeClientFromCallback(pcb);
    }

    return ERR_OK;
}


void WifiTcpTransport::errorCallback(
    void* arg,
    err_t err
)
{
    auto* self =
        static_cast<WifiTcpTransport*>(arg);

    if (self != nullptr)
    {
        self->handleError(err);
    }
}


void WifiTcpTransport::handleError(err_t err)
{
    (void)err;

    // lwIP has already freed the PCB when the error callback is made.
    clientPcb_ = nullptr;
    resetBuffers();
}


void WifiTcpTransport::closeClientFromCallback(
    tcp_pcb* pcb
)
{
    if (pcb == nullptr)
    {
        clientPcb_ = nullptr;
        resetBuffers();
        return;
    }

    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);

    const err_t closeResult =
        tcp_close(pcb);

    if (closeResult != ERR_OK)
    {
        tcp_abort(pcb);
    }

    if (clientPcb_ == pcb)
    {
        clientPcb_ = nullptr;
    }

    resetBuffers();
}


bool WifiTcpTransport::enqueueRx(
    const uint8_t* data,
    size_t length
)
{
    // Check capacity before modifying the FIFO, so a failed enqueue
    // never leaves a partial protocol fragment behind.
    size_t used;

    if (rxWrite_ >= rxRead_)
    {
        used = rxWrite_ - rxRead_;
    }
    else
    {
        used =
            RxBufferSize -
            rxRead_ +
            rxWrite_;
    }

    const size_t freeSpace =
        RxBufferSize - 1 - used;

    if (length > freeSpace)
    {
        return false;
    }

    for (size_t i = 0; i < length; ++i)
    {
        rxBuffer_[rxWrite_] = data[i];

        rxWrite_ =
            (rxWrite_ + 1) %
            RxBufferSize;
    }

    return true;
}


int WifiTcpTransport::readChar()
{
    if (rxRead_ == rxWrite_)
    {
        return -1;
    }

    const uint8_t value =
        rxBuffer_[rxRead_];

    rxRead_ =
        (rxRead_ + 1) %
        RxBufferSize;

    return value;
}


bool WifiTcpTransport::enqueueTx(
    const uint8_t* data,
    size_t length
)
{
    // Transactional enqueue: either all bytes fit, or none are queued.
    const size_t used = txQueued();

    const size_t freeSpace =
        TxBufferSize - 1 - used;

    if (length > freeSpace)
    {
        return false;
    }

    for (size_t i = 0; i < length; ++i)
    {
        txBuffer_[txWrite_] = data[i];

        txWrite_ =
            (txWrite_ + 1) %
            TxBufferSize;
    }

    return true;
}


void WifiTcpTransport::write(
    const char* data,
    size_t length
)
{
    if (!connected() ||
        data == nullptr ||
        length == 0)
    {
        return;
    }

    // ProtocolTransport::write() cannot report back-pressure.
    // For this first version we use a generously sized FIFO.
    // If it fills, discard this complete write rather than a fragment.
    (void)enqueueTx(
        reinterpret_cast<const uint8_t*>(data),
        length
    );
}


size_t WifiTcpTransport::txQueued() const
{
    if (txWrite_ >= txRead_)
    {
        return txWrite_ - txRead_;
    }

    return
        TxBufferSize -
        txRead_ +
        txWrite_;
}


void WifiTcpTransport::poll()
{
    if (clientPcb_ == nullptr ||
        txRead_ == txWrite_)
    {
        return;
    }

    cyw43_arch_lwip_begin();

    if (clientPcb_ == nullptr)
    {
        cyw43_arch_lwip_end();
        return;
    }

    const u16_t sendBuffer =
        tcp_sndbuf(clientPcb_);

    if (sendBuffer == 0)
    {
        cyw43_arch_lwip_end();
        return;
    }

    size_t contiguous;

    if (txWrite_ > txRead_)
    {
        contiguous =
            txWrite_ - txRead_;
    }
    else
    {
        contiguous =
            TxBufferSize - txRead_;
    }

    contiguous =
        std::min<size_t>(
            contiguous,
            sendBuffer
        );

    // tcp_write() length is u16_t.
    contiguous =
        std::min<size_t>(
            contiguous,
            0xffffu
        );

    const err_t result =
        tcp_write(
            clientPcb_,
            &txBuffer_[txRead_],
            static_cast<u16_t>(contiguous),
            TCP_WRITE_FLAG_COPY
        );

    if (result == ERR_OK)
    {
        txRead_ =
            (txRead_ + contiguous) %
            TxBufferSize;

        tcp_output(clientPcb_);
    }

    // ERR_MEM is not fatal: leave bytes queued and retry on a later poll.
    cyw43_arch_lwip_end();
}


void WifiTcpTransport::resetBuffers()
{
    rxRead_ = 0;
    rxWrite_ = 0;

    txRead_ = 0;
    txWrite_ = 0;

    telnetState_ = TelnetState::Normal;
    previousWasCR_ = false;
}

bool WifiTcpTransport::receiveByte(uint8_t ch)
{
    static constexpr uint8_t IAC  = 255;
    static constexpr uint8_t WILL = 251;
    static constexpr uint8_t WONT = 252;
    static constexpr uint8_t DO   = 253;
    static constexpr uint8_t DONT = 254;

    // Strip basic Telnet negotiation bytes.
    switch (telnetState_)
    {
        case TelnetState::Normal:
        {
            if (ch == IAC)
            {
                telnetState_ = TelnetState::Iac;
                return true;
            }
            break;
        }

        case TelnetState::Iac:
        {
            if (ch == IAC)
            {
                // Escaped literal 0xFF.
                telnetState_ = TelnetState::Normal;
                break;
            }

            if (ch == WILL || ch == WONT || ch == DO || ch == DONT)
            {
                telnetState_ = TelnetState::Option;
                return true;
            }

            telnetState_ = TelnetState::Normal;
            return true;
        }

        case TelnetState::Option:
        {
            // Ignore the option number.
            telnetState_ = TelnetState::Normal;
            return true;
        }
    }

    // Normalize terminal line endings so ProtocolSession sees one LF.
    if (ch == '\r')
    {
        const uint8_t newline = '\n';
        previousWasCR_ = true;
        return enqueueRx(&newline, 1);
    }

    if (previousWasCR_)
    {
        previousWasCR_ = false;
        if (ch == '\n' || ch == '\0')
        {
            return true;
        }
    }

    return enqueueRx(&ch, 1);
}
