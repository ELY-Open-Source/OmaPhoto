#pragma once
#include "Document/EditorSession.h"
#include <QAbstractButton>
#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>
#include <functional>
#include <vector>

// New Canvas units; print sizes become pixels at the DPI.
enum class NewCanvasUnit { pixels, inches, centimeters, millimeters };
QString rawValue(NewCanvasUnit unit);
// The unit written out, for its pill.
QString name(NewCanvasUnit unit);
NewCanvasUnit next(NewCanvasUnit unit);
// Whole pixels for a typed size, or none.
std::optional<int> pixels(NewCanvasUnit unit, const QString &text, double resolution);
// Whole pixels, or print sizes to two decimals at most.
QString text(NewCanvasUnit unit, int pixels, double resolution);

// What a new canvas starts as: clear, white or black.
enum class NewCanvasBackground { transparent, white, black };
QString title(NewCanvasBackground background);
NewCanvasBackground next(NewCanvasBackground background);
std::optional<QColor> color(NewCanvasBackground background);

// New Canvas sizes in pixels, upright as held.
struct CanvasPreset {
    QString title;
    int width;
    int height;
    // Resolutions, Apple screens, then social formats; the menu divides them.
    static const std::vector<std::vector<CanvasPreset>> groups;
    static const std::vector<CanvasPreset> all;
};

// The welcome: a new canvas, a project or an image.
class NewCanvasSheet : public QWidget {
    Q_OBJECT
public:
    using Create = std::function<void(int width, int height, double resolution, std::optional<QColor> background)>;
    NewCanvasSheet(EditorSession &session, Create onCreate, std::function<void()> onOpen, QWidget *parent = nullptr);

    // A copied picture's size, read from its header alone.
    static std::optional<QSize> clipboardDimensions(const QMimeData *clipboard);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void synchronize();
    void validate();
    void create();
    // Checks the preset the fields match, else Custom.
    void checkPreset();
    std::optional<int> pixelWidth() const;
    std::optional<int> pixelHeight() const;
    // The same canvas written in another unit.
    void switchUnit(NewCanvasUnit unit);
    // A size in pixels, written in the unit in use.
    void setPixels(int width, int height);

    EditorSession &m_session;
    const Create m_onCreate;
    QLineEdit *const m_width;
    QLineEdit *const m_height;
    QLabel *const m_note;
    QPushButton *const m_create;
    QToolButton *const m_presets;
    QAbstractButton *const m_backgroundPill;
    QAbstractButton *const m_unitPill;
    QAbstractButton *const m_resolutionPill;
    std::vector<QLabel *> m_unitLabels;
    // Always pixels, 72 DPI and clear at first.
    NewCanvasUnit m_unit = NewCanvasUnit::pixels;
    double m_resolution = 72;
    NewCanvasBackground m_background = NewCanvasBackground::transparent;
    bool m_suggestedClipboardSize = false;
};
