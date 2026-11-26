#include "FileIcons.h"

#include "FileKinds.h"

#include <QAbstractFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

namespace kode
{

namespace
{

constexpr qint32 defaultSide{32};

// The id without the "?v=<n>" the sidebar adds to load the icons again
// (the folders' color follows the accent color); a '?' in a path is percent-encoded.
QStringView withoutVersion(const QString &id)
{
    const qsizetype query{id.lastIndexOf(QLatin1Char('?'))};
    return query < 0 ? QStringView{id} : QStringView{id}.first(query);
}

QSize sideFor(const QSize &requestedSize)
{
    return requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize{defaultSide, defaultSide};
}

// The badge: across the lower half of the page, its label as large as fits.
void drawBadge(QImage &image, const FileKind &kind)
{
    const qreal side{static_cast<qreal>(image.width())};
    const QRectF badge{side * 0.12, side * 0.46, side * 0.76, side * 0.36};
    QPainter painter{&image};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QPainterPath path;
    path.addRoundedRect(badge, side * 0.06, side * 0.06);
    painter.fillPath(path, kind.color);
    QFont font;
    font.setBold(true);
    font.setPixelSize(static_cast<qint32>(badge.height() * 0.72));
    const qreal room{badge.width() * 0.88};
    if (const qreal width{QFontMetricsF{font}.horizontalAdvance(kind.label)}; width > room) {
        font.setPixelSize(std::max(1, static_cast<qint32>(font.pixelSize() * room / width)));
    }
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, kind.label);
}

} // namespace

FileIcons::FileIcons() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage FileIcons::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Called from the image loading threads: a provider of its own each time.
    const QAbstractFileIconProvider provider;
    const QIcon icon{provider.icon(QFileInfo{QLatin1Char('/') + QUrl::fromPercentEncoding(withoutVersion(id).toUtf8())})
    };
    const QImage image{icon.pixmap(sideFor(requestedSize), 1.0).toImage()};
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

CodeIcons::CodeIcons() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage CodeIcons::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QAbstractFileIconProvider provider;
    QImage image{provider.icon(QAbstractFileIconProvider::File).pixmap(sideFor(requestedSize), 1.0).toImage()};
    image.convertTo(QImage::Format_ARGB32_Premultiplied);
    if (const std::optional<FileKind> kind{fileKindByKey(withoutVersion(id))}) {
        drawBadge(image, *kind);
    }
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

} // namespace kode
