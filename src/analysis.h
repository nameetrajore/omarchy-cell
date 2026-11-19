#pragma once
#include <QVariantMap>
#include <QVariantList>

namespace Analysis {
QVariantList windows(const QVariantList &samples);
QVariantMap compare(const QVariantList &samples);
}
