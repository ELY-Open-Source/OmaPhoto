#include "AcceptanceFixtures.h"
#include "Document/LayerAppearance.h"
#include "Document/SmudgeLiquify.h"
#include "UI/NativeLayerList.h"
#include "UI/TransformInspector.h"
#include "UI/TypeControls.h"
#include <QCheckBox>
#include <QLineEdit>

// The acceptance run, part five: 1.4.5's editing.
class AcceptanceEditTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void textPaintingSmearAndMasks();
    void moveKeysAndBlending();
};

namespace {
QImage filled(int width, int height, QColor color)
{
    QImage image = BrushRaster::context(width, height, false);
    image.fill(color);
    return image;
}

QUuid add(App &app, const QImage &image, const QString &name, QPointF centre)
{
    app.session().insert(ImportedImage(image, image, name), centre);
    return app.session().activeLayerID().value();
}

int red(App &app, QUuid id, int x, int y)
{
    for (const ImageLayer &layer : app.session().document().value().layers)
        if (layer.id == id)
            return qRed(layer.asset.value().image().pixel(x, y));
    throw std::runtime_error("no such layer");
}

// The canvas's pixel at a document point, at 100%.
QColor onCanvas(App &app, QPointF point)
{
    const QPointF view = app.session().viewport.viewPoint(point + QPointF(0.5, 0.5), app.session().document().value().size());
    return app.canvas().grab().toImage().pixelColor(int(std::floor(view.x())), int(std::floor(view.y())));
}

bool near(QColor a, QColor b)
{
    return qAbs(a.red() - b.red()) <= 2 && qAbs(a.green() - b.green()) <= 2 && qAbs(a.blue() - b.blue()) <= 2;
}
}

