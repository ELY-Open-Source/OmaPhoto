#pragma once
#include <QtGlobal>
#include <memory>

class LibRaw;

// Linux-only: Lens Correction from lensfun's camera and lens profiles.
namespace LensCorrection {
// Lensfun knows the file's camera and a calibrated lens.
bool supports(const LibRaw &raw);
// Vignetting, then distortion: linear RGB on 16-bit scale, unclipped.
std::unique_ptr<float[]> apply(const LibRaw &raw, const quint16 *pixels, int width, int height);
}
