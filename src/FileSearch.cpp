#include "FileSearch.h"

#include "FileTree.h"

#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QPromise>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <numeric>
#include <utility>

namespace kode
{

using namespace Qt::StringLiterals;

namespace
{

// More would be a list nobody reads: the best ones are kept.
constexpr qsizetype mostMatches{200};
// A folder this large is listed this far.
constexpr qsizetype mostFiles{200000};
// Score tiers apart: a better tier always wins.
constexpr qint32 tier{10000};

// Folders of what is built or fetched, not written.
bool leftOut(const QString &name)
{
    static constexpr std::array names{u".git", u"node_modules", u"build"};
    return std::ranges::contains(names, QStringView{name}) || name.startsWith(u"cmake-build-"_s);
}

// A word starts at the name's start, after a separator or at a capital after a small letter.
bool startsWord(QStringView name, qsizetype at)
{
    if (at == 0) {
        return true;
    }
    const QChar before{name.at(at - 1)};
    return !before.isLetterOrNumber() || (name.at(at).isUpper() && before.isLower());
}

// Each of `query`'s letters in `name` in order, the next starting a word
// before the next at all; empty when one is missing.
QList<qsizetype> lettersInOrder(QStringView name, QStringView query)
{
    QList<qsizetype> found;
    found.reserve(query.size());
    qsizetype next{0};
    for (const QChar character : query) {
        qsizetype any{-1};
        qsizetype word{-1};
        for (qsizetype at{next}; at < name.size() && word < 0; ++at) {
            if (name.at(at).toCaseFolded() != character.toCaseFolded()) {
                continue;
            }
            any = any < 0 ? at : any;
            word = startsWord(name, at) ? at : -1;
        }
        const qsizetype at{word >= 0 ? word : any};
        if (at < 0) {
            return {};
        }
        found.append(at);
        next = at + 1;
    }
    return found;
}

} // namespace

FileSearch::FileSearch(QObject *parent) : QAbstractListModel(parent)
{
    // Each key typed: filtered once the event loop is free.
    m_filterTimer.setSingleShot(true);
    m_filterTimer.setInterval(0);
    connect(&m_filterTimer, &QTimer::timeout, this, &FileSearch::filter);
}

qint32 FileSearch::score(QStringView name, QStringView query)
{
    if (name.startsWith(query, Qt::CaseInsensitive)) {
        return 0;
    }
    if (const qsizetype at{name.indexOf(query, 0, Qt::CaseInsensitive)}; at >= 0) {
        return tier + static_cast<qint32>(at);
    }
    const QList<qsizetype> letters{lettersInOrder(name, query)};
    if (letters.isEmpty()) {
        return -1;
    }
    const auto wordStarts{std::ranges::count_if(letters, [name](qsizetype at) { return startsWord(name, at); })};
    constexpr qint32 perMiss{100};
    return (2 * tier) + (static_cast<qint32>(query.size() - wordStarts) * perMiss) +
           static_cast<qint32>(letters.last());
}

QList<qsizetype> FileSearch::matchedLetters(QStringView name, QStringView query)
{
    if (const qsizetype at{name.indexOf(query, 0, Qt::CaseInsensitive)}; at >= 0) {
        QList<qsizetype> letters(query.size());
        std::ranges::iota(letters, at);
        return letters;
    }
    return lettersInOrder(name, query);
}

QStringList FileSearch::listFiles(const QString &folder, bool hidden, qsizetype limit)
{
    const QDir root{folder};
    QStringList files;
    QStringList folders{folder};
    const QDir::Filters filters{
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | (hidden ? QDir::Hidden : QDir::Filters{})
    };
    while (!folders.isEmpty() && files.size() < limit) {
        const QString current{folders.takeLast()};
        for (const QFileInfo &entry : QDir{current}.entryInfoList(filters)) {
            if (!entry.isDir()) {
                files.append(root.relativeFilePath(entry.filePath()));
            } else if (!entry.isSymLink() && !leftOut(entry.fileName())) {
                // Links to folders aside: they may lead back up.
                folders.append(entry.filePath());
            }
        }
    }
    return files;
}

void FileSearch::setFolder(const QUrl &folder)
{
    if (folder == m_folder) {
        return;
    }
    m_folder = folder;
    m_files.clear();
    emit folderChanged();
    if (active()) {
        list();
    }
    filter();
}

void FileSearch::setQuery(const QString &query)
{
    if (query == m_query) {
        return;
    }
    m_query = query;
    emit queryChanged();
    m_filterTimer.start();
}

void FileSearch::setShowHidden(bool show)
{
    if (show == showHidden()) {
        return;
    }
    m_flags = show ? (m_flags | ShowHidden) : (m_flags & ~ShowHidden);
    emit showHiddenChanged();
    if (active()) {
        list();
    }
}

void FileSearch::setActive(bool active)
{
    if (active == this->active()) {
        return;
    }
    m_flags = active ? (m_flags | Active) : (m_flags & ~Active);
    emit activeChanged();
    // Listed again each time: files come and go between searches.
    if (active) {
        list();
    }
}

void FileSearch::setMarkColor(const QColor &color)
{
    if (color == m_markColor) {
        return;
    }
    m_markColor = color;
    emit markColorChanged();
    if (!m_matches.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<qint32>(m_matches.size()) - 1), {MarkedNameRole});
    }
}

