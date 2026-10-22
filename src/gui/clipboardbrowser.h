// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once


#include "common/clipboardmode.h"
#include "gui/clipboardbrowsershared.h"
#include "item/clipboardmodel.h"
#include "item/itemdelegate.h"
#include "item/itemfilter.h"
#include "item/itemwidget.h"

#include <QListView>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

class ItemEditorWidget;
class ItemFactory;
class QPersistentModelIndex;
class QProgressBar;
class QPushButton;

/** List view of clipboard items. */
class ClipboardBrowser final : public QListView
{
    Q_OBJECT

    public:
        ClipboardBrowser(
                const QString &tabName,
                const ClipboardBrowserSharedPtr &sharedData,
                QWidget *parent = nullptr);

        /** Close all external editors and save items if needed. */
        ~ClipboardBrowser();

        /**
         * Move item with given @a hash to the top of the list.
         *
         * @return true only if item exists
         */
        bool moveToTop(uint itemHash);

        /** Sort selected items. */
        void sortItems(const QModelIndexList &indexes);
        void sortItems(const QList<QPersistentModelIndex> &sorted);

        /** Reverse order of selected items. */
        void reverseItems(const QModelIndexList &indexes);

        /** Index of item in given row. */
        QModelIndex index(int i) const { return m.index(i,0); }

        /** Returns concatenation of selected items. */
        const QString selectedText() const;

        /**
         * Set tab name.
         *
         * This is ID used for saving items.
         *
         * If ID is empty saving is disabled.
         */
        bool setTabName(const QString &tabName);
        const QString &tabName() const { return m_tabName; }

        void setMaxItemCount(int count);
        void setStoreItems(bool store);

        /**
         * Return true only if the internal editor widget is open.
         */
        bool isInternalEditorOpen() const;

        /**
         * Return true only if an external editor is open.
         */
        bool isExternalEditorOpen() const;

        /**
         * Close internal and external editor
         * unless user don't want to discard changed (show message box).
         *
         * @return true only if editors were closed
         */
        bool maybeCloseEditors();

        /**
         * Override to disable default QAbstractItemView search.
         */
        void keyboardSearch(const QString &text) override;

        QVariantMap copyIndex(const QModelIndex &index) const;

        QVariantMap copyIndexes(const QModelIndexList &indexes) const;

        void removeIndexes(const QModelIndexList &indexes, QString *error = nullptr);

        bool canRemoveItems(const QModelIndexList &indexes, QString *error = nullptr);

        /** Render preview image with items. */
        QPixmap renderItemPreview(const QModelIndexList &indexes, int maxWidth, int maxHeight);

        /** Removes items from end of list without notifying plugins. */
        bool allocateSpaceForNewItems(int newItemCount);

        /** Add new item to the browser. */
        bool add(
                const QString &txt, //!< Text of new item.
                int row = 0 //!< Target row for the new item (negative to append item).
                );

        /**
         * Add new item to the browser.
         * @a item is automatically deleted after it's no longer needed.
         */
        bool add(
                const QVariantMap &data, //!< Data for new item.
                int row = 0 //!< Target row for the new item (negative to append item).
                );

        bool addReversed(const QVector<QVariantMap> &dataList, int row);

        bool addAndSelect(const QVariantMap &data, int row);

        /**
         * Add item and remove duplicates.
         */
        void addUnique(const QVariantMap &data, ClipboardMode mode);

        void setItemsData(const QMap<QPersistentModelIndex, QVariantMap> &itemsData);

        /** Number of items in list. */
        int length() const { return m.rowCount(); }

        /** Receive key event. */
        void keyEvent(QKeyEvent *event) { keyPressEvent(event); }
        /** Move item to clipboard. */
        void moveToClipboard(const QModelIndex &ind);
        /** Move items to clipboard. */
        void moveToClipboard(const QModelIndexList &indexes);
        /** Show only items matching the regular expression. */
        void filterItems(const ItemFilterPtr &filter);
        /** Open editor. */
        bool openEditor(const QModelIndex &index, const QString &format, const QByteArray &content, bool changeClipboard = false);
        /** Open editor for an item. */
        bool openEditor(const QModelIndex &index, const QString &format);

        /** Set current item. */
        void setCurrent(int row, bool keepSelection = false, bool setCurrentOnly = false);

        /**
         * Save items to configuration if needed.
         */
        void saveUnsavedItems();

        /**
         * Create and edit new item.
         */
        void editNew(
            const QString &format, const QByteArray &content, bool changeClipboard = false);

        /** Edit item in given @a row. */
        void editRow(int row, const QString &format);

        void move(int key);

        void move(const QModelIndexList &indexes, int targetRow);

        QWidget *currentItemPreview(QWidget *parent);

        void findNext();

        void findPrevious();

        /**
         * Load items from configuration.
         * This function does nothing if model is disabled (e.g. loading failed previously).
         * @see setID, saveItems
         */
        bool loadItems(const QByteArray &itemData = QByteArray());

