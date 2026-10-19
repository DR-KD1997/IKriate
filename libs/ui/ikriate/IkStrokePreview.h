/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKSTROKEPREVIEW_H
#define IKSTROKEPREVIEW_H

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QStringList>

#include <KoCanvasResourcesInterface.h>
#include <brushengine/kis_paintop_preset.h>

#include "kritaui_export.h"

/**
 * Renders a short sample stroke ("squiggle") with a brush preset, the way
 * the brush library shows brushes.
 *
 * Strokes are painted with the real brush engine in black on transparent;
 * the result is used as an alpha mask and coloured at draw time, so one
 * cached image serves every skin. Images are cached in memory and on disk
 * (keyed by the preset's content), and rendered lazily, a few per event-loop
 * turn, so a long list never blocks the interface.
 */
class KRITAUI_EXPORT IkStrokePreview : public QObject
{
    Q_OBJECT
public:
    static IkStrokePreview *instance();

    /// Gives presets access to canvas resources (colours, gradient) while
    /// rendering. Optional; without it presets that need them fall back.
    void setCanvasResourcesInterface(KoCanvasResourcesInterfaceSP iface);

    /// Cached preview, or a null image while it is being rendered (preview()
    /// queues it and emits previewReady() when done).
    QImage preview(KisPaintOpPresetSP preset, const QSize &size);

    /// Colours an alpha-mask preview.
    static QImage tinted(const QImage &mask, const QColor &color);

    /// Renders immediately (used by the queue and by tests).
    QImage render(KisPaintOpPresetSP preset, const QSize &size) const;

Q_SIGNALS:
    void previewReady(const QString &key);

private:
    IkStrokePreview();
    QString keyFor(KisPaintOpPresetSP preset, const QSize &size) const;
    QString cacheFile(const QString &key) const;
    void processQueue();

    struct Job {
        QString key;
        KisPaintOpPresetSP preset;
        QSize size;
    };
    QHash<QString, QImage> m_cache;
    QList<Job> m_queue;
    QSet<QString> m_queued;
    bool m_scheduled {false};
    KoCanvasResourcesInterfaceSP m_canvasResources;
};

#endif // IKSTROKEPREVIEW_H
