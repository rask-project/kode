#include "Ssh.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <ranges>

namespace kode::remote::ssh
{

namespace
{

// Where the agents live on a host, under $HOME.
constexpr QStringView agentsFolder{u".cache/kode/remote"};

// A command for sh -c, as one word for the login shell (bash, zsh, fish, csh).
QString shell(const QString &script)
{
    return QStringLiteral("sh -c ") + quote(script);
}

// Prints the SHA-256 of "$1" with whichever tool the host has.
const QString hashFunction{QStringLiteral("h() { { sha256sum \"$1\" || shasum -a 256 \"$1\" || openssl dgst -sha256 -r "
                                          "\"$1\"; } 2>/dev/null | cut -d' ' -f1; }; ")};

QString versionFolder(const QString &version)
{
    return QStringLiteral("\"$HOME/") + agentsFolder + u'/' + version + u'"';
}

// Where Include names a relative file: in ~/.ssh.
QString includePath(const QString &path, const QString &sshFolder)
{
    if (path.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    return QDir::isAbsolutePath(path) ? path : QDir{sshFolder}.filePath(path);
}

// The files a glob names ("config.d/*").
QStringList expand(const QString &pattern)
{
    const QFileInfo info{pattern};
    if (!info.fileName().contains(QLatin1Char('*')) && !info.fileName().contains(QLatin1Char('?'))) {
        return {pattern};
    }
    QStringList files;
    const QDir folder{info.path()};
    std::ranges::for_each(folder.entryList({info.fileName()}, QDir::Files, QDir::Name), [&](const QString &name) {
        files.emplace_back(folder.filePath(name));
    });
    return files;
}

} // namespace

bool validDestination(const QString &destination)
{
    static const QRegularExpression allowed{QStringLiteral(R"(^[A-Za-z0-9_.@%+\[\]:-]+$)")};
    return !destination.isEmpty() && !destination.startsWith(QLatin1Char('-')) && allowed.match(destination).hasMatch();
}

QStringList arguments(const QString &destination, const QString &controlFolder, const QString &remoteCommand)
{
    return {
        QStringLiteral("-o"), QStringLiteral("ControlMaster=auto"),
        QStringLiteral("-o"), QStringLiteral("ControlPath=") + controlFolder + QStringLiteral("/%C"),
        QStringLiteral("-o"), QStringLiteral("ControlPersist=600"),
        QStringLiteral("-o"), QStringLiteral("ServerAliveInterval=15"),
        QStringLiteral("-o"), QStringLiteral("ServerAliveCountMax=3"),
        QStringLiteral("-o"), QStringLiteral("ConnectTimeout=20"),
        QStringLiteral("-o"), QStringLiteral("ForwardAgent=no"),
        QStringLiteral("-o"), QStringLiteral("ForwardX11=no"),
        QStringLiteral("-o"), QStringLiteral("ClearAllForwardings=yes"),
        QStringLiteral("-o"), QStringLiteral("PermitLocalCommand=no"),
        QStringLiteral("--"), destination,
        remoteCommand,
    };
}

QString quote(const QString &text)
{
    QString quoted{text};
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return u'\'' + quoted + u'\'';
}

QString probeCommand(const QString &version)
{
    return shell(
        hashFunction + QStringLiteral("uname -s; uname -m; printf '%s\\n' \"$HOME\"; f=") + versionFolder(version) +
        QStringLiteral("/kode-remote; if [ -x \"$f\" ]; then h \"$f\"; else echo none; fi")
    );
}

QString installCommand(const QString &version)
{
    const QString root{QStringLiteral("\"$HOME/") + agentsFolder + u'"'};
    const QString folder{versionFolder(version)};
    return shell(
        hashFunction + QStringLiteral("umask 077 && mkdir -p ") + folder +
        QStringLiteral(" && chmod 700 \"$HOME/.cache/kode\" ") + root + u' ' + folder + QStringLiteral(" && cat > ") +
        folder + QStringLiteral("/kode-remote.tmp && chmod 700 ") + folder +
        QStringLiteral("/kode-remote.tmp && mv -f ") + folder + QStringLiteral("/kode-remote.tmp ") + folder +
        QStringLiteral("/kode-remote && for o in ") + root + QStringLiteral("/*; do [ \"$o\" = ") + folder +
        QStringLiteral(" ] || rm -rf \"$o\"; done; h ") + folder + QStringLiteral("/kode-remote")
    );
}

QString bridgeCommand(const QString &home, const QString &version)
{
    return quote(home + u'/' + agentsFolder + u'/' + version + QStringLiteral("/kode-remote")) +
           QStringLiteral(" bridge");
}

std::optional<Probe> parseProbe(QByteArrayView output)
{
    const QStringList lines{QString::fromUtf8(output).split(QLatin1Char('\n'))};
    constexpr qsizetype fields{4};
    if (lines.size() < fields) {
        return std::nullopt;
    }
    QString arch{lines[1].trimmed()};
    if (arch == QLatin1String("aarch64")) {
        arch = QStringLiteral("arm64");
    }
    const QString hash{lines[3].trimmed()};
    Probe probe{
        .os = lines[0].trimmed().toLower(),
        .arch = arch,
        .home = lines[2],
        .hash = hash == QLatin1String("none") ? QString{} : hash,
    };
    if (probe.os.isEmpty() || probe.arch.isEmpty() || !probe.home.startsWith(QLatin1Char('/'))) {
        return std::nullopt;
    }
    return probe;
}

bool isTransient(const QString &error)
{
    static const QStringList transient{
        QStringLiteral("timed out"),
        QStringLiteral("Network is unreachable"),
        QStringLiteral("No route to host"),
        QStringLiteral("Connection refused"),
        QStringLiteral("Connection reset"),
        QStringLiteral("Connection closed"),
        QStringLiteral("closed by remote host"),
        QStringLiteral("Broken pipe"),
        QStringLiteral("Temporary failure in name resolution"),
        QStringLiteral("The connection to the host closed."),
    };
    return std::ranges::any_of(transient, [&error](const QString &part) {
        return error.contains(part, Qt::CaseInsensitive);
    });
}

QStringList configuredHosts(const QString &configPath)
{
    const QString sshFolder{QFileInfo{configPath}.absolutePath()};
    static const QRegularExpression separator{QStringLiteral(R"([\s=]+)")};
    // The files being read, innermost last: an Include is read where it stands, as ssh does.
    struct Open {
        QStringList lines;
        qsizetype next{0};
    };
    QList<Open> open;
    QSet<QString> read;
    const auto include{[&open, &read](const QString &path) {
        QFile file{path};
        if (read.contains(path) || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return;
        }
        read.insert(path);
        open.emplace_back(Open{.lines = QString::fromUtf8(file.readAll()).split(u'\n'), .next = 0});
    }};
    include(configPath);
    QStringList hosts;
    while (!open.isEmpty()) {
        Open &top{open.last()};
        if (top.next >= top.lines.size()) {
            open.removeLast();
            continue;
        }
        const QStringList words{top.lines[top.next++].trimmed().split(separator, Qt::SkipEmptyParts)};
        if (words.size() < 2 || words[0].startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QString keyword{words[0].toLower()};
        const QStringList values{words.sliced(1)};
        if (keyword == QLatin1String("include")) {
            QStringList files;
            std::ranges::for_each(values, [&](const QString &value) {
                files.append(expand(includePath(value, sshFolder)));
            });
            // Pushed last first: the first is read first.
            std::ranges::for_each(files | std::views::reverse, include);
            continue;
        }
        if (keyword != QLatin1String("host")) {
            continue;
        }
        std::ranges::for_each(values, [&hosts](const QString &name) {
            const bool pattern{
                name.contains(QLatin1Char('*')) || name.contains(QLatin1Char('?')) || name.startsWith(QLatin1Char('!'))
            };
            if (!pattern && !hosts.contains(name)) {
                hosts.append(name);
            }
        });
    }
    return hosts;
}

} // namespace kode::remote::ssh
