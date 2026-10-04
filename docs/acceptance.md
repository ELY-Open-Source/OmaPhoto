# OmaPhoto 1.4.5 acceptance

The acceptance table for OmaPhoto 1.4.5, the Linux port of Compositor 1.4.5. Each row is a feature from the README. The rows under "New in 1.4.5" cover what changed since 1.3.3. A "pending" result waits for the release.

The acceptance run drives the app's own window, the way a person uses it. It chooses menu entries, drags on the canvas, types into sheets and answers the real file panels. Then it checks concrete results: pixels, layers, sizes and files on disk. The run is six test programs: `AcceptanceTests`, `AcceptanceColorTests` and `AcceptanceProjectTests` for the table, `AcceptanceNewTests`, `AcceptanceEditTests` and `AcceptanceFinishTests` for the rows new in 1.4.5. They take the screenshots below when `ACCEPTANCE_SHOTS` names a folder.

```sh
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e QT_QPA_PLATFORM=offscreen -v "$PWD:$PWD" -w "$PWD" omaphoto-dev \
  sh -c 'cmake -S . -B build && cmake --build build --target AcceptanceTests AcceptanceColorTests AcceptanceProjectTests AcceptanceNewTests AcceptanceEditTests AcceptanceFinishTests &&
         for t in AcceptanceTests AcceptanceColorTests AcceptanceProjectTests AcceptanceNewTests AcceptanceEditTests AcceptanceFinishTests; do ACCEPTANCE_SHOTS=$PWD/docs/acceptance build/$t || exit 1; done'
```

The screenshots come from Qt's offscreen platform, so floating panels appear without the window manager's title bars. The run uses the built-in dark theme, except for the last row.

## Table

