#include "SyntaxHighlighter.h"

#include "SemanticStyle.h"
#include "TextBuffer.h"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <ranges>
#include <utility>

namespace kode
{

using namespace Qt::StringLiterals;

namespace
{

// A text style's look in `theme` (no background).
QTextCharFormat textStyleFormat(const KSyntaxHighlighting::Theme &theme, KSyntaxHighlighting::Theme::TextStyle style)
{
    QTextCharFormat format;
    if (const QRgb color{theme.textColor(style)}; color != 0U) {
        format.setForeground(QColor::fromRgba(color));
    }
    if (theme.isBold(style)) {
        format.setFontWeight(QFont::Bold);
    }
    if (theme.isItalic(style)) {
        format.setFontItalic(true);
    }
    return format;
}

// Text a bracket in means nothing to the code.
bool isNonCode(KSyntaxHighlighting::Theme::TextStyle style)
{
    using enum KSyntaxHighlighting::Theme::TextStyle;
    switch (style) {
    case Char:
    case SpecialChar:
    case String:
    case VerbatimString:
    case SpecialString:
    case Comment:
    case Documentation:
    case CommentVar:
    case RegionMarker:
    case Information:
    case Warning:
    case Alert:
        return true;
    default:
        return false;
    }
}

// The regions `events` (see m_regions) begin and leave open, in order.
QList<qint32> openRegions(const QList<qint32> &events)
{
    QList<qint32> open;
    for (const qint32 event : events) {
        if (event > 0) {
            open.append(event);
            continue;
        }
        const auto found{std::ranges::find(std::views::reverse(open), -event)};
        if (found != std::views::reverse(open).end()) {
            open.erase(std::prev(found.base()));
        }
    }
    return open;
}

// Kode's own themes, in the app's resources (assets/themes): the
// repository looks in <path>/themes.
const QString themesPath{QStringLiteral(":/kode/syntax")};
const QString lightSuffix{QStringLiteral(" Light")};
const QString darkSuffix{QStringLiteral(" Dark")};

std::unique_ptr<KSyntaxHighlighting::Repository> makeRepository()
{
    auto repository{std::make_unique<KSyntaxHighlighting::Repository>()};
    repository->addCustomSearchPath(themesPath);
    return repository;
}

// Loading the definitions takes a while: one repository for the process.
KSyntaxHighlighting::Repository &repository()
{
    static const std::unique_ptr<KSyntaxHighlighting::Repository> instance{makeRepository()};
    return *instance;
}

// Lines highlighted between deadline checks.
constexpr qsizetype linesPerCheck{32};

// Interpreters named in a shebang whose definition has another name.
constexpr std::array<std::pair<QLatin1StringView, QLatin1StringView>, 6> interpreters{{
    {QLatin1StringView{"sh"}, QLatin1StringView{"Bash"}},
    {QLatin1StringView{"bash"}, QLatin1StringView{"Bash"}},
    {QLatin1StringView{"zsh"}, QLatin1StringView{"Zsh"}},
    {QLatin1StringView{"node"}, QLatin1StringView{"JavaScript"}},
    {QLatin1StringView{"python"}, QLatin1StringView{"Python"}},
    {QLatin1StringView{"ruby"}, QLatin1StringView{"Ruby"}},
}};

// "#!/usr/bin/env python3" -> "python".
QString interpreter(const QString &firstLine)
{
    if (!firstLine.startsWith(QLatin1StringView{"#!"})) {
        return {};
    }
    const QStringList words{firstLine.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts)};
    if (words.isEmpty()) {
        return {};
    }
    QString program{QFileInfo{words.first()}.fileName()};
    if (program == QLatin1StringView{"env"}) {
        const auto named{std::ranges::find_if(words | std::views::drop(1), [](const QString &word) {
            return !word.startsWith(QLatin1Char('-'));
        })};
        program = named != words.end() ? *named : QString{};
    }
    while (!program.isEmpty() && (program.back().isDigit() || program.back() == QLatin1Char('.'))) {
        program.chop(1);
    }
    return program;
}

} // namespace

SyntaxHighlighter::SyntaxHighlighter(const TextBuffer *buffer) : m_buffer{buffer} {}

