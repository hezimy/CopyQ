// SPDX-License-Identifier: GPL-3.0-or-later

#include "clipboarddialog.h"
#include "ui_clipboarddialog.h"

#include "common/common.h"
#include "common/contenttype.h"
#include "common/mimetypes.h"
#include "common/shortcuts.h"
#include "common/timer.h"
#include "gui/iconfactory.h"
#include "gui/icons.h"
#include "gui/theme.h"
#include "item/itemtypeflags.h"

#include "common/appconfig.h"
#include "platform/platformclipboard.h"

#include <QBuffer>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGraphicsPixmapItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QLocale>
#include <QListWidgetItem>
#include <QMimeData>
#include <QMovie>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QShowEvent>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>

namespace {

// Limit number of characters to load at once - performance reasons.
constexpr int batchLoadCharacters = 4096;
constexpr int priorityHigh = 100;
constexpr int priorityMedium = priorityHigh / 2;
constexpr int priorityLow = priorityMedium / 2;
constexpr int priorityLower = priorityLow / 2;
constexpr int priorityLowest = 0;

constexpr double minZoom = 0.10;
constexpr double maxZoom = 16.0;

// Fixed zoom levels, so that zooming in and out always gives the same, round
// values (100%, 125%, 150%, 200% ... and 75%, 50%, 25% ...).
constexpr double zoomLevels[] = {
    0.10, 0.25, 0.50, 0.75, 1.00, 1.25, 1.50, 2.00,
    3.00, 4.00, 6.00, 8.00, 12.00, 16.00
};
constexpr int zoomLevelCount = int(sizeof(zoomLevels) / sizeof(zoomLevels[0]));

/// Human readable data size. Values below 1 MB are always shown in KB.
QString formatItemSize(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);

    if (bytes < 1024 * 1024) {
        const double kb = bytes / 1024.0;
        return QStringLiteral("%1 KB").arg(
                QString::number(kb, 'f', bytes < 10240 ? 1 : 0));
    }

    if (bytes < qint64(1024) * 1024 * 1024) {
        return QStringLiteral("%1 MB").arg(
                QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    }

    return QStringLiteral("%1 GB").arg(
            QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2));
}

/// Local path from the item text, with "file:" URL percent-escapes decoded.
QString localPathFromItemText(const QString &text)
{
    const QString firstLine = text.trimmed().split(QLatin1Char('\n')).value(0).trimmed();

    if ( firstLine.startsWith(QLatin1String("file://"), Qt::CaseInsensitive) ) {
        const QString localFile = QUrl(firstLine).toLocalFile();
        if ( !localFile.isEmpty() )
            return localFile;
    }

    return itemType::localPathFromText(text);
}

int formatSortPriority(const QString &format)
{
    if (format == mimeText)
        return priorityHigh + 2;

    if (format == mimeHtml)
        return priorityHigh + 1;

    if ( format.startsWith("text/") )
        return priorityHigh;

    if ( format.startsWith("application/") )
        return priorityLow;

    if ( format.contains("/x-") )
        return priorityLower;

    if ( format.isEmpty() || format[0].isUpper() )
        return priorityLowest;

    return priorityMedium;
}

} // namespace

ClipboardDialog::ClipboardDialog(QWidget *parent)
    : QDialog(parent)
    , m_clipboard(platformNativeInterface()->clipboard())
{
    init();

    const QMimeData *clipData = m_clipboard->mimeData(ClipboardMode::Clipboard);
    if (clipData) {
        m_dataFromClipboard = true;
        for (const auto &format : clipData->formats()) {
            m_data.insert(format, QVariant());
        }
        setData(m_data);
    }
}

