/**
 * @file HttpClientNetworkTransportTests.cpp
 *
 * This module contains the unit tests of the
 * HttpNetworkTransport::HttpClientNetworkTransport class.
 *
 * © 2018 by Richard Walters
 */

#include <condition_variable>
#include <gtest/gtest.h>
#include <HttpNetworkTransport/HttpClientNetworkTransport.hpp>
#include <inttypes.h>
#include <mutex>
#include <stdint.h>
#include <SystemAbstractions/NetworkEndpoint.hpp>
#include <SystemAbstractions/StringExtensions.hpp>
#include <thread>
#include <vector>

namespace {

    /**
     * This holds information about one client that is connected
     * to the server used in the text fixture for these tests.
     */
    struct Client {
        /**
         * This is the server end of the connection between the unit under
         * test and the server.
         */
        std::shared_ptr< SystemAbstractions::NetworkConnection > connection;

        /**
         * This holds any data received from the client.
         */
        std::vector< uint8_t > dataReceived;

        /**
         * This flag indicates whether or not the connection to the client
         * was broken by the client.
         */
        bool broken = false;
    };

}

/**
 * This is the test fixture for these tests, providing common
 * setup and teardown for each test.
 */
struct HttpClientNetworkTransportTests
    : public ::testing::Test
{
    // Properties

    /**
     * This is the unit under test.
     */
    HttpNetworkTransport::HttpClientNetworkTransport transport;

    /**
     * This is a real network server used to test that the unit under test
     * can actually connect to a real server.
     */
    SystemAbstractions::NetworkEndpoint server;

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

    /**
     * This collects information about any connections
     * established (presumably by the unit under test) to the server.
     */
    std::vector< Client > clients;

    /**
     * This holds any data received from the server.
     */
    std::vector< uint8_t > dataReceived;

    /**
     * This flag indicates whether or not the connection to the server
     * was broken by the server.
     */
    bool broken = false;

    /**
     * This is the delegate, to provide to the unit under test,
     * which should be called when data arrives from the server.
     */
    Http::Connection::DataReceivedDelegate dataReceivedDelegate;

    /**
     * This is the delegate, to provide to the unit under test,
     * which should be called when the connection to the server is broken
     * by the server.
     */
    Http::Connection::BrokenDelegate brokenDelegate;

    /**
     * This is used to wake up threads which may be waiting for some
     * state in the fixture to be changed.
     */
    std::condition_variable_any waitCondition;

    /**
     * This is used to synchronize access to the object.
     */
    std::mutex mutex;

    // Methods

    /**
     * This method waits for the given number of connections to be established
     * with the server.
     *
     * @param[in] numConnections
     *     This is the number of connections to await.
     *
     * @return
     *     An indication of whether or not the given number of connections
     *     were established with the server before a reasonable amount of
     *     time has elapsed is returned.
     */
    bool AwaitConnections(size_t numConnections) {
        std::unique_lock< std::mutex > lock(mutex);
        return waitCondition.wait_for(
            lock,
            std::chrono::seconds(1),
            [this, numConnections]{
                return (clients.size() >= numConnections);
            }
        );
    }

    /**
     * This method waits for the server to break the connection
     * to the unit under test.
     *
     * @return
     *     An indication of whether or not the server breaks their
     *     end of the connection before a reasonable amount of
     *     time has elapsed is returned.
     */
    bool AwaitServerBreak() {
        std::unique_lock< std::mutex > lock(mutex);
        return waitCondition.wait_for(
            lock,
            std::chrono::seconds(1),
            [this]{
                return broken;
            }
        );
    }

    /**
     * This method waits for the client to break the connection
     * at the given index of the collection of connections
     * currently established with the server.
     *
     * @param[in] connectionIndex
     *     This is the index of the connection for which to await
     *     a client-side break.
     *
     * @return
     *     An indication of whether or not the client breaks their
     *     end of the connection before a reasonable amount of
     *     time has elapsed is returned.
     */
    bool AwaitClientBreak(size_t connectionIndex) {
        std::unique_lock< std::mutex > lock(mutex);
        return waitCondition.wait_for(
            lock,
            std::chrono::seconds(1),
            [this, connectionIndex]{
                return clients[connectionIndex].broken;
            }
        );
    }

    /**
     * This method waits for the server to send the given number
     * of bytes to the unit under test.
     *
     * @param[in] amount
     *     This is the number of bytes to await.
     *
     * @return
     *     An indication of whether or not the server has sent
     *     the given number of bytes before a reasonable amount of
     *     time has elapsed is returned.
     */
    bool AwaitServerData(size_t amount) {
        std::unique_lock< std::mutex > lock(mutex);
        return waitCondition.wait_for(
            lock,
            std::chrono::seconds(1),
            [this, amount]{
                return (dataReceived.size() >= amount);
            }
        );
    }

    /**
     * This method waits for the client to send the given number
     * of bytes through the connection at the given index of the
     * collection of connections currently established with the server.
     *
     * @param[in] connectionIndex
     *     This is the index of the connection for which to await
     *     data from the client.
     *
     * @param[in] amount
     *     This is the number of bytes to await.
     *
     * @return
     *     An indication of whether or not the client has sent
     *     the given number of bytes before a reasonable amount of
     *     time has elapsed is returned.
     */
    bool AwaitClientData(
        size_t connectionIndex,
        size_t amount
    ) {
        std::unique_lock< std::mutex > lock(mutex);
        return waitCondition.wait_for(
            lock,
            std::chrono::seconds(1),
            [this, connectionIndex, amount]{
                return (clients[connectionIndex].dataReceived.size() >= amount);
            }
        );
    }

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
            0
        );
        const auto newConnectionDelegate = [this](
            std::shared_ptr< SystemAbstractions::NetworkConnection > newConnection
        ){
            std::unique_lock< decltype(mutex) > lock(mutex);
            size_t connectionIndex = clients.size();
            if (
                newConnection->Process(
                    [this, connectionIndex](const std::vector< uint8_t >& data){
                        std::unique_lock< decltype(mutex) > lock(mutex);
                        auto& dataReceived = clients[connectionIndex].dataReceived;
                        dataReceived.insert(
                            dataReceived.end(),
                            data.begin(),
                            data.end()
                        );
                        waitCondition.notify_all();
                    },
                    [this, connectionIndex](bool graceful){
                        std::unique_lock< decltype(mutex) > lock(mutex);
                        auto& broken = clients[connectionIndex].broken;
                        broken = true;
                        waitCondition.notify_all();
                    }
                )
            ) {
                Client newClient;
                newClient.connection = newConnection;
                clients.push_back(std::move(newClient));
                waitCondition.notify_all();
            }
        };
        const auto packetReceivedDelegate = [](
            uint32_t address,
            uint16_t port,
            const std::vector< uint8_t >& body
        ){
        };
        ASSERT_TRUE(
            server.Open(
                newConnectionDelegate,
                packetReceivedDelegate,
                SystemAbstractions::NetworkEndpoint::Mode::Connection,
                0x7F000001,
                0,
                0
            )
        );
        dataReceivedDelegate = [this](
            std::vector< uint8_t > data
        ){
            std::lock_guard< std::mutex > lock(mutex);
            dataReceived.insert(
                dataReceived.end(),
                data.begin(),
                data.end()
            );
            waitCondition.notify_all();
        };
        brokenDelegate = [this](bool){
            std::lock_guard< std::mutex > lock(mutex);
            broken = true;
            waitCondition.notify_all();
        };
    }

    virtual void TearDown() {
        server.Close();
        if (!transportWasMoved) {
            diagnosticsUnsubscribeDelegate();
        }
    }
};