| Feature | What the run does and checks | Screenshot | Suites that hold the details | Result |
| --- | --- | --- | --- | --- |
| Layers, folders, masks, clipping masks, blend modes, opacity | Imports a photo and paints a layer. Clips it to the photo and sets Multiply at 60%. Masks the photo and groups both in a folder. Undo takes a step back. | [01](acceptance/01-layers.png) | `GroupTests`, `LayerMaskTests`, `LiveMaskTests`, `LayerAppearanceTests`, `LayerRendererTests`, `HistoryTests` | pass |
| Selections | Drags a marquee to within 2 px. The wand takes the flat ground. Select › Subject finds the red disc with the U²-Net model. | [02](acceptance/02-selections.png) | `SelectionTests`, `MagicWandTests`, `SelectSubjectTests`, `SelectionEditTests`, `ObjectSelectionTests`, `SelectionFeatherTests` | pass |
| Painting tools | Drags a gradient that fades to clear, a rectangle shape and a soft white brush stroke. Types "Hello OmaPhoto" on the canvas and colours "Hello" red. That leaves one colour run, committed with Ctrl+Return. | [03](acceptance/03-painting.png) | `BrushTests`, `GradientTests`, `ShapeToolTests`, `TypeToolTests`, `TextColorRunsTests`, `SpotHealingTests`, `CloneStampTests`, `SmudgeLiquifyTests` | pass |
| Move, transform, distort; rulers, guides, grid | Drags the layer with the Move tool. Transform Layer halves it, and Return applies. Rulers, a guide and the grid are shown. | [04](acceptance/04-transform-guides-grid.png) | `TransformSessionTests`, `DistortTests`, `GuideTests`, `FloatingSelectionTests` | pass |
| Adjustments as edits and as layers | Hue/Saturation turns the red disc green. An Invert layer inverts the view and leaves the pixels alone. A Curves layer opens its editor. | [05](acceptance/05-adjustments.png) | `HueSaturationTests`, `LevelsTests`, `CurvesControlsTests`, `AdjustmentLayerTests`, `AdjustmentEditorTests`, `ImageAdjustmentTests` | pass |
| Camera Raw Filter, docked | Opens in the dock beside the canvas. One stop of exposure previews, commits and brightens the ground. | [06](acceptance/06-camera-raw.png) | `CameraRawTests`, `CameraRawSessionTests`, `CameraRawPanelTests`, `CameraRawCanvasTests` | pass |
| Layer effects | A 12 px red stroke rings a shape outside its edge, and a drop shadow's sheet shows. The shape keeps its fill. | [07](acceptance/07-effects.png) | `LayerEffectsTests`, `LayerEffectsKernelTests`, `EffectsPanelTests`, `EffectsCanvasTests`, `InnerGlowTests` | pass |
| Filters | Dither leaves only black and white dots. Remove Background masks the photo around its subject: the centre stays opaque and a corner turns clear. | [08](acceptance/08-filters-dither.png), [09](acceptance/09-remove-background.png) | `DitherTests`, `FilterTests`, `FinishingFilterTests`, `SubjectRemovalTests`, `RemoveBackgroundTests` | pass |
| Crop, trim, canvas size, image size; PNG and JPEG export | Crops to 1000 × 600. Canvas Size widens it to 1400. Trim finds the photo's own 1200 again. Image Size halves it. Exports a PNG through the save panel and a JPEG through its sheet and panel, then reads both back at 600 × 300. | [10](acceptance/10-canvas-size.png), [11](acceptance/11-export-jpeg.png) | `CropTests`, `ImageTrimTests`, `ProjectResizeTests`, `ProjectExportTests`, `JPEGExportTests` | pass |
| Imports | Takes JPEG, PNG, TIFF, HEIC, SVG, a layered Photoshop file (it arrives as a folder) and a camera RAW file, which asks how to develop it. That makes eight layers with no import error. | [12](acceptance/12-raw-develop.png), [13](acceptance/13-imports.png) | `ImageImportTests`, `PSDImportTests`, `RawImporterTests`, `RawDevelopTests`, `SVGImportTests` | pass |
| Open Recent; Keyboard Shortcuts window | Saves a project, and Open Recent lists it. The shortcuts window opens over the editor. | [15](acceptance/15-shortcuts.png) | `OpenRecentTests`, `RecentProjectsCoreTests`, `KeyboardShortcutsTests`, `KeyboardShortcutsSheetTests` | pass |
| Scrub a number by its label | Dragging the Opacity title 40 points left lowers the layer's opacity. | [14](acceptance/14-scrub.png) | `NumericScrubTests`, `ScrubbableLabelsTests`, `ScrubbableSheetsTests` | pass |
| Keep working while a project saves | Another layer is added while a save runs. The file holds the state the save began with. | — | `ProjectSaveWhileEditingTests` | pass |
| Reload a project changed on disk | With unsaved edits open, another program renames a layer on disk. The app's alert asks, Revert is chosen, and the saved state shows with the new name. | [15](acceptance/15-shortcuts.png) | `ExternalChangeTests` | pass |
| Follows the Omarchy theme | Starts in the built-in dark. A Catppuccin Latte `colors.toml` written while the app runs retints the window. | [16](acceptance/16-omarchy-light.png) | `OmarchyThemeTests` | pass |
| Builds and tests on each system | The full suite runs on Ubuntu 24.04 (Qt 6.4.2), Ubuntu 26.04, Arch, Fedora 43 and 44, and NixOS. Warnings count as errors. | — | `scripts/distro-check.sh` | pending |
| Packages and install scripts | Each install script runs from GitHub in a fresh container. On Ubuntu 24.04 and 26.04, Fedora 43 and 44 and Arch, it checks its package against `SHA256SUMS`, installs it and starts 1.4.5. On NixOS, `nixos.sh` resolves `v1.4.5`, builds its flake and starts 1.4.5. Debian 13 is refused, as intended. | — | `DesktopIntegrationTests`, `.github/workflows/release.yml` | pending |

## New in 1.4.5

