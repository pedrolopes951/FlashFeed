#include "gui/MainWindow.hpp"
#include <QApplication>
#include <QStyleFactory>
#include "gui/GuiMetaTypes.hpp" 
#include <vector>         

int main(int argc, char *argv[])
{
    // Register custom types for use in queued signals/slots
    qRegisterMetaType<MarketDataEntry>("MarketDataEntry");
    qRegisterMetaType<std::vector<MarketDataEntry>>("std::vector<MarketDataEntry>");

    QApplication a(argc, argv);
    MainWindow w;
    w.setWindowTitle("FlashFeed");
    w.show();
    return a.exec();
}