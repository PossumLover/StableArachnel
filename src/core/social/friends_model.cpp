#include "friends_model.h"

#include <algorithm>

namespace arachnel::core {

FriendsModel::FriendsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FriendsModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_friends.size();
}

QVariant FriendsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_friends.size())
        return {};
    const FriendEntry& entry = m_friends.at(index.row());
    switch (role) {
    case FriendIdRole:
        return entry.friendId;
    case NicknameRole:
        return entry.nickname;
    case PublicKeyRole:
        return entry.publicKey;
    case OnlineRole:
        return entry.online;
    case CurrentGameIdRole:
        return entry.currentGameId;
    case CurrentGameTitleRole:
        return entry.currentGameTitle;
    case CurrentGameCoverUrlRole:
        return entry.currentGameCoverUrl;
    case AddedAtRole:
        return entry.addedAt;
    case LastSeenAtRole:
        return entry.lastSeenAt;
    case SuggestedGameIdRole:
        return entry.suggestedGameId;
    case SuggestedGameTitleRole:
        return entry.suggestedGameTitle;
    case SuggestedCoverUrlRole:
        return entry.suggestedCoverUrl;
    case SuggestedAtRole:
        return entry.suggestedAt;
    default:
        return {};
    }
}

QHash<int, QByteArray> FriendsModel::roleNames() const
{
    return {
        {FriendIdRole, "friendId"},
        {NicknameRole, "nickname"},
        {PublicKeyRole, "publicKey"},
        {OnlineRole, "online"},
        {CurrentGameIdRole, "currentGameId"},
        {CurrentGameTitleRole, "currentGameTitle"},
        {CurrentGameCoverUrlRole, "currentGameCoverUrl"},
        {AddedAtRole, "addedAt"},
        {LastSeenAtRole, "lastSeenAt"},
        {SuggestedGameIdRole, "suggestedGameId"},
        {SuggestedGameTitleRole, "suggestedGameTitle"},
        {SuggestedCoverUrlRole, "suggestedCoverUrl"},
        {SuggestedAtRole, "suggestedAt"},
    };
}

namespace {

// Playing first, then online, then offline by most recently seen - the people you
// could join right now at the top.
int presenceRank(const FriendEntry& entry)
{
    if (entry.online && !entry.currentGameId.isEmpty())
        return 0;
    return entry.online ? 1 : 2;
}

bool sameRow(const FriendEntry& a, const FriendEntry& b)
{
    return a.friendId == b.friendId && a.nickname == b.nickname && a.online == b.online
        && a.currentGameId == b.currentGameId && a.currentGameTitle == b.currentGameTitle
        && a.currentGameCoverUrl == b.currentGameCoverUrl && a.lastSeenAt == b.lastSeenAt
        && a.suggestedGameId == b.suggestedGameId && a.suggestedGameTitle == b.suggestedGameTitle
        && a.suggestedCoverUrl == b.suggestedCoverUrl && a.suggestedAt == b.suggestedAt;
}

} // namespace

void FriendsModel::setFriends(QVector<FriendEntry> friends)
{
    std::sort(friends.begin(), friends.end(), [](const FriendEntry& a, const FriendEntry& b) {
        const int ra = presenceRank(a);
        const int rb = presenceRank(b);
        if (ra != rb)
            return ra < rb;
        if (ra == 2 && a.lastSeenAt != b.lastSeenAt)
            return a.lastSeenAt > b.lastSeenAt; // ISO-8601 sorts as text
        return QString::localeAwareCompare(a.nickname, b.nickname) < 0;
    });

    // Presence polls every few seconds. Same people in the same order: update the
    // changed rows in place instead of resetting, so delegates (and anything being
    // typed into them) survive the poll.
    bool sameOrder = friends.size() == m_friends.size();
    for (int i = 0; sameOrder && i < friends.size(); ++i)
        sameOrder = friends.at(i).friendId == m_friends.at(i).friendId;
    if (sameOrder) {
        for (int i = 0; i < friends.size(); ++i) {
            if (sameRow(friends.at(i), m_friends.at(i)))
                continue;
            m_friends[i] = friends.at(i);
            const QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx);
        }
        return;
    }

    beginResetModel();
    m_friends = std::move(friends);
    endResetModel();
    emit countChanged();
}

QVariantMap FriendsModel::friendInfo(int row) const
{
    if (row < 0 || row >= m_friends.size())
        return {};
    const FriendEntry& entry = m_friends.at(row);
    return {
        {QStringLiteral("friendId"), entry.friendId},
        {QStringLiteral("nickname"), entry.nickname},
        {QStringLiteral("publicKey"), entry.publicKey},
        {QStringLiteral("online"), entry.online},
        {QStringLiteral("currentGameId"), entry.currentGameId},
        {QStringLiteral("currentGameTitle"), entry.currentGameTitle},
        {QStringLiteral("currentGameCoverUrl"), entry.currentGameCoverUrl},
        {QStringLiteral("addedAt"), entry.addedAt},
        {QStringLiteral("lastSeenAt"), entry.lastSeenAt},
        {QStringLiteral("suggestedGameId"), entry.suggestedGameId},
        {QStringLiteral("suggestedGameTitle"), entry.suggestedGameTitle},
        {QStringLiteral("suggestedCoverUrl"), entry.suggestedCoverUrl},
        {QStringLiteral("suggestedAt"), entry.suggestedAt},
    };
}

int FriendsModel::indexOfFriend(const QString& friendId) const
{
    for (int i = 0; i < m_friends.size(); ++i) {
        if (m_friends.at(i).friendId == friendId)
            return i;
    }
    return -1;
}

} // namespace arachnel::core