ClipboardDialog::ClipboardDialog(
        const QPersistentModelIndex &index, QAbstractItemModel *model, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
    , m_index(index)
{
    init();

    setWindowTitle( tr("Item Content") );
    connectModel();
    onDataChanged(m_index, m_index);
}

ClipboardDialog::~ClipboardDialog()
{
    delete ui;
}

void ClipboardDialog::connectModel()
{
    if (m_modelConnection)
        disconnect(m_modelConnection);

    if (m_model) {
        m_modelConnection = connect( m_model.data(), &QAbstractItemModel::dataChanged,
                                     this, &ClipboardDialog::onDataChanged );
    }
}

void ClipboardDialog::setNavigationView(QListView *view)
{
    m_navigationView = view;
}

void ClipboardDialog::setIndex(
        const QPersistentModelIndex &index, QAbstractItemModel *model)
{
    m_index = index;

    if (m_model != model) {
        m_model = model;
        connectModel();
    }

    const QVariantMap data = m_index.isValid()
            ? m_index.data(contentType::data).toMap()
            : QVariantMap();
    setData(data);
    resizeToContent();
}

void ClipboardDialog::onListWidgetFormatsCurrentItemChanged(
        QListWidgetItem *current, QListWidgetItem *)
{
    ui->actionRemove_Format->setEnabled(current != nullptr);

    const QString mime = current ? current->text() : QString();
    const bool hasImage = mime.startsWith(QLatin1String("image")) ;
    const QByteArray animationFormat =
            QString(mime).remove(QRegularExpression(QLatin1String("^image/"))).toUtf8();
    const bool hasAnimation = QMovie::supportedFormats().contains(animationFormat);

    ui->textEdit->clear();

    QVariant value = m_data.value(mime);
    if ( m_dataFromClipboard && !value.isValid() ) {
        const auto data = m_clipboard->mimeData(ClipboardMode::Clipboard);
        value = m_data[mime] = data ? data->data(mime) : QByteArray();
    }

    const QByteArray bytes = value.toByteArray();

    m_timerTextLoad.stop();

    m_image = QPixmap();
    m_timerUpdateImage.stop();
    hideAnimation();

    // An image that was copied as a file path (or an URL) is shown as an image
    // as well instead of showing the path as text.
    QString imageFromPath;
    if (!hasImage && !hasAnimation) {
        const QString path = localPathFromItemText( dataToText(bytes, mime) );
        if ( !itemType::imagePathFromText(path).isEmpty() && QFileInfo::exists(path) )
            imageFromPath = path;
    }

    const bool showsImage = hasImage || !imageFromPath.isEmpty();

    // For an image loaded from a file show the file size, not the size of the
    // path text.
    qint64 shownBytes = bytes.size();
    if ( !imageFromPath.isEmpty() )
        shownBytes = QFileInfo(imageFromPath).size();

    ui->textEdit->setVisible(!showsImage);
    ui->graphicsView->setVisible(showsImage);

    if (hasAnimation) {
        if (m_animation)
            m_animation->deleteLater();

        if (m_animationBuffer)
            m_animationBuffer->deleteLater();

        m_animationBuffer = new QBuffer(this);
        m_animationBuffer->open(QIODevice::ReadWrite);
        m_animationBuffer->write(bytes);
        m_animationBuffer->seek(0);

        m_animation = new QMovie(this);
        m_animation->setDevice(m_animationBuffer);
        m_animation->setFormat(animationFormat);
        showAnimation();
    } else if (hasImage) {
        QPixmap pix;
        pix.loadFromData( bytes, mime.toLatin1() );
        m_image = pix;
        // Show the image in its original size, scaled down only if it does not
        // fit the screen.
        setZoom( zoomToFitScreen() );
        updateImage();
        resizeToContent();
    } else if (!imageFromPath.isEmpty()) {
        QPixmap pix( imageFromPath );
        if ( !pix.isNull() ) {
            m_image = pix;
            setZoom( zoomToFitScreen() );
            updateImage();
            resizeToContent();
        } else {
            m_textToShow = dataToText(bytes, mime);
            addText();
        }
    } else {
        m_textToShow = dataToText(bytes, mime);
        addText();
    }

    if (m_zoomBar)
        m_zoomBar->setVisible(showsImage || hasAnimation);

    if (current) {
        // Compact so that it does not squeeze the zoom controls.
        const QString sizeText = formatItemSize( shownBytes );
        QString text = sizeText;
        if ( !m_image.isNull() ) {
            text = tr("<strong>%1 × %2</strong>", "Image dimensions in pixels")
                    .arg(m_image.width()).arg(m_image.height())
                    + QStringLiteral(" &nbsp;·&nbsp; ") + sizeText;
        }
        ui->labelProperties->setText(text);
    } else {
        ui->labelProperties->setText(QString());
    }
}

void ClipboardDialog::onActionRemoveFormatTriggered()
{
     QListWidgetItem *item = ui->listWidgetFormats->currentItem();
     if (item) {
         const QString mimeToRemove = item->text();
         m_data.remove(mimeToRemove);
         delete item;

         if (m_model)
             m_model->setData(m_index, mimeToRemove, contentType::removeFormats);
         else
             emit changeClipboard(m_data);
     }
}

void ClipboardDialog::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight)
{
    if (m_index.isValid()
            && topLeft.row() <= m_index.row()
            && m_index.row() <= bottomRight.row())
    {
        setData( m_index.data(contentType::data).toMap() );
    }
}

