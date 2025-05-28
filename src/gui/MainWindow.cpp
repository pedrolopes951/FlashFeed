#include "MainWindow.hpp"
#include "gui/MarketDataWorker.hpp"
#include "gui/MarketDataTableModel.hpp"

#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QMessageBox>
#include <QThread>
#include <QDebug>
#include <QTableView>
#include <QHeaderView>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_centralWidget = new QWidget(this);
    m_mainLayout = new QVBoxLayout(m_centralWidget); // Main layout for the central widget

    // --- Connection Group ---
    m_connectionFormLayout = new QFormLayout(); // Layout for address and port

    m_serverAddressEdit = new QLineEdit("127.0.0.1", this);
    m_serverPortEdit = new QLineEdit("8080", this);

    m_connectionFormLayout->addRow("Server Address:", m_serverAddressEdit);
    m_connectionFormLayout->addRow("Server Port:", m_serverPortEdit);

    m_connectButton = new QPushButton("Connect", this);
    m_connectionStatusLabel = new QLabel("Status: Disconnected", this);
    m_connectionStatusLabel->setAlignment(Qt::AlignCenter);

    // --- Subscription Group ---
    m_subscriptionFormLayout = new QFormLayout();

    m_symbolEdit = new QLineEdit("AAPL", this);
    m_subscribeButton = new QPushButton("Subscribe", this);
    m_subscribeButton->setEnabled(false);
    m_subscriptionFormLayout->addRow("Symbol:", m_symbolEdit);

    // --- Data Table View (NEW) ---
    m_dataModel = new MarketDataTableModel(this);
    m_tableView = new QTableView(this);
    m_tableView->setModel(m_dataModel);

    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows); // Select whole rows
    m_tableView->horizontalHeader()->setStretchLastSection(true);     // Make last column stretch
    m_tableView->verticalHeader()->setVisible(false);                 // Hide vertical row numbers

    // --- General Status Label & Test Button  ---
    m_statusLabel = new QLabel("Welcome! Please connect to the server.", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    // --- Add all components to the main layout ---
    m_mainLayout->addLayout(m_connectionFormLayout);
    m_mainLayout->addWidget(m_connectButton);
    m_mainLayout->addWidget(m_connectionStatusLabel);
    m_mainLayout->addSpacing(10); // Reduced spacing

    m_mainLayout->addLayout(m_subscriptionFormLayout);
    m_mainLayout->addWidget(m_subscribeButton);
    m_mainLayout->addSpacing(10); // Reduced spacing

    m_mainLayout->addWidget(m_tableView, 1); // Add table view, set stretch factor to 1 to take available space

    m_mainLayout->addWidget(m_statusLabel);
    // m_mainLayout->addStretch(); // Stretch factor on table view handles this better

    setCentralWidget(m_centralWidget);

    // --- Connect signals to slots ---
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectButtonClicked);
    connect(m_subscribeButton, &QPushButton::clicked, this, &MainWindow::onSubscribeButtonClicked);

    // --- Setup Worker Thread and Worker Object ---
    m_worker = new MarketDataWorker();
    m_workerThread = new QThread(this); // Thread

    m_worker->moveToThread(m_workerThread);

    connect(m_worker, &MarketDataWorker::connectedToServer, this, &MainWindow::onWorkerConnected);
    connect(m_worker, &MarketDataWorker::disconnectedFromServer, this, &MainWindow::onWorkerDisconnected);
    connect(m_worker, &MarketDataWorker::statusMessage, this, &MainWindow::onWorkerStatusMessage);
    connect(m_worker, &MarketDataWorker::connectionError, this, &MainWindow::onWorkerError);
    connect(m_worker, &MarketDataWorker::subscribedToSymbol, this, &MainWindow::onWorkerSubscribed);
    connect(m_worker, &MarketDataWorker::newDataArrived, this, &MainWindow::onWorkerNewData);

    connect(this, &MainWindow::destroyed, this, [this]()
            {
        qDebug() << "MainWindow destroyed signal: Initiating worker and thread shutdown.";
        if (m_worker && m_workerThread->isRunning()) { // Check if worker exists
            QMetaObject::invokeMethod(m_worker, "requestStop", Qt::QueuedConnection);
        }
        
        if (m_workerThread && m_workerThread->isRunning()){
            m_workerThread->quit();
            if (!m_workerThread->wait(5000)) {
                qWarning() << "MainWindow destroyed signal: QThread did not finish gracefully, terminating.";
                m_workerThread->terminate();
                m_workerThread->wait();
            } else {
                qDebug() << "MainWindow destroyed signal: QThread finished gracefully.";
            }
        } });

    m_workerThread->start(); // Start the worker thread's event loop.

    QMetaObject::invokeMethod(m_worker, "startService", Qt::QueuedConnection); // Launches a new seprate std::threa called m_asiothread

    resize(500, 400);
}

