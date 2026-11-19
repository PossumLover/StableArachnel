#pragma once

#include <QDebug>
#include <QJsonDocument>
#include <QSaveFile>
#include <utility>

namespace arachnel::core {
inline bool saveJsonAtomically(const QString& path, const QJsonDocument& doc, QString* error)
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
        if (file.write(bytes) == bytes.size() && file.commit())
            return true;
    }
    *error = QStringLiteral("%1: %2").arg(path, file.errorString());
    qWarning().noquote() << *error;
    return false;
}
}
