#include "Document/EditorSession.h"
#include "UI/BrushControls.h"
#include "UI/GradientControls.h"
#include "UI/LassoControls.h"
#include <QPushButton>
#include <QScrollBar>
#include <QtTest>

// Swift 1.4.7: narrow tool bars scroll their controls, never squeeze.
namespace {
std::unique_ptr<EditorSession> withLayer(NavigationTool tool)
{
    auto session = std::make_unique<EditorSession>();
    session->createDocument(40, 20);
    QImage pixels(40, 20, QImage::Format_RGBA8888_Premultiplied);
    pixels.fill(Qt::white);
    session->insert(ImportedImage(pixels, pixels, QStringLiteral("White")));
    session->selectTool(tool);
    return session;
}

// The control's place inside the scroll area's view, or none.
bool inView(QScrollArea &area, QWidget &control)
{
    const QRect place(control.mapTo(area.viewport(), QPoint(0, 0)), control.size());
    return area.viewport()->rect().contains(place);
}

// Too narrow a bar: its controls keep their size, scrolling.
void scrolls(ToolHeaderBar &bar, const char *name, QWidget &last)
{
    bar.resize(440, bar.height());
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    auto &area = *bar.findChild<QScrollArea *>(QString::fromLatin1(name));
    QVERIFY2(bar.minimumSizeHint().width() < 440, name);
    QCOMPARE(bar.height(), 42);
    QVERIFY2(area.widget()->width() >= area.widget()->minimumSizeHint().width(), name);
    QVERIFY2(area.horizontalScrollBar()->maximum() > 0 && !area.horizontalScrollBar()->isVisible(), name);
    QVERIFY2(!inView(area, last) && bar.title->isVisible(), name);
    // A sideways wheel scrolls it; the last control comes in.
    for (int notch = 0; notch < 40 && !inView(area, last); ++notch) {
        const QPoint middle = area.viewport()->rect().center();
        QWheelEvent wheel(middle, area.viewport()->mapToGlobal(middle), QPoint(), QPoint(-120, 0), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(area.viewport(), &wheel);
    }
    QVERIFY2(inView(area, last), name);
    // Wide, nothing scrolls and the content starts at the edge.
    bar.resize(2400, bar.height());
    QTRY_COMPARE(area.horizontalScrollBar()->maximum(), 0);
    QCOMPARE(area.widget()->layout()->itemAt(0)->widget()->mapTo(&area, QPoint(0, 0)).x(), 0);
    // Packed at its own size: the room is after it.
    QVERIFY2(last.mapTo(&area, QPoint(last.width(), 0)).x() < area.width() - 500, name);
}
}

class ToolHeaderScrollTests : public QObject {
    Q_OBJECT
private slots:
    void theBrushBarScrolls();
    void theGradientBarScrollsAndKeepsItsButtons();
    void theSelectionBarScrollsAndKeepsDeselect();
};

void ToolHeaderScrollTests::theBrushBarScrolls()
{
    const auto session = withLayer(NavigationTool::brush);
    BrushControls bar(*session);
    // The mask note scrolls with the controls, after them.
    session->addMask();
    QWidget &note = *bar.findChild<QWidget *>("brushMaskNote");
    scrolls(bar, "brushScroll", note);
    QVERIFY(note.isVisible() && note.parentWidget() == bar.findChild<QScrollArea *>("brushScroll")->widget());
}

void ToolHeaderScrollTests::theGradientBarScrollsAndKeepsItsButtons()
{
    const auto session = withLayer(NavigationTool::gradient);
    GradientControls bar(*session);
    session->beginGradient(QPointF(1, 1));
    session->moveGradient(std::nullopt, QPointF(30, 1));
    scrolls(bar, "gradientScroll", *bar.findChild<QWidget *>("gradientOpacity"));
    // Cancel and Apply stay at the end, outside the scroll.
    bar.resize(440, bar.height());
    auto &apply = *bar.findChild<QPushButton *>("gradientApply");
    QTRY_VERIFY(apply.isVisible() && apply.geometry().right() < 440 && apply.parentWidget() == &bar);
}

void ToolHeaderScrollTests::theSelectionBarScrollsAndKeepsDeselect()
{
    const auto session = withLayer(NavigationTool::marquee);
    LassoControls bar(*session);
    session->selectAll();
    scrolls(bar, "selectionScroll", *bar.findChild<QWidget *>("selectionFeatherAmount"));
    bar.resize(440, bar.height());
    QPushButton *deselect = nullptr;
    for (QPushButton *button : bar.findChildren<QPushButton *>())
        if (button->text() == "Deselect")
            deselect = button;
    QVERIFY(deselect);
    QTRY_VERIFY(deselect->isVisible() && deselect->geometry().right() < 440 && deselect->parentWidget() == &bar);
}

QTEST_MAIN(ToolHeaderScrollTests)
#include "ToolHeaderScrollTests.moc"
