/**
 * @file HttpServerNetworkTransportTests.cpp
 *
 * This module contains the unit tests of the
 * Http::Server class.
 *
 * © 2018 by Richard Walters
 */

#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <HttpNetworkTransport/HttpServerNetworkTransport.hpp>
#include <inttypes.h>
#include <mutex>
#include <SystemAbstractions/NetworkConnection.hpp>
#include <SystemAbstractions/StringExtensions.hpp>
#include <thread>
#include <vector>

namespace {

    /**
     * This is a substitute for a real connection, and used to test
     * the SetConnectionDecoratorFactory method of HttpServerNetworkTransport.
     */
    struct MockConnection
        : public SystemAbstractions::INetworkConnection
    {
        // Properties

        /**
         * This holds a copy of the last message sent through the connection.
         */
        std::vector< uint8_t > messageSent;

        /**
         * This is used to coordinate events between threads.
         */
        std::condition_variable condition;

        /**
         * This is used to synchronize access to the object.
         */
        std::mutex mutex;

        /**
         * This flag indicates whether or not the Process method
         * was called.
         */
        bool processCalled = false;

        /**
         * This flag indicates whether or not the Process method
         * should return an indication of success.
         */
        bool processShouldSucceed = true;

        /**
         * This flag indicates whether or not the Close method was called.
         */
        bool closeCalled = false;

        /**
         * This flag indicates the value of the "clean" parameter
         * given to the last call to the Close method.
         */
        bool closedCleanly = false;

        // Methods

        /**
         * This method waits up to a reasonable amount of time for
         * a message to be sent through the connection.
         *
         * @return
         *     An indication of whether or not a message was sent
         *     through the connection within a reasonable amount
         *     of time is returned.
         */
        bool AwaitSendMessage() {
            std::unique_lock< decltype(mutex) > lock(mutex);
            return condition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this]{ return !messageSent.empty(); }
            );
        }

        /**
         * This method waits up to a reasonable amount of time for
         * the connection to be closed.
         *
         * @return
         *     An indication of whether or not the connection was closed
         *     within a reasonable amount of time is returned.
         */
        bool AwaitClose() {
            std::unique_lock< decltype(mutex) > lock(mutex);
            return condition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [this]{ return closeCalled; }
            );
        }

        // SystemAbstractions::INetworkConnection

        virtual SystemAbstractions::DiagnosticsSender::UnsubscribeDelegate SubscribeToDiagnostics(
            SystemAbstractions::DiagnosticsSender::DiagnosticMessageDelegate delegate,
            size_t minLevel = 0
        ) override {
            return []{};
        }

        virtual bool Connect(uint32_t peerAddress, uint16_t peerPort) override {
            return true;
        }

        virtual bool Process(
            MessageReceivedDelegate messageReceivedDelegate,
            BrokenDelegate brokenDelegate
        ) override {
            processCalled = true;
            return processShouldSucceed;
        }

        virtual uint32_t GetPeerAddress() const override{
            return 0;
        }

        virtual uint16_t GetPeerPort() const override {
            return 0;
        }

        virtual bool IsConnected() const override {
            return !closeCalled;
        }

        virtual uint32_t GetBoundAddress() const override {
            return 0;
        }

        virtual uint16_t GetBoundPort() const override {
            return 0;
        }

        virtual void SendMessage(const std::vector< uint8_t >& message) override {
            std::lock_guard< decltype(mutex) > lock(mutex);
            messageSent = message;
            condition.notify_all();
        }

        virtual void Close(bool clean = false) override {
            std::lock_guard< decltype(mutex) > lock(mutex);
            closeCalled = true;
            closedCleanly = clean;
            condition.notify_all();
        }
    };

}

/**
 * This is the test fixture for these tests, providing common
 * setup and teardown for each test.
 */
