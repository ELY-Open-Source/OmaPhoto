#include "UI/ExportAsSheet.h"
#include "UI/KeyboardShortcuts.h"
#include "UI/ByteCounts.h"
#include "UI/ColorPickerSheet+Dialog.h"
#include "UI/LayerIcons.h"
#include "Document/DocumentLimits.h"
#include "UI/ColorPickerSheet.h"
#include <QButtonGroup>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QMenu>
#include <QToolButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

const QString ExportAsSheet::qualityKey = QStringLiteral("jpegExportQuality");

namespace {
QLabel *text(const QString &words, int pixels, QFont::Weight weight, QPalette::ColorRole role, QWidget *parent)
{
    auto *label = new QLabel(words, parent);
    label->setTextFormat(Qt::PlainText);
    QFont font = label->font();
    font.setPixelSize(pixels);
    font.setWeight(weight);
    label->setFont(font);
    label->setForegroundRole(role);
    return label;
}

// Swift reads a finite number alone, clamped to 0–1.
double startQuality()
{
    bool number = false;
    const double saved = QSettings().value(ExportAsSheet::qualityKey).toDouble(&number);
    return number && std::isfinite(saved) ? std::clamp(saved, 0.0, 1.0) : JPEGOptions().quality;
}

// Swift's 0...2 decimals: its shortest digits, halves to even.
QString inches(double value)
{
    const QString digits = QString::number(value, 'f', QLocale::FloatingPointShortest);
    const qsizetype point = digits.indexOf(QLatin1Char('.'));
    const QString fraction = point < 0 ? QString() : digits.mid(point + 1);
    qint64 hundredths = digits.left(point < 0 ? digits.size() : point).toLongLong() * 100 + fraction.left(2).leftJustified(2, QLatin1Char('0')).toLongLong();
    const QString rest = fraction.mid(2);
    const int half = rest.isEmpty() ? -1 : QString(rest).compare(QStringLiteral("5").leftJustified(rest.size(), QLatin1Char('0')));
    if (half > 0 || (half == 0 && hundredths % 2 == 1))
        ++hundredths;
    QString shown = QLocale(QLocale::English, QLocale::UnitedStates).toString(hundredths / 100.0, 'f', 2);
    while (shown.contains(QLatin1Char('.')) && (shown.endsWith(QLatin1Char('0')) || shown.endsWith(QLatin1Char('.'))))
        shown.chop(1);
    return shown;
}

// Swift's Int field: a whole Int64, else a number truncated.
std::optional<qint64> typedInteger(const QLineEdit &field)
{
    QLocale locale = field.locale();
    locale.setNumberOptions(locale.numberOptions() | QLocale::RejectGroupSeparator);
    // QLocale's readers skip spaces at either end.
    const QString text = field.text();
    bool number = false;
    const qint64 whole = locale.toLongLong(text, &number);
    if (number)
        return whole;
    const double value = locale.toDouble(text, &number);
    // Int64 holds whatever lies strictly inside ±2^63.
    if (!number || !(std::abs(value) < 0x1p63))
        return std::nullopt;
    return qint64(std::trunc(value));
}

struct Made {
    std::optional<QByteArray> data;
    QImage preview;
    std::optional<QString> failure;
};
}

