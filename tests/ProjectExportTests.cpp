#include "DialogDesk.h"
#include "MenuFixtures.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectController.h"
#include "UI/ExportAsSheet.h"
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QStandardPaths>
#include <QtTest>

// The PNG and JPEG exports: panels, the sheet, files, alerts.
class ProjectExportTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void aPNGGoesWhereThePanelSays();
    void aNameTakesTheTypesSuffix();
    void aCancelledExportWritesNothing();
    void aFailedWriteIsExplained();
    void aJPEGPassesThroughItsSheet();
    void aJPEGNeedsAWindowAndADocument();
    void theFileMenuOffersBothExports();
    void exportAsWritesEachFormatAndRemembersTheLast();
    void theMenuOpensExportAsOnTheLastFormat();
};

namespace {
struct Desk {
    EditorSession session;
    ProjectController controller{session};
    QWidget window;
    DialogDesk dialogs;
    bool done = false;
    Desk()
    {
        session.createDocument(40, 20);
        QImage red(10, 10, QImage::Format_RGBA8888_Premultiplied);
        red.fill(Qt::red);
        session.insert(ImportedImage(red, QImage(), "Red"), QPointF(25, 10));
        window.resize(600, 400);
        window.show();
        controller.window = &window;
        dialogs.note = [this] { return QStringLiteral("busy %1").arg(session.isProjectBusy()); };
    }
    std::function<void()> finished()
    {
        done = false;
        return [this] { done = true; };
    }
    static QString path(const QString &name) { return QDir::current().absoluteFilePath(name); }
    // The shown sheet; a closed one may await deletion.
    ExportAsSheet *sheet()
    {
        for (ExportAsSheet *shown : window.findChildren<ExportAsSheet *>())
            if (shown->isVisible())
                return shown;
        return nullptr;
    }
};

// The shown panel's type, which the desk does not record.
QString panelFilters()
{
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *panel = qobject_cast<QFileDialog *>(widget); panel && panel->isVisible())
            return panel->nameFilters().join(QLatin1Char(';'));
    return QString();
}

QByteArray contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

void ProjectExportTests::initTestCase()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings().clear();
}

void ProjectExportTests::aPNGGoesWhereThePanelSays()
{
    Desk desk;
    const int steps = desk.session.history.undoCount();
    // Named in full: panels open where the last went.
    desk.dialogs.replies = {Desk::path("shot")};
    desk.dialogs.note = panelFilters;
    desk.controller.exportPNG(desk.finished());
    QVERIFY(desk.session.isProjectBusy());
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen, QStringList{"panel|Export PNG|save|file|Untitled.png|png|PNG image (*.png)"});
    QVERIFY(!desk.session.isProjectBusy());
    const QByteArray written = contents(Desk::path("shot.png"));
    QCOMPARE(written, ImageExporter::pngData(desk.session.projectSnapshot().value()));
    const QImage image = QImage::fromData(written, "png");
    QCOMPARE(image.size(), QSize(40, 20));
    QCOMPARE(image.pixelColor(25, 10), QColor(255, 0, 0));
    QCOMPARE(image.pixelColor(5, 5).alpha(), 0);
    QCOMPARE(desk.session.history.undoCount(), steps);
}

void ProjectExportTests::aNameTakesTheTypesSuffix()
{
    Desk desk;
    desk.session.setProjectPath(Desk::path("Poster.comp"));
    // A name with another suffix gains the type's own.
    desk.dialogs.replies = {Desk::path("poster.jpg")};
    desk.controller.exportPNG(desk.finished());
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen, QStringList{"panel|Export PNG|save|file|Poster.png|png|busy 1"});
    QVERIFY(QFileInfo::exists(Desk::path("poster.jpg.png")) && !QFileInfo::exists(Desk::path("poster.jpg")));
    // One in any case keeps it.
    desk.dialogs.seen.clear();
    desk.dialogs.replies = {Desk::path("LOUD.PNG")};
    desk.controller.exportPNG(desk.finished());
    QTRY_VERIFY(desk.done);
    QVERIFY(QFileInfo::exists(Desk::path("LOUD.PNG")) && !QFileInfo::exists(Desk::path("LOUD.PNG.png")));
    // Anything at the completed name stays, even a dangling link.
    QVERIFY(QFile::link(Desk::path("nowhere"), Desk::path("taken.jpg.png")));
    desk.dialogs.seen.clear();
    desk.dialogs.replies = {Desk::path("taken.jpg"), "OK"};
    desk.controller.exportPNG(desk.finished());
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen.value(1), QString("alert|2|Couldn’t export PNG|“taken.jpg.png” already exists. Choose another name.|OK|busy 1"));
    QVERIFY(QFileInfo(Desk::path("taken.jpg.png")).isSymLink() && !QFileInfo::exists(Desk::path("nowhere")));
    QVERIFY(!desk.session.isProjectBusy());
}

