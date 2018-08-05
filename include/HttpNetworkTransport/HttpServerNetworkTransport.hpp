#ifndef HTTP_NETWORK_TRANSPORT_HTTP_SERVER_NETWORK_TRANSPORT_HPP
#define HTTP_NETWORK_TRANSPORT_HTTP_SERVER_NETWORK_TRANSPORT_HPP

/**
 * @file HttpServerNetworkTransport.hpp
 *
 * This module declares the HttpNetworkTransport::HttpServerNetworkTransport
 * class.
 *
 * © 2018 by Richard Walters
 */

#include <memory>
#include <Http/ServerTransport.hpp>
#include <SystemAbstractions/DiagnosticsSender.hpp>

namespace HttpNetworkTransport {

    /**
     * This is an implementation of Http::ServerTransport
     * that uses the real network available through the operating system.
     */
    class HttpServerNetworkTransport
        : public Http::ServerTransport
    {
        // Lifecycle management
    public:
        ~HttpServerNetworkTransport();
        HttpServerNetworkTransport(const HttpServerNetworkTransport&) = delete;
        HttpServerNetworkTransport(HttpServerNetworkTransport&&) = delete;
        HttpServerNetworkTransport& operator=(const HttpServerNetworkTransport&) = delete;
        HttpServerNetworkTransport& operator=(HttpServerNetworkTransport&&) = delete;

        // Public methods
    public:
        /**
         * This is the default constructor.
         */
        HttpServerNetworkTransport();

        /**
         * This method forms a new subscription to diagnostic
         * messages published by the transport.
         *
         * @param[in] delegate
         *     This is the function to call to deliver messages
         *     to the subscriber.
         *
         * @param[in] minLevel
         *     This is the minimum level of message that this subscriber
         *     desires to receive.
         *
         * @return
         *     A function is returned which may be called
         *     to terminate the subscription.
         */
        SystemAbstractions::DiagnosticsSender::UnsubscribeDelegate SubscribeToDiagnostics(
            SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate delegate,
            size_t minLevel = 0
        );

        // Http::ServerTransport
    public:
        virtual bool BindNetwork(
            uint16_t port,
            NewConnectionDelegate newConnectionDelegate
        ) override;
        virtual uint16_t GetBoundPort() override;
        virtual void ReleaseNetwork() override;

        // Private properties
    private:
        /**
         * This is the type of structure that contains the private
         * properties of the instance.  It is defined in the implementation
         * and declared here to ensure that it is scoped inside the class.
         */
        struct Impl;

        /**
         * This contains the private properties of the instance.
         */
        std::unique_ptr< struct Impl > impl_;
    };

}

#endif /* HTTP_NETWORK_TRANSPORT_HTTP_SERVER_NETWORK_TRANSPORT_HPP */
