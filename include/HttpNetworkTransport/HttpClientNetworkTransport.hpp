#ifndef HTTP_NETWORK_TRANSPORT_HTTP_CLIENT_NETWORK_TRANSPORT_HPP
#define HTTP_NETWORK_TRANSPORT_HTTP_CLIENT_NETWORK_TRANSPORT_HPP

/**
 * @file HttpClientNetworkTransport.hpp
 *
 * This module declares the HttpNetworkTransport::HttpClientNetworkTransport
 * class.
 *
 * © 2018 by Richard Walters
 */

#include <memory>
#include <Http/ClientTransport.hpp>
#include <SystemAbstractions/DiagnosticsSender.hpp>

namespace HttpNetworkTransport {

    /**
     * This is an implementation of Http::ClientTransport
     * that uses the real network available through the operating system.
     */
    class HttpClientNetworkTransport
        : public Http::ClientTransport
    {
        // Lifecycle management
    public:
        ~HttpClientNetworkTransport() noexcept;
        HttpClientNetworkTransport(const HttpClientNetworkTransport&) = delete;
        HttpClientNetworkTransport(HttpClientNetworkTransport&&) noexcept = delete;
        HttpClientNetworkTransport& operator=(const HttpClientNetworkTransport&) = delete;
        HttpClientNetworkTransport& operator=(HttpClientNetworkTransport&&) noexcept = delete;

        // Public methods
    public:
        /**
         * This is the default constructor.
         */
        HttpClientNetworkTransport();

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

        // Http::ClientTransport
    public:
        virtual std::shared_ptr< Http::Connection > Connect(
            const std::string& hostNameOrAddress,
            uint16_t port,
            Http::Connection::DataReceivedDelegate dataReceivedDelegate,
            Http::Connection::BrokenDelegate brokenDelegate
        ) override;

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
        std::unique_ptr< Impl > impl_;
    };

}

#endif /* HTTP_NETWORK_TRANSPORT_HTTP_CLIENT_NETWORK_TRANSPORT_HPP */