TEST_F(HttpClientNetworkTransportTests, Connect) {
    const auto connection = transport.Connect(
        "localhost",
        server.GetBoundPort(),
        dataReceivedDelegate,
        brokenDelegate
    );
    ASSERT_FALSE(connection == nullptr);
    ASSERT_TRUE(AwaitConnections(1));
}

TEST_F(HttpClientNetworkTransportTests, BreakClientSide) {
    const auto connection = transport.Connect(
        "localhost",
        server.GetBoundPort(),
        dataReceivedDelegate,
        brokenDelegate
    );
    ASSERT_TRUE(AwaitConnections(1));
    connection->Break(false);
    ASSERT_TRUE(AwaitClientBreak(0));
}

TEST_F(HttpClientNetworkTransportTests, BreakServerSide) {
    const auto connection = transport.Connect(
        "localhost",
        server.GetBoundPort(),
        dataReceivedDelegate,
        brokenDelegate
    );
    ASSERT_TRUE(AwaitConnections(1));
    clients[0].connection->Close(false);
    ASSERT_TRUE(AwaitServerBreak());
}

TEST_F(HttpClientNetworkTransportTests, ClientSend) {
    const auto connection = transport.Connect(
        "localhost",
        server.GetBoundPort(),
        dataReceivedDelegate,
        brokenDelegate
    );
    ASSERT_TRUE(AwaitConnections(1));
    const std::vector< uint8_t > testData{1, 2, 3, 4, 5};
    connection->SendData(testData);
    ASSERT_TRUE(AwaitClientData(0, testData.size()));
    EXPECT_EQ(testData, clients[0].dataReceived);
}

TEST_F(HttpClientNetworkTransportTests, ServerSend) {
    const auto connection = transport.Connect(
        "localhost",
        server.GetBoundPort(),
        dataReceivedDelegate,
        brokenDelegate
    );
    ASSERT_TRUE(AwaitConnections(1));
    const std::vector< uint8_t > testData{1, 2, 3, 4, 5};
    clients[0].connection->SendMessage(testData);
    ASSERT_TRUE(AwaitServerData(testData.size()));
    EXPECT_EQ(testData, dataReceived);
}
