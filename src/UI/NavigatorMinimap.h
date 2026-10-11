#pragma once
#include "Document/EditorSession.h"
#include <QTimer>
#include <QWidget>

// Swift's NavigatorMinimap: the document in small, from 300%.
class NavigatorMinimap : public QWidget {
    Q_OBJECT
public:
    static constexpr double zoomShown = 3;
    // The most room the document takes, in points.
    static constexpr QSizeF largest{126, 90};
    // Pixels on the picture's longer side.
    static constexpr int picturePixels = 300;
    // Swift's AppStorage key; off until View › Navigator.
    static constexpr const char *visibleKey = "navigator.visible";
    static bool visibleSetting();
    static void setVisibleSetting(bool visible);
    // Fitted inside `largest`, its proportions kept, rounded.
    static QSizeF sizeFor(QSizeF document);

    // What the picture comes from: layers, not the selection.
    struct RenderKey {
        QUuid documentID;
        QSizeF size;
        std::vector<ImageLayer> layers;
        int revision = 0;
        RenderKey(const std::optional<CanvasDocument> &document, int revision);
        friend bool operator==(const RenderKey &, const RenderKey &) = default;
    };

    explicit NavigatorMinimap(EditorSession &session, QWidget *parent = nullptr);
    void synchronize();
    const QImage &picture() const { return m_picture; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void render();
    void centre(QPointF point);

    EditorSession &m_session;
    QImage m_picture;
    std::optional<RenderKey> m_key;
    // Edits settle first: the picture redraws once, afterwards.
    QTimer *const m_wait;
};