void ProjectExportTests::aCancelledExportWritesNothing()
{
    Desk desk;
    // Without a window the panel stands alone.
    desk.controller.window = nullptr;
    desk.dialogs.replies = {"<cancel>"};
    desk.controller.exportPNG(desk.finished());
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen, QStringList{"panel|Export PNG|save|file|Untitled.png|png|busy 1"});
    QVERIFY(!desk.session.isProjectBusy());
    QCOMPARE(QDir::current().entryList(QDir::Files), QStringList());
    // A JPEG cancelled at its sheet opens no panel.
    desk.controller.window = &desk.window;
    desk.controller.exportAs(ExportFormat::jpeg, desk.finished());
    QTRY_VERIFY(desk.sheet());
    QTest::keyClick(desk.sheet(), Qt::Key_Escape);
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen.size(), 1);
    QVERIFY(!desk.session.isProjectBusy());
    QVERIFY(!QSettings().contains(ExportAsSheet::qualityKey));
    // Cancelled at its panel, nothing is written.
    desk.dialogs.replies = {"<cancel>"};
    desk.controller.exportAs(ExportFormat::jpeg, desk.finished());
    QTRY_VERIFY(desk.sheet() && desk.sheet()->findChild<QPushButton *>("exportExport")->isEnabled());
    desk.sheet()->findChild<QPushButton *>("exportExport")->click();
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen.value(1), QString("panel|Export JPEG|save|file|Untitled.jpeg|jpeg|busy 1"));
    QCOMPARE(QDir::current().entryList(QDir::Files), QStringList());
    QVERIFY(!desk.session.isProjectBusy());
}

void ProjectExportTests::aFailedWriteIsExplained()
{
    Desk desk;
    QVERIFY(QDir::current().mkdir("locked"));
    QVERIFY(QFile::setPermissions(Desk::path("locked"), QFile::ReadOwner | QFile::ExeOwner));
    desk.dialogs.replies = {Desk::path("locked/shot.png"), "OK"};
    desk.controller.exportPNG(desk.finished());
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen.size(), 2);
    QVERIFY(desk.dialogs.seen.value(1).startsWith("alert|2|Couldn’t export PNG|"));
    QVERIFY(desk.dialogs.seen.value(1).endsWith("|OK|busy 1"));
    QVERIFY(!QFileInfo::exists(Desk::path("locked/shot.png")));
    QVERIFY(!desk.session.isProjectBusy());
    QVERIFY(QFile::setPermissions(Desk::path("locked"), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
}

void ProjectExportTests::aJPEGPassesThroughItsSheet()
{
    Desk desk;
    QSettings().remove(ExportAsSheet::qualityKey);
    desk.dialogs.replies = {Desk::path("photo")};
    desk.dialogs.note = panelFilters;
    desk.controller.exportAs(ExportFormat::jpeg, desk.finished());
    QVERIFY(desk.session.isProjectBusy());
    // The render runs aside; then the sheet holds the window.
    QTRY_VERIFY(desk.sheet());
    auto *dialog = qobject_cast<QDialog *>(desk.sheet()->parentWidget());
    QVERIFY(dialog && dialog->isVisible() && dialog->windowModality() == Qt::WindowModal);
    QCOMPARE(dialog->windowTitle(), QString("Export As"));
    QCOMPARE(dialog->minimumSize(), dialog->maximumSize());
    auto *exporting = desk.sheet()->findChild<QPushButton *>("exportExport");
    desk.sheet()->findChild<QSlider *>("exportQuality")->setValue(50);
    QTRY_VERIFY(exporting->isEnabled());
    exporting->click();
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen, QStringList{"panel|Export JPEG|save|file|Untitled.jpeg|jpeg|JPEG image (*.jpeg *.jpg *.jpe)"});
    const ExportRaster raster = ImageExporter::render(desk.session.projectSnapshot().value());
    QCOMPARE(contents(Desk::path("photo.jpeg")), ImageExporter::jpeg(raster, {.quality = 0.5}).data);
    QCOMPARE(QSettings().value(ExportAsSheet::qualityKey).toDouble(), 0.5);
    QVERIFY(!desk.session.isProjectBusy());
    // Every JPEG suffix is one; another gains the preferred.
    for (const QString &name : {QStringLiteral("a.jpg"), QStringLiteral("b.JPE"), QStringLiteral("c.png")}) {
        desk.dialogs.replies = {Desk::path(name)};
        desk.controller.exportAs(ExportFormat::jpeg, desk.finished());
        QTRY_VERIFY(desk.sheet() && desk.sheet()->findChild<QPushButton *>("exportExport")->isEnabled());
        desk.sheet()->findChild<QPushButton *>("exportExport")->click();
        QTRY_VERIFY(desk.done);
    }
    QVERIFY(QFileInfo::exists(Desk::path("a.jpg")) && QFileInfo::exists(Desk::path("b.JPE")) && QFileInfo::exists(Desk::path("c.png.jpeg")));
}

