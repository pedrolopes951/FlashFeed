#include "gui/MarketDataTableModel.hpp" 
#include <QBrush>                       
#include <QColor>                       

MarketDataTableModel::MarketDataTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    // Initialize column headers
    m_columnHeaders << "Timestamp" << "Open" << "High" << "Low" << "Close" << "Volume";
}

// --- Required QAbstractTableModel Overrides Implementation ---

int MarketDataTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return static_cast<int>(m_marketDataEntries.size());
}

int MarketDataTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_columnHeaders.size(); 
}

QVariant MarketDataTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_marketDataEntries.size() || index.column() >= m_columnHeaders.size())
        return QVariant(); 

    const MarketDataEntry &entry = m_marketDataEntries[index.row()];

    if (role == Qt::DisplayRole) { // The data to be displayed as text
        switch (index.column()) {
            case 0: return QString::fromStdString(entry.m_timestamp);
            case 1: return entry.m_open;  // QVariant can handle double
            case 2: return entry.m_high;
            case 3: return entry.m_low;
            case 4: return entry.m_close;
            case 5: return entry.m_volume;
            default: return QVariant();
        }
    }
    // Optional: Add other roles like Qt::TextAlignmentRole
    else if (role == Qt::TextAlignmentRole) {
        if (index.column() >= 1 && index.column() <= 5) { // Align numeric columns to the right
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter); // Default alignment
    }

    return QVariant(); // Default for other roles
}

QVariant MarketDataTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();

    if (orientation == Qt::Horizontal) { // Column headers
        if (section >= 0 && section < m_columnHeaders.size()) {
            return m_columnHeaders[section];
        }
    }
    // else if (orientation == Qt::Vertical) { // Row headers (just row numbers for now)
    //     return section + 1;
    // }
    return QVariant();
}

// --- Slot Implementation ---

void MarketDataTableModel::updateMarketData(const std::vector<MarketDataEntry> &newData)
{
    // This informs any connected views that the model is about to be completely reset.
    // It's simpler than calculating specific row insertions/deletions if the
    // entire dataset is being replaced or significantly changed.
    beginResetModel();

    m_marketDataEntries = newData; // Replace the internal data store

    endResetModel(); // Tell views the model has been reset and they need to refetch all data
}

#include "moc_MarketDataTableModel.cpp"