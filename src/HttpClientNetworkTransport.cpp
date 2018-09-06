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
        std::shared_ptr< SystemAbstractions::NetworkConnection > adaptee;

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
        }

        virtual void SetBrokenDelegate(BrokenDelegate newBrokenDelegate) override {
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

        // Methods

        /**
         * This is the constructor for the structure.
         */
        Impl()
            : diagnosticsSender(std::make_shared< SystemAbstractions::DiagnosticsSender >("HttpClientNetworkTransport"))
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

    std::shared_ptr< Http::Connection > HttpClientNetworkTransport::Connect(
        const std::string& hostNameOrAddress,
        uint16_t port,
        Http::Connection::DataReceivedDelegate dataReceivedDelegate,
        Http::Connection::BrokenDelegate brokenDelegate
    ) {
        const auto adapter = std::make_shared< ConnectionAdapter >();
        adapter->adaptee = std::make_shared< SystemAbstractions::NetworkConnection >();
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
        if (
            !adapter->adaptee->Process(
                dataReceivedDelegate,
                brokenDelegate
            )
        ) {
            return nullptr;
        }
        return adapter;
    }

}
