#include "UI/NewCanvasSheet.h"
#include <QtTest>

// Swift's NewCanvasUnitTests: units, DPI and the first layer.
class NewCanvasUnitTests : public QObject {
    Q_OBJECT
private slots:
    void printSizesBecomePixelsAtTheDPI();
    void unitsRoundTripAndCycle();
    void newProjectKeepsItsResolution();
    void backgroundFillsTheFirstLayer();
};

void NewCanvasUnitTests::printSizesBecomePixelsAtTheDPI()
{
    // US Letter at 300 DPI is 2550 × 3300.
    QCOMPARE(pixels(NewCanvasUnit::inches, "8.5", 300), std::optional(2550));
    QCOMPARE(pixels(NewCanvasUnit::inches, "11", 300), std::optional(3300));
    QCOMPARE(pixels(NewCanvasUnit::millimeters, "210", 300), std::optional(2480));
    QCOMPARE(pixels(NewCanvasUnit::centimeters, "2,54", 72), std::optional(72));
    QCOMPARE(pixels(NewCanvasUnit::pixels, "1920", 300), std::optional(1920));
    QCOMPARE(pixels(NewCanvasUnit::pixels, "19.5", 72), std::nullopt);
    QCOMPARE(pixels(NewCanvasUnit::inches, "200", 300), std::nullopt);
    QCOMPARE(pixels(NewCanvasUnit::inches, "0", 300), std::nullopt);
    // Rounded to the nearest pixel, halves away from zero.
    QCOMPARE(pixels(NewCanvasUnit::inches, "0.005", 300), std::optional(2));
    QCOMPARE(pixels(NewCanvasUnit::inches, "0.0049", 300), std::optional(1));
    QCOMPARE(pixels(NewCanvasUnit::inches, "0.0016", 300), std::nullopt);
    QCOMPARE(pixels(NewCanvasUnit::inches, " 1\t", 72), std::optional(72));
    QCOMPARE(pixels(NewCanvasUnit::inches, "\t\u00a01", 72), std::optional(72));
    // Foundation's .whitespaces holds U+200B; a line break is none.
    QCOMPARE(pixels(NewCanvasUnit::inches, "\u200b1\u200b", 72), std::optional(72));
    QCOMPARE(pixels(NewCanvasUnit::inches, "1\n", 72), std::nullopt);
    // Infinity and NaN fail the range and the whole number.
    QCOMPARE(pixels(NewCanvasUnit::inches, "inf", 72), std::nullopt);
    QCOMPARE(pixels(NewCanvasUnit::pixels, "nan", 72), std::nullopt);
    QCOMPARE(pixels(NewCanvasUnit::pixels, "30000", 72), std::optional(30000));
    QCOMPARE(pixels(NewCanvasUnit::pixels, "30001", 72), std::nullopt);
}

void NewCanvasUnitTests::unitsRoundTripAndCycle()
{
    QCOMPARE(text(NewCanvasUnit::inches, 1920, 72), QString("26.67"));
    QCOMPARE(text(NewCanvasUnit::inches, 2550, 300), QString("8.5"));
    QCOMPARE(text(NewCanvasUnit::inches, 3000, 300), QString("10"));
    QCOMPARE(text(NewCanvasUnit::millimeters, 2480, 300), QString("209.97"));
    QCOMPARE(text(NewCanvasUnit::centimeters, 30000, 72), QString("1058.33"));
    // Exact halves go to the even digit, as Foundation rounds.
    QCOMPARE(text(NewCanvasUnit::inches, 153, 72), QString("2.12"));
    QCOMPARE(text(NewCanvasUnit::inches, 9, 72), QString("0.12"));
    QCOMPARE(text(NewCanvasUnit::inches, 27, 72), QString("0.38"));
    QCOMPARE(text(NewCanvasUnit::pixels, 30000, 72), QString("30000"));
    for (NewCanvasUnit unit : {NewCanvasUnit::inches, NewCanvasUnit::centimeters, NewCanvasUnit::millimeters})
        QCOMPARE(pixels(unit, text(unit, 2550, 300), 300), std::optional(2550));
    QCOMPARE(next(NewCanvasUnit::pixels), NewCanvasUnit::inches);
    QCOMPARE(next(NewCanvasUnit::inches), NewCanvasUnit::centimeters);
    QCOMPARE(next(NewCanvasUnit::centimeters), NewCanvasUnit::millimeters);
    QCOMPARE(next(NewCanvasUnit::millimeters), NewCanvasUnit::pixels);
    QCOMPARE(rawValue(NewCanvasUnit::millimeters), QString("mm"));
    QCOMPARE(name(NewCanvasUnit::centimeters), QString("Centimeters"));
}

void NewCanvasUnitTests::newProjectKeepsItsResolution()
{
    EditorSession session;
    session.createNewProject(2550, 3300, 300);
    QCOMPARE(session.document().value().resolution, 300.0);
    QCOMPARE(session.projectSnapshot().value().manifest.resolution, std::optional(300.0));
    // A resolution out of 1 to 9,600 is 72.
    for (double odd : {0.5, 9601.0, qQNaN()}) {
        session.createNewProject(10, 10, odd);
        QCOMPARE(session.document().value().resolution, 72.0);
    }
    session.createNewProject(10, 10, 9600);
    QCOMPARE(session.document().value().resolution, 9600.0);
    session.createNewProject(10, 10, 1);
    QCOMPARE(session.document().value().resolution, 1.0);
}

void NewCanvasUnitTests::backgroundFillsTheFirstLayer()
{
    QCOMPARE(next(NewCanvasBackground::transparent), NewCanvasBackground::white);
    QCOMPARE(next(NewCanvasBackground::white), NewCanvasBackground::black);
    QCOMPARE(next(NewCanvasBackground::black), NewCanvasBackground::transparent);
    QCOMPARE(title(NewCanvasBackground::white), QString("White canvas"));
    for (const auto &[background, value] : {std::pair(NewCanvasBackground::white, 255), std::pair(NewCanvasBackground::black, 0)}) {
        EditorSession session;
        session.createNewProject(40, 30, 72, color(background));
        const ImageLayer layer = session.document().value().layers.front();
        QCOMPARE(layer.name, QString("Background"));
        QCOMPARE(session.activeLayerID(), std::optional(layer.id));
        const QImage image = layer.asset.value().image();
        QCOMPARE(image.size(), QSize(40, 30));
        QCOMPARE(image.format(), QImage::Format_RGBA8888_Premultiplied);
        QCOMPARE(image.pixelColor(0, 0), QColor(value, value, value));
        QCOMPARE(image.pixelColor(39, 29), QColor(value, value, value));
        QCOMPARE(layer.asset.value().thumbnail.pixelColor(0, 0), QColor(value, value, value));
    }
    // A thumbnail, not the whole picture, for the panel.
    EditorSession large;
    large.createNewProject(400, 300, 72, color(NewCanvasBackground::black));
    QCOMPARE(large.document().value().layers.front().asset.value().thumbnail.size(), QSize(96, 72));
    EditorSession session;
    session.createNewProject(40, 30, 72, color(NewCanvasBackground::transparent));
    QCOMPARE(session.document().value().layers.front().name, QString("Layer 1"));
    QVERIFY(!session.document().value().layers.front().asset);
}

QTEST_MAIN(NewCanvasUnitTests)
#include "NewCanvasUnitTests.moc"
