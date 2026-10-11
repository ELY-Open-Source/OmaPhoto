#include "Document/ProjectWorkspace.h"
#include "IO/ImageExporter.h"
#include "IO/ProjectController.h"
#include "UI/ExportAsSheet.h"
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLayout>
#include <QtConcurrent>

namespace {
// Swift's suggestion: the project's name, else Untitled.
QString suggested(const std::optional<QString> &project, const QString &suffix)
{
    return (project ? ProjectTab::nameWithoutSuffix(*project) : QStringLiteral("Untitled")) + suffix;
}

// The panel's filter and a name's suffixes, preferred first.
std::pair<QString, QStringList> filtering(ExportFormat format)
{
    switch (format) {
    case ExportFormat::png:
        return {QStringLiteral("PNG image (*.png)"), {QStringLiteral("png")}};
    case ExportFormat::jpeg:
        return {QStringLiteral("JPEG image (*.jpeg *.jpg *.jpe)"), {QStringLiteral("jpeg"), QStringLiteral("jpg"), QStringLiteral("jpe")}};
    case ExportFormat::pdf:
        return {QStringLiteral("PDF document (*.pdf)"), {QStringLiteral("pdf")}};
    }
    throw std::logic_error("unknown export format");
}
}

QString rawValue(ExportFormat format)
{
    switch (format) {
    case ExportFormat::png:
        return QStringLiteral("PNG");
    case ExportFormat::jpeg:
        return QStringLiteral("JPEG");
    case ExportFormat::pdf:
        return QStringLiteral("PDF");
    }
    throw std::logic_error("unknown export format");
}

// Swift's NSSavePanel for one type, whose suffix a name needs.
void ProjectController::exportPanel(ExportFormat format, const QString &failure, std::function<void(std::optional<QString>)> then)
{
    const auto [filter, suffixes] = filtering(format);
    auto *panel = new QFileDialog(window, QStringLiteral("Export ") + rawValue(format));
    panel->setAttribute(Qt::WA_DeleteOnClose);
    panel->setAcceptMode(QFileDialog::AcceptSave);
    panel->setNameFilter(filter);
    panel->setDefaultSuffix(suffixes.first());
    panel->selectFile(suggested(session.projectPath(), QLatin1Char('.') + suffixes.first()));
    // Moved, never copied: `then` may hold a whole snapshot.
    connect(panel, &QDialog::finished, this, [this, panel, suffixes, failure, then = std::move(then)](int result) mutable {
        if (result != QDialog::Accepted) {
            then(std::nullopt);
            return;
        }
        const QString chosen = panel->selectedFiles().value(0);
        if (suffixes.contains(QFileInfo(chosen).suffix().toLower())) {
            then(chosen);
            return;
        }
        // The panel never saw this name: nothing there is replaced.
        const QFileInfo renamed(chosen + QLatin1Char('.') + suffixes.first());
        if (renamed.exists() || renamed.isSymLink()) {
            showError(failure, QStringLiteral("“%1” already exists. Choose another name.").arg(renamed.fileName()),
                      [then = std::move(then)]() mutable { then(std::nullopt); });
            return;
        }
        then(renamed.filePath());
    });
    panel->open();
}

// Encoding and the disk block: off the UI thread.
void ProjectController::exportWork(std::function<void()> work, const QString &failure, std::function<void()> end)
try {
    auto *watcher = new QFutureWatcher<std::optional<QString>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, failure, end] {
        watcher->deleteLater();
        if (const std::optional<QString> message = watcher->result()) {
            showError(failure, *message, end);
            return;
        }
        end();
    });
    watcher->setFuture(QtConcurrent::run([work = std::move(work)]() -> std::optional<QString> {
        try {
            work();
            return std::nullopt;
        } catch (const std::runtime_error &error) {
            return QString::fromUtf8(error.what());
        }
    }));
} catch (const std::bad_alloc &) {
    showError(failure, QString::fromUtf8(ExportError(ExportError::Kind::render).what()), end);
}

void ProjectController::exportPNG(std::function<void()> done)
{
    if (!session.document() || !begin()) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    const auto end = [this, done] {
        session.setIsProjectBusy(false);
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    const QString failure = QStringLiteral("Couldn’t export PNG");
    std::optional<ProjectSnapshot> snapshot;
    try {
        snapshot = session.projectSnapshot();
    } catch (const std::bad_alloc &) {
        showError(failure, QString::fromUtf8(ExportError(ExportError::Kind::render).what()), end);
        return;
    }
    exportPanel(ExportFormat::png, failure, [this, snapshot = std::move(snapshot.value()), failure, end](std::optional<QString> path) {
        if (!path) {
            end();
            return;
        }
        // The snapshot's copy may find no memory.
        try {
            exportWork([snapshot, path = *path] { ImageExporter::exportPNG(snapshot, path); }, failure, end);
        } catch (const std::bad_alloc &) {
            showError(failure, QString::fromUtf8(ExportError(ExportError::Kind::render).what()), end);
        }
    });
}

void ProjectController::exportAs(std::optional<ExportFormat> start, std::function<void()> done)
{
    if (!window || !session.document() || !begin()) {
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
        return;
    }
    const auto end = [this, done] {
        session.setIsProjectBusy(false);
        if (done)
            QMetaObject::invokeMethod(this, done, Qt::QueuedConnection);
    };
    const QString failure = QStringLiteral("Couldn’t export");
    const QString render = QString::fromUtf8(ExportError(ExportError::Kind::render).what());
    std::optional<ProjectSnapshot> snapshot;
    try {
        snapshot = session.projectSnapshot();
    } catch (const std::bad_alloc &) {
        showError(failure, render, end);
        return;
    }
    auto *watcher = new QFutureWatcher<Rendered>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, failure, end, format = start.value_or(m_lastExportFormat)] {
        watcher->deleteLater();
        Rendered rendered = watcher->future().takeResult();
        if (!rendered.raster) {
            showError(failure, rendered.failure, end);
            return;
        }
        QDialog *dialog = sheet(QStringLiteral("Export As"));
        auto chosen = std::make_shared<std::optional<ExportAsSheet::Chosen>>();
        dialog->layout()->addWidget(new ExportAsSheet(std::move(*rendered.raster), session, format, [dialog, chosen](std::optional<ExportAsSheet::Chosen> picked) {
            *chosen = std::move(picked);
            dialog->done(*chosen ? QDialog::Accepted : QDialog::Rejected);
        }, dialog));
        connect(dialog, &QDialog::finished, this, [this, chosen, failure, end] {
            if (!*chosen) {
                end();
                return;
            }
            m_lastExportFormat = (*chosen)->format;
            exportPanel((*chosen)->format, failure, [this, data = (*chosen)->data, failure, end](std::optional<QString> path) {
                if (!path) {
                    end();
                    return;
                }
                exportWork([data, path = *path] { ImageExporter::write(data, path); }, failure, end);
            });
        });
        dialog->open();
    });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot.value())]() -> Rendered {
        try {
            return {ImageExporter::render(snapshot), QString()};
        } catch (const ExportError &error) {
            return {std::nullopt, QString::fromUtf8(error.what())};
        }
    }));
}