struct HttpServerNetworkTransportTests
    : public ::testing::Test
{
    // Properties

    /**
     * This is the unit under test.
     */
    HttpNetworkTransport::HttpServerNetworkTransport transport;

    /**
     * This flag is used to tell the test fixture if we
     * moved the unit under test.
     */
    bool transportWasMoved = false;

    /**
     * These are the diagnostic messages that have been
     * received from the unit under test.
     */
    std::vector< std::string > diagnosticMessages;

    /**
     * This is the delegate obtained when subscribing
     * to receive diagnostic messages from the unit under test.
     * It's called to terminate the subscription.
     */
    SystemAbstractions::DiagnosticsSender::UnsubscribeDelegate diagnosticsUnsubscribeDelegate;

    /**
     * If this flag is set, we will print all received diagnostic
     * messages, in addition to storing them.
     */
    bool printDiagnosticMessages = false;

    // Methods

    // ::testing::Test

    virtual void SetUp() {
        diagnosticsUnsubscribeDelegate = transport.SubscribeToDiagnostics(
            [this](
                std::string senderName,
                size_t level,
                std::string message
            ){
                diagnosticMessages.push_back(
                    SystemAbstractions::sprintf(
                        "%s[%zu]: %s",
                        senderName.c_str(),
                        level,
                        message.c_str()
                    )
                );
                if (printDiagnosticMessages) {
                    printf(
                        "%s[%zu]: %s\n",
                        senderName.c_str(),
                        level,
                        message.c_str()
                    );
                }
            },
            1
        );
    }

    virtual void TearDown() {
        if (!transportWasMoved) {
            diagnosticsUnsubscribeDelegate();
            transport.ReleaseNetwork();
        }
    }
};

TEST_F(HttpServerNetworkTransportTests, BindNetwork) {
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
                return nullptr;
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

TEST_F(HttpServerNetworkTransportTests, ReleaseNetwork) {
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
                return nullptr;
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

TEST_F(HttpServerNetworkTransportTests, DataTransmissionFromClient) {
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
                return nullptr;
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    ASSERT_TRUE(
        client.Process(
            [](const std::vector< uint8_t >& message){},
            [](bool){}
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
        "127.0.0.1",
        connections[0]->GetPeerAddress()
    );
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

TEST_F(HttpServerNetworkTransportTests, DataTransmissionToClient) {
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    std::vector< uint8_t > dataReceived;
    const auto dataReceivedDelegate = [](
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
                return nullptr;
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
            [](bool){}
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

TEST_F(HttpServerNetworkTransportTests, DataReceivedShouldNotRaceConnectionDelegate) {
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
                return nullptr;
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    ASSERT_TRUE(
        client.Process(
            [](const std::vector< uint8_t >& message){},
            [](bool){}
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

TEST_F(HttpServerNetworkTransportTests, ClientBrokenAbruptly) {
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    bool broken = false;
    const auto brokenDelegate = [&condition, &mutex, &broken](bool){
        std::lock_guard< std::mutex > lock(mutex);
        broken = true;
        condition.notify_all();
    };
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex, brokenDelegate](
                std::shared_ptr< Http::Connection > connection
            ){
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                connection->SetBrokenDelegate(brokenDelegate);
                condition.notify_all();
                return nullptr;
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    ASSERT_TRUE(
        client.Process(
            [](const std::vector< uint8_t >& message){},
            [](bool){}
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
    diagnosticMessages.clear();
    client.Close(false);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&broken]{ return broken; }
            )
        );
    }
    const auto clientSideId = SystemAbstractions::sprintf(
        "127.0.0.1:%" PRIu16,
        client.GetBoundPort()
    );
    ASSERT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: connection closed abruptly by peer",
                clientSideId.c_str()
            ),
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: closed connection",
                clientSideId.c_str()
            ),
        }),
        diagnosticMessages
    );
}

TEST_F(HttpServerNetworkTransportTests, ClientBrokenGracefullyServerClosesAbruptly) {
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    bool broken = false;
    const auto brokenDelegate = [&condition, &mutex, &broken](bool){
        std::lock_guard< std::mutex > lock(mutex);
        broken = true;
        condition.notify_all();
    };
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex, brokenDelegate](
                std::shared_ptr< Http::Connection > connection
            ){
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                connection->SetBrokenDelegate(brokenDelegate);
                condition.notify_all();
                return nullptr;
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    ASSERT_TRUE(
        client.Process(
            [](const std::vector< uint8_t >& message){},
            [](bool){}
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
    diagnosticMessages.clear();
    client.Close(true);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(10),
                [&broken]{ return broken; }
            )
        );
    }
    const auto clientSideId = SystemAbstractions::sprintf(
        "127.0.0.1:%" PRIu16,
        client.GetBoundPort()
    );
    ASSERT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: connection closed gracefully by peer",
                clientSideId.c_str()
            ),
        }),
        diagnosticMessages
    );
    diagnosticMessages.clear();
    connections[0]->Break(false);
    ASSERT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: closed connection",
                clientSideId.c_str()
            ),
        }),
        diagnosticMessages
    );
}

