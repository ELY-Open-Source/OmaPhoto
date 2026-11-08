#include "IO/LensCorrection.h"
#include "IO/ImageExporter.h"
#include "Logging.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>
#include <lensfun.h>
#include <libraw.h>
#include <mutex>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
// Lensfun's searches write scores into the shared database.
std::mutex lock;

bool readFolder(lfDatabase &loaded, const QString &folder)
{
    const bool read = loaded.LoadDirectory(QFile::encodeName(folder).constData());
    qCInfo(lcIO).noquote() << "lens profiles in" << folder << ":" << (read ? "loaded" : "none");
    return read;
}

// Lensfun's own, the data path, the user's folder; later wins.
const lfDatabase &database()
{
    static lfDatabase loaded;
    [[maybe_unused]] static const bool once = [] {
        bool found = loaded.Load() == LF_NO_ERROR;
        qCInfo(lcIO) << "lensfun's own lens profiles:" << (found ? "loaded" : "none");
        QStringList folders = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, "lensfun/version_1", QStandardPaths::LocateDirectory);
        std::reverse(folders.begin(), folders.end());
        for (const QString &folder : folders)
            found |= readFolder(loaded, folder);
        // Lensfun's user folder overrides all, as its Load intends.
        found |= readFolder(loaded, QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/lensfun");
        if (!found)
            qCWarning(lcIO) << "lensfun found no lens database";
        return true;
    }();
    return loaded;
}

struct Found {
    const lfCamera *camera = nullptr;
    const lfLens *lens = nullptr;
};

// The file's camera and lens, as lensfun names them.
Found find(const LibRaw &raw)
{
    const std::lock_guard guard(lock);
    const lfDatabase &db = database();
    const lfCamera **cameras = db.FindCamerasExt(raw.imgdata.idata.make, raw.imgdata.idata.model);
    if (!cameras)
        return {};
    const Found camera{cameras[0]};
    lf_free(cameras);
    if (!raw.imgdata.lens.Lens[0])
        return camera;
    const char *maker = raw.imgdata.lens.LensMake[0] ? raw.imgdata.lens.LensMake : nullptr;
    const lfLens **lenses = db.FindLenses(camera.camera, maker, raw.imgdata.lens.Lens);
    if (!lenses)
        return camera;
    const Found found{camera.camera, lenses[0]};
    lf_free(lenses);
    return found;
}

bool calibrated(const Found &found)
{
    return found.camera && found.lens
        && ((found.lens->CalibDistortion && found.lens->CalibDistortion[0]) || (found.lens->CalibVignetting && found.lens->CalibVignetting[0]));
}
}

bool LensCorrection::supports(const LibRaw &raw)
{
    const bool known = calibrated(find(raw));
    qCInfo(lcIO).noquote() << "lens profile for" << raw.imgdata.idata.make << raw.imgdata.idata.model << "with" << raw.imgdata.lens.Lens << ":"
                           << (known ? "found" : "none");
    return known;
}

std::unique_ptr<float[]> LensCorrection::apply(const LibRaw &raw, const quint16 *pixels, int width, int height)
{
    const Found found = find(raw);
    if (!calibrated(found))
        throw std::logic_error("lens correction without a lens profile");
    const size_t count = size_t(width) * size_t(height) * 3;
    // Floats: lensfun's 16-bit path clips brightened corners.
    std::unique_ptr<float[]> source(new (std::nothrow) float[count]), result(new (std::nothrow) float[count]);
    if (!source || !result)
        throw ExportError(ExportError::Kind::render);
    std::copy(pixels, pixels + count, source.get());
    lfModifier modifier(found.lens, found.camera->CropFactor, width, height);
    const float focal = raw.imgdata.other.focal_len, aperture = raw.imgdata.other.aperture;
    // Focus distance is seldom recorded: lensfun's last resort.
    int done = modifier.Initialize(found.lens, LF_PF_F32, focal, aperture, 1000, 1, found.lens->Type, LF_MODIFY_VIGNETTING | LF_MODIFY_DISTORTION, false);
    // A distortion scales to fill the frame, nothing else.
    if (done & LF_MODIFY_DISTORTION && modifier.AddCoordCallbackScale(0))
        done |= LF_MODIFY_SCALE;
    qCInfo(lcIO).noquote() << "lens correction:" << lf_mlstr_get(found.lens->Model) << "at" << focal << "mm, f/" << aperture << "flags" << done;
    if (done & LF_MODIFY_VIGNETTING)
        modifier.ApplyColorModification(source.get(), 0, 0, width, height, LF_CR_3(RED, GREEN, BLUE), width * 3 * int(sizeof(float)));
    if (!(done & LF_MODIFY_DISTORTION))
        return source;
    // Lensfun 0.3.4's SSE pairs lanes wrongly; unaligned, it runs plain.
    std::vector<float> store(size_t(width) * 2 + 1);
    float *const places = store.data() + ((reinterpret_cast<uintptr_t>(store.data()) & 0xf) ? 0 : 1);
    // Each pixel reads where the lens put it; black outside.
    for (int y = 0; y < height; ++y) {
        modifier.ApplyGeometryDistortion(0, float(y), width, 1, places);
        float *out = result.get() + size_t(y) * size_t(width) * 3;
        for (int x = 0; x < width; ++x) {
            const float at[2] = {places[size_t(x) * 2], places[size_t(x) * 2 + 1]};
            const int left = int(std::floor(at[0])), top = int(std::floor(at[1]));
            const float across = at[0] - float(left), down = at[1] - float(top);
            float sum[3] = {0, 0, 0};
            for (int j = 0; j < 2; ++j)
                for (int i = 0; i < 2; ++i) {
                    const int column = left + i, row = top + j;
                    if (column < 0 || column >= width || row < 0 || row >= height)
                        continue;
                    const float weight = (i ? across : 1 - across) * (j ? down : 1 - down);
                    const float *in = source.get() + (size_t(row) * size_t(width) + size_t(column)) * 3;
                    for (int c = 0; c < 3; ++c)
                        sum[c] += weight * in[c];
                }
            for (int c = 0; c < 3; ++c)
                out[size_t(x) * 3 + size_t(c)] = sum[c];
        }
    }
    return result;
}
