#include "LanguageServers.h"

#include "Document.h"
#include "LanguageServer.h"
#include "TextBuffer.h"
#include "TextDocument.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonObject>
#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <optional>
#include <span>

namespace kode::lsp
{

using namespace Qt::StringLiterals;

namespace
{

// A way to start a server: a program looked for in the search path, and its
// arguments, space separated.
struct Candidate {
    QLatin1StringView program;
    QLatin1StringView arguments;
};

constexpr std::array clang{Candidate{.program = "clangd"_L1, .arguments = {}}};
constexpr std::array python{
    Candidate{.program = "basedpyright-langserver"_L1, .arguments = "--stdio"_L1},
    Candidate{.program = "pyright-langserver"_L1, .arguments = "--stdio"_L1},
    Candidate{.program = "pylsp"_L1, .arguments = {}},
};
constexpr std::array rust{Candidate{.program = "rust-analyzer"_L1, .arguments = {}}};
constexpr std::array go{Candidate{.program = "gopls"_L1, .arguments = {}}};
constexpr std::array typescript{Candidate{.program = "typescript-language-server"_L1, .arguments = "--stdio"_L1}};
constexpr std::array qml{Candidate{.program = "qmlls"_L1, .arguments = {}}};
constexpr std::array swift{Candidate{.program = "sourcekit-lsp"_L1, .arguments = {}}};
constexpr std::array shell{Candidate{.program = "bash-language-server"_L1, .arguments = "start"_L1}};
constexpr std::array lua{Candidate{.program = "lua-language-server"_L1, .arguments = {}}};
// jdtls keeps a workspace per project in the user's caches by itself.
constexpr std::array java{Candidate{.program = "jdtls"_L1, .arguments = {}}};
// JetBrains' first, then the community's.
constexpr std::array kotlin{
    Candidate{.program = "kotlin-lsp"_L1, .arguments = "--stdio"_L1},
    Candidate{.program = "kotlin-language-server"_L1, .arguments = {}},
};
constexpr std::array yaml{Candidate{.program = "yaml-language-server"_L1, .arguments = "--stdio"_L1}};
constexpr std::array json{Candidate{.program = "vscode-json-language-server"_L1, .arguments = "--stdio"_L1}};
constexpr std::array html{Candidate{.program = "vscode-html-language-server"_L1, .arguments = "--stdio"_L1}};
constexpr std::array css{Candidate{.program = "vscode-css-language-server"_L1, .arguments = "--stdio"_L1}};
constexpr std::array markdown{Candidate{.program = "marksman"_L1, .arguments = "server"_L1}};
constexpr std::array cmake{Candidate{.program = "neocmakelsp"_L1, .arguments = "stdio"_L1}};
constexpr std::array dockerfile{Candidate{.program = "docker-langserver"_L1, .arguments = "--stdio"_L1}};
constexpr std::array terraform{Candidate{.program = "terraform-ls"_L1, .arguments = "serve"_L1}};
constexpr std::array toml{Candidate{.program = "taplo"_L1, .arguments = "lsp stdio"_L1}};

// A highlighting definition's language, by the protocol's id, and its servers.
struct Language {
    QLatin1StringView syntax;
    QLatin1StringView id;
    std::span<const Candidate> servers;
};

constexpr std::array languages{
    Language{.syntax = "C"_L1, .id = "c"_L1, .servers = clang},
    Language{.syntax = "C++"_L1, .id = "cpp"_L1, .servers = clang},
    Language{.syntax = "ISO C++"_L1, .id = "cpp"_L1, .servers = clang},
    Language{.syntax = "Objective-C"_L1, .id = "objective-c"_L1, .servers = clang},
    Language{.syntax = "Objective-C++"_L1, .id = "objective-cpp"_L1, .servers = clang},
    Language{.syntax = "Python"_L1, .id = "python"_L1, .servers = python},
    Language{.syntax = "Rust"_L1, .id = "rust"_L1, .servers = rust},
    Language{.syntax = "Go"_L1, .id = "go"_L1, .servers = go},
    Language{.syntax = "JavaScript"_L1, .id = "javascript"_L1, .servers = typescript},
    Language{.syntax = "TypeScript"_L1, .id = "typescript"_L1, .servers = typescript},
    Language{.syntax = "QML"_L1, .id = "qml"_L1, .servers = qml},
    Language{.syntax = "Swift"_L1, .id = "swift"_L1, .servers = swift},
    Language{.syntax = "Bash"_L1, .id = "shellscript"_L1, .servers = shell},
    Language{.syntax = "Zsh"_L1, .id = "shellscript"_L1, .servers = shell},
    Language{.syntax = "Lua"_L1, .id = "lua"_L1, .servers = lua},
    Language{.syntax = "Java"_L1, .id = "java"_L1, .servers = java},
    Language{.syntax = "Kotlin"_L1, .id = "kotlin"_L1, .servers = kotlin},
    Language{.syntax = "YAML"_L1, .id = "yaml"_L1, .servers = yaml},
    Language{.syntax = "JSON"_L1, .id = "json"_L1, .servers = json},
    Language{.syntax = "HTML"_L1, .id = "html"_L1, .servers = html},
    Language{.syntax = "CSS"_L1, .id = "css"_L1, .servers = css},
    Language{.syntax = "Markdown"_L1, .id = "markdown"_L1, .servers = markdown},
    Language{.syntax = "CMake"_L1, .id = "cmake"_L1, .servers = cmake},
    Language{.syntax = "Dockerfile"_L1, .id = "dockerfile"_L1, .servers = dockerfile},
    Language{.syntax = "Terraform"_L1, .id = "terraform"_L1, .servers = terraform},
    Language{.syntax = "TOML"_L1, .id = "toml"_L1, .servers = toml},
};

// The languages' names in Settings, by their servers, and how to install one.
struct ServerGroup {
    QLatin1StringView languages;
    std::span<const Candidate> servers;
    QLatin1StringView install;
};

constexpr std::array serverGroups{
    ServerGroup{.languages = "C, C++, Objective-C"_L1, .servers = clang, .install = "brew install llvm"_L1},
    ServerGroup{.languages = "Python"_L1, .servers = python, .install = "brew install basedpyright"_L1},
    ServerGroup{.languages = "Rust"_L1, .servers = rust, .install = "brew install rust-analyzer"_L1},
    ServerGroup{.languages = "Go"_L1, .servers = go, .install = "brew install gopls"_L1},
    ServerGroup{
        .languages = "JavaScript, TypeScript"_L1,
        .servers = typescript,
        .install = "brew install typescript-language-server"_L1
    },
    ServerGroup{.languages = "QML"_L1, .servers = qml, .install = "brew install qt"_L1},
    // Comes with Xcode's command-line tools.
    ServerGroup{.languages = "Swift"_L1, .servers = swift, .install = "xcode-select --install"_L1},
    ServerGroup{.languages = "Bash, Zsh"_L1, .servers = shell, .install = "brew install bash-language-server"_L1},
    ServerGroup{.languages = "Lua"_L1, .servers = lua, .install = "brew install lua-language-server"_L1},
    ServerGroup{.languages = "Java"_L1, .servers = java, .install = "brew install jdtls"_L1},
    ServerGroup{.languages = "Kotlin"_L1, .servers = kotlin, .install = "brew install --cask kotlin-lsp"_L1},
    ServerGroup{.languages = "YAML"_L1, .servers = yaml, .install = "brew install yaml-language-server"_L1},
    ServerGroup{.languages = "JSON"_L1, .servers = json, .install = "brew install vscode-langservers-extracted"_L1},
    ServerGroup{.languages = "HTML"_L1, .servers = html, .install = "brew install vscode-langservers-extracted"_L1},
    ServerGroup{.languages = "CSS"_L1, .servers = css, .install = "brew install vscode-langservers-extracted"_L1},
    ServerGroup{.languages = "Markdown"_L1, .servers = markdown, .install = "brew install marksman"_L1},
    ServerGroup{.languages = "CMake"_L1, .servers = cmake, .install = "brew install neocmakelsp"_L1},
    ServerGroup{
        .languages = "Dockerfile"_L1,
        .servers = dockerfile,
        .install = "brew install dockerfile-language-server"_L1
    },
    ServerGroup{.languages = "Terraform"_L1, .servers = terraform, .install = "brew install terraform-ls"_L1},
    ServerGroup{.languages = "TOML"_L1, .servers = toml, .install = "brew install taplo"_L1},
};

// C and the like: a header and its source.
constexpr std::array headerSuffixes{"h"_L1, "hh"_L1, "hpp"_L1, "hxx"_L1, "h++"_L1};
constexpr std::array sourceSuffixes{"c"_L1, "cc"_L1, "cpp"_L1, "cxx"_L1, "c++"_L1, "m"_L1, "mm"_L1};

// Larger texts are not sent: each would go whole to the server when opened.
constexpr qsizetype maxLength{qsizetype{8} * 1024 * 1024};
// How long the login shell may take to tell its PATH.
constexpr qint32 shellTimeoutMs{5000};
constexpr QLatin1StringView pathMarker{"__KODE_PATH__"};

const Language *languageFor(const QString &syntax)
{
    const auto found{std::ranges::find(languages, syntax, &Language::syntax)};
    return found != languages.cend() ? &*found : nullptr;
}

struct Program {
    QString path;
    QStringList arguments;
};

std::optional<Program> findProgram(std::span<const Candidate> candidates, const QStringList &folders)
{
    for (const Candidate &candidate : candidates) {
        const QString name{candidate.program};
        const QString path{QStandardPaths::findExecutable(name, folders)};
        if (!path.isEmpty()) {
            return Program{.path = path, .arguments = QString{candidate.arguments}.split(u' ', Qt::SkipEmptyParts)};
        }
    }
    return std::nullopt;
}

// A command typed by the user: its program (a path, '~' for the home
// folder, or a name looked for in `folders`) and arguments, split as a shell would.
std::optional<Program> findCommand(const QString &command, const QStringList &folders)
{
    QStringList arguments{QProcess::splitCommand(command)};
    if (arguments.isEmpty()) {
        return std::nullopt;
    }
    QString program{arguments.takeFirst()};
    if (program.startsWith(u'~')) {
        program = QDir::homePath() + program.sliced(1);
    }
    const QString path{
        program.contains(u'/') ? (QFileInfo{program}.isExecutable() ? program : QString{})
                               : QStandardPaths::findExecutable(program, folders)
    };
    if (path.isEmpty()) {
        return std::nullopt;
    }
    return Program{.path = path, .arguments = arguments};
}

bool isClangd(const QString &program)
{
    return QFileInfo{program}.fileName() == u"clangd"_s;
}

// The other file of a header and its source, beside it; empty: none.
QString counterpart(const QString &file)
{
    const QFileInfo info{file};
    const QString suffix{info.suffix().toLower()};
    const bool header{std::ranges::contains(headerSuffixes, suffix)};
    if (!header && !std::ranges::contains(sourceSuffixes, suffix)) {
        return {};
    }
    const QString base{info.path() + u'/' + info.completeBaseName() + u'.'};
    for (const QLatin1StringView other : header ? std::span<const QLatin1StringView>{sourceSuffixes}
                                                : std::span<const QLatin1StringView>{headerSuffixes}) {
        if (const QString candidate{base + other}; QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

// A Location or LocationLink: where it points.
QVariantMap location(const QJsonValue &value)
{
    const bool link{value[u"targetUri"_s].isString()};
    const QUrl file{value[link ? u"targetUri"_s : u"uri"_s].toString()};
    const QJsonValue start{value[link ? u"targetSelectionRange"_s : u"range"_s][u"start"_s]};
    const qint64 line{start[u"line"_s].toInteger()};
    return {
        {u"file"_s, file},
        {u"line"_s, line},
        {u"column"_s, start[u"character"_s].toInteger()},
        {u"label"_s, u"%1:%2"_s.arg(file.fileName()).arg(line + 1)},
    };
}

// textDocument/definition's result: none, one or a list.
QVariantList locations(const QJsonValue &result)
{
    QVariantList list;
    if (result.isObject()) {
        list.append(location(result));
    }
    // Parentheses: braces would make a list holding the array.
    const QJsonArray items(result.toArray());
    std::ranges::transform(items, std::back_inserter(list), [](const QJsonValue &item) { return location(item); });
    return list;
}

// The nearest folder above `file` with a .git, else the file's.
QUrl rootFor(const QString &file)
{
    const QString folder{QFileInfo{file}.absolutePath()};
    QDir dir{folder};
    while (!dir.exists(u".git"_s)) {
        if (!dir.cdUp()) {
            return QUrl::fromLocalFile(folder);
        }
    }
    return QUrl::fromLocalFile(dir.absolutePath());
}

// The folder of the compilation database clangd would not find itself: it
// looks in the root and in build/, not in a build folder of each
// configuration (build/Debug, cmake-build-debug…). The newest wins.
QString compilationDatabase(const QString &root)
{
    const QString name{u"compile_commands.json"_s};
    const QDir dir{root};
    if (dir.exists(name) || dir.exists(u"build/"_s + name)) {
        return {};
    }
    QFileInfoList found;
    for (const QString &parent : {root, root + u"/build"_s}) {
        for (const QFileInfo &folder : QDir{parent}.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (const QFileInfo file{folder.filePath() + u'/' + name}; file.isFile()) {
                found.append(file);
            }
        }
    }
    const auto newest{std::ranges::max_element(found, {}, [](const QFileInfo &file) { return file.lastModified(); })};
    return newest != found.cend() ? newest->absolutePath() : QString{};
}

QJsonObject serverOptions(const Program &program, const QUrl &root)
{
    if (!isClangd(program.path)) {
        return {};
    }
    QJsonObject options{{u"clangdFileStatus"_s, true}};
    if (const QString database{compilationDatabase(root.toLocalFile())}; !database.isEmpty()) {
        options.insert(u"compilationDatabasePath"_s, database);
    }
    return options;
}

QJsonObject protocolPosition(qsizetype line, qsizetype character)
{
    return {{u"line"_s, line}, {u"character"_s, character}};
}

// Characters, from `value`'s line and character (UTF-16, as the buffer), clamped to the text.
qsizetype positionIn(const TextBuffer &buffer, const QJsonValue &value)
{
    const qsizetype line{std::clamp(value[u"line"_s].toInteger(), qint64{0}, buffer.lineCount() - 1)};
    const qsizetype character{std::clamp(value[u"character"_s].toInteger(), qint64{0}, buffer.lineLength(line))};
    return buffer.lineStart(line) + character;
}

} // namespace

LanguageServers::LanguageServers(QObject *parent) : QObject(parent)
{
    m_bindTimer.setSingleShot(true);
    m_bindTimer.setInterval(0);
    connect(&m_bindTimer, &QTimer::timeout, this, &LanguageServers::bindScheduled);
    // An interactive login shell, as a terminal starts: PATH is often set in .zshrc.
    m_shell.setProgram(qEnvironmentVariable("SHELL", u"/bin/zsh"_s));
    m_shell.setArguments({u"-ilc"_s, u"printf '\\n%s%s%s' %1 \"$PATH\" %1"_s.arg(pathMarker)});
    m_shell.setStandardInputFile(QProcess::nullDevice());
    m_shell.setStandardErrorFile(QProcess::nullDevice());
    connect(&m_shell, &QProcess::finished, this, &LanguageServers::readShellPath);
    connect(&m_shell, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            readShellPath();
        }
    });
    m_shellTimeout.setSingleShot(true);
    connect(&m_shellTimeout, &QTimer::timeout, &m_shell, &QProcess::kill);
    m_shell.start();
    m_shellTimeout.start(shellTimeoutMs);
}

LanguageServers::~LanguageServers() = default;

void LanguageServers::readShellPath()
{
    m_shellTimeout.stop();
    const QString output{QString::fromLocal8Bit(m_shell.readAllStandardOutput())};
    const qsizetype start{output.indexOf(pathMarker)};
    const qsizetype end{output.lastIndexOf(pathMarker)};
    QStringList folders{
        start >= 0 && end > start ? output.mid(start + pathMarker.size(), end - start - pathMarker.size()).split(u':')
                                  : qEnvironmentVariable("PATH").split(u':')
    };
    folders.removeAll(QString{});
    m_shellPath = folders.join(u':');
    restart();
}

QStringList LanguageServers::searchPath() const
{
    QStringList folders{m_searchFolders};
    for (const QString &folder : m_shellPath.split(u':', Qt::SkipEmptyParts)) {
        if (!folders.contains(folder)) {
            folders.append(folder);
        }
    }
    return folders;
}

QStringList LanguageServers::shellPath() const
{
    return m_shellPath.split(u':', Qt::SkipEmptyParts);
}

void LanguageServers::setEnabled(bool enabled)
{
    if (enabled == this->enabled()) {
        return;
    }
    m_flags = enabled ? (m_flags | Enabled) : (m_flags & ~Enabled);
    emit settingsChanged();
    restart();
}

void LanguageServers::setSearchFolders(const QStringList &folders)
{
    if (folders == m_searchFolders) {
        return;
    }
    m_searchFolders = folders;
    emit settingsChanged();
    restart();
}

void LanguageServers::setClangdThreads(qint32 threads)
{
    if (threads == m_clangdThreads) {
        return;
    }
    m_clangdThreads = threads;
    emit settingsChanged();
    restart();
}

void LanguageServers::copyInstallCommand(qint32 index)
{
    if (index >= 0 && index < static_cast<qint32>(serverGroups.size())) {
        QGuiApplication::clipboard()->setText(QString{serverGroups.at(static_cast<std::size_t>(index)).install});
    }
}

void LanguageServers::setCustomServers(const QVariantList &servers)
{
    if (servers == m_customServers) {
        return;
    }
    m_customServers = servers;
    emit settingsChanged();
    restart();
}

std::optional<LanguageServers::Choice> LanguageServers::serverFor(const QString &syntax) const
{
    // The user's own first, for the syntax as the status bar shows it.
    const auto custom{std::ranges::find_if(m_customServers, [&syntax](const QVariant &item) {
        return item.toMap().value(u"syntax"_s).toString() == syntax;
    })};
    if (custom != m_customServers.cend()) {
        const QVariantMap server{custom->toMap()};
        std::optional<Program> program{findCommand(server.value(u"command"_s).toString(), searchPath())};
        if (!program.has_value()) {
            return std::nullopt;
        }
        const QString id{server.value(u"languageId"_s).toString()};
        return Choice{
            .path = program->path,
            .arguments = program->arguments,
            .languageId = id.isEmpty() ? syntax.toLower() : id,
        };
    }
    const Language *const language{languageFor(syntax)};
    if (language == nullptr) {
        return std::nullopt;
    }
    std::optional<Program> program{findProgram(language->servers, searchPath())};
    if (!program.has_value()) {
        return std::nullopt;
    }
    if (isClangd(program->path)) {
        // Analyzing and indexing in the background, on few threads: less CPU.
        program->arguments << u"-j=%1"_s.arg(m_clangdThreads) << u"--background-index-priority=low"_s;
    }
    return Choice{.path = program->path, .arguments = program->arguments, .languageId = QString{language->id}};
}

void LanguageServers::detect()
{
    if (m_shell.state() != QProcess::NotRunning) {
        return;
    }
    m_shell.start();
    m_shellTimeout.start(shellTimeoutMs);
}

void LanguageServers::restart()
{
    for (Entry &entry : m_entries) {
        unbind(entry);
    }
    m_servers.clear();
    m_found.clear();
    const QStringList folders{searchPath()};
    for (const ServerGroup &group : serverGroups) {
        const std::optional<Program> program{findProgram(group.servers, folders)};
        QStringList names;
        for (const Candidate &candidate : group.servers) {
            if (const QString name{QFileInfo{QString{candidate.program}}.fileName()}; !names.contains(name)) {
                names.append(name);
            }
        }
        m_found.append(QVariantMap{
            {u"languages"_s, QString{group.languages}},
            {u"path"_s, program.has_value() ? program->path : QString{}},
            {u"servers"_s, names.join(u", "_s)},
            {u"install"_s, QString{group.install}},
        });
    }
    m_customFound.clear();
    for (const QVariant &item : std::as_const(m_customServers)) {
        QVariantMap server{item.toMap()};
        const std::optional<Program> program{findCommand(server.value(u"command"_s).toString(), folders)};
        server.insert(u"path"_s, program.has_value() ? program->path : QString{});
        m_customFound.append(server);
    }
    emit serversChanged();
    if (!enabled() || m_shellPath.isEmpty()) {
        return;
    }
    for (Entry &entry : m_entries) {
        bind(entry);
    }
}

qint32 LanguageServers::findDefinition(const Document *document, qsizetype position)
{
    const auto found{m_entries.constFind(document)};
    if (found == m_entries.cend() || found->server == nullptr || found->document == nullptr) {
        return 0;
    }
    const TextBuffer &buffer{found->document->textDocument()->buffer()};
    const qsizetype line{buffer.lineAt(position)};
    const qint32 ticket{++m_lastTicket};
    find(found->server)
        ->request(
            u"textDocument/definition"_s,
            QJsonObject{
                {u"textDocument"_s, QJsonObject{{u"uri"_s, found->uri.toString()}}},
                {u"position"_s, protocolPosition(line, position - buffer.lineStart(line))},
            },
            // A qint64 copy: the lambda keeps no padding.
            [this, wide = qint64{ticket}](const QJsonValue &result) {
                emit locationsFound(static_cast<qint32>(wide), locations(result));
            }
        );
    return ticket;
}

qint32 LanguageServers::switchHeaderSource(const Document *document)
{
    if (document == nullptr || !document->fileUrl().isLocalFile()) {
        return 0;
    }
    const qint32 ticket{++m_lastTicket};
    const auto answer{[this, wide = qint64{ticket}](const QUrl &file) {
        QVariantList list;
        if (!file.isEmpty()) {
            list.append(QVariantMap{
                {u"file"_s, file},
                {u"line"_s, -1},
                {u"column"_s, 0},
                {u"label"_s, file.fileName()},
            });
        }
        emit locationsFound(static_cast<qint32>(wide), list);
    }};
    if (const QString beside{counterpart(document->fileUrl().toLocalFile())}; !beside.isEmpty()) {
        // After the caller has its ticket.
        QTimer::singleShot(0, this, [answer, beside] { answer(QUrl::fromLocalFile(beside)); });
        return ticket;
    }
    // clangd knows the project's: textDocument/switchSourceHeader, its own.
    const auto found{m_entries.constFind(document)};
    if (found == m_entries.cend() || found->server == nullptr) {
        QTimer::singleShot(0, this, [answer] { answer({}); });
        return ticket;
    }
    LanguageServer *const server{find(found->server)};
    if (server == nullptr || !isClangd(server->program())) {
        QTimer::singleShot(0, this, [answer] { answer({}); });
        return ticket;
    }
    server->request(
        u"textDocument/switchSourceHeader"_s,
        QJsonObject{{u"uri"_s, found->uri.toString()}},
        [answer](const QJsonValue &result) { answer(QUrl{result.toString()}); }
    );
    return ticket;
}

void LanguageServers::add(Document *document)
{
    m_entries.insert(document, Entry{.document = document, .uri = {}, .server = nullptr, .version = 0});
    const TextDocument *const text{document->textDocument()};
    connect(document, &Document::fileUrlChanged, this, [this, document] { scheduleBind(document); });
    connect(document, &Document::loadingChanged, this, [this, document] { scheduleBind(document); });
    connect(text, &TextDocument::syntaxChanged, this, [this, document] { scheduleBind(document); });
    connect(
        text,
        &TextDocument::replaced,
        this,
        [this, document](qsizetype position, const QString &removed, const QString &inserted) {
            documentEdited(document, position, removed, inserted);
        }
    );
    connect(text, &TextDocument::reset, this, [this, document] { documentReset(document); });
    scheduleBind(document);
}

void LanguageServers::remove(const Document *document)
{
    const auto found{m_entries.find(document)};
    if (found == m_entries.end()) {
        return;
    }
    unbind(*found);
    if (found->document != nullptr) {
        disconnect(found->document, nullptr, this, nullptr);
        disconnect(found->document->textDocument(), nullptr, this, nullptr);
    }
    m_toBind.remove(document);
    m_entries.erase(found);
}

void LanguageServers::scheduleBind(const Document *document)
{
    m_toBind.insert(document);
    m_bindTimer.start();
}

void LanguageServers::bindScheduled()
{
    for (const Document *document : std::as_const(m_toBind)) {
        if (const auto found{m_entries.find(document)}; found != m_entries.end()) {
            bind(*found);
        }
    }
    m_toBind.clear();
}

void LanguageServers::bind(Entry &entry)
{
    unbind(entry);
    Document *const document{entry.document};
    if (!enabled() || m_shellPath.isEmpty() || document == nullptr || document->loading() ||
        !document->fileUrl().isLocalFile()) {
        return;
    }
    TextDocument *const text{document->textDocument()};
    if (text->length() > maxLength) {
        return;
    }
    const std::optional<Choice> choice{serverFor(text->syntax())};
    if (!choice.has_value()) {
        return;
    }
    const QString file{document->fileUrl().toLocalFile()};
    const QUrl root{rootFor(file)};
    const Program program{.path = choice->path, .arguments = choice->arguments};
    LanguageServer *const server{this->server(program.path, program.arguments, serverOptions(program, root), root)};
    entry.server = server;
    entry.uri = QUrl::fromLocalFile(file);
    entry.version = 1;
    server->notify(
        u"textDocument/didOpen"_s,
        QJsonObject{
            {u"textDocument"_s,
             QJsonObject{
                 {u"uri"_s, entry.uri.toString()},
                 {u"languageId"_s, choice->languageId},
                 {u"version"_s, entry.version},
                 {u"text"_s, text->text()},
             }}
        }
    );
    showStatus(entry);
}

void LanguageServers::unbind(Entry &entry)
{
    if (entry.server == nullptr) {
        return;
    }
    if (LanguageServer *const server{find(entry.server)}; server != nullptr) {
        server->notify(
            u"textDocument/didClose"_s,
            QJsonObject{{u"textDocument"_s, QJsonObject{{u"uri"_s, entry.uri.toString()}}}}
        );
    }
    entry.server = nullptr;
    entry.fileState.clear();
    if (entry.document == nullptr) {
        return;
    }
    TextDocument *const text{entry.document->textDocument()};
    text->setLanguageServer({}, {});
    if (!text->diagnostics().isEmpty()) {
        text->setDiagnostics({});
    }
}

void LanguageServers::showStatus(const Entry &entry)
{
    if (entry.document == nullptr || entry.server == nullptr) {
        return;
    }
    const LanguageServer *const server{find(entry.server)};
    // clangd's "idle" and the like: done with this file.
    const bool analyzing{!entry.fileState.isEmpty() && entry.fileState != u"idle"_s};
    entry.document->textDocument()->setLanguageServer(
        QFileInfo{server->program()}.fileName(),
        analyzing ? tr("Analyzing") : server->activity()
    );
}

void LanguageServers::fileStatusChanged(const LanguageServer *server, const QUrl &uri, const QString &state)
{
    const auto found{std::ranges::find_if(m_entries, [server, &uri](const Entry &entry) {
        return entry.server == server && entry.uri == uri;
    })};
    if (found == m_entries.end()) {
        return;
    }
    found->fileState = state;
    showStatus(*found);
}

void LanguageServers::activityChanged(const LanguageServer *server)
{
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.server == server) {
            showStatus(entry);
        }
    }
}

