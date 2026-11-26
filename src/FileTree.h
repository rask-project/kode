#pragma once

#include <QCollator>
#include <QFileSystemModel>
#include <QList>
#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode
{

// A folder's files for the sidebar's TreeView, as VS Code's explorer:
// folders first, then files, by name as the Finder sorts them (numbers by
// value). QFileSystemModel reads the folders in its own thread as they are
// expanded and follows changes on disk; this keeps the name column only.
// Also what the sidebar's menu does to files (FileOperations): each reports
// a failure with errorOccurred(), and a move or rename with moved() (open
// documents follow their file).
class FileTree : public QSortFilterProxyModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QUrl folder READ folder WRITE setFolder NOTIFY folderChanged)
    // The folder that holds it; none at the top of the disk.
    Q_PROPERTY(QUrl parentFolder READ parentFolder NOTIFY folderChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    // The folder's index, for TreeView.rootIndex; invalid without a folder.
    Q_PROPERTY(QModelIndex rootIndex READ rootIndex NOTIFY rootIndexChanged)
public:
    enum Role : quint16 {
        // "image://codeicon/<language>" for a code file (CodeIcons), else
        // "image://fileicon/<path>": the system's icon (FileIcons).
        IconSourceRole = Qt::UserRole + 100,
        IsFolderRole,
        FileUrlRole,
    };
    Q_ENUM(Role)

    explicit FileTree(QObject *parent = nullptr);
    ~FileTree() override;

    [[nodiscard]] QUrl folder() const { return m_folder; }
    [[nodiscard]] QUrl parentFolder() const;
    void setFolder(const QUrl &url);
    [[nodiscard]] bool showHidden() const;
    void setShowHidden(bool show);
    [[nodiscard]] QModelIndex rootIndex() const;
    // `file` is in the folder (at any depth).
    Q_INVOKABLE [[nodiscard]] bool contains(const QUrl &file) const;
    // The entry of `file`, invalid when it is not in the folder. The folders
    // on the way are read in full (TreeView reads a folder only when it has
    // no entries yet, and this one would have one).
    Q_INVOKABLE [[nodiscard]] QModelIndex indexOf(const QUrl &file);
    // For QML, which cannot ask a QModelIndex.
    Q_INVOKABLE [[nodiscard]] static bool isValid(const QModelIndex &index) noexcept { return index.isValid(); }

    // The folder `url` is, or the one it is in: where a new file or a paste goes.
    Q_INVOKABLE [[nodiscard]] static QUrl folderFor(const QUrl &url);
    // Each returns the new entry, or an empty URL (and errorOccurred()).
    Q_INVOKABLE QUrl createFile(const QUrl &folder);
    Q_INVOKABLE QUrl createFolder(const QUrl &folder);
    Q_INVOKABLE QUrl rename(const QUrl &url, const QString &name);
    Q_INVOKABLE QUrl duplicate(const QUrl &url);
    Q_INVOKABLE bool moveToTrash(const QUrl &url);
    // Puts `url` on the clipboard as a file (the Finder pastes it too); a cut
    // one moves when pasted here.
    Q_INVOKABLE void copy(const QUrl &url, bool cut);
    // The clipboard holds files.
    Q_INVOKABLE [[nodiscard]] static bool canPaste();
    // Copies (moves, when cut here) the clipboard's files into `folder`; the last one pasted.
    Q_INVOKABLE QUrl paste(const QUrl &folder);
    Q_INVOKABLE static void copyPath(const QUrl &url);
    Q_INVOKABLE static void revealInFinder(const QUrl &url);

    [[nodiscard]] QVariant data(const QModelIndex &index, qint32 role) const override;
    [[nodiscard]] QHash<qint32, QByteArray> roleNames() const override;

signals:
    void folderChanged();
    void showHiddenChanged();
    void rootIndexChanged();
    // The entries of the folder `url` were read (they arrive from another thread).
    void folderLoaded(const QUrl &url);
    void errorOccurred(const QString &message);
    // `from` (a file or folder) is now `to`.
    void moved(const QUrl &from, const QUrl &to);

protected:
    [[nodiscard]] bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    [[nodiscard]] bool filterAcceptsColumn(qint32 column, const QModelIndex &parent) const override;

private:
    QFileSystemModel m_files;
    QCollator m_collator;
    QUrl m_folder;
    // The files cut here, until pasted.
    QList<QUrl> m_cut;
};

} // namespace kode