void ClipboardDialog::showAnimation()
{
    if (!m_animationLabel) {
        // The label is owned by the proxy widget once it is added to the scene.
        m_animationLabel = new QLabel;
        m_animationLabel->setAlignment(Qt::AlignCenter);
        m_animationProxy = m_scene->addWidget(m_animationLabel);
    }

    m_animationLabel->setMovie(m_animation);
    m_animation->start();

    const QSize frameSize = m_animation->currentPixmap().size();
    if (frameSize.isValid() && !frameSize.isEmpty())
        m_animationLabel->setFixedSize(frameSize);
    else
        m_animationLabel->setFixedSize(m_animation->currentPixmap().size());

    m_animationProxy->setPos(0, 0);
    m_animationProxy->setVisible(true);
    if (m_pixmapItem)
        m_pixmapItem->setVisible(false);

    m_scene->setSceneRect(QRectF(QPointF(0, 0), QSizeF(m_animationLabel->size())));
    setZoom(zoomToFitScreen());
    resizeToContent();
}

void ClipboardDialog::hideAnimation()
{
    if (m_animationLabel)
        m_animationLabel->setMovie(nullptr);
    if (m_animationProxy)
        m_animationProxy->setVisible(false);
    if (m_pixmapItem)
        m_pixmapItem->setVisible(true);
}

void ClipboardDialog::updateImage()
{
    if (!m_pixmapItem)
        return;

    // The animation is shown by another item of the scene.
    if (m_animationProxy && m_animationProxy->isVisible())
        return;

    if (m_image.isNull()) {
        m_pixmapItem->setPixmap(QPixmap());
        m_scene->setSceneRect(QRectF());
        return;
    }

    // The pixmap is never resized - the scene transformation does the zooming,
    // so the original data stay untouched and 1:1 shows real pixels.
    m_pixmapItem->setPixmap(m_image);
    m_pixmapItem->setTransformationMode(Qt::SmoothTransformation);
    m_pixmapItem->setOffset(0, 0);
    m_scene->setSceneRect(
        QRectF(QPointF(0, 0), QSizeF(m_image.size() / m_image.devicePixelRatio())) );

    applyZoom();
}

void ClipboardDialog::applyZoom()
{
    ui->graphicsView->resetTransform();
    ui->graphicsView->scale(m_zoom, m_zoom);
    ui->graphicsView->centerOn(m_pixmapItem);

    if (m_zoomLabel)
        m_zoomLabel->setText(QString::number(qRound(m_zoom * 100)) + QStringLiteral("%"));
}

