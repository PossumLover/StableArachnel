#include "transport_stubs.h"
#include "torrent_session.h"
#include "http_download_session.h"

namespace arachnel::core {
struct TorrentSession::Impl {};
TorrentSession::TorrentSession(QObject* parent) : QObject(parent) {}
TorrentSession::~TorrentSession() = default;
bool TorrentSession::addJob(const QString& id, const QString&, const QString&)
{
    transport_test::torrentStarts.append(id);
    return true;
}
void TorrentSession::cancel(const QString& id, bool) { transport_test::torrentStops.append(id); }
void TorrentSession::setPaused(const QString&, bool) {}
void TorrentSession::removeResumeFile(const QString&) {}

HttpDownloadSession::HttpDownloadSession(QObject* parent) : QObject(parent) {}
HttpDownloadSession::~HttpDownloadSession() = default;
bool HttpDownloadSession::addJob(const QString& id, const QString&, const QString&, const QString&)
{
    transport_test::httpStarts.append(id);
    return true;
}
void HttpDownloadSession::cancel(const QString&) {}
}
