#include "gui/MarketDataWorker.hpp"
#include "Logger.hpp"
#include <QDebug>
#include <QThread>
#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/write.hpp>
#include <nlohmann/json.hpp> 
#include <sstream>

namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

QString threadIdToString(const std::thread::id &id)
{
    std::ostringstream oss;
    oss << id;
    return QString::fromStdString(oss.str());
}

QString qThreadIdToString(Qt::HANDLE id)
{
    return QString("0x%1").arg(reinterpret_cast<quintptr>(id), QT_POINTER_SIZE * 2, 16, QChar('0'));
}

MarketDataWorker::MarketDataWorker(QObject *parent)
    : QObject(parent),
      m_ioContext(std::make_unique<net::io_context>()), // Initialize io_context
      m_resolver(*m_ioContext), // Initialize resolver with the io_context
      m_isConnected(false)
{
    qDebug() << "MarketDataWorker instance created in thread:" << qThreadIdToString(QThread::currentThreadId());
}

MarketDataWorker::~MarketDataWorker()
{
    qDebug() << "MarketDataWorker instance being destroyed in thread:" << qThreadIdToString(QThread::currentThreadId());
    m_asioThreadShouldExit = true;

    if (m_workGuard)
    {
        m_workGuard->reset();
    }
    if (m_ioContext && !m_ioContext->stopped())
    {
        m_ioContext->stop();
    }

    if (m_asioThread.joinable())
    {
        qDebug() << "MarketDataWorker destructor: Joining Asio thread...";
        try
        {
            m_asioThread.join();
            qDebug() << "MarketDataWorker destructor: Asio thread joined.";
        }
        catch (const std::system_error &e)
        {
            qWarning() << "MarketDataWorker destructor: Exception while joining Asio thread:" << e.what();
        }
    }
}

void MarketDataWorker::startService()
{
    qDebug() << "MarketDataWorker (QThread" << qThreadIdToString(QThread::currentThreadId()) << "): startService slot called to launch Asio thread.";

    if (m_asioThread.joinable())
    {
        qDebug() << "MarketDataWorker: Asio service thread might already be active or needs joining.";
        return;
    }

    m_asioThreadShouldExit = false;
    if (m_ioContext->stopped())
    {
        m_ioContext->restart();
    }

    m_workGuard = std::make_unique<work_guard_type>(m_ioContext->get_executor());

    m_asioThread = std::thread([this]()
                               {
        qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): Starting io_context::run().";
        
        QMetaObject::invokeMethod(this, [this](){ emit statusMessage("Worker Asio service started."); }, Qt::QueuedConnection);

        try {
            m_ioContext->run();
        } catch (const std::exception& e) {
            qCritical() << "MarketDataWorker: Exception in io_context::run() (Asio std::thread " << threadIdToString(std::this_thread::get_id()) << "):" << e.what();
             QMetaObject::invokeMethod(this, [this, e_what = std::string(e.what())](){
                emit connectionError(QString("Worker Asio thread error: %1").arg(e_what.c_str()));
                if (m_isConnected) { m_isConnected = false; emit disconnectedFromServer(); }
             }, Qt::QueuedConnection);
        }
        
        qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): io_context::run() EXITED.";
        
        QMetaObject::invokeMethod(this, [this](){
            emit statusMessage("Worker Asio service loop stopped.");
            if (m_isConnected) {
                m_isConnected = false;
                emit disconnectedFromServer();
            }
        }, Qt::QueuedConnection); });
}

void MarketDataWorker::requestStop()
{
    qDebug() << "MarketDataWorker (QThread" << qThreadIdToString(QThread::currentThreadId()) << "): requestStop called.";
    m_asioThreadShouldExit = true;

    net::post(*m_ioContext, [this]()
              {
                  qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): Processing stop request.";

                  if (m_socket && m_socket->is_open())
                  {
                      qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): Closing socket.";
                      boost::system::error_code ignored_ec;
                      m_socket->shutdown(tcp::socket::shutdown_both, ignored_ec);
                      m_socket->close(ignored_ec);
                  }

                  if (m_workGuard)
                  {
                      qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): Resetting work guard.";
                      m_workGuard->reset();
                  }

              });
}