ExportAsSheet::ExportAsSheet(ExportRaster raster, EditorSession &session, ExportFormat format, std::function<void(std::optional<Chosen>)> finish, QWidget *parent)
    : QWidget(parent), m_raster(std::move(raster)), m_session(session), m_finish(std::move(finish)),
      m_settings{format, JPEGOptions(), m_raster.image.width(), m_raster.image.height()}, m_wait(new QTimer(this)),
      m_preview(new JPEGPreview([this] { synchronize(); }, this)), m_spinner(new QProgressBar(m_preview)), m_formats(new QButtonGroup(this)),
      m_width(new PickerField([this] {
          if (const std::optional<qint64> value = typedInteger(*m_width))
              setScale(double(*value) / m_raster.image.width());
          // A number applies; other text goes back.
          showSize();
          synchronize();
      }, [](int) {}, this)),
      m_height(new PickerField([this] {
          if (const std::optional<qint64> value = typedInteger(*m_height))
              setScale(double(*value) / m_raster.image.height());
          // A number applies; other text goes back.
          showSize();
          synchronize();
      }, [](int) {}, this)),
      m_scale(new QToolButton(this)), m_quality(new QSlider(Qt::Horizontal, this)), m_percent(text(QString(), 13, QFont::Normal, QPalette::WindowText, this)),
      m_page(text(QString(), 13, QFont::Normal, QPalette::PlaceholderText, this)),
      m_matteSwatch(new DialogColorSwatch(
          QStringLiteral("Export Background"), [this] { return PaletteColor{m_settings.options.red, m_settings.options.green, m_settings.options.blue}; },
          [this](const PaletteColor &matte) { setMatte(matte); }, session, this)),
      m_note(text(QString(), 13, QFont::Normal, QPalette::PlaceholderText, this)), m_failure(text(QString(), 13, QFont::Normal, QPalette::BrightText, this)),
      m_bytes(text(QString(), 13, QFont::Normal, QPalette::WindowText, this)),
      m_updating(text(QStringLiteral("Updating…"), 13, QFont::Normal, QPalette::PlaceholderText, this)),
      m_export(new QPushButton(QStringLiteral("Export…"), this)), m_fit(new QPushButton(QStringLiteral("Fit"), this)), m_zoomIn(new QPushButton(this)),
      m_zoomOut(new QPushButton(this))
{
    m_settings.options.quality = startQuality();
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(24, 24, 24, 24);
    column->setSpacing(16);
    // The title row sits 8 above; it inherits 8.
    auto *top = new QVBoxLayout;
    top->setSpacing(8);
    auto *title = new QHBoxLayout;
    title->addWidget(text(QStringLiteral("Export As"), 17, QFont::Bold, QPalette::WindowText, this));
    title->addStretch(1);
    m_fit->setObjectName(QStringLiteral("exportFit"));
    m_zoomIn->setObjectName(QStringLiteral("exportZoomIn"));
    m_zoomIn->setAccessibleName(QStringLiteral("Zoom in"));
    m_zoomOut->setObjectName(QStringLiteral("exportZoomOut"));
    m_zoomOut->setAccessibleName(QStringLiteral("Zoom out"));
    for (QPushButton *button : {m_fit, m_zoomIn, m_zoomOut}) {
        button->setAutoDefault(false);
        title->addWidget(button);
    }
    connect(m_fit, &QPushButton::clicked, this, [this] { m_preview->setZoom(std::nullopt); });
    connect(m_zoomIn, &QPushButton::clicked, this, [this] { zoomBy(1); });
    connect(m_zoomOut, &QPushButton::clicked, this, [this] { zoomBy(-1); });
    top->addLayout(title);
    m_preview->setObjectName(QStringLiteral("exportPreview"));
    m_spinner->setObjectName(QStringLiteral("exportSpinner"));
    m_spinner->setRange(0, 0);
    m_spinner->setTextVisible(false);
    m_spinner->setGeometry(264, 149, 32, 32);
    top->addWidget(m_preview);
    column->addLayout(top);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(12);
    // Swift's segmented picker: touching buttons, one checked.
    auto *formats = new QHBoxLayout;
    formats->setSpacing(0);
    for (const ExportFormat each : allExportFormats) {
        auto *button = new QToolButton(this);
        button->setText(rawValue(each));
        button->setObjectName(QStringLiteral("exportFormat") + rawValue(each));
        button->setCheckable(true);
        button->setChecked(each == format);
        m_formats->addButton(button, int(each));
        formats->addWidget(button);
    }
    formats->addStretch(1);
    connect(m_formats, &QButtonGroup::idClicked, this, [this](int id) {
        m_settings.format = ExportFormat(id);
        request();
    });
    grid->addWidget(new QLabel(QStringLiteral("Format"), this), 0, 0);
    grid->addLayout(formats, 0, 1);
    auto *size = new QHBoxLayout;
    size->setSpacing(6);
    m_width->setObjectName(QStringLiteral("exportWidth"));
    m_width->setToolTip(QStringLiteral("Width in pixels; the height follows, keeping the proportions"));
    m_height->setObjectName(QStringLiteral("exportHeight"));
    m_height->setToolTip(QStringLiteral("Height in pixels; the width follows, keeping the proportions"));
    for (PickerField *field : {m_width, m_height}) {
        field->setFixedWidth(64);
        field->setAlignment(Qt::AlignRight);
    }
    size->addWidget(m_width);
    size->addWidget(text(QStringLiteral("×"), 13, QFont::Normal, QPalette::PlaceholderText, this));
    size->addWidget(m_height);
    size->addWidget(text(QStringLiteral("px"), 13, QFont::Normal, QPalette::PlaceholderText, this));
    m_scale->setObjectName(QStringLiteral("exportScale"));
    m_scale->setToolTip(QStringLiteral("Scale the export: the document stays as it is"));
    m_scale->setPopupMode(QToolButton::InstantPopup);
    auto *scales = new QMenu(m_scale);
    for (const int percent : {25, 50, 75, 100, 200})
        connect(scales->addAction(QStringLiteral("%1%").arg(percent)), &QAction::triggered, this, [this, percent] {
            setScale(percent / 100.0);
            // The sheet's own change replaces any typing.
            showSize();
            synchronize();
        });
    m_scale->setMenu(scales);
    size->addWidget(m_scale);
    size->addStretch(1);
    grid->addWidget(new QLabel(QStringLiteral("Size"), this), 1, 0);
    grid->addLayout(size, 1, 1);
    // Swift's slider steps by hundredths.
    auto *qualityLabel = new QLabel(QStringLiteral("Quality"), this);
    qualityLabel->setBuddy(m_quality);
    m_quality->setObjectName(QStringLiteral("exportQuality"));
    m_quality->setRange(0, 100);
    m_quality->setFixedWidth(300);
    m_quality->setValue(int(std::lround(m_settings.options.quality * 100)));
    connect(m_quality, &QSlider::valueChanged, this, [this](int hundredths) {
        m_settings.options.quality = hundredths / 100.0;
        request();
    });
    m_percent->setObjectName(QStringLiteral("exportPercent"));
    m_percent->setFixedWidth(45);
    m_percent->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *quality = new QWidget(this);
    auto *qualityRow = new QHBoxLayout(quality);
    qualityRow->setContentsMargins(0, 0, 0, 0);
    qualityRow->addWidget(m_quality);
    qualityRow->addWidget(m_percent);
    qualityRow->addStretch(1);
    grid->addWidget(qualityLabel, 2, 0);
    grid->addWidget(quality, 2, 1);
    m_jpegRow = {qualityLabel, quality};
    auto *pageLabel = new QLabel(QStringLiteral("Page"), this);
    m_page->setObjectName(QStringLiteral("exportPage"));
    grid->addWidget(pageLabel, 3, 0);
    grid->addWidget(m_page, 3, 1);
    m_pdfRow = {pageLabel, m_page};
    // JPEG and a page both need a colour beneath.
    auto *backgroundLabel = new QLabel(QStringLiteral("Background"), this);
    m_matteSwatch->setObjectName(QStringLiteral("exportBackground"));
    m_matteSwatch->setToolTip(QStringLiteral("Color that fills transparent areas"));
    grid->addWidget(backgroundLabel, 4, 0);
    grid->addWidget(m_matteSwatch, 4, 1, Qt::AlignLeft);
    m_backgroundRow = {backgroundLabel, m_matteSwatch};
    column->addLayout(grid);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(12);
    m_note->setObjectName(QStringLiteral("exportNote"));
    buttons->addWidget(m_note);
    buttons->addStretch(1);
    m_failure->setObjectName(QStringLiteral("exportError"));
    m_bytes->setObjectName(QStringLiteral("exportBytes"));
    m_updating->setObjectName(QStringLiteral("exportUpdating"));
    buttons->addWidget(m_failure);
    buttons->addWidget(m_bytes);
    buttons->addWidget(m_updating);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
    cancel->setObjectName(QStringLiteral("exportCancel"));
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, [this] {
        DialogColorSwatch::closePicker(m_session);
        m_finish(std::nullopt);
    });
    m_export->setObjectName(QStringLiteral("exportExport"));
    m_export->setDefault(true);
    // Swift's configuredNativeShortcut: Return and Escape, as remapped.
    NativeShortcut::bind(*this, m_export, cancel);
    // Export rests until a result is ready.
    connect(m_export, &QPushButton::clicked, this, [this] {
        DialogColorSwatch::closePicker(m_session);
        if (m_settings.format == ExportFormat::jpeg)
            QSettings().setValue(qualityKey, m_settings.options.quality);
        m_finish(Chosen{m_result.value().data, m_result.value().settings.format});
    });
    buttons->addWidget(cancel);
    buttons->addWidget(m_export);
    column->addLayout(buttons);

    m_wait->setObjectName(QStringLiteral("exportWait"));
    m_wait->setSingleShot(true);
    m_wait->setInterval(200);
    m_wait->setTimerType(Qt::PreciseTimer);
    connect(m_wait, &QTimer::timeout, this, &ExportAsSheet::encode);
    bindZoomKeys();
    connect(&ShortcutSettings::shared(), &ShortcutSettings::changed, this, &ExportAsSheet::bindZoomKeys);
    request();
}