void LanguageServers::documentEdited(
    const Document *document,
    qsizetype position,
    const QString &removed,
    const QString &inserted
)
{
    const auto found{m_entries.find(document)};
    if (found == m_entries.end() || found->server == nullptr || found->document == nullptr) {
        return;
    }
    LanguageServer *const server{find(found->server)};
    const TextBuffer &buffer{found->document->textDocument()->buffer()};
    QJsonObject change;
    if (server->incrementalSync()) {
        // The range is in the text before the edit: from the start (which
        // the edit leaves where it was) over what it removed.
        const qsizetype line{buffer.lineAt(position)};
        const qsizetype character{position - buffer.lineStart(line)};
        const qsizetype removedLines{removed.count(u'\n')};
        const qsizetype endCharacter{
            removedLines == 0 ? character + removed.size() : removed.size() - removed.lastIndexOf(u'\n') - 1
        };
        change = {
            {u"range"_s,
             QJsonObject{
                 {u"start"_s, protocolPosition(line, character)},
                 {u"end"_s, protocolPosition(line + removedLines, endCharacter)}
             }},
            {u"text"_s, inserted},
        };
    } else {
        change = {{u"text"_s, buffer.toString()}};
    }
    server->notify(
        u"textDocument/didChange"_s,
        QJsonObject{
            {u"textDocument"_s, QJsonObject{{u"uri"_s, found->uri.toString()}, {u"version"_s, ++found->version}}},
            {u"contentChanges"_s, QJsonArray{change}},
        }
    );
}