KSyntaxHighlighting::Definition SyntaxHighlighter::detect(const QString &fileName, const QString &firstLine)
{
    if (KSyntaxHighlighting::Definition byName{repository().definitionForFileName(fileName)}; byName.isValid()) {
        return byName;
    }
    const QString program{interpreter(firstLine)};
    if (program.isEmpty()) {
        return {};
    }
    const auto known{std::ranges::find_if(interpreters, [&program](const auto &entry) {
        return entry.first == program;
    })};
    if (known != interpreters.end()) {
        return repository().definitionForName(known->second);
    }
    return repository().definitionForName(program);
}

KSyntaxHighlighting::Definition SyntaxHighlighter::definitionForName(const QString &name)
{
    return repository().definitionForName(name);
}

KSyntaxHighlighting::Theme SyntaxHighlighter::themeNamed(const QString &name)
{
    const KSyntaxHighlighting::Theme named{repository().theme(name)};
    return named.isValid() ? named : repository().defaultTheme();
}

QStringList SyntaxHighlighter::themeFamilies()
{
    QStringList names;
    std::ranges::transform(repository().themes(), std::back_inserter(names), &KSyntaxHighlighting::Theme::name);
    QStringList families;
    for (const QString &name : names) {
        if (!name.endsWith(lightSuffix)) {
            continue;
        }
        QString family{name.chopped(lightSuffix.size())};
        if (names.contains(family + darkSuffix)) {
            families.emplace_back(std::move(family));
        }
    }
    std::ranges::sort(families, [](const QString &a, const QString &b) { return a.localeAwareCompare(b) < 0; });
    return families;
}

QString SyntaxHighlighter::themeFamily(const QString &name)
{
    const QStringList families{themeFamilies()};
    const auto family{std::ranges::find_if(families, [&name](const QString &family) {
        return name == family || name == family + lightSuffix || name == family + darkSuffix;
    })};
    return family != families.end() ? *family : QStringLiteral("Kode");
}

QString SyntaxHighlighter::themeName(const QString &name, bool dark)
{
    return themeFamily(name) + (dark ? darkSuffix : lightSuffix);
}

QList<KSyntaxHighlighting::Definition> SyntaxHighlighter::definitions()
{
    QList<KSyntaxHighlighting::Definition> visible{repository().definitions()};
    // "None", the empty definition, has no section: plain text stands for it.
    visible.removeIf([](const KSyntaxHighlighting::Definition &definition) {
        return definition.isHidden() || definition.section().isEmpty();
    });
    return visible;
}

void SyntaxHighlighter::setDefinition(const KSyntaxHighlighting::Definition &definition)
{
    AbstractHighlighter::setDefinition(definition);
    m_charFormats.clear();
    m_startsInNonCode.clear();
    reset();
}

void SyntaxHighlighter::setTheme(const KSyntaxHighlighting::Theme &theme)
{
    AbstractHighlighter::setTheme(theme);
    m_charFormats.clear();
    readSemanticStyles();
}

void SyntaxHighlighter::readSemanticStyles()
{
    m_semanticStyles.clear();
    m_semanticFormats.clear();
    QFile file{theme().filePath()};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject styles{QJsonDocument::fromJson(file.readAll()).object().value(u"semantic-styles"_s).toObject()};
    for (auto style{styles.begin()}; style != styles.end(); ++style) {
        const QJsonObject value{style.value().toObject()};
        QTextCharFormat format;
        if (const QString color{value.value(u"text-color"_s).toString()}; !color.isEmpty()) {
            format.setForeground(QColor::fromString(color));
        }
        if (value.value(u"bold"_s).toBool()) {
            format.setFontWeight(QFont::Bold);
        }
        if (value.value(u"italic"_s).toBool()) {
            format.setFontItalic(true);
        }
        m_semanticStyles.insert(style.key(), format);
    }
}