// A window-modal sheet blocks the menus: it takes their keys.
void ExportAsSheet::bindZoomKeys()
{
    const std::array<QKeySequence, 4> originals{QKeySequence(Qt::CTRL | Qt::Key_0), QKeySequence(Qt::CTRL | Qt::Key_1),
                                                QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Minus)};
    const std::array<std::function<void()>, 4> actions{[this] { m_preview->setZoom(std::nullopt); }, [this] { m_preview->setZoom(1.0); },
                                                       [this] { zoomBy(1); }, [this] { zoomBy(-1); }};
    for (size_t index = 0; index < m_zoomKeys.size(); ++index) {
        if (!m_zoomKeys[index]) {
            m_zoomKeys[index] = new QShortcut(this);
            connect(m_zoomKeys[index], &QShortcut::activated, this, actions[index]);
        }
        m_zoomKeys[index]->setKey(ShortcutSettings::shared().menu(originals[index]));
    }
    synchronize();
}

// SwiftUI cancels the task when the sheet goes.
ExportAsSheet::~ExportAsSheet()
{
    m_cancelled->store(true);
    releaseFocus(*this);
    DialogColorSwatch::closePicker(m_session);
}

// Swift's task(id:): the last request stops; this waits 200 ms.
void ExportAsSheet::request()
{
    if (m_cancelled)
        m_cancelled->store(true);
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    m_error.reset();
    m_wait->start();
    synchronize();
}