void AcceptanceEditTests::textPaintingSmearAndMasks()
{
    App app;
    app.session().createDocument(1200, 800, true);
    // A face for the selected word, a preview, and Undo.
    app.session().selectTool(NavigationTool::type);
    app.press(QPointF(100, 200));
    app.release(QPointF(100, 200));
    QTRY_VERIFY(app.session().textDraft());
    QTest::keyClicks(&app.canvas(), QStringLiteral("Hello World"));
    for (int letter = 0; letter < 5; ++letter)
        QTest::keyClick(&app.canvas(), Qt::Key_Left, Qt::ShiftModifier);
    auto *faces = app.window.findChild<TypeFontPicker *>(QStringLiteral("typeFont"));
    QVERIFY(faces);
    const QString mono = QStringLiteral("DejaVuSansMono");
    // Pointing at a face previews it; Escape puts it back.
    QTest::mouseMove(faces->view()->viewport(), popupRow(*faces, mono));
    QTRY_COMPARE(app.session().textDraft().value().style.fontNameAt(6), mono);
    QTest::keyClick(faces->view(), Qt::Key_Escape);
    QTRY_VERIFY(app.session().textDraft().value().style.fontNameAt(6) != mono);
    choose(*faces, mono);
    QTRY_COMPARE(app.session().textDraft().value().style.fontRuns.value(), (std::vector<LayerTextFontRun>{{6, 5, mono}}));
    app.window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&app.window));
    app.canvas().setFocus();
    QTRY_VERIFY(app.canvas().hasFocus());
    QTest::keyClick(&app.canvas(), Qt::Key_End);
    QTest::keyClicks(&app.canvas(), QStringLiteral("!!"));
    QTest::keyClick(&app.canvas(), Qt::Key_Z, Qt::ControlModifier);
    QCOMPARE(app.session().textDraft().value().style.content, QStringLiteral("Hello World"));
    QTest::keyClick(&app.canvas(), Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(!app.session().textDraft());
    // A brush paints in a scaled-down layer's own pixels.
    const QUuid half = add(app, filled(800, 800, Qt::black), QStringLiteral("Half"), QPointF(600, 400));
    app.session().beginTransform();
    LayerTransform quarter = app.session().activeLayer().value().transform;
    quarter.origin = QPointF(500, 300);
    quarter.size = QSizeF(200, 200);
    app.session().previewTransform(quarter);
    app.session().commitTransform();
    app.session().selectTool(NavigationTool::brush);
    app.session().setBrushSettings({.diameter = 20, .hardness = 1, .red = 1, .green = 1, .blue = 1});
    app.drag(QPointF(600, 330), QPointF(600, 470));
    app.settle();
    QCOMPARE(app.session().activeLayer().value().asset.value().size(), QSize(800, 800));
    int painted = 0;
    for (int x = 0; x < 800; ++x)
        painted += red(app, half, x, 400) > 128;
    QVERIFY2(painted >= 76 && painted <= 84, qPrintable(QString::number(painted)));
    // A hidden layer says why it cannot paint.
    app.session().toggleLayerVisibility(half);
    app.press(QPointF(560, 400));
    app.release(QPointF(560, 400));
    QVERIFY(app.session().brushError().value().contains(QStringLiteral("is hidden")));
    app.session().setBrushError(std::nullopt);
    app.session().toggleLayerVisibility(half);
    // The Blur brush's Radius sets how far it softens.
    QImage step = filled(400, 200, Qt::black);
    QPainter(&step).fillRect(200, 0, 200, 200, Qt::white);
    const QUuid edge = add(app, step, QStringLiteral("Edge"), QPointF(200, 600));
    const auto softened = [&](const QString &radius) {
        app.session().selectTool(NavigationTool::blur);
        app.session().setBlurMode(BlurToolMode::blur);
        app.session().setBrushSettings({.diameter = 120, .hardness = 1, .opacity = 1});
        // The bar's own Radius field.
        auto *field = app.window.findChild<QLineEdit *>(QStringLiteral("blurRadius"));
        field->setFocus();
        field->selectAll();
        QTest::keyClicks(field, radius);
        QTest::keyClick(field, Qt::Key_Return);
        app.drag(QPointF(200, 520), QPointF(200, 680));
        app.settle();
        int soft = 0;
        for (int x = 100; x < 300; ++x)
            soft += red(app, edge, x, 100) > 10 && red(app, edge, x, 100) < 245;
        app.action("undo").trigger();
        return soft;
    };
    const int narrow = softened(QStringLiteral("2")), wide = softened(QStringLiteral("20"));
    QVERIFY2(narrow < 16 && wide > 40, qPrintable(QString("%1 %2").arg(narrow).arg(wide)));
    // Liquify keeps the stripes sharp as it pushes them.
    QImage stripes = filled(400, 200, Qt::black);
    for (int x = 0; x < 400; x += 20)
        QPainter(&stripes).fillRect(x, 0, 10, 200, Qt::white);
    const QUuid striped = add(app, stripes, QStringLiteral("Stripes"), QPointF(800, 600));
    app.session().selectTool(NavigationTool::blur);
    app.session().setBlurMode(BlurToolMode::liquify);
    app.session().setBrushSettings({.diameter = 80, .hardness = 0.5, .opacity = 0.7});
    app.drag(QPointF(720, 600), QPointF(840, 600));
    app.settle();
    int moved = 0, grey = 0;
    for (int x = 0; x < 400; ++x) {
        const int value = red(app, striped, x, 100);
        moved += value != qRed(stripes.pixel(x, 100));
        grey += value > 40 && value < 215;
    }
    QVERIFY2(moved > 20 && grey * 5 < moved, qPrintable(QString("%1 %2").arg(moved).arg(grey)));
    // Smudge leaves one fading trail, no copies along it.
    QImage block = filled(400, 200, Qt::white);
    QPainter(&block).fillRect(0, 0, 100, 200, Qt::red);
    const QUuid smeared = add(app, block, QStringLiteral("Block"), QPointF(200, 200));
    app.session().setBlurMode(BlurToolMode::smudge);
    app.session().setBrushSettings({.diameter = 60, .hardness = 0.5, .opacity = 0.8});
    app.drag(QPointF(50, 200), QPointF(300, 200));
    app.settle();
    const auto green = [&](int x) {
        for (const ImageLayer &layer : app.session().document().value().layers)
            if (layer.id == smeared)
                return qGreen(layer.asset.value().image().pixel(x, 100));
        throw std::runtime_error("no such layer");
    };
    QVERIFY2(green(110) < 200, qPrintable(QString::number(green(110))));
    for (int x = 110; x < 300; x += 10)
        QVERIFY2(green(x + 10) >= green(x) - 3, qPrintable(QString("%1: %2 then %3").arg(x).arg(green(x)).arg(green(x + 10))));
    app.session().selectLayer(striped);
    // The mask button reveals; Alt hides; Alt-click shows alone.
    app.session().selectTool(NavigationTool::marquee);
    app.drag(QPointF(650, 520), QPointF(750, 680));
    auto *mask = app.window.findChild<QToolButton *>(QStringLiteral("addLayerMask"));
    QTest::mouseClick(mask, Qt::LeftButton);
    QImage shown = app.session().activeLayer().value().mask.value().asset.image();
    QCOMPARE(qGray(shown.pixel(100, 100)), 255);
    QCOMPARE(qGray(shown.pixel(5, 100)), 0);
    app.action("undo").trigger();
    QTest::mouseClick(mask, Qt::LeftButton, Qt::AltModifier);
    shown = app.session().activeLayer().value().mask.value().asset.image();
    QVERIFY(qGray(shown.pixel(100, 100)) == 0 && qGray(shown.pixel(5, 100)) == 255);
    LayerThumbnailButton *thumbnail = nullptr;
    for (LayerCell *cell : app.window.findChild<NativeLayerList *>()->cells())
        if (cell->layerID() == striped)
            thumbnail = cell->findChild<LayerThumbnailButton *>("maskThumbnail");
    QVERIFY(thumbnail);
    QTest::mouseClick(thumbnail, Qt::LeftButton, Qt::AltModifier, thumbnail->rect().center());
    QCOMPARE(app.session().maskAloneLayer().value().id, striped);
    QTRY_VERIFY(app.window.findChild<QWidget *>(QStringLiteral("maskAloneBadge"))->isVisible());
    // The canvas shows the mask: black where hidden, white past.
    QTRY_COMPARE(onCanvas(app, QPointF(700, 600)), QColor(Qt::black));
    QCOMPARE(onCanvas(app, QPointF(620, 600)), QColor(Qt::white));
    QCOMPARE(onCanvas(app, QPointF(50, 150)), QColor(Qt::white));
    app.shot(QStringLiteral("18-mask-alone"));
}