QTextCharFormat SyntaxHighlighter::semanticFormat(quint32 style)
{
    if (const auto cached{m_semanticFormats.constFind(style)}; cached != m_semanticFormats.cend()) {
        return *cached;
    }
    const quint32 category{std::min<quint32>(style & semantic::categoryMask, semantic::CategoryCount - 1)};
    const bool themed{!m_semanticStyles.isEmpty()};
    QTextCharFormat format{
        themed ? m_semanticStyles.value(semantic::names.at(category))
               : textStyleFormat(theme(), semantic::fallbacks.at(category))
    };
    if (!themed && category == semantic::VirtualMethod) {
        format.setFontItalic(true);
    }
    // As Qt Creator: the category sets its weight and slant either way (a
    // color only if it has one); each mixin's color wins, its italic and
    // bold only add.
    format.setFontWeight(format.fontWeight() == QFont::Bold ? QFont::Bold : QFont::Normal);
    format.setFontItalic(format.fontItalic());
    for (std::size_t bit{0}; bit < semantic::mixinNames.size(); ++bit) {
        if ((style & (semantic::Declaration << bit)) == 0U) {
            continue;
        }
        const QLatin1StringView name{semantic::mixinNames.at(bit)};
        QTextCharFormat mixin;
        if (themed) {
            mixin = m_semanticStyles.value(name);
        } else if (name == QLatin1StringView{"Declaration"}) {
            mixin.setFontWeight(QFont::Bold);
        } else if (name == QLatin1StringView{"OutputArgument"}) {
            mixin.setFontItalic(true);
        } else if (name == QLatin1StringView{"Function"}) {
            mixin = textStyleFormat(theme(), KSyntaxHighlighting::Theme::Function);
        }
        if (mixin.hasProperty(QTextFormat::ForegroundBrush)) {
            format.setForeground(mixin.foreground());
        }
        if (!format.fontItalic() && mixin.fontItalic()) {
            format.setFontItalic(true);
        }
        if (format.fontWeight() == QFont::Normal && mixin.fontWeight() == QFont::Bold) {
            format.setFontWeight(QFont::Bold);
        }
    }
    m_semanticFormats.insert(style, format);
    return format;
}

bool SyntaxHighlighter::isActive() const
{
    return definition().isValid();
}

void SyntaxHighlighter::reset()
{
    m_states.clear();
    m_valid = 0;
    m_resumeEnd = 0;
    m_checkFrom = 0;
}

void SyntaxHighlighter::linesReplaced(qsizetype line, qsizetype removedLines, qsizetype insertedLines)
{
    if (line >= m_states.size()) {
        return;
    }
    const qsizetype delta{insertedLines - removedLines};
    // Keep the states from before the edit aligned with their lines: the
    // edited lines' end state becomes the last one's.
    const qsizetype removable{std::min(removedLines, m_states.size() - line - 1)};
    m_states.remove(line, removable);
    m_states.insert(line, insertedLines, KSyntaxHighlighting::State{});

    const bool resuming{m_resumeEnd > m_valid};
    const qsizetype validBefore{resuming ? m_resumeEnd : m_valid};
    if (validBefore <= line + removedLines) {
        // Nothing past the edit was known: no states to resume with.
        m_valid = std::min(m_valid, line);
        m_resumeEnd = 0;
        m_states.resize(m_valid);
        return;
    }
    m_resumeEnd = validBefore + delta;
    m_checkFrom = resuming ? std::max(m_checkFrom + (m_checkFrom > line ? delta : 0), line + insertedLines)
                           : line + insertedLines;
    m_valid = std::min(m_valid, line);
    m_states.resize(m_resumeEnd);
}

bool SyntaxHighlighter::advance(qsizetype line, const QDeadlineTimer &deadline)
{
    if (!isActive()) {
        return true;
    }
    line = std::min(line, m_buffer->lineCount());
    qsizetype sinceCheck{0};
    while (m_valid < line) {
        if (++sinceCheck == linesPerCheck) {
            sinceCheck = 0;
            if (deadline.hasExpired()) {
                return false;
            }
        }
        m_regions.clear();
        KSyntaxHighlighting::State state{highlightLine(m_buffer->line(m_valid), startState(m_valid))};
        if (m_valid < m_states.size()) {
            // Ends as it did before the edit: the rest was right already.
            if (m_valid >= m_checkFrom && m_valid < m_resumeEnd && m_states.at(m_valid) == state) {
                m_valid = m_resumeEnd;
                m_resumeEnd = 0;
                continue;
            }
            m_states[m_valid] = std::move(state);
        } else {
            m_states.append(std::move(state));
        }
        ++m_valid;
    }
    if (m_valid >= m_resumeEnd) {
        m_resumeEnd = 0;
    }
    return true;
}