double ClipboardDialog::zoomToFitScreen() const
{
    if ( m_image.isNull() )
        return 1.0;

    const QRect available = availableScreenRect();
    if ( available.width() < 200 || available.height() < 200 )
        return 1.0;

    // Images that are much bigger than the screen would be useless at 1:1
    // (they would be cropped to a small fraction), so they are scaled down.
    const QSize imageSize( m_image.width(), m_image.height() );
    if ( imageSize.width() <= available.width() * 2
         && imageSize.height() <= available.height() * 2 )
    {
        return 1.0;
    }

    const QSize maxSize( available.width() * 88 / 100, available.height() * 78 / 100 );

    double zoom = 1.0;
    if ( imageSize.width() > maxSize.width() )
        zoom = qMin(zoom, double(maxSize.width()) / imageSize.width());
    if ( imageSize.height() > maxSize.height() )
        zoom = qMin(zoom, double(maxSize.height()) / imageSize.height());
    return zoom;
}

void ClipboardDialog::setZoom(double zoom)
{
    m_zoom = qBound(minZoom, zoom, maxZoom);
    applyZoom();
}

double ClipboardDialog::nextZoom(double zoom, int direction) const
{
    if (direction > 0) {
        for (int i = 0; i < zoomLevelCount; ++i) {
            if ( zoomLevels[i] > zoom + 0.001 )
                return zoomLevels[i];
        }
        return zoomLevels[zoomLevelCount - 1];
    }

    for (int i = zoomLevelCount - 1; i >= 0; --i) {
        if ( zoomLevels[i] < zoom - 0.001 )
            return zoomLevels[i];
    }
    return zoomLevels[0];
}

void ClipboardDialog::navigateTo(int delta)
{
    if ( !m_model || !m_index.isValid() || delta == 0 )
        return;

    const int step = delta > 0 ? 1 : -1;
    const int count = m_model->rowCount();
    int row = m_index.row() + step;

    // Skip rows that are not shown in the item list (group headers, items
    // hidden by the type filter and items folded in groups).
    while ( row >= 0 && row < count ) {
        const QModelIndex index = m_model->index(row, 0);
        const bool hidden = m_navigationView && m_navigationView->isRowHidden(row);
        const QVariantMap data = index.data(contentType::data).toMap();
        if ( !hidden && !data.contains(mimeItemGroup) ) {
            m_index = QPersistentModelIndex(index);
            setData(data);
            return;
        }
        row += step;
    }
}

bool ClipboardDialog::handleWheel(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();

    if ( event->modifiers() & Qt::ControlModifier ) {
        setZoom( nextZoom(m_zoom, delta > 0 ? 1 : -1) );
        return true;
    }

    if ( m_model && m_index.isValid() ) {
        navigateTo( delta > 0 ? -1 : 1 );
        return true;
    }

    return false;
}

void ClipboardDialog::wheelEvent(QWheelEvent *event)
{
    if ( handleWheel(event) )
        event->accept();
    else
        QDialog::wheelEvent(event);
}

bool ClipboardDialog::eventFilter(QObject *watched, QEvent *event)
{
    // The image view would scroll with the wheel, but the wheel is used for
    // zooming and for switching to the previous/next item instead.
    if ( event->type() == QEvent::Wheel && watched == ui->graphicsView->viewport() )
        return handleWheel(static_cast<QWheelEvent*>(event));

    return QDialog::eventFilter(watched, event);
}

void ClipboardDialog::keyPressEvent(QKeyEvent *event)
{
    switch ( event->key() ) {
    case Qt::Key_Up:
    case Qt::Key_PageUp:
        navigateTo(-1);
        return;
    case Qt::Key_Down:
    case Qt::Key_PageDown:
        navigateTo(1);
        return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        setZoom( nextZoom(m_zoom, 1) );
        return;
    case Qt::Key_Minus:
        setZoom( nextZoom(m_zoom, -1) );
        return;
    case Qt::Key_0:
        setZoom(1.0);
        return;
    default:
        break;
    }
    QDialog::keyPressEvent(event);
}

QRect ClipboardDialog::availableScreenRect() const
{
    if (QScreen *currentScreen = screen())
        return currentScreen->availableGeometry();

    if (QScreen *primaryScreen = QGuiApplication::primaryScreen())
        return primaryScreen->availableGeometry();

    return QRect(0, 0, 1280, 800);
}