        /**
         * Return true only if row is filtered and should be hidden.
         */
        bool isFiltered(int row) const;

        bool isLoaded() const;

        /**
         * Save items to configuration.
         * @see setID, loadItems
         */
        bool saveItems();

        /** Move current item to clipboard. */
        void moveToClipboard();

        /** True when the row is a collapsible group header. */
        bool isGroupRow(int row) const;

        /** Insert a group header so following items belong to it until the next group. */
        void insertGroup(const QString &name, int row);

        /** Collapse or expand a group header. */
        void toggleGroupCollapsed(int row);

        /** Rename a group header. */
        void renameGroup(int row, const QString &name);

        /** Group header immediately above this row, or -1 when the row is not inside a group. */
        int groupHeaderForRow(int row) const;

        /** Stored group name for a header row. */
        QString groupName(int headerRow) const;

        /** Move selected non-header items to the end of the group. Items already in it stay put. */
        void moveSelectedToGroup(int headerRow);

        /** Move selected items that belong to a group out of every group. */
        void removeSelectedFromGroup();

        /**
         * Row of the nth visible clipboard item, skipping group headers
         * and items hidden by a collapsed group or the current filter.
         * @return -1 when there is no such item.
         */
        /**
         * Returns the row of the nth item in the whole history.
         *
         * The item type filter and the group folding are ignored so that the
         * numbered paste shortcuts always paste the same items, and group
         * headers are skipped.
         */
        int pasteRow(int nth) const;

        /** Item type shown by the filter bar below the list. Types are exclusive. */
        enum class ItemTypeFilter {
            All,
            Marked,
            Text,
            Image,
            TextAndImage,
            File,
            Group,
        };

        /**
         * Filter rows by item type. Applied on top of the text filter.
         *
         * "Marked" shows items with any tag.
         */
        void setItemTypeFilter(ItemTypeFilter filter);

        ItemTypeFilter itemTypeFilter() const { return m_itemTypeFilter; }

        /** Edit selected unhidden items. */
        void editSelected();

        /** Edit notes for current item. */
        void editNotes();

        /** Open editor with text of all selected items or for single selected item. */
        bool openEditor();

        /** Remove selected unhidden items. */
        void remove();

        /** Show content of current item. */
        void showItemContent();

        void emitItemCount();

        using QListView::isIndexHidden;
        using QListView::isRowHidden;
        using QListView::verticalOffset;

    signals:
        void runOnRemoveItemsHandler(const QList<QPersistentModelIndex> &indexes, bool *canRemove);

        /** Show list request. */
        void requestShow(const ClipboardBrowser *self);
        /** Request clipboard change. */
        void changeClipboard(const QVariantMap &data);

        /** Emitted on error. */
        void error(const QString &errorString);

        void editingFinished();

        void itemCountChanged(const QString &tabName, int count);

        void showContextMenu(const QPoint &position);

        void itemsChanged(const ClipboardBrowser *self);

        void itemSelectionChanged(const ClipboardBrowser *self);

        void internalEditorStateChanged(const ClipboardBrowser *self);

        void searchRequest();
        void searchHideRequest();
        void searchShowRequest(const QString &pattern);

        void closeExternalEditors();

        void itemWidgetCreated(const PersistentDisplayItem &selection);

        void filterProgressChanged(int percent);

    protected:
        void keyPressEvent(QKeyEvent *event) override;
        void contextMenuEvent(QContextMenuEvent *) override;
        void resizeEvent(QResizeEvent *event) override;
        void showEvent(QShowEvent *event) override;
        void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;
        void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
        void focusInEvent(QFocusEvent *event) override;
        void focusOutEvent(QFocusEvent *event) override;

        void dragEnterEvent(QDragEnterEvent *event) override;
        void dragLeaveEvent(QDragLeaveEvent *event) override;
        void dragMoveEvent(QDragMoveEvent *event) override;
        void dropEvent(QDropEvent *event) override;

        void paintEvent(QPaintEvent *e) override;

        void mousePressEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;
        void mouseMoveEvent(QMouseEvent *event) override;

        void enterEvent(QEnterEvent *event) override;

        void scrollContentsBy(int dx, int dy) override;

        void doItemsLayout() override;

    private slots:
        void itemModified(const QByteArray &bytes, const QString &mime, const QModelIndex &index);

        void onEditorNeedsChangeClipboard(const QByteArray &bytes, const QString &mime);

        void closeExternalEditor(QObject *editor, const QModelIndex &index);

    private:
        void onRowsInserted(const QModelIndex &parent, int first, int last);

        void onItemCountChanged();

        void onEditorSave();

        void onEditorCancel();

        void onEditorInvalidate();

        void setClipboardFromEditor();

        /**
         * Save items to configuration after an interval.
         */
        void delayedSaveItems(int ms);

        /**
         * Update item and editor sizes.
         */
        void updateSizes();

        void updateCurrent();

        /**
         * Hide row if filtered out, otherwise show.
         * @return true only if hidden
         */
        bool hideFiltered(int row);