TEST_F(HttpServerNetworkTransportTests, ClientBrokenGracefullyServerClosesGracefully) {
    std::promise< void > gracefullyBroken;
    std::promise< void > finallyBroken;
    const auto brokenDelegate = [
        &gracefullyBroken,
        &finallyBroken
    ](bool graceful){
        if (graceful) {
            gracefullyBroken.set_value();
        } else {
            finallyBroken.set_value();
        }
    };
    std::promise< std::shared_ptr< Http::Connection > > connectionPromise;
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connectionPromise, brokenDelegate](
                std::shared_ptr< Http::Connection > connection
            ){
                connection->SetBrokenDelegate(brokenDelegate);
                connectionPromise.set_value(connection);
                return nullptr;
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    const auto processResult = client.Process(
        [](const std::vector< uint8_t >& message){},
        [](bool){}
    );
    ASSERT_TRUE(processResult);
    const auto connection = connectionPromise.get_future().get();
    diagnosticMessages.clear();
    client.Close(true);
    auto wasGracefullyBroken = gracefullyBroken.get_future();
    ASSERT_EQ(
        std::future_status::ready,
        wasGracefullyBroken.wait_for(std::chrono::seconds(1))
    );
    const auto clientSideId = SystemAbstractions::sprintf(
        "127.0.0.1:%" PRIu16,
        client.GetBoundPort()
    );
    ASSERT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: connection closed gracefully by peer",
                clientSideId.c_str()
            ),
        }),
        diagnosticMessages
    );
    diagnosticMessages.clear();
    connection->Break(true);
    auto wasFinallyBroken = finallyBroken.get_future();
    EXPECT_EQ(
        std::future_status::ready,
        wasFinallyBroken.wait_for(std::chrono::milliseconds(100))
    );
    ASSERT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: closing connection",
                clientSideId.c_str()
            ),
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: closed connection",
                clientSideId.c_str()
            ),
        }),
        diagnosticMessages
    );
}

TEST_F(HttpServerNetworkTransportTests, ServerBrokenAbruptly) {
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    bool connectionReady = false;
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex, &connectionReady](
                std::shared_ptr< Http::Connection > connection
            ){
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                condition.notify_all();
                return [&condition, &mutex, &connectionReady]{
                    std::lock_guard< std::mutex > lock(mutex);
                    connectionReady = true;
                    condition.notify_all();
                };
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    bool broken = false;
    bool brokenGracefully = false;
    const auto brokenDelegate = [&condition, &mutex, &broken, &brokenGracefully](bool graceful){
        std::lock_guard< std::mutex > lock(mutex);
        broken = true;
        brokenGracefully = graceful;
        condition.notify_all();
    };
    ASSERT_TRUE(
        client.Process(
            [](const std::vector< uint8_t >& message){},
            brokenDelegate
        )
    );
    {
        std::unique_lock< std::mutex > lock(mutex);
        (void)condition.wait_for(
            lock,
            std::chrono::seconds(1),
            [&connectionReady]{
                return connectionReady;
            }
        );
    }
    diagnosticMessages.clear();
    connections[0]->Break(false);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&broken]{ return broken; }
            )
        );
    }
    EXPECT_FALSE(brokenGracefully);
    const auto clientSideId = SystemAbstractions::sprintf(
        "127.0.0.1:%" PRIu16,
        client.GetBoundPort()
    );
    ASSERT_GE(diagnosticMessages.size(), 1);
    EXPECT_EQ(
        SystemAbstractions::sprintf(
            "HttpServerNetworkTransport[1]: %s: closed connection",
            clientSideId.c_str()
        ),
        diagnosticMessages[0]
    );
}