void ClipboardDialog::centerOnScreen()
{
    const QRect available = availableScreenRect();
    const QRect frame = frameGeometry();
    const QSize windowSize = frame.size().expandedTo(size());
    QPoint topLeft = available.center() - QPoint(windowSize.width() / 2, windowSize.height() / 2);

    // The frame adds some decoration above and below the window.
    if (frame.width() > width())
        topLeft.rx() -= (frame.width() - width()) / 2;
    if (frame.height() > height())
        topLeft.ry() -= (frame.height() - height()) / 2;

    move(topLeft);
}

void ClipboardDialog::fillScreen()
{
    if ( isMaximized() )
        return;

    const QRect target = availableScreenRect();
    const QRect current = geometry();
    const QRect frame = frameGeometry();

    // The frame (title bar and borders) is not part of the client area, so the
    // client rectangle has to be inset to keep the whole frame on the screen.
    const int left = current.x() - frame.x();
    const int top = current.y() - frame.y();
    const int right = frame.right() - current.right();
    const int bottom = frame.bottom() - current.bottom();

    QRect wanted( target.x() + left,
                  target.y() + top,
                  target.width() - left - right,
                  target.height() - top - bottom );

    if ( wanted.width() < 200 || wanted.height() < 200 )
        wanted = target;

    if ( m_screenFilling && geometry() == wanted )
        return;

    setGeometry(wanted);
    m_screenFilling = true;
}

void ClipboardDialog::resizeToContent()
{
    const QRect available = availableScreenRect();
    const QSize maxContent( available.width() * 88 / 100, available.height() * 78 / 100 );

    QSize content;

    if ( !m_image.isNull() ) {
        const double dpr = m_image.devicePixelRatio() > 0 ? m_image.devicePixelRatio() : 1.0;
        content = QSize( qRound(m_image.width() / dpr * m_zoom),
                         qRound(m_image.height() / dpr * m_zoom) );
    } else if (m_animationLabel) {
        content = m_animationLabel->size() * m_zoom;
    }

    if ( content.isEmpty() ) {
        // Text: use a reasonable size.
        content = QSize( qMin(available.width() / 2, 560),
                         qMin(available.height() * 2 / 5, 420) );
    }

    // The image does not fit into a normal window at the current zoom - the
    // window is maximized instead of squeezing the image into a small one.
    m_maximizeOnShow = content.width() > maxContent.width()
            || content.height() > maxContent.height();

    // Keep the space needed by the margins, the bottom bar and the title bar.
    const QSize viewSize = ui->graphicsView->size();
    QSize chrome = viewSize.isEmpty() ? QSize(0, 0) : size() - viewSize;
    if ( chrome.width() < 0 || chrome.height() < 0 )
        chrome = QSize(0, 0);

    // The window manager only honors the window state for a visible window, so
    // the maximized state is applied in showEvent() (see it) for a window that
    // is not shown yet.
    if ( !isVisible() ) {
        content = content.expandedTo(QSize(220, 140));
        if (m_maximizeOnShow)
            content = content.boundedTo(maxContent);
        resize(content + chrome);
        applyZoom();
        return;
    }

    if (m_maximizeOnShow) {
        fillScreen();
        applyZoom();
        return;
    }

    m_screenFilling = false;
    if (isMaximized())
        showNormal();

    content = content.expandedTo(QSize(220, 140));

    QSize wanted = content + chrome;
    wanted = wanted.boundedTo(QSize(available.width() * 96 / 100,
                                    available.height() * 92 / 100));

    resize(wanted);

    applyZoom();
    centerOnScreen();
}

void ClipboardDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    m_timerUpdateImage.start();
}

void ClipboardDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    if (m_firstShow) {
        m_firstShow = false;
        // The layout is complete now, so the window chrome size is exact.
        layout()->activate();
        // Same path as when switching items - no window state changes, so that
        // the window manager cannot restore some stale "normal" geometry.
        resizeToContent();
    }
}

