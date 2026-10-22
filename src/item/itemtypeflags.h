// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QStringList>
#include <QVariantMap>

/**
 * Content type of an item.
 *
 * The flags are cached in the item data (see mimeItemType) so that the item
 * type filter does not have to read the item data - large data are stored in
 * separate files and reading them is expensive.
 */
namespace itemType {

enum Flag : quint8 {
    HasText = 1 << 0,
    HasImage = 1 << 1,
    HasFile = 1 << 2,
};

/// Returns flags stored in the item data or 0 if not stored.
quint8 storedFlags(const QVariantMap &data);

/// Computes the flags from the item data (this reads the data).
quint8 computeFlags(const QVariantMap &data);

/// Returns data to store in mimeItemType for given flags.
QByteArray toByteArray(quint8 flags);

/// Returns local paths if the whole text consists of absolute paths (or
/// "file://" URIs), one per line; an empty list otherwise.
QStringList localPathsFromText(const QString &text);

/// Returns a local path if the whole text is a single absolute path (or a
/// "file://" URI); an empty string otherwise.
QString localPathFromText(const QString &text);

/// Returns a local path to an image if the whole text is a path (or a
/// "file://" URI) to an image file; an empty string otherwise.
QString imagePathFromText(const QString &text);

/// Returns the MIME type for an image path (empty string for other files).
QString imageMimeFromPath(const QString &path);

} // namespace itemType
