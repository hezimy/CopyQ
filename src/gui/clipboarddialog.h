// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once


#include <QDialog>
#include <QPixmap>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QRect>
#include <QVariantMap>
#include <QTimer>

#include "platform/platformnativeinterface.h"

class QListView;
class QBuffer;
class QGraphicsPixmapItem;
class QGraphicsProxyWidget;
class QGraphicsScene;
class QLabel;
class QListWidgetItem;
class QMovie;
class QWheelEvent;

namespace Ui {
    class ClipboardDialog;
}

class ClipboardDialog final : public QDialog
{
    Q_OBJECT

public:
    /**
     * Create dialog with clipboard data.
     */
    explicit ClipboardDialog(QWidget *parent = nullptr);

    /**
     * Create dialog with item data.
     */
    explicit ClipboardDialog(const QPersistentModelIndex &index, QAbstractItemModel *model, QWidget *parent = nullptr);

    ~ClipboardDialog();

    /**
     * Switches the dialog to another item.
     *
     * The window is reused so that pressing the shortcut repeatedly does not
     * open more and more windows.
     */
    void setIndex(const QPersistentModelIndex &index, QAbstractItemModel *model);

    /**
     * Sets the item list which decides what items are shown in it.
     *
     * Rows hidden in the list (filtered out or folded in a group) are skipped
     * when navigating to the previous/next item.
     */
    void setNavigationView(QListView *view);

signals:
    void changeClipboard(const QVariantMap &data);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onListWidgetFormatsCurrentItemChanged(
            QListWidgetItem *current, QListWidgetItem *previous);

    void onActionRemoveFormatTriggered();

    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);

    void addText();

    void init();

    void connectModel();

    void setData(const QVariantMap &data);

    /** Shows the image at the current zoom level. */
    void updateImage();

    /** Applies the zoom to the image view. */
    void applyZoom();

    /** Resizes the dialog so that it fits the shown content. */
    void resizeToContent();

    /** Sets the image zoom (1.0 = original size) and updates the controls. */
    void setZoom(double zoom);

    /** Next/previous zoom level of the fixed zoom ladder (direction = 1/-1). */
    double nextZoom(double zoom, int direction) const;

    /** Zoom that makes the image fit the screen (at most 1:1). */
    double zoomToFitScreen() const;

    /** Shows the item at the given offset from the current one. */
    void navigateTo(int delta);

    /** Handles the mouse wheel (zoom or navigation), returns true if used. */
    bool handleWheel(QWheelEvent *event);

    /** Area of the screen the dialog may use. */
    QRect availableScreenRect() const;

    /** Moves the dialog to the middle of the screen. */
    void centerOnScreen();

    /**
     * Makes the window fill the screen without using the maximized window
     * state (state changes race with the window manager when switching items).
     */
    void fillScreen();

    void showAnimation();
    void hideAnimation();

    Ui::ClipboardDialog *ui = nullptr;
    QPointer<QAbstractItemModel> m_model;
    QMetaObject::Connection m_modelConnection;
    QPersistentModelIndex m_index;
    QPointer<QListView> m_navigationView;
    QVariantMap m_data;
    bool m_dataFromClipboard = false;
    QString m_textToShow;
    QTimer m_timerTextLoad;

    QBuffer *m_animationBuffer = nullptr;
    QMovie *m_animation = nullptr;
    QLabel *m_animationLabel = nullptr;
    QGraphicsProxyWidget *m_animationProxy = nullptr;

    QGraphicsScene *m_scene = nullptr;
    QGraphicsPixmapItem *m_pixmapItem = nullptr;

    /// Original (unscaled) image data of the shown format.
    QPixmap m_image;
    QTimer m_timerUpdateImage;
    double m_zoom = 1.0;
    bool m_firstShow = true;
    /// The window must fill the screen (set once it is visible, because the
    /// window manager has to know the frame size).
    bool m_maximizeOnShow = false;
    /// The window currently fills the screen (set by fillScreen()).
    bool m_screenFilling = false;
    QLabel *m_zoomLabel = nullptr;
    QWidget *m_zoomBar = nullptr;

    PlatformClipboardPtr m_clipboard;
};