void ClipboardDialog::addText()
{
    const int scrollValue = ui->textEdit->verticalScrollBar()->value();

    ui->textEdit->appendPlainText( m_textToShow.left(batchLoadCharacters) );
    m_textToShow.remove(0, batchLoadCharacters);

    if ( !m_textToShow.isEmpty() )
        m_timerTextLoad.start();

    ui->textEdit->verticalScrollBar()->setValue(scrollValue);
}

void ClipboardDialog::init()
{
    Q_ASSERT(!ui);

    ui = new Ui::ClipboardDialog;
    ui->setupUi(this);

    connect(ui->listWidgetFormats, &QListWidget::currentItemChanged,
            this, &ClipboardDialog::onListWidgetFormatsCurrentItemChanged);
    connect(ui->actionRemove_Format, &QAction::triggered,
            this, &ClipboardDialog::onActionRemoveFormatTriggered);

    setWindowIcon(appIcon());

    // Behave like a normal window: no always-on-top, resizable and not tied to
    // the main window, so it can be minimized, resized and put behind others.
    setWindowFlags( Qt::Window
                    | Qt::WindowTitleHint
                    | Qt::WindowMinimizeButtonHint
                    | Qt::WindowMaximizeButtonHint
                    | Qt::WindowCloseButtonHint );
    setMinimumSize(320, 240);

    // The format list and the content label are not useful for the normal use.
    ui->group1->hide();
    ui->labelContent->hide();
    ui->horizontalLayout->setStretchFactor(0, 0);
    ui->horizontalLayout->setStretchFactor(1, 1);

    const Theme theme( AppConfig().settings() );
    theme.decorateDialog(this);

    // Image view
    m_scene = new QGraphicsScene(this);
    m_pixmapItem = m_scene->addPixmap(QPixmap());
    ui->graphicsView->setScene(m_scene);
    ui->graphicsView->setAlignment(Qt::AlignCenter);
    ui->graphicsView->setDragMode(QGraphicsView::ScrollHandDrag);
    ui->graphicsView->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    ui->graphicsView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    ui->graphicsView->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    ui->graphicsView->setFrameShape(QFrame::NoFrame);
    // Smooth (bilinear) scaling when zoomed out/in - without this the image is
    // drawn with a nearest-neighbour filter and looks jagged.
    ui->graphicsView->setRenderHints(QPainter::Antialiasing
                                     | QPainter::SmoothPixmapTransform);
    ui->graphicsView->setOptimizationFlag(QGraphicsView::DontSavePainterState, true);
    ui->graphicsView->setOptimizationFlag(QGraphicsView::DontAdjustForAntialiasing, true);
    ui->graphicsView->viewport()->installEventFilter(this);

    // Bottom bar: data size on the left, zoom controls in the middle, close
    // button on the right.
    auto *zoomOut = new QToolButton(this);
    zoomOut->setText(QStringLiteral("\u2212"));
    zoomOut->setAutoRaise(true);
    zoomOut->setToolTip(tr("Zoom Out"));

    m_zoomLabel = new QLabel(QStringLiteral("100%"), this);
    m_zoomLabel->setAlignment(Qt::AlignCenter);
    m_zoomLabel->setMinimumWidth(46);

    auto *zoomIn = new QToolButton(this);
    zoomIn->setText(QStringLiteral("+"));
    zoomIn->setAutoRaise(true);
    zoomIn->setToolTip(tr("Zoom In"));

    auto *zoomReset = new QToolButton(this);
    zoomReset->setText(QStringLiteral("1:1"));
    zoomReset->setAutoRaise(true);
    zoomReset->setToolTip(tr("Original Size"));

    // The zoom controls are the main interaction of the dialog - make them
    // easier to read and to hit.
    QFont zoomFont = font();
    if ( zoomFont.pointSizeF() > 0 )
        zoomFont.setPointSizeF( zoomFont.pointSizeF() * 1.2 );
    else
        zoomFont.setPixelSize( int(zoomFont.pixelSize() * 1.2) );
    zoomFont.setBold(true);

    for ( QWidget *w : QList<QWidget*>{zoomOut, m_zoomLabel, zoomIn, zoomReset} )
        w->setFont(zoomFont);

    zoomOut->setMinimumSize(26, 24);
    zoomIn->setMinimumSize(26, 24);
    zoomReset->setMinimumSize(38, 24);
    m_zoomLabel->setMinimumWidth(56);

    m_zoomBar = new QWidget(this);
    auto *zoomLayout = new QHBoxLayout(m_zoomBar);
    zoomLayout->setContentsMargins(0, 0, 0, 0);
    zoomLayout->setSpacing(2);
    zoomLayout->addWidget(zoomOut);
    zoomLayout->addWidget(m_zoomLabel);
    zoomLayout->addWidget(zoomIn);
    zoomLayout->addWidget(zoomReset);

    connect( zoomOut, &QToolButton::clicked,
             this, [this]() { setZoom(nextZoom(m_zoom, -1)); } );
    connect( zoomIn, &QToolButton::clicked,
             this, [this]() { setZoom(nextZoom(m_zoom, 1)); } );
    connect( zoomReset, &QToolButton::clicked,
             this, [this]() { setZoom(1.0); } );

    ui->verticalLayout_2->removeWidget(ui->labelProperties);
    ui->gridLayout->removeWidget(ui->buttonBox);

    auto *bottomLayout = new QHBoxLayout;
    bottomLayout->addWidget(ui->labelProperties, 0, Qt::AlignVCenter);
    bottomLayout->addStretch(1);
    bottomLayout->addWidget(m_zoomBar, 0, Qt::AlignVCenter);
    bottomLayout->addStretch(1);
    bottomLayout->addWidget(ui->buttonBox, 0, Qt::AlignVCenter);
    ui->gridLayout->addLayout(bottomLayout, 2, 0);

    initSingleShotTimer(&m_timerUpdateImage, 60, this, &ClipboardDialog::updateImage);

    ui->actionRemove_Format->setIcon( getIcon("list-remove", IconTrash) );
    ui->actionRemove_Format->setShortcut(shortcutToRemove());
    ui->listWidgetFormats->addAction(ui->actionRemove_Format);

    onListWidgetFormatsCurrentItemChanged(nullptr, nullptr);

    ensurePolished();
    layout()->activate();

    ui->listWidgetFormats->setFocus(Qt::ActiveWindowFocusReason);
}