void ProjectExportTests::aJPEGNeedsAWindowAndADocument()
{
    Desk desk;
    desk.controller.window = nullptr;
    desk.controller.exportAs(ExportFormat::jpeg, desk.finished());
    QVERIFY(!desk.session.isProjectBusy());
    QTRY_VERIFY(desk.done);
    EditorSession empty;
    ProjectController idle(empty);
    idle.window = &desk.window;
    bool refused = false;
    idle.exportAs(ExportFormat::jpeg, [&refused] { refused = true; });
    idle.exportPNG([&refused] { refused = true; });
    QTRY_VERIFY(refused);
    QVERIFY(desk.dialogs.seen.isEmpty() && !empty.isProjectBusy());
    // A busy project starts nothing either.
    desk.controller.window = &desk.window;
    desk.session.setIsProjectBusy(true);
    desk.controller.exportPNG(desk.finished());
    QTRY_VERIFY(desk.done);
    QVERIFY(desk.dialogs.seen.isEmpty());
    desk.session.setIsProjectBusy(false);
}

// Enabled with a document while the controller can start.
void ProjectExportTests::theFileMenuOffersBothExports()
{
    QTemporaryDir folder;
    Bar bar;
    const auto all = [&](bool enabled) {
        return bar.action("exportPNG").isEnabled() == enabled && bar.action("exportJPEG").isEnabled() == enabled && bar.action("exportAs").isEnabled() == enabled;
    };
    QVERIFY(all(false));
    bar.session().createDocument(8, 8);
    QVERIFY(all(true));
    bar.session().setIsProjectBusy(true);
    QVERIFY(all(false));
    bar.session().setIsProjectBusy(false);
    DialogDesk desk;
    desk.replies = {folder.filePath("Shot.png")};
    bar.action("exportPNG").trigger();
    QTRY_VERIFY(QFileInfo::exists(folder.filePath("Shot.png")) && !bar.session().isProjectBusy());
    bar.window.show();
    bar.action("exportJPEG").trigger();
    QTRY_VERIFY(bar.window.findChild<ExportAsSheet *>());
    // The menus' zoom keys reach the preview, not the canvas.
    ExportAsSheet &sheet = *bar.window.findChild<ExportAsSheet *>();
    QTRY_COMPARE(QApplication::activeWindow(), sheet.window());
    const double zoom = bar.session().viewport.zoom();
    QTest::keyClick(&sheet, Qt::Key_1, Qt::ControlModifier);
    QCOMPARE(sheet.findChild<JPEGPreview *>()->zoom(), std::optional(1.0));
    QCOMPARE(bar.session().viewport.zoom(), zoom);
    // In the dialog only Export takes Return.
    for (const char *name : {"exportFit", "exportZoomIn", "exportZoomOut"})
        QVERIFY(!sheet.findChild<QPushButton *>(QString::fromLatin1(name))->autoDefault());
    QTest::keyClick(&sheet, Qt::Key_Escape);
    QTRY_VERIFY(!bar.session().isProjectBusy());
    QCOMPARE(desk.seen.size(), 1);
}

