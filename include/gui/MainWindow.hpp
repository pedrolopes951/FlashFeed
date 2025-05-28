#pragma once

#include <QMainWindow> // Base class for main application windows
#include <vector>
#include "DataParser.hpp"

class QLabel;
class QPushButton;
class QLineEdit;  
class QFormLayout; 
class QVBoxLayout;
class QWidget;
class QThread;
class MarketDataWorker;
class QTableView; 
class MarketDataTableModel;


class MainWindow : public QMainWindow
{
    Q_OBJECT // Essential macro for any QObject that uses signals, slots, or properties

public:
    explicit MainWindow(QWidget *parent = nullptr); 
    ~MainWindow();                                 

private slots:
    void onConnectButtonClicked(); // Slot the connect button
    void onSubscribeButtonClicked(); // Slot the subscribe button

    void onWorkerConnected();
    void onWorkerDisconnected();
    void onWorkerStatusMessage(const QString &message);
    void onWorkerError(const QString &message);
    void onWorkerSubscribed(const QString &symbol);
    void onWorkerNewData(const QString &symbolName, const std::vector<MarketDataEntry> &data); 

private:
    QLabel *m_statusLabel{nullptr};

    // New UI Elements
    QLineEdit *m_serverAddressEdit{nullptr};
    QLineEdit *m_serverPortEdit{nullptr};
    QPushButton *m_connectButton{nullptr};
    QLabel *m_connectionStatusLabel{nullptr}; 

    QLineEdit *m_symbolEdit{nullptr};
    QPushButton *m_subscribeButton{nullptr};

    QTableView *m_tableView;              
    MarketDataTableModel *m_dataModel;    

    // Layouts and Central Widget
    QWidget *m_centralWidget{nullptr};    
    QVBoxLayout *m_mainLayout{nullptr};    
    QFormLayout *m_connectionFormLayout{nullptr}; 
    QFormLayout *m_subscriptionFormLayout{nullptr}; 
    
    // New members for worker and thread
    QThread* m_workerThread{nullptr};           
    MarketDataWorker* m_worker{nullptr};  
};