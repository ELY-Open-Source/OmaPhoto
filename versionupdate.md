# OmaPhoto 1.4.9

OmaPhoto 1.4.9 catches up with Compositor 1.4.9. It follows 1.4.5.

## New tools and commands

- **View › Search Commands** (Ctrl+F). Type a few letters of any menu command or tool and press Return.
- **View › Toggle Fullscreen** (F). The canvas alone on black over the whole screen. F or Escape brings the panels back.
- **Filter › Last Filter** (Ctrl+Meta+F) runs the last filter again with the same settings.
- **Filter › Scanlines** is its own filter now, with spacing, thickness, glow, dots, wobble, colour split and more.
- **File › Export As** (Ctrl+Alt+Shift+W) writes PNG, JPEG or a one-page PDF, at any size, over a checkerboard preview.
- **Image › Rotate Canvas 90°**, clockwise and counterclockwise.
- **View › Navigator**. From 300% a small map of the whole document shows in the canvas's corner. Click or drag it to move there.
- **New Canvas** takes pixels, inches or centimetres, 72 or 300 DPI, and a clear, white or black background.
- Ctrl (or Meta) with the middle button drags to zoom, as in Blender. Up zooms in.
- WebP pictures import.

## Camera Raw

- Temperature, Tint, Exposure, Contrast, Highlights, Shadows, Whites, Blacks, Vibrance and Saturation follow Photoshop's own measured colour tables. Contrast, Highlights and Shadows adapt to the picture as Photoshop's do.
- The Color Mixer, Calibration, the parametric curve, the vignettes and Color Grading match Photoshop's too.
- Dehaze, Texture, Clarity and Sharpening are nearer Photoshop. Colour noise reduction works.
- The preview is 5 to 20 times faster.

## Painting and editing

- Inner effects keep a pixel's transparency: a half-clear pixel takes the overlay's colour and stays half clear.
- A layer's effects survive Levels, Hue/Saturation, Invert, Canvas Size and Image Size. Image Size scales them. Text and shapes stay live through Canvas Size and Crop.
- Choosing a layer inside a closed folder opens the folder and shows the row.
- A RAW file imports with the frame dithered from 16 bits, and the sheet says Importing… meanwhile.

## Look

- Tool bars scroll when the window is narrow, rather than squeezing.
- The Layers panel's footer icons are brighter; disabled ones dim as the menus' do. A click no longer flashes the panel gray.
- A rename edits the name in place, at the same size.

## Tabs

- The blank start tab has no close button. New tabs take the lowest free Untitled number, so closing the last project leaves Untitled again.

## Projects

- OmaPhoto 1.4.9 saves project format 11 and opens formats 1 to 11, as Compositor 1.4.9 does. Projects move between OmaPhoto 1.4.5 and 1.4.9 freely.

## Packages

DEB (Ubuntu 24.04 and 26.04), RPM (Fedora 43 and 44), Arch (PKGBUILD), Nix (flake) and an AppImage are built from the same install. They now need liblzma, for Camera Raw's measured tables.