void LanguageServers::documentReset(const Document *document)
{
    const auto found{m_entries.find(document)};
    if (found == m_entries.end() || found->server == nullptr || found->document == nullptr) {
        return;
    }
    find(found->server)
        ->notify(
            u"textDocument/didChange"_s,
            QJsonObject{
                {u"textDocument"_s, QJsonObject{{u"uri"_s, found->uri.toString()}, {u"version"_s, ++found->version}}},
                {u"contentChanges"_s, QJsonArray{QJsonObject{{u"text"_s, found->document->textDocument()->text()}}}},
            }
        );
}

void LanguageServers::diagnosticsPublished(
    const LanguageServer *server,
    const QUrl &uri,
    qint64 version,
    const QJsonArray &list
)
{
    const auto found{std::ranges::find_if(m_entries, [server, &uri](const Entry &entry) {
        return entry.server == server && entry.uri == uri;
    })};
    // Diagnostics of an older version would land on moved text: newer ones follow.
    if (found == m_entries.end() || found->document == nullptr || (version >= 0 && version != found->version)) {
        return;
    }
    TextDocument *const text{found->document->textDocument()};
    const TextBuffer &buffer{text->buffer()};
    QList<TextDocument::Diagnostic> diagnostics;
    diagnostics.reserve(list.size());
    std::ranges::transform(list, std::back_inserter(diagnostics), [&buffer](const QJsonValue &item) {
        const QJsonValue range{item[u"range"_s]};
        return TextDocument::Diagnostic{
            .message = item[u"message"_s].toString(),
            .start = positionIn(buffer, range[u"start"_s]),
            .end = positionIn(buffer, range[u"end"_s]),
            .severity = item[u"severity"_s].toInteger(TextDocument::Diagnostic::Error),
        };
    });
    text->setDiagnostics(std::move(diagnostics));
}