void ExportAsSheet::encode()
{
    const std::shared_ptr<std::atomic_bool> cancelled = m_cancelled;
    const Settings requested = m_settings;
    auto *watcher = new QFutureWatcher<Made>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, cancelled, requested] {
        watcher->deleteLater();
        // Swift's check after the await: a newer request supersedes.
        if (cancelled->load())
            return;
        Made made = watcher->future().takeResult();
        if (made.data)
            m_result = Encoded{requested, std::move(*made.data), std::move(made.preview)};
        else
            m_error = made.failure.value();
        synchronize();
    });
    watcher->setFuture(QtConcurrent::run([raster = m_raster, requested, cancelled]() -> Made {
        try {
            const ExportRaster sized = ImageExporter::resized(raster, requested.width, requested.height);
            const JPEGOptions &options = requested.options;
            const QColor background = QColor::fromRgbF(float(options.red), float(options.green), float(options.blue));
            switch (requested.format) {
            case ExportFormat::jpeg: {
                JPEGResult encoded = ImageExporter::jpeg(sized, options, [cancelled] { return cancelled->load(); });
                return {std::move(encoded.data), std::move(encoded.preview), std::nullopt};
            }
            case ExportFormat::png:
                return {ImageExporter::pngData(sized), sized.image, std::nullopt};
            case ExportFormat::pdf:
                return {ImageExporter::pdfData(sized, background), ImageExporter::flattened(sized, background), std::nullopt};
            }
            throw std::logic_error("unknown export format");
        } catch (const CancellationError &) {
            return {};
        } catch (const ExportError &error) {
            return {std::nullopt, QImage(), QString::fromUtf8(error.what())};
        }
    }));
}

void ExportAsSheet::setMatte(const PaletteColor &matte)
{
    JPEGOptions next = m_settings.options;
    next.red = matte.red;
    next.green = matte.green;
    next.blue = matte.blue;
    // Equal settings ask nothing, as Swift's task id.
    if (next == m_settings.options)
        return;
    m_settings.options = next;
    request();
}