TEST_F(HttpServerNetworkTransportTests, ServerBrokenGracefullyClientClosesGracefully) {
    std::condition_variable condition;
    std::mutex mutex;
    bool connectionReady = false;
    std::vector< bool > serverBreaks;
    std::vector< std::shared_ptr< Http::Connection > > connections;
    const auto serverBrokenDelegate = [&condition, &mutex, &serverBreaks](bool graceful){
        std::lock_guard< std::mutex > lock(mutex);
        serverBreaks.push_back(graceful);
        condition.notify_all();
    };
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex, &connectionReady, serverBrokenDelegate](
                std::shared_ptr< Http::Connection > connection
            ){
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                condition.notify_all();
                connection->SetBrokenDelegate(serverBrokenDelegate);
                return [&condition, &mutex, &connectionReady]{
                    std::lock_guard< std::mutex > lock(mutex);
                    connectionReady = true;
                    condition.notify_all();
                };
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    bool clientBroken = false;
    bool clientBrokenGracefully = false;
    const auto clientBrokenDelegate = [&condition, &mutex, &clientBroken, &clientBrokenGracefully](bool graceful){
        std::lock_guard< std::mutex > lock(mutex);
        clientBroken = true;
        clientBrokenGracefully = graceful;
        condition.notify_all();
    };
    ASSERT_TRUE(
        client.Process(
            [](const std::vector< uint8_t >& message){},
            clientBrokenDelegate
        )
    );
    {
        std::unique_lock< std::mutex > lock(mutex);
        (void)condition.wait_for(
            lock,
            std::chrono::seconds(1),
            [&connectionReady]{
                return connectionReady;
            }
        );
    }
    diagnosticMessages.clear();
    connections[0]->Break(true);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&clientBroken]{ return clientBroken; }
            )
        );
    }
    EXPECT_TRUE(clientBrokenGracefully);
    const auto clientSideId = SystemAbstractions::sprintf(
        "127.0.0.1:%" PRIu16,
        client.GetBoundPort()
    );
    ASSERT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: closing connection",
                clientSideId.c_str()
            ),
        }),
        diagnosticMessages
    );
    diagnosticMessages.clear();
    client.Close(true);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&serverBreaks]{ return (serverBreaks.size() == 2); }
            )
        );
        EXPECT_TRUE(serverBreaks[0]);
        EXPECT_FALSE(serverBreaks[1]);
    }
    ASSERT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: connection closed gracefully by peer",
                clientSideId.c_str()
            ),
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: closed connection",
                clientSideId.c_str()
            ),
        }),
        diagnosticMessages
    );
}

TEST_F(HttpServerNetworkTransportTests, ServerBrokenGracefullyClientClosesAbruptly) {
    std::vector< std::shared_ptr< Http::Connection > > connections;
    std::condition_variable condition;
    std::mutex mutex;
    bool connectionReady = false;
    std::vector< bool > serverBreaks;
    const auto serverBrokenDelegate = [&condition, &mutex, &serverBreaks](bool graceful){
        std::lock_guard< std::mutex > lock(mutex);
        serverBreaks.push_back(graceful);
        condition.notify_all();
    };
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [&connections, &condition, &mutex, &connectionReady, serverBrokenDelegate](
                std::shared_ptr< Http::Connection > connection
            ){
                std::lock_guard< std::mutex > lock(mutex);
                connections.push_back(connection);
                condition.notify_all();
                connection->SetBrokenDelegate(serverBrokenDelegate);
                return [&condition, &mutex, &connectionReady]{
                    std::lock_guard< std::mutex > lock(mutex);
                    connectionReady = true;
                    condition.notify_all();
                };
            }
        )
    );
    const auto port = transport.GetBoundPort();
    SystemAbstractions::NetworkConnection client;
    (void)client.Connect(0x7F000001, port);
    bool clientBroken = false;
    bool clientBrokenGracefully = false;
    const auto clientBrokenDelegate = [&condition, &mutex, &clientBroken, &clientBrokenGracefully](bool graceful){
        std::lock_guard< std::mutex > lock(mutex);
        clientBroken = true;
        clientBrokenGracefully = graceful;
        condition.notify_all();
    };
    ASSERT_TRUE(
        client.Process(
            [](const std::vector< uint8_t >& message){},
            clientBrokenDelegate
        )
    );
    {
        std::unique_lock< std::mutex > lock(mutex);
        (void)condition.wait_for(
            lock,
            std::chrono::seconds(1),
            [&connectionReady]{
                return connectionReady;
            }
        );
    }
    diagnosticMessages.clear();
    connections[0]->Break(true);
    {
        std::unique_lock< std::mutex > lock(mutex);
        ASSERT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&clientBroken]{ return clientBroken; }
            )
        );
    }
    EXPECT_TRUE(clientBrokenGracefully);
    const auto clientSideId = SystemAbstractions::sprintf(
        "127.0.0.1:%" PRIu16,
        client.GetBoundPort()
    );
    ASSERT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: closing connection",
                clientSideId.c_str()
            ),
        }),
        diagnosticMessages
    );
    diagnosticMessages.clear();
    client.Close(false);
    {
        std::unique_lock< std::mutex > lock(mutex);
        EXPECT_TRUE(
            condition.wait_for(
                lock,
                std::chrono::seconds(1),
                [&serverBreaks]{ return (serverBreaks.size() == 1); }
            )
        );
        EXPECT_FALSE(serverBreaks[0]);
    }
    ASSERT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: connection closed abruptly by peer",
                clientSideId.c_str()
            ),
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[1]: %s: closed connection",
                clientSideId.c_str()
            ),
        }),
        diagnosticMessages
    );
}

