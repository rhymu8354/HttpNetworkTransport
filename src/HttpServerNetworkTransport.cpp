/**
 * @file HttpServerNetworkTransport.cpp
 *
 * This module contains the implementation of the
 * HttpNetworkTransport::HttpServerNetworkTransport class.
 *
 * © 2018 by Richard Walters
 */

#include <HttpNetworkTransport/HttpServerNetworkTransport.hpp>
#include <SystemAbstractions/NetworkEndpoint.hpp>

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
         * @return
         *     An indication of whether or not the method was
         *     successful is returned.
         */
        bool WireUpAdaptee() {
            return adaptee->Process(
                [this](const std::vector< uint8_t >& message){
                    if (dataReceivedDelegate != nullptr) {
                        dataReceivedDelegate(message);
                    }
                },
                [this]{
                    if (brokenDelegate != nullptr) {
                        brokenDelegate();
                    }
                }
            );
        }

        // Http::Connection

        virtual void SetDataReceivedDelegate(DataReceivedDelegate newDataReceivedDelegate) override {
            dataReceivedDelegate = newDataReceivedDelegate;
        }

        virtual void SetBrokenDelegate(BrokenDelegate newBrokenDelegate) override {
            brokenDelegate = newBrokenDelegate;
        }

        virtual void SendData(std::vector< uint8_t > data) override {
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

        // Methods
    };

    HttpServerNetworkTransport::~HttpServerNetworkTransport() = default;

    HttpServerNetworkTransport::HttpServerNetworkTransport()
        : impl_(new Impl)
    {
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
                if (!adapter->WireUpAdaptee()) {
                    return;
                }
                newConnectionDelegate(adapter);
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
