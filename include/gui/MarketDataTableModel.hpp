#pragma once

#include <QAbstractTableModel>
#include <vector>
#include <QStringList>
#include "DataParser.hpp" 

class MarketDataTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit MarketDataTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

public slots:
    void updateMarketData(const std::vector<MarketDataEntry> &newData);

private:
    std::vector<MarketDataEntry> m_marketDataEntries;
    QStringList m_columnHeaders;
};