QUrl FileSearch::fileAt(qint32 row) const
{
    if (row < 0 || row >= m_matches.size()) {
        return {};
    }
    return QUrl::fromLocalFile(m_folder.toLocalFile() + u'/' + m_files.at(m_matches.at(row)));
}

void FileSearch::list()
{
    const quint64 generation{++m_generation};
    if (!listing()) {
        m_flags |= Listing;
        emit listingChanged();
    }
    QPromise<QStringList> promise;
    QFuture<QStringList> future{promise.future()};
    QThreadPool::globalInstance()->start(
        // A whole word for the flag: the lambda keeps no padding.
        [promise = std::move(promise), folder = m_folder.toLocalFile(), hidden = qint64{showHidden()}]() mutable {
            promise.start();
            promise.addResult(listFiles(folder, hidden != 0, mostFiles));
            promise.finish();
        }
    );
    future.then(this, [this, generation](QStringList files) {
        if (generation != m_generation) {
            return;
        }
        m_files = std::move(files);
        m_flags &= ~Listing;
        emit listingChanged();
        filter();
    });
}

void FileSearch::filter()
{
    const QString query{m_query.trimmed()};
    // Best first: the score, then the shorter path.
    struct Match {
        qsizetype file{0};
        qint32 score{0};
        qint32 length{0};
    };
    QList<Match> found;
    if (!query.isEmpty()) {
        for (qsizetype file{0}; file < m_files.size(); ++file) {
            const QString &path{m_files.at(file)};
            const QStringView name{QStringView{path}.sliced(path.lastIndexOf(u'/') + 1)};
            if (const qint32 score{FileSearch::score(name, query)}; score >= 0) {
                found.append({.file = file, .score = score, .length = static_cast<qint32>(path.size())});
            }
        }
    }
    const auto best{[](const Match &match) { return std::pair{match.score, match.length}; }};
    if (found.size() > mostMatches) {
        std::ranges::nth_element(found, found.begin() + mostMatches, {}, best);
        found.resize(mostMatches);
    }
    std::ranges::sort(found, {}, best);
    beginResetModel();
    m_matches.clear();
    m_matches.reserve(found.size());
    std::ranges::transform(found, std::back_inserter(m_matches), &Match::file);
    endResetModel();
    emit countChanged();
}

qint32 FileSearch::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<qint32>(m_matches.size());
}

QVariant FileSearch::data(const QModelIndex &index, qint32 role) const
{
    if (!index.isValid() || index.row() >= m_matches.size()) {
        return {};
    }
    const QString &relative{m_files.at(m_matches.at(index.row()))};
    const QString path{m_folder.toLocalFile() + u'/' + relative};
    const QString name{relative.sliced(relative.lastIndexOf(u'/') + 1)};
    switch (role) {
    case FileNameRole:
        return name;
    case MarkedNameRole: {
        const QList<qsizetype> letters{matchedLetters(name, m_query.trimmed())};
        QString marked;
        for (qsizetype at{0}; at < name.size(); ++at) {
            const QString letter{QString{name.at(at)}.toHtmlEscaped()};
            marked +=
                letters.contains(at)
                    ? u"<span style=\"background-color:%1\">%2</span>"_s.arg(m_markColor.name(QColor::HexArgb), letter)
                    : letter;
        }
        return marked;
    }
    case PathRole: {
        const QString home{QDir::homePath()};
        return path.startsWith(home + u'/') ? u"~"_s + path.sliced(home.size()) : path;
    }
    case FileUrlRole:
        return QUrl::fromLocalFile(path);
    case IconSourceRole:
        return FileTree::iconSource(path, false);
    default:
        return {};
    }
}

QHash<qint32, QByteArray> FileSearch::roleNames() const
{
    return {
        {FileNameRole, "fileName"},
        {MarkedNameRole, "markedName"},
        {PathRole, "path"},
        {FileUrlRole, "fileUrl"},
        {IconSourceRole, "iconSource"},
    };
}

} // namespace kode