| Feature | What the run checks | Screenshot | Suites that hold the details | Result |
| --- | --- | --- | --- | --- |
| Select › Color Range | Samples the red disc on the canvas; the selection covers the disc, 400 pixels across, and leaves the ground. | — | `ColorRangeTests`, `ColorRangeSheetTests`, `ColorRangeCanvasTests` | pass |
| View › Grid Settings | Types a spacing of 100 in the sheet; Restore Defaults puts the grid back. | — | `GridSettingsSheetTests`, `GuideGridTests` | pass |
| Snapping for selections and shapes | A marquee and a rectangle shape drawn near a guide land on it; with Ctrl they do not. | — | `SnapDrawingCanvasTests`, `TransformSnapTests` | pass |
| New Canvas presets | Chooses Instagram Story from the ⋮ menu; the fields and the check follow, and Create makes 1080 × 1920. | — | `NewCanvasSheetTests` | pass |
| Quick Look preview | A saved project holds `QuickLook/Preview.jpg`, 576 × 1024, and its manifest keeps its four layers. | — | `QuickLookPreviewTests` | pass |
| Ungroup Layers | Ctrl+Shift+G on a folder leaves its layers where the folder stood, in order; Ctrl+Z brings the folder back. | — | `GroupSessionTests`, `LayerContextMenuTests` | pass |
| Project tabs reorder and overflow | Drags a tab past its neighbour; with many tabs, the "more tabs" menu switches to a hidden one. | [17](acceptance/17-tabs.png) | `ProjectTabDragTests`, `ProjectTabLayoutTests` | pass |
| Fonts on selected letters and previews | In the open font menu, the pointer over a face previews it on the selected word and Escape puts it back; a click gives the word that face; Ctrl+Z takes back typing. | — | `TextFontRunsTests`, `TypeFontPreviewTests`, `InlineTextDrawTests` | pass |
| Painting at the layer's resolution | Paints on a layer scaled to 25%: its pixels stay 800 wide and the stroke is 80 of them across. A hidden layer says why it cannot paint. | — | `NativeResolutionPaintTests`, `PaintRefusalTests` | pass |
| Liquify, Smudge and Blur | Liquify moves stripes and leaves few grays; Smudge leaves one fading trail; a Radius of 20 typed in the bar softens wider than 2. | — | `SmudgeLiquifyTests`, `WarpStrokeTests`, `BlurToolTests` | pass |
| Masks as Photoshop's | The mask button reveals the selection, Alt hides it; Alt-click on the mask thumbnail shows the mask alone on the canvas, black where it hides and white past it. | [18](acceptance/18-mask-alone.png) | `MaskAloneTests`, `SelectionEditTests` | pass |
| Move tool keys and values | Held Ctrl flips Auto Select; a typed width applies as one step. | — | `TransformInspectorTests`, `ResizeSnapCanvasTests` | pass |
| Blending and a sharper canvas | Soft Light and a clipping stack in Linear Dodge show on the canvas as exported. | — | `SharpCanvasTests`, `BlendingTests`, `ClippingStackTests` | pass |
| Camera Raw curves | Lifts a region of the parametric curve and adds a point on the point curve, then OK; the ground brightens. | — | `CameraRawCurveTests`, `CameraRawCurveGraphTests` | pass |
| Dither ASCII and Scanlines | In the Dither sheet, ASCII of "@" draws that letter, holes and all, about twice as tall at a text size of 28; Scanlines draws dark gaps between rows. | — | `DitherTests`, `ScanlinesTests` | pass |
| Export JPEG preview | The View keys zoom the preview to 100%, in, and back to Fit. | — | `JPEGPreviewTests`, `JPEGExportSheetTests` | pass |
| Quit mid-edit | Quits with a gradient pending: it is applied before the one question. With Levels open, Levels closes and the window goes. | — | `QuitPendingEditsTests`, `WindowTeardownTests` | pass |

## Known differences from Compositor

`AGENTS.md` lists each place where the port differs from the Swift app, and why. The main ones:

- Remove Background and Select Subject use U²-Net on ONNX Runtime, not Apple's Vision.
- Rendering is CPU only.
- There is no updater.
- A `.comp` project is a folder.
- A Wayland compositor places windows itself.
