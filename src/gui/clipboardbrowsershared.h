// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once


#include "common/navigationstyle.h"
#include "common/encryption.h"
#include "gui/menuitems.h"
#include "gui/theme.h"

#include <QByteArray>
#include <QString>

#include <memory>

class ActionHandler;
class ItemFactory;
class NotificationDaemon;
class PasswordPrompt;

struct ClipboardBrowserShared {
    QString editor;
    int maxItems = 100;
    bool textWrap = true;
    NavigationStyle navigationStyle = NavigationStyle::Default;
    bool saveOnReturnKey = false;
    bool moveItemOnReturnKey = false;
    bool showSimpleItems = false;
    bool numberSearch = false;
    int minutesToExpire = 0;
    int encryptedExpireSeconds = 0;
    bool tabsEncrypted = false;
    int saveDelayMsOnItemAdded = 0;
    int saveDelayMsOnItemModified = 0;
    int saveDelayMsOnItemRemoved = 0;
    int saveDelayMsOnItemMoved = 0;
    int saveDelayMsOnItemEdited = 0;
    bool rowIndexFromOne = true;
    /// Ignore clipboard echoes of an item that was just pasted.
    bool suppressReaddHasText = false;
    QString suppressReaddText;
    QByteArray suppressReaddImage;
    qint64 suppressReaddUntilMs = 0;
    ItemFactory *itemFactory = nullptr;
    ActionHandler *actions = nullptr;
    NotificationDaemon *notifications = nullptr;
    PasswordPrompt *passwordPrompt = nullptr;
    Theme theme;
    MenuItems menuItems;
    Encryption::EncryptionKey encryptionKey;
};

using ClipboardBrowserSharedPtr = std::shared_ptr<ClipboardBrowserShared>;