void MarketDataWorker::processConnect(const QString &address, int port)
{
    qDebug() << "MarketDataWorker (QThread" << qThreadIdToString(QThread::currentThreadId()) << "): processConnect called with" << address << ":" << port;

    if (m_isConnected || (m_socket && m_socket->is_open()))
    {
        emit statusMessage("Worker: Already connected or connection in progress.");
        if (m_isConnected)
            emit connectedToServer();
        return;
    }

    if (m_asioThread.joinable() && !m_isConnected)
    { // Check if joinable AND we are not considering ourselves connected
        qDebug() << "MarketDataWorker::processConnect: Previous Asio thread is joinable. Joining...";
        if (m_workGuard)
            m_workGuard->reset();
        if (!m_ioContext->stopped())
            m_ioContext->stop(); // Ensure it stops

        try
        {
            m_asioThread.join(); // Join the old thread
            qDebug() << "MarketDataWorker::processConnect: Previous Asio thread joined.";
        }
        catch (const std::system_error &e)
        {
            qWarning() << "MarketDataWorker::processConnect: Error joining previous Asio thread:" << e.what();
        }
    }

    if (m_ioContext->stopped())
    {
        qDebug() << "MarketDataWorker::processConnect: io_context was stopped. Restarting.";
        m_ioContext->restart();
    }

    if (!m_asioThread.joinable() || m_ioContext->stopped())
    {
        qDebug() << "MarketDataWorker::processConnect: Asio service thread needs (re)start.";
        m_workGuard = std::make_unique<work_guard_type>(m_ioContext->get_executor());

        if (m_asioThread.joinable())
            m_asioThread.join();
        m_asioThread = std::thread([this]()
                                   { 
            qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): (Re)Starting io_context::run().";
            QMetaObject::invokeMethod(this, [this]()
                                      { emit statusMessage("Worker Asio service (re)started."); }, Qt::QueuedConnection);
            try
            {
                m_ioContext->run();
            }
            catch (const std::exception &e)
            { 
            }
            qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): io_context::run() EXITED (after reconnect attempt).";
            QMetaObject::invokeMethod(this, [this]()
                                      { emit statusMessage("Worker Asio service loop stopped (after reconnect)."); }, Qt::QueuedConnection); });
    }

    m_socket = std::make_shared<tcp::socket>(*m_ioContext);
    emit statusMessage(QString("Worker: Resolving %1:%2...").arg(address).arg(port));
    doResolve(address, QString::number(port));
}
void MarketDataWorker::doResolve(const QString &address, const QString &portStr)
{
    qDebug() << "MarketDataWorker (QThread" << qThreadIdToString(QThread::currentThreadId()) << "): In doResolve. Posting async_resolve for" << address << ":" << portStr;

    m_resolver.async_resolve(address.toStdString(), portStr.toStdString(),
                             [this, address, portStr](const boost::system::error_code &ec, const tcp::resolver::results_type &endpoints)
                             {
                                 qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): In handleResolve callback for" << address << ":" << portStr << ". EC:" << ec.message().c_str();
                                 handleResolve(ec, endpoints);
                             });
    qDebug() << "MarketDataWorker (QThread" << qThreadIdToString(QThread::currentThreadId()) << "): After posting async_resolve for" << address << ":" << portStr;
}

void MarketDataWorker::handleResolve(const boost::system::error_code &ec,
                                     const tcp::resolver::results_type &endpoints)
{
    if (ec)
    {
        qDebug() << "MarketDataWorker (thread" << QThread::currentThreadId() << "): Resolve error -" << ec.message().c_str();
        emit statusMessage(QString("Worker: Resolve error: %1").arg(ec.message().c_str()));
        emit connectionError(QString("Resolve failed: %1").arg(ec.message().c_str()));
        return;
    }

    qDebug() << "MarketDataWorker (thread" << QThread::currentThreadId() << "): Resolve successful. Attempting connect.";
    emit statusMessage("Worker: Host resolved. Connecting...");
    doConnect(endpoints);
}

void MarketDataWorker::doConnect(const tcp::resolver::results_type &endpoints)
{
    net::async_connect(*m_socket, endpoints,
                       [this](const boost::system::error_code &ec, const tcp::endpoint & /*endpoint*/)
                       {
                           handleConnect(ec);
                       });
}

void MarketDataWorker::handleConnect(const boost::system::error_code &ec)
{
    try
    {
        if (ec)
        {
            qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): Connect error -" << ec.message().c_str();
            closeSocket();
            return;
        }
        qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): Connect successful!";
        m_isConnected = true;
        emit statusMessage("Worker: Connection successful!");
        emit connectedToServer();
    }
    catch (const std::exception &e)
    {
        qCritical() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): Exception in handleConnect:" << e.what();
        QMetaObject::invokeMethod(this, [this, errMsg = QString::fromStdString(e.what())]()
                                  {
            emit connectionError(QString("Internal worker error during connect: %1").arg(errMsg));
            if(m_isConnected){ m_isConnected = false; emit disconnectedFromServer(); } }, Qt::QueuedConnection);
        if (m_workGuard)
            m_workGuard->reset(); // Allow service to stop if it's a fatal error for the io_context loop
    }
}