void ProjectExportTests::exportAsWritesEachFormatAndRemembersTheLast()
{
    Desk desk;
    const auto checked = [&desk] {
        for (const ExportFormat format : allExportFormats)
            if (desk.sheet()->findChild<QAbstractButton *>(QStringLiteral("exportFormat") + rawValue(format))->isChecked())
                return format;
        throw std::runtime_error("no format checked");
    };
    // First, PNG; a PDF written then starts the next.
    desk.dialogs.replies = {Desk::path("page")};
    desk.dialogs.note = panelFilters;
    desk.controller.exportAs(std::nullopt, desk.finished());
    QTRY_VERIFY(desk.sheet());
    QCOMPARE(checked(), ExportFormat::png);
    desk.sheet()->findChild<QAbstractButton *>("exportFormatPDF")->click();
    auto *exporting = desk.sheet()->findChild<QPushButton *>("exportExport");
    QTRY_VERIFY(exporting->isEnabled());
    exporting->click();
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen, QStringList{"panel|Export PDF|save|file|Untitled.pdf|pdf|PDF document (*.pdf)"});
    const ExportRaster raster = ImageExporter::render(desk.session.projectSnapshot().value());
    QCOMPARE(contents(Desk::path("page.pdf")), ImageExporter::pdfData(raster, QColor(Qt::white)));
    QVERIFY(!desk.session.isProjectBusy());
    desk.controller.exportAs(std::nullopt, desk.finished());
    QTRY_VERIFY(desk.sheet());
    QCOMPARE(checked(), ExportFormat::pdf);
    QTest::keyClick(desk.sheet(), Qt::Key_Escape);
    QTRY_VERIFY(desk.done);
    // Export JPEG starts on JPEG, whatever came last.
    desk.controller.exportAs(ExportFormat::jpeg, desk.finished());
    QTRY_VERIFY(desk.sheet());
    QCOMPARE(checked(), ExportFormat::jpeg);
    QTest::keyClick(desk.sheet(), Qt::Key_Escape);
    QTRY_VERIFY(desk.done);
    // A PNG through Export As, scaled to half.
    desk.dialogs.seen.clear();
    desk.dialogs.replies = {Desk::path("half")};
    desk.controller.exportAs(ExportFormat::png, desk.finished());
    QTRY_VERIFY(desk.sheet());
    auto *width = desk.sheet()->findChild<QLineEdit *>("exportWidth");
    width->setFocus();
    width->selectAll();
    QTest::keyClicks(width, "20");
    QTest::keyClick(width, Qt::Key_Return);
    QTRY_VERIFY(desk.sheet()->findChild<QPushButton *>("exportExport")->isEnabled());
    desk.sheet()->findChild<QPushButton *>("exportExport")->click();
    QTRY_VERIFY(desk.done);
    QCOMPARE(desk.dialogs.seen, QStringList{"panel|Export PNG|save|file|Untitled.png|png|PNG image (*.png)"});
    QCOMPARE(contents(Desk::path("half.png")), ImageExporter::pngData(ImageExporter::resized(raster, 20, 10)));
}

void ProjectExportTests::theMenuOpensExportAsOnTheLastFormat()
{
    QTemporaryDir folder;
    Bar bar;
    bar.session().createDocument(8, 8);
    bar.window.show();
    DialogDesk desk;
    const auto shown = [&bar]() -> ExportAsSheet & {
        if (!QTest::qWaitFor([&bar] { return bar.window.findChild<ExportAsSheet *>() && bar.window.findChild<ExportAsSheet *>()->isVisible(); }))
            throw std::runtime_error("no sheet");
        return *bar.window.findChild<ExportAsSheet *>();
    };
    const auto checked = [](ExportAsSheet &sheet, const char *name) { return sheet.findChild<QAbstractButton *>(QString::fromLatin1(name))->isChecked(); };
    const auto closed = [&bar] { return !bar.session().isProjectBusy() && !bar.window.findChild<ExportAsSheet *>(); };
    // PNG first; a PDF written through the menu comes next.
    bar.action("exportAs").trigger();
    QVERIFY(checked(shown(), "exportFormatPNG"));
    shown().findChild<QAbstractButton *>("exportFormatPDF")->click();
    desk.replies = {folder.filePath("Page")};
    QTRY_VERIFY(shown().findChild<QPushButton *>("exportExport")->isEnabled());
    shown().findChild<QPushButton *>("exportExport")->click();
    QTRY_VERIFY(QFileInfo::exists(folder.filePath("Page.pdf")) && closed());
    bar.action("exportAs").trigger();
    QVERIFY(checked(shown(), "exportFormatPDF"));
    QTest::keyClick(&shown(), Qt::Key_Escape);
    QTRY_VERIFY(closed());
    // Export JPEG starts on JPEG; Export As stays on PDF.
    bar.action("exportJPEG").trigger();
    QVERIFY(checked(shown(), "exportFormatJPEG"));
    QTest::keyClick(&shown(), Qt::Key_Escape);
    QTRY_VERIFY(closed());
    bar.action("exportAs").trigger();
    QVERIFY(checked(shown(), "exportFormatPDF"));
    QTest::keyClick(&shown(), Qt::Key_Escape);
    QTRY_VERIFY(closed());
}

QTEST_MAIN(ProjectExportTests)
#include "ProjectExportTests.moc"
