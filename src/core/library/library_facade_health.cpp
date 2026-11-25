#include "core_controller_impl.h"

#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent>

namespace arachnel::core {
namespace {

LibraryHealthInput healthInput(const LibraryGame& game)
{
    LibraryHealthInput input{game.installPath, game.title, game.executableOverride,
                             game.selectedLaunchOptionId, {}};
    input.launchOptions.reserve(game.launchOptions.size());
    for (const auto& option : game.launchOptions)
        input.launchOptions.append({option.id, option.executable, option.isDefault});
    return input;
}

} // namespace

QVariantList CoreController::libraryHealth(const QString& entryId) const
{
    const auto it = m_libraryHealth.constFind(entryId);
    return it != m_libraryHealth.cend() ? it->issues : QVariantList{};
}

void CoreController::syncLibraryHealth()
{
    QStringList cleared;
    for (auto it = m_libraryHealth.begin(); it != m_libraryHealth.end();) {
        const auto* game = m_libraryStore.gameById(it.key());
        if (!game || healthInput(*game) != it->input) {
            if (!it->issues.isEmpty())
                cleared.append(it.key());
            it = m_libraryHealth.erase(it);
        } else {
            ++it;
        }
    }
    for (const QString& id : cleared)
        emit libraryHealthChanged(id);
    queueLibraryHealthSync(m_libraryStore.games(), m_pendingLibraryHealth);
    startLibraryHealthCheck();
}

void CoreController::queueLibraryHealthRefresh(const QString& gameId)
{
    // A write below the install root can leave its mtime unchanged.
    m_pendingLibraryHealth.insert(gameId, LibraryHealthCheckMode::Refresh);
    startLibraryHealthCheck();
}

void CoreController::startLibraryHealthCheck()
{
    if (m_libraryHealthScheduled || m_libraryHealthRunning || m_pendingLibraryHealth.isEmpty())
        return;
    m_libraryHealthScheduled = true;
    // Coalesce a library sync and its install completion, and keep disk probes serial on HDDs.
    QTimer::singleShot(0, this, [this]() {
        m_libraryHealthScheduled = false;
        while (!m_pendingLibraryHealth.isEmpty()) {
            const auto next = m_pendingLibraryHealth.constBegin();
            const QString id = next.key();
            const auto mode = next.value();
            m_pendingLibraryHealth.erase(next);
            const auto* game = m_libraryStore.gameById(id);
            if (!game || game->installPath.isEmpty())
                continue;

            const LibraryHealthInput input = healthInput(*game);
            const LibraryHealthResult cached = m_libraryHealth.value(id);
            auto* watcher = new QFutureWatcher<LibraryHealthResult>(this);
            m_libraryHealthRunning = true;
            connect(watcher, &QFutureWatcher<LibraryHealthResult>::finished, this,
                    [this, watcher, id]() {
                        const LibraryHealthResult result = watcher->result();
                        watcher->deleteLater();
                        const auto* current = m_libraryStore.gameById(id);
                        if (current && healthInput(*current) == result.input
                            && !m_pendingLibraryHealth.contains(id)) {
                            const bool changed = libraryHealth(id) != result.issues;
                            m_libraryHealth.insert(id, result);
                            if (changed)
                                emit libraryHealthChanged(id);
                        }
                        m_libraryHealthRunning = false;
                        startLibraryHealthCheck();
                    });
            watcher->setFuture(QtConcurrent::run([input, cached, mode]() {
                return checkLibraryHealth(input, cached, mode);
            }));
            return;
        }
    });
}

} // namespace arachnel::core
