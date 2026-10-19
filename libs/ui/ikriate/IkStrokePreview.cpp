/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkStrokePreview.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QStandardPaths>
#include <QTimer>
#include <QtMath>

#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>
#include <KisGlobalResourcesInterface.h>

#include <KisRunnableStrokeJobData.h>
#include <brushengine/KisPerStrokeRandomSource.h>
#include <brushengine/kis_paintop.h>
#include <brushengine/kis_paint_information.h>
#include <kis_random_source.h>
#include <brushengine/kis_paintop_settings.h>
#include <kis_distance_information.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_paint_layer.h>
#include <kis_painter.h>

namespace
{
// Engines whose result depends on what is already on the canvas (or that
// draw nothing on their own) get the preset's icon instead.
const QStringList kNoStrokeEngines = {
    QStringLiteral("duplicate"), QStringLiteral("filter"), QStringLiteral("deformbrush"),
    QStringLiteral("experimentbrush"), QStringLiteral("tangentnormal"),
};

QPointF cubic(const QPointF &p0, const QPointF &p1, const QPointF &p2, const QPointF &p3, qreal t)
{
    const qreal u = 1 - t;
    return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
}
}

IkStrokePreview *IkStrokePreview::instance()
{
    static IkStrokePreview *s = new IkStrokePreview();
    return s;
}

IkStrokePreview::IkStrokePreview()
{
}

void IkStrokePreview::setCanvasResourcesInterface(KoCanvasResourcesInterfaceSP iface)
{
    m_canvasResources = iface;
}

QString IkStrokePreview::keyFor(KisPaintOpPresetSP preset, const QSize &size) const
{
    // Content-based: an edited preset gets a new preview.
    QString md5 = preset->md5Sum(false);
    if (md5.isEmpty() || preset->isDirty()) {
        QByteArray data = preset->name().toUtf8();
        data += preset->settings() ? preset->settings()->getString("brush_definition").toUtf8() : QByteArray();
        data += QByteArray::number(preset->settings() ? preset->settings()->paintOpSize() : 0.0);
        md5 = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
    }
    return QStringLiteral("v3-%1-%2x%3").arg(md5).arg(size.width()).arg(size.height());
}

QString IkStrokePreview::cacheFile(const QString &key) const
{
    static const QString dir = [] {
        const QString d = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/stroke-previews");
        QDir().mkpath(d);
        return d;
    }();
    return dir + '/' + key + QStringLiteral(".png");
}

QImage IkStrokePreview::preview(KisPaintOpPresetSP preset, const QSize &size)
{
    if (!preset || size.isEmpty()) return QImage();
    const QString key = keyFor(preset, size);
    auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd()) return it.value();

    QImage disk(cacheFile(key));
    if (!disk.isNull()) {
        m_cache.insert(key, disk);
        return disk;
    }
    if (!m_queued.contains(key)) {
        m_queued.insert(key);
        m_queue.append(Job{key, preset, size});
        if (!m_scheduled) {
            m_scheduled = true;
            QTimer::singleShot(0, this, &IkStrokePreview::processQueue);
        }
    }
    return QImage();
}

void IkStrokePreview::processQueue()
{
    m_scheduled = false;
    // A few per turn keeps scrolling smooth.
    for (int i = 0; i < 3 && !m_queue.isEmpty(); ++i) {
        const Job job = m_queue.takeFirst();
        m_queued.remove(job.key);
        QImage img = render(job.preset, job.size);
        if (img.isNull()) {
            // Fall back to the preset's own icon, scaled to fit.
            const QImage icon = job.preset->image();
            img = QImage(job.size, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);
            if (!icon.isNull()) {
                QPainter p(&img);
                p.setRenderHint(QPainter::SmoothPixmapTransform);
                const QSize s = icon.size().scaled(job.size * 0.9, Qt::KeepAspectRatio);
                p.drawImage(QRect(QPoint((job.size.width() - s.width()) / 2, (job.size.height() - s.height()) / 2), s), icon);
            }
            img.setText(QStringLiteral("ikriate-icon"), QStringLiteral("1"));
        }
        m_cache.insert(job.key, img);
        img.save(cacheFile(job.key));
        Q_EMIT previewReady(job.key);
    }
    if (!m_queue.isEmpty()) {
        m_scheduled = true;
        QTimer::singleShot(1, this, &IkStrokePreview::processQueue);
    }
}

