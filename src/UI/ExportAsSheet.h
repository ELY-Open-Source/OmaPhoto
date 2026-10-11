#pragma once
#include "Document/ColorPalette.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectController.h"
#include <QAbstractScrollArea>
#include <QWidget>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QButtonGroup;
class QLabel;
class QProgressBar;
class QToolButton;
class PickerField;
class QShortcut;
class QPushButton;
class QSlider;
class QTimer;
class DialogColorSwatch;
class EditorSession;

// Swift's JPEGPreview: the export, fitted or zoomed, dragged.
class JPEGPreview : public QAbstractScrollArea {
    Q_OBJECT
public:
    static constexpr QSize frame{560, 330};
    static constexpr std::array<double, 6> steps{0.25, 0.5, 1, 2, 4, 8};
    // The zoom at which the whole image fits `frame`.
    static double fitZoom(int width, int height, QSizeF frame, double displayScale);
    // The next step past `zoom`: 1 in, −1 out.
    static std::optional<double> step(double zoom, int direction);

    JPEGPreview(std::function<void()> zoomChanged, QWidget *parent);
    // `pixels`: the export's size, which the image may undercut.
    void setImage(const QImage &image, QSize pixels, bool updating);
    // None fits; 1 is a screen pixel a pixel.
    std::optional<double> zoom() const { return m_zoom; }
    void setZoom(std::optional<double> zoom);
    double shownZoom() const;
    void zoomBy(int direction);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    static QPixmap checkerboard(double ratio);
    QSizeF shownSize(double zoom) const;
    void scrollTo(QPointF fraction);
    void updateRanges();
    void showCursor(bool grabbing);

    QSize m_pixels;
    // Each change of zoom, for the sheet's buttons.
    const std::function<void()> m_zoomChanged;
    QImage m_image;
    // The image shrunk to the size last drawn.
    QImage m_reduced;
    bool m_updating = false;
    std::optional<double> m_zoom;
    // A drag's press and the scroll it began from.
    std::optional<std::pair<QPoint, QPoint>> m_drag;
};

// Swift's ExportAsSheet: PNG, JPEG or PDF, sized and previewed.
class ExportAsSheet : public QWidget {
    Q_OBJECT
public:
    // The last export's JPEG quality, where the next one starts.
    static const QString qualityKey;
    // What Export… hands over: the bytes and their format.
    struct Chosen {
        QByteArray data;
        ExportFormat format;
    };
    // What is written; each change makes a new preview.
    struct Settings {
        ExportFormat format;
        JPEGOptions options;
        int width;
        int height;
        bool operator==(const Settings &) const = default;
    };

    ExportAsSheet(ExportRaster raster, EditorSession &session, ExportFormat format, std::function<void(std::optional<Chosen>)> finish,
                  QWidget *parent = nullptr);
    ~ExportAsSheet() override;

    const Settings &settings() const { return m_settings; }

protected:
    // The zoom glyphs follow the theme.
    void changeEvent(QEvent *event) override;

private:
    struct Encoded {
        Settings settings;
        QByteArray data;
        QImage preview;
    };
    void request();
    void encode();
    void setMatte(const PaletteColor &matte);
    // Both sides follow one, the proportions kept, within the limits.
    void setScale(double scale);
    // Both fields show the size, typing or not.
    void showSize();
    double shownZoom() const;
    void zoomBy(int direction);
    void synchronize();
    // The View menu's zoom keys, as remapped, zoom the preview.
    void bindZoomKeys();

    const ExportRaster m_raster;
    EditorSession &m_session;
    const std::function<void(std::optional<Chosen>)> m_finish;
    Settings m_settings;
    std::optional<Encoded> m_result;
    std::optional<QString> m_error;
    // The newest request's flag, Swift's task cancellation.
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QTimer *const m_wait;
    JPEGPreview *const m_preview;
    QProgressBar *const m_spinner;
    QButtonGroup *const m_formats;
    PickerField *const m_width;
    PickerField *const m_height;
    QToolButton *const m_scale;
    QSlider *const m_quality;
    QLabel *const m_percent;
    QLabel *const m_page;
    DialogColorSwatch *const m_matteSwatch;
    // Rows shown by format: Quality for JPEG, Page for PDF.
    std::vector<QWidget *> m_jpegRow, m_pdfRow, m_backgroundRow;
    QLabel *const m_note;
    QLabel *const m_failure;
    QLabel *const m_bytes;
    QLabel *const m_updating;
    QPushButton *const m_export;
    QPushButton *const m_fit;
    QPushButton *const m_zoomIn;
    QPushButton *const m_zoomOut;
    // Fit, Actual Pixels, Zoom In and Zoom Out.
    std::array<QShortcut *, 4> m_zoomKeys{};
};