void MarketDataWorker::processSubscribe(const QString &symbol)
{
    qDebug() << "MarketDataWorker (thread" << QThread::currentThreadId() << "): processSubscribe called for" << symbol;
    if (!m_isConnected || !m_socket || !m_socket->is_open())
    {
        emit statusMessage("Worker: Cannot subscribe. Not connected.");
        emit subscriptionError(symbol, "Not connected to server.");
        return;
    }

    m_currentSymbol = symbol; // Store the symbol we are subscribing to
    emit statusMessage(QString("Worker: Subscribing to %1...").arg(symbol));
    doWriteSubscribe();
}

void MarketDataWorker::doWriteSubscribe()
{
    std::string request = "SUBSCRIBE " + m_currentSymbol.toStdString() + "\n";
    net::async_write(*m_socket, net::buffer(request),
                     [this](const boost::system::error_code &ec, std::size_t bytes_transferred)
                     {
                         handleWriteSubscribe(ec, bytes_transferred);
                     });
}

void MarketDataWorker::handleWriteSubscribe(const boost::system::error_code &ec, std::size_t /*bytes_transferred*/)
{
    if (ec)
    {
        qDebug() << "MarketDataWorker (thread" << QThread::currentThreadId() << "): Write subscribe error -" << ec.message().c_str();
        emit statusMessage(QString("Worker: Subscribe send error: %1").arg(ec.message().c_str()));
        emit subscriptionError(m_currentSymbol, QString("Failed to send subscribe request: %1").arg(ec.message().c_str()));
        return;
    }

    qDebug() << "MarketDataWorker (thread" << QThread::currentThreadId() << "): Subscribe message sent for" << m_currentSymbol;
    emit statusMessage(QString("Worker: Subscribe request sent for %1.").arg(m_currentSymbol));
    emit subscribedToSymbol(m_currentSymbol);

    doReadHeader();
}

void MarketDataWorker::processDisconnect()
{
    qDebug() << "MarketDataWorker (thread" << QThread::currentThreadId() << "): processDisconnect called.";
    emit statusMessage("Worker: Disconnecting...");
    closeSocket(); // Close socket and update state

    if (m_workGuard)
    {
        m_workGuard->reset(); // Allow io_context::run() to exit if no other work
    }

    if (!m_isConnected)
    { // Should have been set to false by closeSocket
        emit disconnectedFromServer();
    }
}

void MarketDataWorker::closeSocket()
{
    if (m_socket && m_socket->is_open())
    {
        boost::system::error_code ec;
        m_socket->shutdown(tcp::socket::shutdown_both, ec); // Graceful shutdown
        if (ec)
        {
            qDebug() << "MarketDataWorker: Socket shutdown error:" << ec.message().c_str();
        }
        m_socket->close(ec);
        if (ec)
        {
            qDebug() << "MarketDataWorker: Socket close error:" << ec.message().c_str();
        }
    }
    if (m_isConnected)
    { // Only emit if we were connected
        m_isConnected = false;
        qDebug() << "MarketDataWorker: Socket closed, m_isConnected set to false.";
    }
}

void MarketDataWorker::doReadHeader()
{
    if (!m_isConnected || !m_socket || !m_socket->is_open())
    {
        qDebug() << "MarketDataWorker::doReadHeader: Not connected, aborting read.";
        return;
    }
    qDebug() << "MarketDataWorker (thread" << QThread::currentThreadId() << "): Starting async_read_until for header.";
    m_responseBuffer.consume(m_responseBuffer.size()); // Clear buffer before new read

    net::async_read_until(*m_socket, m_responseBuffer, "\n",
                          [this](const boost::system::error_code &ec, std::size_t bytes_transferred)
                          {
                              handleReadHeader(ec, bytes_transferred);
                          });
}

void MarketDataWorker::handleReadHeader(const boost::system::error_code &ec, std::size_t bytes_transferred)
{
    if (ec)
    {
        onSocketError(ec);
        return;
    }

    if (bytes_transferred > 0)
    {
        std::istream header_stream(&m_responseBuffer);
        std::string header_line;
        std::getline(header_stream, header_line);

        qDebug() << "MarketDataWorker: Received header line:" << header_line.c_str();

        if (header_line.rfind("DATA_SIZE:", 0) == 0)
        {
            try
            {
                std::size_t payloadSize = std::stoull(header_line.substr(10));
                qDebug() << "MarketDataWorker: Expecting payload of size" << payloadSize;
                doReadPayload(payloadSize);
            }
            catch (const std::exception &e)
            {
                qWarning() << "MarketDataWorker: Error parsing DATA_SIZE from header:" << e.what();
                emit statusMessage(QString("Worker: Invalid DATA_SIZE header: %1").arg(e.what()));
                doReadHeader();
            }
        }
        else if (header_line.rfind("ERROR:", 0) == 0)
        {
            qWarning() << "MarketDataWorker: Received ERROR from server:" << header_line.substr(6).c_str();
            emit statusMessage(QString("Worker: Server error: %1").arg(header_line.substr(6).c_str()));
            doReadHeader();
        }
        else
        {
            qWarning() << "MarketDataWorker: Received unknown header:" << header_line.c_str();
            emit statusMessage(QString("Worker: Unknown server message: %1").arg(header_line.c_str()));
            doReadHeader(); // Try to read next header
        }
    }
    else
    {
        qDebug() << "MarketDataWorker::handleReadHeader: Read 0 bytes for header, no error. EOF assumed or delimiter not found.";
        onSocketError(net::error::eof); // Treat as EOF
    }
}