QImage IkStrokePreview::render(KisPaintOpPresetSP source, const QSize &size) const
{
    if (!source || !source->settings() || size.isEmpty()) return QImage();
    if (kNoStrokeEngines.contains(source->paintOp().id())) return QImage();

    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(nullptr, size.width(), size.height(), cs, QStringLiteral("ikriate-stroke-preview"));
    KisPaintLayerSP layer = new KisPaintLayer(image, QStringLiteral("stroke"), OPACITY_OPAQUE_U8);
    image->addNode(layer);
    KisPaintDeviceSP device = layer->paintDevice();

    KisPaintOpPresetSP preset;
    if (m_canvasResources) {
        preset = source->cloneWithResourcesSnapshot(KisGlobalResourcesInterface::instance(), m_canvasResources, nullptr);
    } else {
        preset = source->clone().dynamicCast<KisPaintOpPreset>();
    }
    if (!preset || !preset->settings()) return QImage();

    KisPaintOpSettingsSP settings = preset->settings();
    const qreal h = size.height();
    // Small enough to show the shape of the stroke, large enough for texture.
    const qreal maxSize = h * 0.24;
    const qreal sz = settings->paintOpSize();
    settings->setPaintOpSize(qBound<qreal>(2.0, sz, maxSize));
    settings->setEraserMode(false);
    QString op = settings->paintOpCompositeOp();
    if (op.isEmpty() || op == COMPOSITE_ERASE || op == COMPOSITE_COPY || op == COMPOSITE_DESTINATION_IN
        || op == COMPOSITE_DESTINATION_ATOP) {
        op = COMPOSITE_OVER;
    }

    KisPainter painter(device);
    painter.setPaintColor(KoColor(Qt::black, cs));
    painter.setBackgroundColor(KoColor(Qt::white, cs));
    painter.setOpacityF(qMax<qreal>(0.35, settings->paintOpOpacity()));
    painter.setCompositeOpId(op);
    painter.setPaintOpPreset(preset, layer, image);
    if (!painter.paintOp()) {
        if (qEnvironmentVariableIsSet("IKRIATE_DEBUG_PREVIEWS")) qDebug("IKriate preview %s: no paintop", qPrintable(source->name()));
        return QImage();
    }

    // A relaxed "Z" squiggle like a quick test scribble.
    const qreal w = size.width();
    const QPointF p0(w * 0.07, h * 0.62);
    const QPointF c1(w * 0.40, h * 0.05);
    const QPointF c2(w * 0.55, h * 0.05);
    const QPointF p1(w * 0.30, h * 0.70);
    const QPointF c3(w * 0.55, h * 1.05);
    const QPointF c4(w * 0.80, h * 0.10);
    const QPointF p2(w * 0.93, h * 0.36);

    KisDistanceInformation distance;
    const int steps = 48;
    qreal time = 0;
    // Fixed seeds: scattered and textured brushes draw the same preview every time.
    KisRandomSourceSP rnd(new KisRandomSource(7));
    KisPerStrokeRandomSourceSP strokeRnd(new KisPerStrokeRandomSource());
    KisPaintInformation prev(p0, 0.1);
    prev.setCurrentTime(time);
    prev.setRandomSource(rnd);
    prev.setPerStrokeRandomSource(strokeRnd);
    for (int i = 1; i <= steps; ++i) {
        const qreal t = qreal(i) / steps;
        const QPointF pos = t <= 0.5 ? cubic(p0, c1, c2, p1, t * 2) : cubic(p1, c3, c4, p2, (t - 0.5) * 2);
        // Pressure swells in the middle and tapers at both ends.
        const qreal pressure = qBound<qreal>(0.05, std::sin(M_PI * t) * 0.95 + 0.05, 1.0);
        time += 12.0;
        KisPaintInformation next(pos, pressure, 0, 0, 0, 0, 1.0, time, 0.0);
        next.setCurrentTime(time);
        next.setRandomSource(rnd);
        next.setPerStrokeRandomSource(strokeRnd);
        painter.paintLine(prev, next, &distance);
        prev = next;
    }

    // Brush engines that render dabs asynchronously (the pixel brush, ...)
    // only queue them while painting; flush the queue here.
    if (settings->needsAsynchronousUpdates()) {
        for (int guard = 0; guard < 256; ++guard) {
            QVector<KisRunnableStrokeJobData *> jobs;
            const std::pair<int, bool> state = painter.paintOp()->doAsynchronousUpdate(jobs);
            for (KisRunnableStrokeJobData *job : jobs) job->run();
            const bool ranJobs = !jobs.isEmpty();
            qDeleteAll(jobs);
            if (!state.second && !ranJobs) break;
        }
    }

    QImage result = device->convertToQImage(nullptr, 0, 0, size.width(), size.height());
    if (result.isNull()) return QImage();

    // Nothing visible (blenders on an empty canvas, ...): use the icon instead.
    qint64 alphaSum = 0;
    const QImage a = result.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < a.height(); y += 2) {
        const QRgb *line = reinterpret_cast<const QRgb *>(a.constScanLine(y));
        for (int x = 0; x < a.width(); x += 2) alphaSum += qAlpha(line[x]);
    }
    if (qEnvironmentVariableIsSet("IKRIATE_DEBUG_PREVIEWS")) {
        qDebug("IKriate preview %s (%s): alpha %lld, canvas resources %d", qPrintable(source->name()),
               qPrintable(source->paintOp().id()), alphaSum, m_canvasResources ? 1 : 0);
    }
    if (alphaSum < 255 * 8) return QImage();
    return result.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage IkStrokePreview::tinted(const QImage &mask, const QColor &color)
{
    if (mask.isNull()) return mask;
    if (mask.text(QStringLiteral("ikriate-icon")) == QLatin1String("1")) {
        return mask; // a preset icon: keep its colours
    }
    QImage out(mask.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.drawImage(0, 0, mask);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(out.rect(), color);
    return out;
}