void LanguageServers::serverFinished(const LanguageServer *server)
{
    for (Entry &entry : m_entries) {
        if (entry.server != server) {
            continue;
        }
        entry.server = nullptr;
        entry.fileState.clear();
        if (entry.document != nullptr) {
            entry.document->textDocument()->setDiagnostics({});
            entry.document->textDocument()->setLanguageServer({}, {});
        }
    }
    std::erase_if(m_servers, [server](const std::unique_ptr<LanguageServer> &owned) { return owned.get() == server; });
}

LanguageServer *LanguageServers::server(
    const QString &program,
    const QStringList &arguments,
    const QJsonObject &options,
    const QUrl &root
)
{
    const auto found{std::ranges::find_if(m_servers, [&](const std::unique_ptr<LanguageServer> &server) {
        return server->program() == program && server->arguments() == arguments && server->root() == root;
    })};
    if (found != m_servers.cend()) {
        return found->get();
    }
    auto started{std::make_unique<LanguageServer>(program, arguments, options, searchPath().join(u':'), root)};
    const LanguageServer *const key{started.get()};
    connect(
        started.get(),
        &LanguageServer::diagnosticsPublished,
        this,
        [this, key](const QUrl &uri, qint64 version, const QJsonArray &list) {
            diagnosticsPublished(key, uri, version, list);
        }
    );
    connect(
        started.get(),
        &LanguageServer::fileStatusChanged,
        this,
        [this, key](const QUrl &uri, const QString &state) { fileStatusChanged(key, uri, state); }
    );
    connect(started.get(), &LanguageServer::activityChanged, this, [this, key] { activityChanged(key); });
    // Queued: the server is deleted then, not from its own signal.
    connect(started.get(), &LanguageServer::finished, this, [this, key] { serverFinished(key); }, Qt::QueuedConnection);
    m_servers.emplace_back(std::move(started));
    return m_servers.back().get();
}

LanguageServer *LanguageServers::find(const LanguageServer *server) const
{
    const auto found{std::ranges::find(m_servers, server, &std::unique_ptr<LanguageServer>::get)};
    return found != m_servers.cend() ? found->get() : nullptr;
}

} // namespace kode::lsp