        /// Properties of an item needed to decide its visibility.
        ///
        /// Reading the item data is expensive (large data are stored in
        /// separate files), so it is read once per row and reused.
        struct RowTypeInfo {
            bool isGroup = false;
            bool collapsed = false;
            bool hasText = false;
            bool hasImage = false;
            bool hasFile = false;
            bool isMarked = false;
        };

        RowTypeInfo rowTypeInfo(int row) const;

        /// Cached row properties. Reading the item data is expensive (large
        /// data are stored in separate files), so the properties are kept
        /// until the item data change.
        enum RowFlag : quint8 {
            RowFlagKnown = 1 << 0,
            RowFlagGroup = 1 << 1,
            RowFlagMarked = 1 << 5,
            RowFlagFile = 1 << 6,
            RowFlagCollapsed = 1 << 2,
            RowFlagText = 1 << 3,
            RowFlagImage = 1 << 4,
        };
        mutable QVector<quint8> m_rowFlags;

        void insertRowFlags(int first, int count);
        void removeRowFlags(int first, int last);
        void invalidateRowFlags(int first, int last);

        /// Decides the visibility of a row from already fetched properties.
        /// @param headerRow Row of the group containing the item (-1 if none).
        bool isRowHiddenByFilter(int row, const RowTypeInfo &info,
                                 int headerRow, bool headerCollapsed) const;

        /// Applies the type filter and the group folding to all rows.
        ///
        /// The work is split into short time slices to keep the window
        /// responsive even for tabs with thousands of items.
        void startGroupFoldingPass();
        void continueGroupFoldingPass(int passId);

        void applyGroupFolding();

        /// Stores the content type flags in items that miss them.
        ///
        /// Items created before the flags were introduced are updated in the
        /// background so that later filtering needs no item data at all.
        void startTypeFlagsBackfill();
        void continueTypeFlagsBackfill();
        int m_typeFlagsBackfillRow = -1;
        /// Suppresses saving while item data are updated internally.
        int m_suppressSave = 0;

        /// Coalesces the folding into a single pass in the event loop.
        ///
        /// Used for model signals so that the view is not modified while the
        /// model is delivering the change.
        void scheduleGroupFolding();
        bool m_groupFoldingScheduled = false;

        QString groupTitle(const QString &name, bool collapsed) const;

        /**
         * Connects signals and starts external editor.
         */
        bool startEditor(QObject *editor);

        void setEditorWidget(ItemEditorWidget *editor, bool changeClipboard = false);

        void editItem(const QModelIndex &index, const QString &format = QString(), bool changeClipboard = false);

        void updateEditorGeometry();

        void updateCurrentItem();

        /**
         * Get index near given @a point.
         * If space between items is at the @a point, return next item.
         */
        QModelIndex indexNear(int offset) const;

        int getDropRow(QPoint position) const;

        void connectModelAndDelegate();

        void disconnectModel();

        void updateItemMaximumSize();

        void processDragAndDropEvent(QDropEvent *event);

        /// Removes indexes without notifying or asking plugins.
        void dropIndexes(const QModelIndexList &indexes);
        void dropIndexes(QList<QPersistentModelIndex> &indexes);

        void focusEditedIndex();

        int findNextVisibleRow(int row);
        int findPreviousVisibleRow(int row);
        int findVisibleRowFrom(int row);

        void preloadCurrentPage();
        void preload(int pixels, int direction, const QModelIndex &start);

        void updateCurrentIndex();

        void moveToTop(const QModelIndex &index);

        void maybeEmitEditingFinished();

        QModelIndex firstUnpinnedIndex() const;

        void dragDropScroll();

        int currentRowFromSearch(const QString &search);

        void filterBatch(int filterId, const QPersistentModelIndex &lastIndex);

        ItemSaverPtr m_itemSaver;

        QString m_tabName;
        int m_maxItemCount = 200;
        bool m_storeItems = true;

        ClipboardModel m;
        ItemDelegate d;
        QTimer m_timerSave;
        QTimer m_timerEmitItemCount;
        QTimer m_timerUpdateSizes;
        QTimer m_timerUpdateCurrent;
        QTimer m_timerDragDropScroll;
        QTimer m_timerPreload;
        bool m_ignoreMouseMoveWithButtonPressed = false;
        bool m_resizing = false;
        bool m_resizeEvent = false;

        QPointer<ItemEditorWidget> m_editor;
        int m_externalEditorsOpen = 0;

        ClipboardBrowserSharedPtr m_sharedData;

        ItemTypeFilter m_itemTypeFilter = ItemTypeFilter::All;

        /// State of the running (time sliced) group folding pass.
        int m_groupFoldingPassId = 0;
        int m_groupFoldingRow = 0;
        int m_groupFoldingHeaderRow = -1;
        bool m_groupFoldingHeaderCollapsed = false;

        int m_dragTargetRow;
        QPoint m_dragStartPosition;

        bool m_selectNewItems = false;

        int m_lastFilterId = 0;
};