void AcceptanceEditTests::moveKeysAndBlending()
{
    App app;
    QVERIFY(scene(1200, 800).save(app.path("photo.png")));
    app.session().createDocument(1200, 800);
    app.importFile(app.path("photo.png"));
    // Held Ctrl flips Auto Select in the bar.
    app.session().selectTool(NavigationTool::move);
    auto *autoSelect = app.window.findChild<QCheckBox *>(QStringLiteral("transformAutoSelect"));
    QVERIFY(autoSelect);
    const bool before = autoSelect->isChecked();
    QTest::keyPress(&app.canvas(), Qt::Key_Control, Qt::ControlModifier);
    QCOMPARE(autoSelect->isChecked(), !before);
    QTest::keyRelease(&app.canvas(), Qt::Key_Control, Qt::NoModifier);
    QCOMPARE(autoSelect->isChecked(), before);
    // A typed width applies as one step on leaving.
    auto *width = app.window.findChild<QLineEdit *>(QStringLiteral("transformW"));
    QVERIFY(width);
    const int steps = app.session().history.undoCount();
    width->setFocus();
    QTRY_VERIFY(width->hasFocus());
    width->selectAll();
    QTest::keyClicks(width, QStringLiteral("600"));
    app.canvas().setFocus();
    QTRY_COMPARE(app.session().activeLayer().value().transform.size.width(), 600.0);
    QCOMPARE(app.session().history.undoCount(), steps + 1);
    app.action("undo").trigger();
    // Soft Light and a Linear Dodge stack show as exported.
    app.session().zoom(1);
    const QUuid grey = add(app, filled(1200, 800, QColor(77, 77, 77)), QStringLiteral("Grey"), QPointF(600, 400));
    app.session().setLayerBlendMode(LayerBlendMode::softLight);
    const QList<QPointF> points{QPointF(600, 400), QPointF(300, 100), QPointF(900, 700)};
    for (const QPointF point : points)
        QTRY_VERIFY2(near(onCanvas(app, point), app.shown(int(point.x()), int(point.y()))), qPrintable(onCanvas(app, point).name()));
    add(app, filled(1200, 800, QColor(0, 0, 160, 160)), QStringLiteral("Clipped"), QPointF(600, 400));
    app.session().toggleClippingMask(app.session().activeLayerID().value());
    app.session().selectLayer(grey);
    app.session().setLayerBlendMode(LayerBlendMode::linearDodge);
    for (const QPointF point : points)
        QTRY_VERIFY2(near(onCanvas(app, point), app.shown(int(point.x()), int(point.y()))), qPrintable(onCanvas(app, point).name()));
}

QTEST_MAIN(AcceptanceEditTests)
#include "AcceptanceEditTests.moc"