void MarketDataWorker::doReadPayload(std::size_t expectedPayloadSize)
{
    if (!m_isConnected || !m_socket || !m_socket->is_open())
    {
        qDebug() << "MarketDataWorker::doReadPayload: Not connected, aborting read.";
        return;
    }

    std::size_t already_in_buffer = m_responseBuffer.size();
    std::size_t needed = 0;
    if (expectedPayloadSize > already_in_buffer)
    {
        needed = expectedPayloadSize - already_in_buffer;
    }

    qDebug() << "MarketDataWorker (thread" << QThread::currentThreadId() << "): Starting async_read for payload. Need" << needed << "bytes.";
    net::async_read(*m_socket, m_responseBuffer, net::transfer_exactly(needed),
                    [this, expectedPayloadSize](const boost::system::error_code &ec, std::size_t bytes_transferred)
                    {
                        handleReadPayload(ec, bytes_transferred, expectedPayloadSize);
                    });
}

void MarketDataWorker::handleReadPayload(const boost::system::error_code &ec, std::size_t /*bytes_transferred_in_this_op*/, std::size_t expectedPayloadSize)
{
    if (ec)
    {
        onSocketError(ec);
        return;
    }

    if (m_responseBuffer.size() >= expectedPayloadSize)
    {
        // We have enough data for the payload
        const char *data_ptr = net::buffer_cast<const char *>(m_responseBuffer.data());
        std::string payload_str(data_ptr, expectedPayloadSize);
        m_responseBuffer.consume(expectedPayloadSize); // Consume the processed payload

        qDebug() << "MarketDataWorker: Received payload of size" << payload_str.length();

        try
        {
            json jsonData = json::parse(payload_str);
            if (jsonData.is_array())
            {
                std::vector<MarketDataEntry> entries = jsonData.get<std::vector<MarketDataEntry>>();
                emit statusMessage(QString("Worker: Parsed %1 entries for %2.").arg(entries.size()).arg(m_currentSymbol));
                emit newDataArrived(m_currentSymbol, entries);
            }
            else
            {
                qWarning() << "MarketDataWorker: Received JSON payload is not an array.";
                emit statusMessage("Worker: Invalid data format received (not an array).");
            }
        }
        catch (const json::parse_error &e)
        {
            qWarning() << "MarketDataWorker: JSON parse error -" << e.what();
            emit statusMessage(QString("Worker: Data parse error: %1").arg(e.what()));
        }
        catch (const std::exception &e)
        {
            qWarning() << "MarketDataWorker: Data processing error -" << e.what();
            emit statusMessage(QString("Worker: Data processing error: %1").arg(e.what()));
        }

        doReadHeader();
    }
    else
    {
        qWarning() << "MarketDataWorker::handleReadPayload: Did not receive full payload. Expected"
                   << expectedPayloadSize << "got" << m_responseBuffer.size();
        onSocketError(net::error::misc_errors::not_found);
    }
}

void MarketDataWorker::onSocketError(const boost::system::error_code &ec)
{
    qDebug() << "MarketDataWorker (Asio std::thread" << threadIdToString(std::this_thread::get_id()) << "): Socket error -" << ec.message().c_str();

    if (ec == net::error::operation_aborted)
    {
        qDebug() << "MarketDataWorker: Operation canceled (likely due to disconnect). This is expected.";
    }
    else
    {
        emit statusMessage(QString("Worker: Socket error: %1").arg(ec.message().c_str()));
    }

    if (ec == net::error::eof || ec == net::error::connection_reset || ec == net::error::broken_pipe || ec == net::error::operation_aborted)
    {
        if (ec != net::error::operation_aborted)
        {
            emit connectionError(QString("Server disconnected: %1").arg(ec.message().c_str()));
        }
        closeSocket();
        emit disconnectedFromServer();
    }
    else
    {
        emit connectionError(QString("Network error: %1").arg(ec.message().c_str()));
        closeSocket();
        emit disconnectedFromServer();
    }

    if (m_workGuard)
    {
        m_workGuard->reset();
    }
}

#include "moc_MarketDataWorker.cpp"