KSyntaxHighlighting::State SyntaxHighlighter::startState(qsizetype line) const
{
    return line == 0 || !isActive() ? KSyntaxHighlighting::State{} : m_states.at(line - 1);
}

QList<QTextLayout::FormatRange> SyntaxHighlighter::formats(qsizetype line, QStringView text)
{
    return isActive() ? formats(line, text, startState(line)) : QList<QTextLayout::FormatRange>{};
}

QList<QTextLayout::FormatRange>
SyntaxHighlighter::formats(qsizetype line, QStringView text, const KSyntaxHighlighting::State &start)
{
    if (!isActive()) {
        return {};
    }
    m_collected.clear();
    m_regions.clear();
    m_nonCode.clear();
    m_collecting = line;
    // The end state is known already.
    std::ignore = highlightLine(text, start);
    m_collecting = -1;
    m_opened = openRegions(m_regions).size();
    appendPropertyKeywords(text);
    return m_collected;
}

void SyntaxHighlighter::appendPropertyKeywords(QStringView text)
{
    // As Qt Creator's C++ editor: the words of a Q_PROPERTY(…) are keywords.
    static constexpr std::array words{
        u"READ",
        u"WRITE",
        u"MEMBER",
        u"RESET",
        u"NOTIFY",
        u"REVISION",
        u"DESIGNABLE",
        u"SCRIPTABLE",
        u"STORED",
        u"USER",
        u"CONSTANT",
        u"FINAL",
        u"BINDABLE",
        u"REQUIRED",
    };
    static constexpr QStringView macro{u"Q_PROPERTY("};
    const qsizetype open{text.indexOf(macro)};
    if (open < 0 || !definition().name().contains(u"C++"_s)) {
        return;
    }
    const qsizetype close{text.indexOf(u')', open)};
    const QStringView inside{text.sliced(open + macro.size(), (close < 0 ? text.size() : close) - open - macro.size())};
    const QTextCharFormat keyword{textStyleFormat(theme(), KSyntaxHighlighting::Theme::Keyword)};
    qsizetype at{0};
    while (at < inside.size()) {
        if (!inside.at(at).isLetter()) {
            ++at;
            continue;
        }
        qsizetype end{at};
        while (end < inside.size() && (inside.at(end).isLetterOrNumber() || inside.at(end) == u'_')) {
            ++end;
        }
        const QStringView word{inside.sliced(at, end - at)};
        if (std::ranges::contains(words, word)) {
            m_collected.append(
                {.start = static_cast<qint32>(open + macro.size() + at),
                 .length = static_cast<qint32>(word.size()),
                 .format = keyword}
            );
        }
        at = end;
    }
}

void SyntaxHighlighter::guess(Guess &guess, qsizetype first, qsizetype end, const QDeadlineTimer &deadline)
{
    if (!isActive()) {
        return;
    }
    end = std::min(end, m_buffer->lineCount());
    const qsizetype from{std::max(first - guessLead, qsizetype{0})};
    if (guess.states.isEmpty() || first < guess.first || from > guess.first + guess.states.size()) {
        guess.first = std::clamp(m_valid, from, first);
        guess.states = {isReady(guess.first) ? startState(guess.first) : KSyntaxHighlighting::State{}};
    }
    qsizetype sinceCheck{0};
    while (guess.first + guess.states.size() < end) {
        if (++sinceCheck == linesPerCheck) {
            sinceCheck = 0;
            if (deadline.hasExpired()) {
                return;
            }
        }
        const qsizetype line{guess.first + guess.states.size() - 1};
        m_regions.clear();
        KSyntaxHighlighting::State next{highlightLine(m_buffer->line(line), guess.states.constLast())};
        guess.states.append(std::move(next));
    }
}

void SyntaxHighlighter::guessEdited(Guess &guess, qsizetype line)
{
    if (line < guess.first) {
        guess.states.clear();
        return;
    }
    // The edited line still starts as guessed.
    guess.states.resize(std::min(guess.states.size(), line - guess.first + 1));
}

QList<indentation::Span> SyntaxHighlighter::nonCode(qsizetype line, QStringView text)
{
    if (!isActive() || !isReady(line)) {
        return {};
    }
    std::ignore = formats(line, text);
    return m_nonCode;
}