MainWindow::~MainWindow()
{
    qDebug() << "MainWindow destructor called.";
}

void MainWindow::onConnectButtonClicked()
{
    if (m_connectButton->text() == "Connect")
    {
        QString address = m_serverAddressEdit->text();
        // Basic validation for port
        bool portOk;
        int port = m_serverPortEdit->text().toInt(&portOk);
        if (!portOk || port <= 0 || port > 65535)
        {
            QMessageBox::warning(this, "Invalid Port", "Please enter a valid port number (1-65535).");
            m_connectionStatusLabel->setText("Status: Invalid Port");
            return;
        }

        m_connectionStatusLabel->setText("Status: Connecting...");
        m_statusLabel->setText(QString("Requesting connection to %1:%2...").arg(address).arg(port));

        // Use QMetaObject::invokeMethod to call the worker's slot in its own thread.
        // Qt::QueuedConnection is important for cross-thread calls.
        QMetaObject::invokeMethod(m_worker, "processConnect", Qt::QueuedConnection,
                                  Q_ARG(QString, address), Q_ARG(int, port));
    }
    else
    { // "Disconnect" button was clicked
        m_connectionStatusLabel->setText("Status: Disconnecting...");
        m_statusLabel->setText("Requesting disconnection...");
        QMetaObject::invokeMethod(m_worker, "processDisconnect", Qt::QueuedConnection);
    }
}

void MainWindow::onSubscribeButtonClicked()
{
    if (!m_subscribeButton->isEnabled())
    {
        qDebug() << "Subscribe button clicked while disabled.";
        return;
    }
    QString symbol = m_symbolEdit->text();
    if (symbol.isEmpty())
    {
        QMessageBox::warning(this, "Invalid Symbol", "Please enter a symbol to subscribe.");
        m_statusLabel->setText("Status: Symbol required for subscription.");
        return;
    }

    m_statusLabel->setText(QString("Requesting subscription to %1...").arg(symbol));
    QMetaObject::invokeMethod(m_worker, "processSubscribe", Qt::QueuedConnection,
                              Q_ARG(QString, symbol));
}

void MainWindow::onWorkerConnected()
{
    qDebug() << "MainWindow (thread" << QThread::currentThreadId() << "): Received connectedToServer signal.";
    m_connectionStatusLabel->setText("Status: Connected!");
    m_statusLabel->setText("Successfully connected to server.");
    m_connectButton->setText("Disconnect");
    m_subscribeButton->setEnabled(true);
}

void MainWindow::onWorkerDisconnected()
{
    qDebug() << "MainWindow (thread" << QThread::currentThreadId() << "): Received disconnectedFromServer signal.";
    m_connectionStatusLabel->setText("Status: Disconnected.");
    m_statusLabel->setText("Disconnected from server.");
    m_connectButton->setText("Connect");
    m_subscribeButton->setEnabled(false);
}

void MainWindow::onWorkerStatusMessage(const QString &message)
{
    qDebug() << "MainWindow (thread" << QThread::currentThreadId() << "): Received statusMessage:" << message;
    m_statusLabel->setText(message);
}

void MainWindow::onWorkerError(const QString &message)
{
    qDebug() << "MainWindow (thread" << QThread::currentThreadId() << "): Received connectionError:" << message;
    m_connectionStatusLabel->setText("Status: Error!");
    m_statusLabel->setText("Error: " + message);
    QMessageBox::critical(this, "Connection Error", message);
    m_connectButton->setText("Connect");
    m_subscribeButton->setEnabled(false);
}

void MainWindow::onWorkerSubscribed(const QString &symbol)
{
    qDebug() << "MainWindow (thread" << QThread::currentThreadId() << "): Received subscribedToSymbol signal for" << symbol;
    m_statusLabel->setText("Successfully subscribed to: " + symbol);
}

void MainWindow::onWorkerNewData(const QString &symbolName, const std::vector<MarketDataEntry> &data)
{
    qDebug() << "MainWindow (thread" << QThread::currentThreadId() << "): Received newDataArrived signal for" << symbolName << "with" << data.size() << "entries";
    m_statusLabel->setText(QString("Data updated for %1 (%2 entries).").arg(symbolName).arg(data.size()));

    // Update the data model
    if (m_dataModel)
    {
        m_dataModel->updateMarketData(data);
        if (!data.empty())
        {                                           
            m_tableView->resizeColumnsToContents();
        }
    }
}

#include "moc_MainWindow.cpp" // It explicitly includes MOC-generated code
