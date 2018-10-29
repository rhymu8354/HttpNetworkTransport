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
     * This is used to make the setting and usage of the delegates
     * in ConnectionAdapter thread-safe.
     */
    struct ConnectionDelegates {
        /**
         * This is used to synchronize access to the delegates.
         */
        std::recursive_mutex mutex;

        /**
         * This is the delegate to call whenever data is recevied
         * from the remote peer.
         */
        Http::Connection::DataReceivedDelegate dataReceivedDelegate;

        /**
         * This is the delegate to call whenever the connection
         * has been broken.
         */
        Http::Connection::BrokenDelegate brokenDelegate;
    };

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
         * This is the object which is implementing the network
         * connection in terms of the operating system's network APIs.
         */
        std::shared_ptr< SystemAbstractions::INetworkConnection > adaptee;

        /**
         * This holds onto the user's delegate and makes their setting
         * and usage thread-safe.
         */
        std::shared_ptr< ConnectionDelegates > delegates = std::make_shared< ConnectionDelegates >();

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
        bool WireUpAdaptee() {
            const auto delegatesCopy = delegates;
            return adaptee->Process(
                [delegatesCopy](const std::vector< uint8_t >& message){
                    Http::Connection::DataReceivedDelegate dataReceivedDelegate;
                    {
                        std::lock_guard< decltype(delegatesCopy->mutex) > lock(delegatesCopy->mutex);
                        dataReceivedDelegate = delegatesCopy->dataReceivedDelegate;
                    }
                    if (dataReceivedDelegate != nullptr) {
                        dataReceivedDelegate(message);
                    }
                },
                [delegatesCopy](bool graceful){
                    Http::Connection::BrokenDelegate brokenDelegate;
                    {
                        std::lock_guard< decltype(delegatesCopy->mutex) > lock(delegatesCopy->mutex);
                        brokenDelegate = delegatesCopy->brokenDelegate;
                    }
                    if (brokenDelegate != nullptr) {
                        brokenDelegate(graceful);
                    }
                }
            );
        }

        // Http::Connection

        virtual std::string GetPeerAddress() override {
            return SystemAbstractions::sprintf(
                "%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 ,
                (uint8_t)((adaptee->GetPeerAddress() >> 24) & 0xFF),
                (uint8_t)((adaptee->GetPeerAddress() >> 16) & 0xFF),
                (uint8_t)((adaptee->GetPeerAddress() >> 8) & 0xFF),
                (uint8_t)(adaptee->GetPeerAddress() & 0xFF)
            );
        }

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
            std::lock_guard< decltype(delegates->mutex) > lock(delegates->mutex);
            delegates->dataReceivedDelegate = newDataReceivedDelegate;
        }

        virtual void SetBrokenDelegate(BrokenDelegate newBrokenDelegate) override {
            std::lock_guard< decltype(delegates->mutex) > lock(delegates->mutex);
            delegates->brokenDelegate = newBrokenDelegate;
        }

        virtual void SendData(const std::vector< uint8_t >& data) override {
            adaptee->SendMessage(data);
        }

        virtual void Break(bool clean) override {
            adaptee->Close(clean);
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

        /**
         * This function is used to decorate new network connections.
         */
        ConnectionDecoratorFactoryFunction connectionDecoratorFactory;

        // Methods

        /**
         * This is the constructor for the structure.
         */
        Impl()
            : diagnosticsSender(std::make_shared< SystemAbstractions::DiagnosticsSender >("HttpServerNetworkTransport"))
            , connectionDecoratorFactory(
                [](std::shared_ptr< SystemAbstractions::INetworkConnection > connection){
                    return connection;
                }
            )
        {
        }
    };

    HttpServerNetworkTransport::~HttpServerNetworkTransport() noexcept = default;

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

    void HttpServerNetworkTransport::SetConnectionDecoratorFactory(ConnectionDecoratorFactoryFunction connectionDecoratorFactory) {
        impl_->connectionDecoratorFactory = connectionDecoratorFactory;
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
                const auto peerId = SystemAbstractions::sprintf(
                    "%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 ":%" PRIu16,
                    (uint8_t)((newConnection->GetPeerAddress() >> 24) & 0xFF),
                    (uint8_t)((newConnection->GetPeerAddress() >> 16) & 0xFF),
                    (uint8_t)((newConnection->GetPeerAddress() >> 8) & 0xFF),
                    (uint8_t)(newConnection->GetPeerAddress() & 0xFF),
                    newConnection->GetPeerPort()
                );
                const auto newDecoratedConnection = impl_->connectionDecoratorFactory(newConnection);
                if (newDecoratedConnection == nullptr) {
                    impl_->diagnosticsSender->SendDiagnosticInformationFormatted(
                        SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                        "unable to construct decorator for connection from '%s'",
                        peerId.c_str()
                    );
                    return;
                }
                const auto adapter = std::make_shared< ConnectionAdapter >();
                adapter->adaptee = newDecoratedConnection;
                auto diagnosticsSender = impl_->diagnosticsSender;
                adapter->adaptee->SubscribeToDiagnostics(
                    [diagnosticsSender, peerId](
                        std::string senderName,
                        size_t level,
                        std::string message
                    ){
                        diagnosticsSender->SendDiagnosticInformationString(
                            level,
                            peerId + ": " + message
                        );
                    },
                    1
                );
                /*
                 * NOTE: It's important to announce the new connection first,
                 * before wiring up the adaptee, so that the new connection
                 * delegate has the opportunity to register its delegates
                 * before the adapter receives callbacks from the adaptee.
                 * Otherwise, callbacks may be lost, if they happen before
                 * the new connection delegate registers to receive them.
                 */
                const auto readyDelegate = newConnectionDelegate(adapter);
                if (!adapter->WireUpAdaptee()) {
                    adapter->adaptee->Close(false);
                    return;
                }
                if (readyDelegate != nullptr) {
                    readyDelegate();
                }
            },
            [](
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
