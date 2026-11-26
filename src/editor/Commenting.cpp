#include "Commenting.h"

#include "Indentation.h"

#include <QStringList>

#include <algorithm>
#include <limits>

namespace kode::commenting
{

namespace
{

bool blank(QStringView line)
{
    return line.trimmed().isEmpty();
}

// `line` past its indentation.
QStringView body(QStringView line)
{
    return line.sliced(indentation::leadingWhitespace(line).size());
}

bool lineCommented(QStringView line, const QString &marker)
{
    return body(line).startsWith(marker);
}

bool blockCommented(QStringView text, const Markers &markers)
{
    const QStringView trimmed{text.trimmed()};
    return trimmed.size() >= markers.open.size() + markers.close.size() && trimmed.startsWith(markers.open) &&
           trimmed.endsWith(markers.close);
}

// `text` without its first `marker` and the space after it.
QString withoutLineMarker(QStringView line, const QString &marker)
{
    const QStringView indent{indentation::leadingWhitespace(line)};
    QStringView rest{line.sliced(indent.size() + marker.size())};
    if (rest.startsWith(QLatin1Char(' '))) {
        rest = rest.sliced(1);
    }
    return indent.toString() + rest.toString();
}

// `text` (trimmed, with its markers) without them and a space inside each.
QString withoutBlockMarkers(QStringView text, const Markers &markers)
{
    const QStringView trimmed{text.trimmed()};
    // trimmed() returns a view into `text`.
    const qsizetype leading{trimmed.constData() - text.constData()};
    QStringView inner{trimmed.sliced(markers.open.size(), trimmed.size() - markers.open.size() - markers.close.size())};
    if (inner.startsWith(QLatin1Char(' '))) {
        inner = inner.sliced(1);
    }
    if (inner.endsWith(QLatin1Char(' '))) {
        inner.chop(1);
    }
    return text.first(leading).toString() + inner.toString() + text.sliced(leading + trimmed.size()).toString();
}

QString wrapped(QStringView line, const Markers &markers)
{
    const QStringView indent{indentation::leadingWhitespace(line)};
    return indent.toString() + markers.open + QLatin1Char(' ') + line.sliced(indent.size()).toString() +
           QLatin1Char(' ') + markers.close;
}

QString toggledWithLineMarker(const QList<QStringView> &lines, const QString &marker, bool atColumnZero)
{
    const bool uncomment{std::ranges::all_of(lines, [&marker](QStringView line) {
        return blank(line) || lineCommented(line, marker);
    })};
    qsizetype column{atColumnZero ? 0 : std::numeric_limits<qsizetype>::max()};
    for (const QStringView line : lines) {
        if (!blank(line)) {
            column = std::min(column, indentation::leadingWhitespace(line).size());
        }
    }
    QStringList result;
    result.reserve(lines.size());
    for (const QStringView line : lines) {
        if (blank(line)) {
            result.append(line.toString());
        } else if (uncomment) {
            result.append(withoutLineMarker(line, marker));
        } else {
            result.append(line.first(column).toString() + marker + QLatin1Char(' ') + line.sliced(column).toString());
        }
    }
    return result.join(QLatin1Char('\n'));
}

QString toggledWithBlockMarkers(const QList<QStringView> &lines, const Markers &markers)
{
    const bool uncomment{std::ranges::all_of(lines, [&markers](QStringView line) {
        return blank(line) || blockCommented(line, markers);
    })};
    QStringList result;
    result.reserve(lines.size());
    for (const QStringView line : lines) {
        if (blank(line)) {
            result.append(line.toString());
        } else {
            result.append(uncomment ? withoutBlockMarkers(line, markers) : wrapped(line, markers));
        }
    }
    return result.join(QLatin1Char('\n'));
}

} // namespace

QString toggledLines(QStringView lines, const Markers &markers, bool atColumnZero)
{
    const QList<QStringView> split{lines.split(QLatin1Char('\n'))};
    if (std::ranges::all_of(split, blank)) {
        return lines.toString();
    }
    if (!markers.line.isEmpty()) {
        return toggledWithLineMarker(split, markers.line, atColumnZero);
    }
    if (!markers.open.isEmpty() && !markers.close.isEmpty()) {
        return toggledWithBlockMarkers(split, markers);
    }
    return lines.toString();
}

QString toggledBlock(QStringView text, const Markers &markers)
{
    if (markers.open.isEmpty() || markers.close.isEmpty()) {
        return text.toString();
    }
    if (blockCommented(text, markers)) {
        return withoutBlockMarkers(text, markers);
    }
    return markers.open + QLatin1Char(' ') + text.toString() + QLatin1Char(' ') + markers.close;
}

} // namespace kode::commenting
