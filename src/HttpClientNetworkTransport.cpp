/**
 * @file HttpClientNetworkTransport.cpp
 *
 * This module contains the implementation of the
 * HttpNetworkTransport::HttpClientNetworkTransport class.
 *
 * © 2018 by Richard Walters
 */

#include <HttpNetworkTransport/HttpClientNetworkTransport.hpp>
#include <inttypes.h>
#include <mutex>
#include <SystemAbstractions/DiagnosticsSender.hpp>
#include <SystemAbstractions/NetworkConnection.hpp>
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
     *
     * @note
     *     A different connection object type can be used if the user
     *     sets their own custom connection factory function via
     *     the SetConnectionFactory method.
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
     * HttpClientNetworkTransport instance.
     */
    struct HttpClientNetworkTransport::Impl {
        // Properties

        /**
         * This is a helper object used to generate and publish
         * diagnostic messages.
         */
        std::shared_ptr< SystemAbstractions::DiagnosticsSender > diagnosticsSender;

        /**
         * This function is used to create new network connections.
         */
        ConnectionFactoryFunction connectionFactory;

        // Methods

        /**
         * This is the constructor for the structure.
         */
        Impl()
            : diagnosticsSender(std::make_shared< SystemAbstractions::DiagnosticsSender >("HttpClientNetworkTransport"))
            , connectionFactory(
                [](const std::string&){
                    const auto connection = std::make_shared< SystemAbstractions::NetworkConnection >();
                    return connection;
                }
            )
        {
        }
    };

    HttpClientNetworkTransport::~HttpClientNetworkTransport() noexcept = default;

    HttpClientNetworkTransport::HttpClientNetworkTransport()
        : impl_(new Impl)
    {
    }

    SystemAbstractions::DiagnosticsSender::UnsubscribeDelegate HttpClientNetworkTransport::SubscribeToDiagnostics(
        SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate delegate,
        size_t minLevel
    ) {
        return impl_->diagnosticsSender->SubscribeToDiagnostics(delegate, minLevel);
    }

    void HttpClientNetworkTransport::SetConnectionFactory(ConnectionFactoryFunction connectionFactory) {
        impl_->connectionFactory = connectionFactory;
    }

    std::shared_ptr< Http::Connection > HttpClientNetworkTransport::Connect(
        const std::string& hostNameOrAddress,
        uint16_t port,
        Http::Connection::DataReceivedDelegate dataReceivedDelegate,
        Http::Connection::BrokenDelegate brokenDelegate
    ) {
        const auto adapter = std::make_shared< ConnectionAdapter >();
        adapter->adaptee = impl_->connectionFactory(hostNameOrAddress);
        if (adapter->adaptee == nullptr) {
            impl_->diagnosticsSender->SendDiagnosticInformationFormatted(
                SystemAbstractions::DiagnosticsSender::Levels::ERROR,
                "unable to construct connection to '%s:%" PRIu16 "'",
                hostNameOrAddress.c_str(),
                port
            );
            return nullptr;
        }
        auto diagnosticsSender = impl_->diagnosticsSender;
        adapter->adaptee->SubscribeToDiagnostics(
            [diagnosticsSender](
                std::string senderName,
                size_t level,
                std::string message
            ){
                diagnosticsSender->SendDiagnosticInformationString(
                    level,
                    senderName + ": " + message
                );
            },
            1
        );
        const uint32_t address = SystemAbstractions::NetworkConnection::GetAddressOfHost(hostNameOrAddress);
        if (address == 0) {
            return nullptr;
        }
        if (!adapter->adaptee->Connect(address, port)) {
            return nullptr;
        }
        adapter->delegates->dataReceivedDelegate = dataReceivedDelegate;
        adapter->delegates->brokenDelegate = brokenDelegate;
        const auto delegatesCopy = adapter->delegates;
        if (
            !adapter->adaptee->Process(
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
            )
        ) {
            return nullptr;
        }
        return adapter;
    }

}
