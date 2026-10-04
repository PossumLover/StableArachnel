#include "torrent_metadata_fetcher.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QThreadPool>
#include <QTimer>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>

#include <exception>

namespace arachnel::core {

namespace {

constexpr int kPollIntervalMs = 200;
// A catalog queues many links; one session for the whole queue keeps its DHT routing table
// instead of bootstrapping again for every link.
constexpr int kDefaultIdleSessionMs = 120000;

lt::settings_pack probeSettings()
{
    lt::settings_pack pack;
    pack.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0,[::]:0");
    pack.set_int(lt::settings_pack::alert_mask,
                 lt::alert_category::status | lt::alert_category::error
                     | lt::alert_category::storage);
    pack.set_int(lt::settings_pack::active_downloads, 1);
    pack.set_int(lt::settings_pack::active_seeds, 0);
    pack.set_bool(lt::settings_pack::enable_dht, true);
    pack.set_bool(lt::settings_pack::enable_lsd, false);
    pack.set_bool(lt::settings_pack::enable_upnp, false);
    pack.set_bool(lt::settings_pack::enable_natpmp, false);
    // Removing a probe tells its trackers it stopped; don't let that hold up a teardown.
    pack.set_int(lt::settings_pack::stop_tracker_timeout, 1);
    return pack;
}

QString probeSavePath()
{
    const QString dir = QDir::tempPath() + QStringLiteral("/sprout-magnet-probe");
    QDir().mkpath(dir);
    return dir;
}

QStringList fileNamesOf(const lt::torrent_info& info)
{
    QStringList names;
    const lt::file_storage& files = info.files();
    names.reserve(files.num_files());
    for (const lt::file_index_t index : files.file_range()) {
        const std::string path = files.file_path(index);
        if (!path.empty())
            names.append(QString::fromStdString(path));
    }
    return names;
}

} // namespace

QString magnetInfoHashKey(const QString& magnetUri)
{
    static const QRegularExpression re(
        QStringLiteral(R"(btih:([a-fA-F0-9]{40}|[A-Z2-7]{32}))"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = re.match(magnetUri);
    if (!match.hasMatch())
        return {};
    return match.captured(1).toLower();
}

struct MagnetMetadataProbe::Session {
    lt::session session{lt::session_params{probeSettings()}};
    lt::info_hash_t infoHash;
    lt::torrent_handle handle;
};

MagnetMetadataProbe::MagnetMetadataProbe(QObject* parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
    , m_idleTimer(new QTimer(this))
{
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &MagnetMetadataProbe::poll);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(kDefaultIdleSessionMs);
    connect(m_idleTimer, &QTimer::timeout, this, &MagnetMetadataProbe::dropIdleSession);
}

MagnetMetadataProbe::~MagnetMetadataProbe() = default;

void MagnetMetadataProbe::setIdleTimeout(int ms)
{
    m_idleTimer->setInterval(ms);
}

void MagnetMetadataProbe::cancel()
{
    m_pollTimer->stop();
    m_idleTimer->stop();
    m_busy = false;
    m_magnetUri.clear();
    m_session.reset();
}

bool MagnetMetadataProbe::start(const QString& magnetUri, int timeoutMs)
{
    if (m_busy)
        return false;

    lt::error_code ec;
    lt::add_torrent_params params = lt::parse_magnet_uri(magnetUri.toStdString(), ec);
    if (ec)
        return false;

    try {
        if (!m_session)
            m_session = std::make_unique<Session>();
        params.save_path = probeSavePath().toStdString();
        // Upload mode fetches the metadata but never a piece of the game. Not
        // stop_when_ready: a magnet counts as ready at once and gets paused before its
        // metadata arrives.
        params.flags |= lt::torrent_flags::upload_mode;
        params.flags &= ~(lt::torrent_flags::paused | lt::torrent_flags::auto_managed
                          | lt::torrent_flags::duplicate_is_error);
        m_session->infoHash = params.info_hashes;
        m_session->handle = {};
        m_session->session.async_add_torrent(std::move(params));
    } catch (const std::exception&) {
        m_session.reset();
        return false;
    }

    m_idleTimer->stop();
    m_magnetUri = magnetUri;
    m_deadlineMs = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    m_busy = true;
    m_pollTimer->start();
    return true;
}

void MagnetMetadataProbe::poll()
{
    if (!m_busy || !m_session)
        return;

    try {
        std::vector<lt::alert*> alerts;
        m_session->session.pop_alerts(&alerts);
        for (lt::alert* alert : alerts) {
            if (auto* added = lt::alert_cast<lt::add_torrent_alert>(alert)) {
                if (added->params.info_hashes != m_session->infoHash) {
                    // A probe that timed out before its torrent was even added.
                    if (!added->error && added->handle.is_valid())
                        m_session->session.remove_torrent(added->handle,
                                                          lt::session::delete_files);
                    continue;
                }
                if (added->error) {
                    finish({});
                    return;
                }
                m_session->handle = added->handle;
                continue;
            }

            const lt::torrent_handle& current = m_session->handle;
            if (!current.is_valid())
                continue;
            const auto isCurrent = [&current](const lt::torrent_alert* a) {
                return a && a->handle == current;
            };

            if (isCurrent(lt::alert_cast<lt::metadata_received_alert>(alert))) {
                // The file list comes back in the resume data, without a blocking call.
                current.save_resume_data(lt::torrent_handle::save_info_dict);
            } else if (auto* saved = lt::alert_cast<lt::save_resume_data_alert>(alert);
                       isCurrent(saved)) {
                finish(saved->params.ti ? fileNamesOf(*saved->params.ti) : QStringList());
                return;
            } else if (isCurrent(lt::alert_cast<lt::save_resume_data_failed_alert>(alert))
                       || isCurrent(lt::alert_cast<lt::metadata_failed_alert>(alert))
                       || isCurrent(lt::alert_cast<lt::torrent_error_alert>(alert))) {
                finish({});
                return;
            }
        }
    } catch (const std::exception&) {
        finish({});
        return;
    }

    if (QDateTime::currentMSecsSinceEpoch() >= m_deadlineMs)
        finish({});
}

void MagnetMetadataProbe::finish(const QStringList& fileNames)
{
    m_pollTimer->stop();
    if (m_session) {
        try {
            if (m_session->handle.is_valid())
                m_session->session.remove_torrent(m_session->handle, lt::session::delete_files);
        } catch (const std::exception&) {
        }
        m_session->handle = {};
        m_session->infoHash = {};
    }
    const QString magnetUri = m_magnetUri;
    m_magnetUri.clear();
    m_idleTimer->start();
    // Queued, so a slot that starts the next probe never runs inside this one's poll().
    // Busy until it is delivered: a probe started in between would take this result.
    QMetaObject::invokeMethod(
        this,
        [this, magnetUri, fileNames]() {
            m_busy = false;
            emit finished(magnetUri, fileNames);
        },
        Qt::QueuedConnection);
}

void MagnetMetadataProbe::dropIdleSession()
{
    if (m_busy || !m_session)
        return;
    // Destroying a session joins its network thread; keep that wait off this thread.
    Session* idle = m_session.release();
    QThreadPool::globalInstance()->start([idle]() { delete idle; });
}

} // namespace arachnel::core
