#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QList>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode
{

// Finding a file of a folder (⌘K, as Qt Creator's Locate): the files whose
// name matches what is typed, best first. The folder's files are listed on
// a worker thread while `active` (.git, build folders and node_modules
// aside, hidden ones only with `showHidden`); each query filters that list.
class FileSearch : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    // Listing the folder's files: while the search shows.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // The folder is being listed.
    Q_PROPERTY(bool listing READ listing NOTIFY listingChanged)
    Q_PROPERTY(qint32 count READ count NOTIFY countChanged)
    // Behind the letters of a name that match (`markedName`).
    Q_PROPERTY(QColor markColor READ markColor WRITE setMarkColor NOTIFY markColorChanged)
public:
    enum Role : quint16 {
        FileNameRole = Qt::UserRole + 1,
        // The name as rich text, the letters that match marked.
        MarkedNameRole,
        // The file's path, the home folder as "~".
        PathRole,
        FileUrlRole,
        IconSourceRole,
    };

    explicit FileSearch(QObject *parent = nullptr);

    [[nodiscard]] qint32 rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, qint32 role) const override;
    [[nodiscard]] QHash<qint32, QByteArray> roleNames() const override;

    [[nodiscard]] QUrl folder() const { return m_folder; }
    void setFolder(const QUrl &folder);
    [[nodiscard]] QString query() const { return m_query; }
    void setQuery(const QString &query);
    [[nodiscard]] bool showHidden() const noexcept { return (m_flags & ShowHidden) != 0U; }
    void setShowHidden(bool show);
    [[nodiscard]] bool active() const noexcept { return (m_flags & Active) != 0U; }
    void setActive(bool active);
    [[nodiscard]] bool listing() const noexcept { return (m_flags & Listing) != 0U; }
    [[nodiscard]] qint32 count() const noexcept { return static_cast<qint32>(m_matches.size()); }
    [[nodiscard]] QColor markColor() const { return m_markColor; }
    void setMarkColor(const QColor &color);
    // The `row`th file found; empty past the end.
    Q_INVOKABLE [[nodiscard]] QUrl fileAt(qint32 row) const;

    // How well `name` matches `query`, lower better (case aside): starting
    // with it, then holding it, then having its letters in order, the more
    // of them starting words ("EV" in EditorView) the better; -1: no match.
    [[nodiscard]] static qint32 score(QStringView name, QStringView query);
    // Where `query`'s letters fall in `name`, as score() matched them.
    [[nodiscard]] static QList<qsizetype> matchedLetters(QStringView name, QStringView query);
    // The files under `folder` (relative paths), the folders search leaves
    // out aside; at most `limit`.
    [[nodiscard]] static QStringList listFiles(const QString &folder, bool hidden, qsizetype limit);

signals:
    void folderChanged();
    void queryChanged();
    void showHiddenChanged();
    void activeChanged();
    void listingChanged();
    void countChanged();
    void markColorChanged();

private:
    static constexpr quint64 ShowHidden{1U << 0U};
    static constexpr quint64 Active{1U << 1U};
    static constexpr quint64 Listing{1U << 2U};

    void list();
    void filter();

    QStringList m_files;
    // Indices into m_files, best first.
    QList<qsizetype> m_matches;
    QTimer m_filterTimer;
    QUrl m_folder;
    QString m_query;
    QColor m_markColor;
    // Bumped by each listing: an older one's result is dropped.
    quint64 m_generation{0};
    quint64 m_flags{0U};
};

} // namespace kode
