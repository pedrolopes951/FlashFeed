#pragma once

#include "DataParser.hpp" // To get MarketDataEntry definition
#include <vector>
#include <QMetaType>

// Declare the meta types here, where Qt is known
Q_DECLARE_METATYPE(MarketDataEntry);
Q_DECLARE_METATYPE(std::vector<MarketDataEntry>);