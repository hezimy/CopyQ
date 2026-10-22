// SPDX-License-Identifier: GPL-3.0-or-later

#include "itemtypeflags.h"

#include "common/mimetypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace {

/// True if the HTML flavor references an image ("<img", case-insensitive).
///
/// Browsers and office apps copy mixed text+image content as HTML with an
/// <img> reference only - there is no image/* flavor in the clipboard at all,
/// so the image has to be detected inside the HTML.
bool htmlHasImage(const QByteArray &html)
{
    const int last = html.size() - 4;
    for (int i = 0; i <= last; ++i) {
        if (html.at(i) != '<')
            continue;
        const char c1 = html.at(i + 1) | 0x20;
        const char c2 = html.at(i + 2) | 0x20;
        const char c3 = html.at(i + 3) | 0x20;
        if (c1 == 'i' && c2 == 'm' && c3 == 'g')
            return true;
    }
    return false;
}

bool isImageSuffix(const QString &suffix)
{
    static const QStringList imageSuffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("webp"),
        QStringLiteral("svg"), QStringLiteral("ico"), QStringLiteral("tif"),
        QStringLiteral("tiff"), QStringLiteral("avif"), QStringLiteral("heic"),
    };
    return imageSuffixes.contains(suffix);
}

} // namespace

namespace itemType {

/// Version of the stored flags in the two highest bits. Flags written by an
/// older version are ignored and recomputed.
///
/// Version 1 must not collide with the previous format (0x80 + flags).
constexpr quint8 FlagsVersion = 1;
constexpr quint8 FlagsVersionShift = 6;
constexpr quint8 FlagsMask = 0x3F;

namespace {

/// Normalizes one line to a local path; an empty string if it is not one.
QString toLocalPath(const QString &line)
{
    const QString trimmed = line.trimmed();
    if ( trimmed.isEmpty() )
        return QString();

    // Web links are kept as text - only local files are files.
    if ( trimmed.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
         || trimmed.startsWith(QLatin1String("https://"), Qt::CaseInsensitive) )
    {
        return QString();
    }

    QString path = trimmed;
    if ( path.startsWith(QLatin1String("file://"), Qt::CaseInsensitive) )
        path = path.mid(7);

    // "file:///C:/dir/file.txt" -> "C:/dir/file.txt" (the third slash is not
    // part of the Windows path).
    if ( path.size() > 2 && path.at(0) == QLatin1Char('/')
         && path.at(2) == QLatin1Char(':') )
    {
        path = path.mid(1);
    }

    // Require an absolute path: "C:/...", "/..." or "//server/share/...".
    const bool absolute =
            ( path.size() > 2 && path.at(1) == QLatin1Char(':') )
            || path.startsWith(QLatin1Char('/'))
            || path.startsWith(QLatin1String("\\\\"));
    if ( !absolute )
        return QString();

    if ( !path.contains(QLatin1Char('/')) && !path.contains(QLatin1Char('\\')) )
        return QString();

    return path;
}

} // namespace

QStringList localPathsFromText(const QString &text)
{
    QStringList paths;

    const QString trimmed = text.trimmed();
    if ( trimmed.isEmpty() || trimmed.size() > 4096 )
        return paths;

    const auto lines = trimmed.split(QLatin1Char('\n'));
    if ( lines.size() > 100 )
        return paths;

    for ( const auto &line : lines ) {
        const QString path = toLocalPath(line);
        if ( path.isEmpty() ) {
            // Empty lines are allowed, anything else is not a path.
            if ( line.trimmed().isEmpty() )
                continue;
            return QStringList();
        }
        paths.append(path);
    }

    return paths;
}

QString localPathFromText(const QString &text)
{
    const QStringList paths = localPathsFromText(text);
    return paths.size() == 1 ? paths.first() : QString();
}

QString imageMimeFromPath(const QString &path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if ( dot < 0 )
        return QString();

    const QString suffix = path.mid(dot + 1).toLower();
    if ( suffix == QLatin1String("png") ) return QStringLiteral("image/png");
    if ( suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg") )
        return QStringLiteral("image/jpeg");
    if ( suffix == QLatin1String("gif") ) return QStringLiteral("image/gif");
    if ( suffix == QLatin1String("bmp") ) return QStringLiteral("image/bmp");
    if ( suffix == QLatin1String("webp") ) return QStringLiteral("image/webp");
    if ( suffix == QLatin1String("svg") ) return QStringLiteral("image/svg+xml");
    if ( suffix == QLatin1String("ico") ) return QStringLiteral("image/x-icon");
    if ( suffix == QLatin1String("tif") || suffix == QLatin1String("tiff") )
        return QStringLiteral("image/tiff");
    if ( suffix == QLatin1String("avif") ) return QStringLiteral("image/avif");
    if ( suffix == QLatin1String("heic") ) return QStringLiteral("image/heic");
    return QString();
}

QString imagePathFromText(const QString &text)
{
    const QString path = localPathFromText(text);
    if ( path.isEmpty() )
        return QString();

    const int dot = path.lastIndexOf(QLatin1Char('.'));
    if ( dot < 0 || dot == path.size() - 1 )
        return QString();

    return isImageSuffix(path.mid(dot + 1).toLower()) ? path : QString();
}

quint8 storedFlags(const QVariantMap &data)
{
    const QByteArray bytes = data.value(mimeItemType).toByteArray();
    if (bytes.size() != 1)
        return 0;

    const quint8 flags = static_cast<quint8>(bytes.at(0));

    // Flags saved by an older version of the classification are ignored.
    if ( (flags >> FlagsVersionShift) != FlagsVersion )
        return 0;

    return flags & FlagsMask;
}

quint8 computeFlags(const QVariantMap &data)
{
    quint8 flags = 0;

    bool hasImage = false;

    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if ( it.key().startsWith(QLatin1String("image/")) ) {
            hasImage = true;
            break;
        }
    }

    if ( !hasImage && data.contains(mimeHtml)
         && htmlHasImage(data.value(mimeHtml).toByteArray()) )
    {
        hasImage = true;
    }

    const QString text = QString::fromUtf8(data.value(mimeText).toByteArray());
    const bool hasText = !text.trimmed().isEmpty();

    if (hasImage) {
        flags |= HasImage;
        if (hasText)
            flags |= HasText;
    } else if (hasText) {
        const QStringList paths = localPathsFromText(text);
        if ( paths.size() == 1 && !imagePathFromText(text).isEmpty() ) {
            // An image stored as a path to a temporary file.
            flags |= HasImage;
        } else if ( !paths.isEmpty() ) {
            // A file (or a list of files) stored as paths.
            flags |= HasFile;
        } else {
            flags |= HasText;
        }
    }

    return flags;
}

QByteArray toByteArray(quint8 flags)
{
    const auto value = static_cast<quint8>(
        (flags & FlagsMask) | (FlagsVersion << FlagsVersionShift) );
    return QByteArray(1, static_cast<char>(value));
}

} // namespace itemType
