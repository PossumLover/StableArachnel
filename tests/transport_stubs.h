#pragma once

#include <QStringList>

namespace transport_test {
inline QStringList torrentStarts;
inline QStringList torrentStops;
inline QStringList httpStarts;
inline void reset()
{
    torrentStarts.clear();
    torrentStops.clear();
    httpStarts.clear();
}
}
