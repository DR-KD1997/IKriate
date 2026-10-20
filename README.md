<p align="center"><img src="krita/pics/branding/IKriate/256-apps-krita.png" width="128" alt="IKriate icon"></p>

# IKriate

IKriate is a digital painting application for Windows, Linux and Android tablets, derived from
[Krita](https://krita.org). It keeps Krita's painting engine, brushes, file formats and colour
management, and replaces the interface with a modern, minimal one built around the canvas.

> IKriate is an independent project. It is not made, endorsed or supported by the Krita
> Foundation or KDE. Please report IKriate problems here, not to Krita's bug tracker.

## What IKriate adds

| | |
|---|---|
| **Home and gallery** | Starts on a gallery of your artworks with favourites, a recycle bin and a single **+** button for a new canvas, a template or an existing file. The Android launch screen stays up until the gallery is ready, so there is no black screen while the app loads. |
| **Three layouts** | *Desktop* (top bar, tool rail, colour and layers panel, brush bar; default on computers), *Kriate* (touch-first tool bar with a docked brush library; default on Android tablets) and *Minimal* (a few floating buttons around a full-screen canvas). All three are on every build: **Settings › Theme › Layout**. Krita's docker interface stays available for power users under **Settings › Advanced**. |
| **Brush library** | Brushes grouped into Pencils, Pens, Calligraphy, Markers, Paint, Watercolor, Sprayers and more, each with a live stroke preview, favourites, recents and your own brushes under *New*. |
| **Draw, erase, smudge** | Erasing is a mode, not a brush: any brush can draw or erase, and the pen's eraser end switches to erase mode with the current brush. |
| **Pen-first touch** | Only the pen draws by default; one finger pans, two fingers zoom and rotate. Finger painting can be turned on in the canvas input settings. |
| **Touch gestures** | Two- to five-finger taps and two- to four-finger double taps can each run a function of your choice (**Settings › Advanced › Touch Gestures**). By default two fingers undo, three redo, four hide the interface and a three-finger double tap flips the canvas. |
| **Symmetry** | The symmetry button (all three layouts) paints mirrored left and right, top and bottom, both, or radially with 2–16 sections. |
| **Layer colours** | The three-circles button adjusts the selected layer's hue, saturation and brightness, or its colour balance, with a live preview. |
| **Edit Interface** | **More › Edit Interface** pauses the tools so you can drag buttons between the bars and columns of a layout. Each layout remembers its own arrangement, and **Reset** puts it back. |
| **Practice** | The lightbulb on the home screen starts guided exercises: timed drawing from your own reference images, memory drawing, value studies and warm-ups. |
| **Auto-Save** | When you go back to the gallery, changes are saved without asking. New artworks go to *Pictures/IKriate* (*Documents/IKriate* on Android). You can turn this off in **Settings**. |
| **Skins and tinting** | Follow-system, light and dark themes, an accent colour and a tint for the interface, plus bundled and custom skins (**Settings › Theme**). Interface scale and font change immediately, without a restart. |
| **Side-by-side with Krita** | IKriate keeps its own settings (`ikriaterc`) and resource folder (`ikriate`), so it can be installed next to Krita without either touching the other's data. It opens and saves Krita documents (`.kra`). |

## Custom skins

A skin is a KDE colour scheme file with an optional `[IKriate]` section, saved as `.ikskin`
(any `.colors` file also works). Start by copying a bundled skin from
`krita/data/ikriate/skins/` into your skins folder (**Settings › Advanced › Open Skins Folder**):

```ini
[IKriate]
Accent=242,165,65        ; selection and active-control colour
ChromeRadius=14          ; corner radius of floating panels, px
PanelOpacity=240         ; 120-255, opacity of panels over the canvas
Stylesheet=my-skin.qss   ; optional Qt stylesheet next to the skin
Description=One line shown in the skin picker
```

## Download

Windows, Linux and Android packages are on the
[Releases](https://github.com/DR-KD1997/IKriate/releases) page. The Android APK is
debug-signed: allow installing from unknown sources, and uninstall it before installing a
future release-signed version.

## Building

See [packaging/README-IKRIATE.md](packaging/README-IKRIATE.md) for Linux, Windows and Android
builds, both locally and on GitHub Actions. Windows and Linux builds use Qt 6; the Android
build still uses Qt 5, because there are no Qt 6 Android dependency packages for Krita yet.

## Licence

IKriate is free software under the GNU General Public License v3 (see `COPYING` and
`LICENSES/`), like the Krita code it is based on. Krita's original README is kept in
[README.krita.md](README.krita.md).
