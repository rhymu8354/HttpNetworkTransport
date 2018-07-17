/**
 * @file HttpServerNetworkTransportTests.cpp
 *
 * This module contains the unit tests of the
 * Http::Server class.
 *
 * © 2018 by Richard Walters
 */

#include <condition_variable>
#include <gtest/gtest.h>
#include <HttpNetworkTransport/HttpServerNetworkTransport.hpp>
#include <inttypes.h>
#include <mutex>
#include <SystemAbstractions/NetworkConnection.hpp>
#include <SystemAbstractions/StringExtensions.hpp>
#include <vector>

TEST(HttpServerNetworkTransportTests, BindNetwork) {
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    HttpNetworkTransport::HttpServerNetworkTransport transport;
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex](
                std::shared_ptr< Http::Connection > connection
            ){
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                condition.notify_all();
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    ASSERT_TRUE(
        client.Connect(
            0x7F000001,
            port
        )
    );
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&connections]{
                    return !connections.empty();
                }
            )
        );
    }
}

TEST(HttpServerNetworkTransportTests, ReleaseNetwork) {
    HttpNetworkTransport::HttpServerNetworkTransport transport;
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex](
                std::shared_ptr< Http::Connection > connection
            ){
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                condition.notify_all();
            }
        )
    );
    const auto port = transport.GetBoundPort();
    transport.ReleaseNetwork();
    SystemAbstractions::NetworkConnection client;
    ASSERT_FALSE(
        client.Connect(
            0x7F000001,
            port
        )
    );
}

TEST(HttpServerNetworkTransportTests, DataTransmissionFromClient) {
    HttpNetworkTransport::HttpServerNetworkTransport transport;
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    std::vector< uint8_t > dataReceived;
    const auto dataReceivedDelegate = [&condition, &mutex, &dataReceived](
        std::vector< uint8_t > data
    ){
        std::lock_guard< std::mutex > lock(mutex);
        dataReceived.insert(
            dataReceived.end(),
            data.begin(),
            data.end()
        );
        condition.notify_all();
    };
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex, dataReceivedDelegate](
                std::shared_ptr< Http::Connection > connection
            ){
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                connection->SetDataReceivedDelegate(dataReceivedDelegate);
                condition.notify_all();
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    ASSERT_TRUE(
        client.Process(
            [](const std::vector< uint8_t >& message){
            },
            []{
            }
        )
    );
    {
        std::unique_lock< std::mutex > lock(mutex);
        (void)condition.wait_for(
            lock,
            std::chrono::seconds(1),
            [&connections]{
                return !connections.empty();
            }
        );
    }
    ASSERT_EQ(
        SystemAbstractions::sprintf(
            "127.0.0.1:%" PRIu16,
            client.GetBoundPort()
        ),
        connections[0]->GetPeerId()
    );
    const std::string messageAsString = "Hello, World!";
    const std::vector< uint8_t > messageAsBytes(
        messageAsString.begin(),
        messageAsString.end()
    );
    client.SendMessage(messageAsBytes);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&dataReceived, &messageAsBytes]{
                    return (dataReceived.size() == messageAsBytes.size());
                }
            )
        );
    }
    ASSERT_EQ(messageAsBytes, dataReceived);
}

TEST(HttpServerNetworkTransportTests, DataTransmissionToClient) {
    HttpNetworkTransport::HttpServerNetworkTransport transport;
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    std::vector< uint8_t > dataReceived;
    const auto dataReceivedDelegate = [&condition, &mutex, &dataReceived](
        std::vector< uint8_t > data
    ){
    };
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex, dataReceivedDelegate](
                std::shared_ptr< Http::Connection > connection
            ){
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                connection->SetDataReceivedDelegate(dataReceivedDelegate);
                condition.notify_all();
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    ASSERT_TRUE(
        client.Process(
            [&dataReceived, &mutex, &condition](const std::vector< uint8_t >& message){
                std::lock_guard< std::mutex > lock(mutex);
                dataReceived.insert(
                    dataReceived.end(),
                    message.begin(),
                    message.end()
                );
                condition.notify_all();
            },
            []{
            }
        )
    );
    {
        std::unique_lock< std::mutex > lock(mutex);
        (void)condition.wait_for(
            lock,
            std::chrono::seconds(1),
            [&connections]{
                return !connections.empty();
            }
        );
    }
    const std::string messageAsString = "Hello, World!";
    const std::vector< uint8_t > messageAsBytes(
        messageAsString.begin(),
        messageAsString.end()
    );
    connections[0]->SendData(messageAsBytes);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&dataReceived, &messageAsBytes]{
                    return (dataReceived.size() == messageAsBytes.size());
                }
            )
        );
    }
    ASSERT_EQ(messageAsBytes, dataReceived);
}

TEST(HttpServerNetworkTransportTests, DataReceivedShouldNotRaceConnectionDelegate) {
    HttpNetworkTransport::HttpServerNetworkTransport transport;
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    std::vector< uint8_t > dataReceived;
    const auto dataReceivedDelegate = [&condition, &mutex, &dataReceived](
        std::vector< uint8_t > data
    ){
        std::lock_guard< std::mutex > lock(mutex);
        dataReceived.insert(
            dataReceived.end(),
            data.begin(),
            data.end()
        );
        condition.notify_all();
    };
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex, dataReceivedDelegate](
                std::shared_ptr< Http::Connection > connection
            ){
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                connection->SetDataReceivedDelegate(dataReceivedDelegate);
                condition.notify_all();
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    ASSERT_TRUE(
        client.Process(
            [](const std::vector< uint8_t >& message){
            },
            []{
            }
        )
    );
    const std::string messageAsString = "Hello, World!";
    const std::vector< uint8_t > messageAsBytes(
        messageAsString.begin(),
        messageAsString.end()
    );
    client.SendMessage(messageAsBytes);
    {
        std::unique_lock< std::mutex > lock(mutex);
        (void)condition.wait_for(
            lock,
            std::chrono::seconds(1),
            [&connections]{
                return !connections.empty();
            }
        );
    }
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&dataReceived, &messageAsBytes]{
                    return (dataReceived.size() == messageAsBytes.size());
                }
            )
        );
    }
    ASSERT_EQ(messageAsBytes, dataReceived);
}