void ClipboardDialog::setData(const QVariantMap &data)
{
    const QString currentFormat = ui->listWidgetFormats->currentIndex().data().toString();
    ui->listWidgetFormats->clear();
    m_data = data;

    QStringList formats = data.keys();
    std::sort( std::begin(formats), std::end(formats), [](const QString &lhs, const QString &rhs) {
        const int lhsPriority = formatSortPriority(lhs);
        const int rhsPriority = formatSortPriority(rhs);
        return lhsPriority == rhsPriority ? lhs < rhs : lhsPriority > rhsPriority;
    } );
    for (const QString &format : formats) {
        ui->listWidgetFormats->addItem(format);
    }

    // Prefer images, then the format selected before, then the first one.
    int preferredRow = -1;
    for (int row = 0; row < ui->listWidgetFormats->count(); ++row) {
        if ( ui->listWidgetFormats->item(row)->text().startsWith(QLatin1String("image/")) ) {
            preferredRow = row;
            break;
        }
    }
    for (int row = 0; row < ui->listWidgetFormats->count(); ++row) {
        if ( ui->listWidgetFormats->item(row)->text() == currentFormat ) {
            if (preferredRow < 0)
                preferredRow = row;
            break;
        }
    }
    if (preferredRow < 0 && ui->listWidgetFormats->count() > 0)
        preferredRow = 0;
    if (preferredRow >= 0)
        ui->listWidgetFormats->setCurrentRow(preferredRow);

    initSingleShotTimer(&m_timerTextLoad, 10, this, &ClipboardDialog::addText);
}
