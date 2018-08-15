/**
 * @file HttpServerNetworkTransport.cpp
 *
 * This module contains the implementation of the
 * HttpNetworkTransport::HttpServerNetworkTransport class.
 *
 * © 2018 by Richard Walters
 */

#include <HttpNetworkTransport/HttpServerNetworkTransport.hpp>
#include <inttypes.h>
#include <mutex>
#include <SystemAbstractions/DiagnosticsSender.hpp>
#include <SystemAbstractions/NetworkEndpoint.hpp>
#include <SystemAbstractions/StringExtensions.hpp>

namespace {

    /**
     * This class is an adapter between two related classes in different
     * libraries:
     * - Http::Connection -- the interface required by the HTTP library
     *   for sending and receiving data across the transport layer.
     * - SystemAbstractions::NetworkConnection -- the class which implements
     *   a connection object in terms of the operating system's network APIs.
     */
    struct ConnectionAdapter
        : public Http::Connection
    {
        // Properties

        /**
         * This is used to synchronize access to the object.
         */
        std::recursive_mutex mutex;

        /**
         * This is the object which is implementing the network
         * connection in terms of the operating system's network APIs.
         */
        std::shared_ptr< SystemAbstractions::NetworkConnection > adaptee;

        /**
         * This is the delegate to call whenever data is recevied
         * from the remote peer.
         */
        DataReceivedDelegate dataReceivedDelegate;

        /**
         * This is the delegate to call whenever the connection
         * has been broken.
         */
        BrokenDelegate brokenDelegate;

        // Methods

        /**
         * This method should be called once the adaptee is in place.
         * It fires up the actual network processing.
         *
         * @param[in] weakSelf
         *     This is a weak pointer to the adapter.  It's passed in
         *     so that the delegates given to the adaptee can refer
         *     back to the adapter even if the adaptee ends up living
         *     longer than the adapter.
         *
         * @return
         *     An indication of whether or not the method was
         *     successful is returned.
         */
        bool WireUpAdaptee(std::weak_ptr< ConnectionAdapter > weakSelf) {
            return adaptee->Process(
                [weakSelf](const std::vector< uint8_t >& message){
                    auto self = weakSelf.lock();
                    if (self == nullptr) {
                        return;
                    }
                    std::lock_guard< decltype(self->mutex) > lock(self->mutex);
                    if (self->dataReceivedDelegate != nullptr) {
                        self->dataReceivedDelegate(message);
                    }
                },
                [weakSelf](bool graceful){
                    auto self = weakSelf.lock();
                    if (self == nullptr) {
                        return;
                    }
                    std::lock_guard< decltype(self->mutex) > lock(self->mutex);
                    if (self->brokenDelegate != nullptr) {
                        self->brokenDelegate(graceful);
                    }
                }
            );
        }

        // Http::Connection

        virtual std::string GetPeerId() override {
            return SystemAbstractions::sprintf(
                "%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 ":%" PRIu16,
                (uint8_t)((adaptee->GetPeerAddress() >> 24) & 0xFF),
                (uint8_t)((adaptee->GetPeerAddress() >> 16) & 0xFF),
                (uint8_t)((adaptee->GetPeerAddress() >> 8) & 0xFF),
                (uint8_t)(adaptee->GetPeerAddress() & 0xFF),
                adaptee->GetPeerPort()
            );
        }

        virtual void SetDataReceivedDelegate(DataReceivedDelegate newDataReceivedDelegate) override {
            std::lock_guard< decltype(mutex) > lock(mutex);
            dataReceivedDelegate = newDataReceivedDelegate;
        }

        virtual void SetBrokenDelegate(BrokenDelegate newBrokenDelegate) override {
            std::lock_guard< decltype(mutex) > lock(mutex);
            brokenDelegate = newBrokenDelegate;
        }

        virtual void SendData(const std::vector< uint8_t >& data) override {
            adaptee->SendMessage(data);
        }

        virtual void Break(bool clean) override {
            // TODO: If clean == true, we may need to hold off until adaptee
            // has written out all its data.
            adaptee->Close();
        }
    };

}

namespace HttpNetworkTransport {

    /**
     * This contains the private properties of a
     * HttpServerNetworkTransport instance.
     */
    struct HttpServerNetworkTransport::Impl {
        // Properties

        /**
         * This is used to implement the tranport layer.
         */
        SystemAbstractions::NetworkEndpoint endpoint;

        /**
         * This is a helper object used to generate and publish
         * diagnostic messages.
         */
        std::shared_ptr< SystemAbstractions::DiagnosticsSender > diagnosticsSender;

        // Methods

        /**
         * This is the constructor for the structure.
         */
        Impl()
            : diagnosticsSender(std::make_shared< SystemAbstractions::DiagnosticsSender >("HttpServerNetworkTransport"))
        {
        }
    };

    HttpServerNetworkTransport::~HttpServerNetworkTransport() = default;

    HttpServerNetworkTransport::HttpServerNetworkTransport()
        : impl_(new Impl)
    {
    }

    SystemAbstractions::DiagnosticsSender::UnsubscribeDelegate HttpServerNetworkTransport::SubscribeToDiagnostics(
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate delegate,
        size_t minLevel
    ) {
        return impl_->diagnosticsSender->SubscribeToDiagnostics(delegate, minLevel);
    }

    bool HttpServerNetworkTransport::BindNetwork(
        uint16_t port,
        NewConnectionDelegate newConnectionDelegate
    ) {
        return impl_->endpoint.Open(
            [
                this,
                newConnectionDelegate
            ](std::shared_ptr< SystemAbstractions::NetworkConnection > newConnection){
                const auto adapter = std::make_shared< ConnectionAdapter >();
                adapter->adaptee = newConnection;
                auto diagnosticsSender = impl_->diagnosticsSender;
                const auto boundId = SystemAbstractions::sprintf(
                    "%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 ":%" PRIu16,
                    (uint8_t)((adapter->adaptee->GetBoundAddress() >> 24) & 0xFF),
                    (uint8_t)((adapter->adaptee->GetBoundAddress() >> 16) & 0xFF),
                    (uint8_t)((adapter->adaptee->GetBoundAddress() >> 8) & 0xFF),
                    (uint8_t)(adapter->adaptee->GetBoundAddress() & 0xFF),
                    adapter->adaptee->GetBoundPort()
                );
                adapter->adaptee->SubscribeToDiagnostics(
                    [diagnosticsSender, boundId](
                        std::string senderName,
                        size_t level,
                        std::string message
                    ){
                        diagnosticsSender->SendDiagnosticInformationString(
                            level,
                            boundId + ": " + message
                        );
                    },
                    1
                );
                newConnectionDelegate(adapter);
                if (!adapter->WireUpAdaptee(adapter)) {
                    return;
                }
            },
            [this](
                uint32_t address,
                uint16_t port,
                const std::vector< uint8_t >& body
            ){
                // NOTE: This should never be called, because it's only used
                // for datagram-oriented network endpoints, and we're
                // explicitly configuring this one as a connection-oriented
                // endpoint.
            },
            SystemAbstractions::NetworkEndpoint::Mode::Connection,
            0,
            0,
            port
        );
    }

    uint16_t HttpServerNetworkTransport::GetBoundPort() {
        return impl_->endpoint.GetBoundPort();
    }

    void HttpServerNetworkTransport::ReleaseNetwork() {
        impl_->endpoint.Close();
    }

}