bool SyntaxHighlighter::startsInNonCode(qsizetype line)
{
    if (!isActive() || !isReady(line)) {
        return false;
    }
    KSyntaxHighlighting::State state{startState(line)};
    const auto known{m_startsInNonCode.constFind(state)};
    if (known != m_startsInNonCode.cend()) {
        return *known;
    }
    // A space there takes the format of what it is inside.
    std::ignore = formats(line, u" ", state);
    const bool inside{!m_nonCode.isEmpty() && m_nonCode.constFirst().start == 0};
    m_startsInNonCode.insert(std::move(state), inside);
    return inside;
}

std::optional<qsizetype> SyntaxHighlighter::regionEnd(qsizetype line, const QDeadlineTimer &deadline)
{
    if (!isActive() || !isReady(line)) {
        return std::nullopt;
    }
    m_regions.clear();
    KSyntaxHighlighting::State state{highlightLine(m_buffer->line(line), startState(line))};
    const QList<qint32> open{openRegions(m_regions)};
    if (open.isEmpty()) {
        return std::nullopt;
    }
    // The outermost one, through those of its kind opened inside it.
    const qint32 region{open.constFirst()};
    auto depth{std::ranges::count(open, region)};
    for (qsizetype next{line + 1}; next < m_buffer->lineCount(); ++next) {
        if ((next - line) % linesPerCheck == 0 && deadline.hasExpired()) {
            return std::nullopt;
        }
        m_regions.clear();
        state = highlightLine(m_buffer->line(next), state);
        for (const qint32 event : std::as_const(m_regions)) {
            if (std::abs(event) == region) {
                depth += event > 0 ? 1 : -1;
            }
            if (depth == 0) {
                return next;
            }
        }
    }
    return std::nullopt;
}

std::optional<qsizetype> SyntaxHighlighter::regionAbove(qsizetype line, const QDeadlineTimer &deadline)
{
    // Backwards, an end skips the begin that matches it.
    QHash<qint32, qsizetype> closed;
    for (qsizetype above{line - 1}; above >= 0 && isActive() && isReady(above); --above) {
        if ((line - above) % linesPerCheck == 0 && deadline.hasExpired()) {
            return std::nullopt;
        }
        m_regions.clear();
        std::ignore = highlightLine(m_buffer->line(above), startState(above));
        for (const qint32 event : std::views::reverse(m_regions)) {
            qsizetype &count{closed[std::abs(event)]};
            if (event < 0) {
                ++count;
            } else if (count == 0) {
                return above;
            } else {
                --count;
            }
        }
    }
    return std::nullopt;
}

void SyntaxHighlighter::applyFolding(int /*offset*/, int /*length*/, KSyntaxHighlighting::FoldingRegion region)
{
    const qint32 id{region.id() + 1};
    m_regions.append(region.type() == KSyntaxHighlighting::FoldingRegion::Begin ? id : -id);
}

void SyntaxHighlighter::applyFormat(int offset, int length, const KSyntaxHighlighting::Format &format)
{
    if (m_collecting < 0 || length == 0) {
        return;
    }
    if (isNonCode(format.textStyle())) {
        if (!m_nonCode.isEmpty() && m_nonCode.last().end == offset) {
            m_nonCode.last().end = offset + length;
        } else {
            m_nonCode.append({.start = offset, .end = offset + length});
        }
    }
    if (format.isDefaultTextStyle(theme())) {
        return;
    }
    m_collected.append({.start = offset, .length = length, .format = charFormat(format)});
}

QTextCharFormat SyntaxHighlighter::charFormat(const KSyntaxHighlighting::Format &format)
{
    const auto cached{m_charFormats.constFind(format.id())};
    if (cached != m_charFormats.cend()) {
        return *cached;
    }
    const KSyntaxHighlighting::Theme &current{theme()};
    QTextCharFormat result;
    if (format.hasTextColor(current)) {
        result.setForeground(format.textColor(current));
    }
    if (format.hasBackgroundColor(current)) {
        result.setBackground(format.backgroundColor(current));
    }
    if (format.isBold(current)) {
        result.setFontWeight(QFont::Bold);
    }
    result.setFontItalic(format.isItalic(current));
    result.setFontUnderline(format.isUnderline(current));
    result.setFontStrikeOut(format.isStrikeThrough(current));
    m_charFormats.insert(format.id(), result);
    return result;
}

} // namespace kode
