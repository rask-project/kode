#include "FileKinds.h"

#include <algorithm>
#include <array>

namespace kode
{

namespace
{

using namespace Qt::StringLiterals;

// `names` are whole file names, not extensions.
inline constexpr quint32 WholeName{1U << 0U};

struct Entry {
    // Extensions (the first is the key) or whole names, separated by spaces.
    QLatin1StringView names;
    QLatin1StringView label;
    QRgb color;
    quint32 flags;
};

constexpr Entry extensions(QLatin1StringView names, QLatin1StringView label, QRgb color)
{
    return Entry{.names = names, .label = label, .color = color, .flags = 0U};
}

constexpr Entry wholeNames(QLatin1StringView names, QLatin1StringView label, QRgb color)
{
    return Entry{.names = names, .label = label, .color = color, .flags = WholeName};
}

// Colors after GitHub's Linguist, darkened where white text needs it.
constexpr std::array entries{
    extensions("c"_L1, "C"_L1, 0x3949ab),
    extensions("h"_L1, "H"_L1, 0x7e57c2),
    extensions("cpp cc cxx c++ ipp tpp"_L1, "C++"_L1, 0xd6336c),
    extensions("hpp hh hxx h++"_L1, "H++"_L1, 0x9c27b0),
    extensions("m"_L1, "M"_L1, 0x3a7bd5),
    extensions("mm"_L1, "MM"_L1, 0x5b5bd6),
    extensions("swift"_L1, "SW"_L1, 0xf05138),
    extensions("qml"_L1, "QML"_L1, 0x3d9a1a),
    extensions("js mjs cjs"_L1, "JS"_L1, 0xb89b00),
    extensions("jsx"_L1, "JSX"_L1, 0x1f8fb5),
    extensions("ts mts cts"_L1, "TS"_L1, 0x3178c6),
    extensions("tsx"_L1, "TSX"_L1, 0x2b6cb0),
    extensions("py pyw pyi"_L1, "PY"_L1, 0x3572a5),
    extensions("rb"_L1, "RB"_L1, 0xcc342d),
    extensions("go"_L1, "GO"_L1, 0x0090b5),
    extensions("rs"_L1, "RS"_L1, 0xb7410e),
    extensions("java"_L1, "JAVA"_L1, 0xb07219),
    extensions("kt kts"_L1, "KT"_L1, 0x8a5cf5),
    extensions("cs"_L1, "C#"_L1, 0x178600),
    extensions("php"_L1, "PHP"_L1, 0x4f5d95),
    extensions("sh bash zsh fish"_L1, "SH"_L1, 0x4e8a2e),
    extensions("lua"_L1, "LUA"_L1, 0x23238e),
    extensions("html htm"_L1, "HTML"_L1, 0xe34c26),
    extensions("css"_L1, "CSS"_L1, 0x563d7c),
    extensions("scss sass less"_L1, "SCSS"_L1, 0xc6538c),
    extensions("json jsonc"_L1, "JSON"_L1, 0xa8741a),
    extensions("xml xsd xsl plist ui qrc"_L1, "XML"_L1, 0x0060ac),
    extensions("yaml yml"_L1, "YML"_L1, 0xcb171e),
    extensions("toml"_L1, "TOML"_L1, 0x9c4221),
    extensions("md markdown"_L1, "MD"_L1, 0x083fa1),
    extensions("sql"_L1, "SQL"_L1, 0xc77700),
    extensions("cmake"_L1, "CM"_L1, 0xda3434),
    extensions("dart"_L1, "DART"_L1, 0x00897b),
    extensions("zig"_L1, "ZIG"_L1, 0xd97a2b),
    extensions("hs"_L1, "HS"_L1, 0x5e5086),
    extensions("ex exs"_L1, "EX"_L1, 0x6e4a7e),
    extensions("vue"_L1, "VUE"_L1, 0x2f9768),
    extensions("svelte"_L1, "SV"_L1, 0xe03a00),
    extensions("r"_L1, "R"_L1, 0x1a7cc4),
    extensions("pl pm"_L1, "PL"_L1, 0x0278a8),
    wholeNames("CMakeLists.txt"_L1, "CM"_L1, 0xda3434),
    wholeNames("Makefile GNUmakefile makefile"_L1, "MK"_L1, 0x427819),
    wholeNames("Dockerfile"_L1, "DKR"_L1, 0x384d54),
};

bool listed(QLatin1StringView names, QStringView name, Qt::CaseSensitivity sensitivity)
{
    const auto parts{names.tokenize(' '_L1)};
    return std::ranges::any_of(parts, [&](QLatin1StringView part) { return name.compare(part, sensitivity) == 0; });
}

FileKind kindOf(const Entry &entry)
{
    // A whole name's key is its label (extension keys are lower case).
    const QLatin1StringView key{(entry.flags & WholeName) != 0U ? entry.label : *entry.names.tokenize(' '_L1).begin()};
    return FileKind{.key = key, .label = entry.label, .color = QColor::fromRgb(entry.color)};
}

} // namespace

std::optional<FileKind> fileKind(QStringView fileName)
{
    const auto byName{std::ranges::find_if(entries, [fileName](const Entry &entry) {
        return (entry.flags & WholeName) != 0U && listed(entry.names, fileName, Qt::CaseSensitive);
    })};
    if (byName != entries.end()) {
        return kindOf(*byName);
    }
    const qsizetype dot{fileName.lastIndexOf(u'.')};
    if (dot <= 0) {
        return std::nullopt;
    }
    const QStringView extension{fileName.mid(dot + 1)};
    const auto byExtension{std::ranges::find_if(entries, [extension](const Entry &entry) {
        return (entry.flags & WholeName) == 0U && listed(entry.names, extension, Qt::CaseInsensitive);
    })};
    if (byExtension == entries.end()) {
        return std::nullopt;
    }
    return kindOf(*byExtension);
}

std::optional<FileKind> fileKindByKey(QStringView key)
{
    const auto found{std::ranges::find_if(entries, [key](const Entry &entry) { return kindOf(entry).key == key; })};
    if (found == entries.end()) {
        return std::nullopt;
    }
    return kindOf(*found);
}

} // namespace kode
