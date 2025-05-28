#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <memory>
#include <thread>           
#include <atomic>           
#include "DataParser.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/executor_work_guard.hpp>

namespace net = boost::asio; 
using tcp = net::ip::tcp;    

class MarketDataWorker : public QObject
{
    Q_OBJECT

public:
    explicit MarketDataWorker(QObject *parent = nullptr);
    ~MarketDataWorker();

public slots:
    void processConnect(const QString &address, int port);
    void processSubscribe(const QString &symbol);
    void processDisconnect();
    void startService(); 
    void requestStop();

private slots:
    void onSocketError(const boost::system::error_code& ec);

private:
    // Boost.Asio members
    std::unique_ptr<net::io_context> m_ioContext;
    std::shared_ptr<tcp::socket> m_socket; 
    tcp::resolver m_resolver;
    net::streambuf m_responseBuffer; 

    // State members
    bool m_isConnected;
    QString m_currentSymbol; 

    // Thread for running io_context
    std::thread m_asioThread;                    
    std::atomic<bool> m_asioThreadShouldExit;

    // Private methods for asynchronous operations
    void doResolve(const QString &address, const QString &portStr);
    void doConnect(const tcp::resolver::results_type& endpoints);
    void doWriteSubscribe();
    void doReadHeader();
    void doReadPayload(std::size_t payloadSize);

    void handleResolve(const boost::system::error_code& ec,
                       const tcp::resolver::results_type& endpoints);
    void handleConnect(const boost::system::error_code& ec);
    void handleWriteSubscribe(const boost::system::error_code& ec, std::size_t bytes_transferred);
    void handleReadHeader(const boost::system::error_code& ec, std::size_t bytes_transferred);
    void handleReadPayload(const boost::system::error_code& ec, std::size_t bytes_transferred, std::size_t expectedPayloadSize);
    void closeSocket();
    using work_guard_type = net::executor_work_guard<net::io_context::executor_type>;
    std::unique_ptr<work_guard_type> m_workGuard;

signals:
    void connectedToServer();
    void disconnectedFromServer();
    void connectionError(const QString &message);
    void subscribedToSymbol(const QString &symbol);
    void subscriptionError(const QString &symbol, const QString &message);
    void newDataArrived(const QString &symbolName, const std::vector<MarketDataEntry> &data); 
    void statusMessage(const QString &message);
};