void ExportAsSheet::setScale(double scale)
{
    const double width = m_raster.image.width(), height = m_raster.image.height();
    const double largest = std::min(DocumentLimits::maxSide / std::max(width, height), std::sqrt(double(DocumentLimits::maxSurfacePixels) / (width * height)));
    const double clamped = std::min(largest, std::max(0.01, std::isfinite(scale) ? scale : 1.0));
    const Settings before = m_settings;
    m_settings.width = std::max(1, int(std::lround(width * clamped)));
    m_settings.height = std::max(1, int(std::lround(height * clamped)));
    if (m_settings != before)
        request();
}

void ExportAsSheet::showSize()
{
    m_width->setText(QString::number(m_settings.width));
    m_height->setText(QString::number(m_settings.height));
}

double ExportAsSheet::shownZoom() const
{
    return m_preview->zoom().value_or(JPEGPreview::fitZoom(m_settings.width, m_settings.height, QSizeF(JPEGPreview::frame), devicePixelRatioF()));
}

void ExportAsSheet::zoomBy(int direction)
{
    if (const std::optional<double> next = JPEGPreview::step(shownZoom(), direction))
        m_preview->setZoom(next);
}

void ExportAsSheet::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange)
        synchronize();
}

void ExportAsSheet::synchronize()
{
    const bool ready = m_result && m_result->settings == m_settings && !m_error;
    const bool updating = !ready && !m_error;
    m_preview->setImage(m_result ? m_result->preview : QImage(), m_result ? QSize(m_result->settings.width, m_result->settings.height) : QSize(), updating);
    m_spinner->setVisible(updating);
    // A field keeps its typing while it holds the keys.
    if (!m_width->hasFocus())
        m_width->setText(QString::number(m_settings.width));
    if (!m_height->hasFocus())
        m_height->setText(QString::number(m_settings.height));
    m_scale->setText(QStringLiteral("%1%").arg(std::lround(double(m_settings.width) / m_raster.image.width() * 100)));
    const ExportFormat format = m_settings.format;
    for (QWidget *each : m_jpegRow)
        each->setVisible(format == ExportFormat::jpeg);
    for (QWidget *each : m_pdfRow)
        each->setVisible(format == ExportFormat::pdf);
    for (QWidget *each : m_backgroundRow)
        each->setVisible(format != ExportFormat::png);
    m_percent->setText(QStringLiteral("%1%").arg(std::lround(m_settings.options.quality * 100)));
    // The page: the export's pixels at the document's resolution.
    const double dpi = m_raster.resolution > 0 ? m_raster.resolution : 72;
    m_page->setText(QStringLiteral("%1 × %2 in at %3 DPI").arg(inches(m_settings.width / dpi), inches(m_settings.height / dpi)).arg(std::lround(dpi)));
    m_note->setText(format == ExportFormat::png ? QStringLiteral("Transparency kept · sRGB") : QStringLiteral("sRGB"));
    m_failure->setVisible(m_error.has_value());
    m_failure->setText(m_error.value_or(QString()));
    m_bytes->setVisible(ready);
    m_bytes->setText(ready ? ByteCounts::file(m_result->data.size()) : QString());
    m_updating->setVisible(updating);
    m_export->setEnabled(ready);
    // The zoom buttons name their keys and the zoom.
    const double zoom = shownZoom();
    const QString percent = QStringLiteral("%1%").arg(std::lround(zoom * 100));
    const auto key = [this](size_t index) { return m_zoomKeys[index] ? m_zoomKeys[index]->key().toString(QKeySequence::NativeText) : QString(); };
    m_fit->setEnabled(m_preview->zoom().has_value());
    m_fit->setToolTip(QStringLiteral("Show the whole image (%1)").arg(key(0)));
    m_zoomIn->setEnabled(JPEGPreview::step(zoom, 1).has_value());
    m_zoomIn->setToolTip(QStringLiteral("Zoom in (%1), now %2. At 100% each pixel of the export is one pixel of the screen").arg(key(2), percent));
    m_zoomOut->setEnabled(JPEGPreview::step(zoom, -1).has_value());
    m_zoomOut->setToolTip(QStringLiteral("Zoom out (%1), now %2").arg(key(3), percent));
    const QColor ink = palette().color(QPalette::ButtonText);
    m_zoomIn->setIcon(LayerIcons::pixmap(LayerIcon::plusMagnifyingGlass, 16, ink, devicePixelRatioF()));
    m_zoomOut->setIcon(LayerIcons::pixmap(LayerIcon::minusMagnifyingGlass, 16, ink, devicePixelRatioF()));
}