TEST_F(HttpServerNetworkTransportTests, SetConnectionDecoratorFactory) {
    const auto decorator = std::make_shared< MockConnection >();
    std::shared_ptr< SystemAbstractions::INetworkConnection > connectionAdapted;
    transport.SetConnectionDecoratorFactory(
        [
            decorator,
            &connectionAdapted
        ](
            std::shared_ptr< SystemAbstractions::INetworkConnection > connection
        ){
            connectionAdapted = connection;
            return decorator;
        }
    );
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
                return nullptr;
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
    const std::string messageAsString = "Hello, World!";
    const std::vector< uint8_t > messageAsBytes(
        messageAsString.begin(),
        messageAsString.end()
    );
    connections[0]->SendData(messageAsBytes);
    EXPECT_TRUE(decorator->AwaitSendMessage());
    ASSERT_EQ(messageAsBytes, decorator->messageSent);
}

TEST_F(HttpServerNetworkTransportTests, ConnectionDecoratorFactoryReturnsNullptr) {
    transport.SetConnectionDecoratorFactory(
        [](
            std::shared_ptr< SystemAbstractions::INetworkConnection > connection
        ){
            return nullptr;
        }
    );
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
                return nullptr;
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
        EXPECT_FALSE(
            condition.wait_for(
                lock,
                std::chrono::milliseconds(100),
                [&connections]{
                    return !connections.empty();
                }
            )
        );
    }
    EXPECT_EQ(
        (std::vector< std::string >{
            SystemAbstractions::sprintf(
                "HttpServerNetworkTransport[10]: unable to construct decorator for connection from '127.0.0.1:%" PRIu16 "'",
                client.GetBoundPort()
            )
        }),
        diagnosticMessages
    );
}

TEST_F(HttpServerNetworkTransportTests, CloseSentUponFailureToWireUpAdaptee) {
    const auto decorator = std::make_shared< MockConnection >();
    decorator->processShouldSucceed = false;
    std::shared_ptr< SystemAbstractions::INetworkConnection > connectionAdapted;
    transport.SetConnectionDecoratorFactory(
        [
            decorator,
            &connectionAdapted
        ](
            std::shared_ptr< SystemAbstractions::INetworkConnection > connection
        ){
            connectionAdapted = connection;
            return decorator;
        }
    );
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
                return nullptr;
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
    ASSERT_TRUE(decorator->AwaitClose());
    EXPECT_FALSE(decorator->closedCleanly);
}

TEST_F(HttpServerNetworkTransportTests, DoNotProcessConnectionIfClosedDuringNewConnectionDelegate) {
    const auto decorator = std::make_shared< MockConnection >();
    transport.SetConnectionDecoratorFactory(
        [
            decorator
        ](
            std::shared_ptr< SystemAbstractions::INetworkConnection > connection
        ){
            return decorator;
        }
    );
    ASSERT_TRUE(
        transport.BindNetwork(
            0,
            [](
                std::shared_ptr< Http::Connection > connection
            ){
                connection->Break(false);
                return nullptr;
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
    transport.ReleaseNetwork();
    EXPECT_FALSE(decorator->processCalled);
}
