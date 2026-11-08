/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkDrawpileFiles.h"
#include "IkSessionMirror.h"

extern "C" {
#include <dpcommon/common.h>
#include <dpengine/canvas_state.h>
#include <dpengine/layer_props.h>
#include <dpengine/layer_props_list.h>
#include <dpengine/project.h>
#include <dpengine/project_worker.h>
#include <dpimpex/image_impex.h>
#include <dpimpex/save.h>
#include <dpmsg/blend_mode.h>
#include <dpmsg/ids.h>
#include <dpmsg/message.h>
#include <dpmsg/messages.h>
}

#include "libclient/drawdance/canvasstate.h"
#include "libclient/drawdance/global.h"
#include "libclient/net/message.h"

#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <mutex>

#include <klocalizedstring.h>

#include <KoColorSpaceRegistry.h>
#include <kis_adjustment_layer.h>
#include <kis_group_layer.h>
#include <kis_image.h>
#include <kis_layer.h>
#include <kis_paint_device.h>

namespace
{
constexpr int kChunk = 256;

/// Krita's RGBA 8 bytes (B, G, R, A) as premultiplied ARGB32 for PutImage.
/// False if the chunk is fully transparent.
bool chunkToImage(const QByteArray &bytes, int w, int h, QImage &img)
{
    img = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    const uchar *src = reinterpret_cast<const uchar *>(bytes.constData());
    bool any = false;
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const uchar *p = src + (y * w + x) * 4;
            const int a = p[3];
            any = any || a != 0;
            line[x] = qRgba((p[2] * a + 127) / 255, (p[1] * a + 127) / 255, (p[0] * a + 127) / 255, a);
        }
    }
    return any;
}

QString loadError(DP_LoadResult result)
{
    switch (result) {
    case DP_LOAD_RESULT_UNKNOWN_FORMAT:
        return i18n("This is not a file format Drawpile knows.");
    case DP_LOAD_RESULT_OPEN_ERROR:
        return i18n("The file could not be opened.");
    case DP_LOAD_RESULT_RECORDING_INCOMPATIBLE:
        return i18n("This recording was made with an incompatible version of Drawpile.");
    case DP_LOAD_RESULT_BAD_DIMENSIONS:
        return i18n("The canvas in this file is too large or empty.");
    case DP_LOAD_RESULT_CORRUPTED:
        return i18n("The file is damaged.");
    default:
        return i18n("The file could not be read: %1", QString::fromUtf8(DP_error()));
    }
}

/// Applies the visibility of @p hidden (layer id -> hidden) to the layer
/// props, which Drawpile otherwise only changes as local (unshared) state.
void applyHidden(DP_TransientLayerPropsList *tlpl, const QHash<int, bool> &hidden)
{
    const int n = DP_transient_layer_props_list_count(tlpl);
    for (int i = 0; i < n; ++i) {
        DP_LayerProps *lp = DP_transient_layer_props_list_at_noinc(tlpl, i);
        const int id = DP_layer_props_id(lp);
        const bool group = DP_layer_props_children_noinc(lp) != nullptr;
        if (!hidden.value(id) && !group) continue;
        DP_TransientLayerProps *tlp = DP_transient_layer_props_list_transient_at_noinc(tlpl, i);
        if (hidden.value(id)) DP_transient_layer_props_hidden_set(tlp, true);
        if (group) applyHidden(DP_transient_layer_props_transient_children(tlp, 0), hidden);
    }
}
}

void IkDrawpileFiles::ensureEngine()
{
    static std::once_flag once;
    std::call_once(once, []() {
        drawdance::initLogging();
        drawdance::initCpuSupport();
        drawdance::initImageImportExport();
        drawdance::initStaticTiles();
        drawdance::DrawContextPool::init();
    });
}

KisImageSP IkDrawpileFiles::load(const QString &path, QString *error)
{
    ensureEngine();
    DP_LoadResult result = DP_LOAD_RESULT_SUCCESS;
    const drawdance::CanvasState cs = drawdance::CanvasState::load(path, &result);
    if (cs.isNull() || result != DP_LOAD_RESULT_SUCCESS) {
        if (error) *error = loadError(result);
        return KisImageSP();
    }
    KisImageSP image = IkSessionMirror::imageFromCanvasState(cs, QFileInfo(path).completeBaseName());
    if (!image && error) *error = i18n("The canvas in this file is empty.");
    return image;
}

bool IkDrawpileFiles::save(KisImageSP image, const QString &path, QString *error)
{
    ensureEngine();
    if (!image) return false;
    const DP_SaveImageType type = DP_save_image_type_guess(path.toUtf8().constData());
    if (type == DP_SAVE_IMAGE_UNKNOWN) {
        if (error) *error = i18n("Drawpile can not save \"%1\" files.", QFileInfo(path).suffix());
        return false;
    }
    image->waitForDone();
    const int W = image->width(), H = image->height();
    const KoColorSpace *rgb = KoColorSpaceRegistry::instance()->rgb8();

    // The canvas is built the way a session builds it: from the same
    // messages, applied to an empty canvas.
    net::MessageList msgs;
    msgs << net::makeCanvasResizeMessage(1, 0, W, H, 0);
    const QColor bg = image->defaultProjectionColor().toQColor();
    if (bg.alpha() > 0) msgs << net::makeCanvasBackgroundMessage(1, bg);

    QHash<int, bool> hidden;
    int count = 0;
    std::function<void(KisNodeSP, int)> add = [&](KisNodeSP parent, int parentId) {
        for (KisNodeSP n = parent->firstChild(); n; n = n->nextSibling()) { // bottom to top
            KisLayer *layer = qobject_cast<KisLayer *>(n.data());
            if (!layer || qobject_cast<KisAdjustmentLayer *>(layer)) continue; // no Drawpile equivalent
            const int i = count++;
            const uint8_t ctx = uint8_t(1 + (i / 256) % 255);
            const int id = DP_layer_id_make(ctx, i % 256);
            KisGroupLayer *group = qobject_cast<KisGroupLayer *>(layer);
            msgs << net::makeLayerTreeCreateMessage(ctx, uint32_t(id), 0, 0, 0,
                                                    group ? DP_MSG_LAYER_TREE_CREATE_FLAGS_GROUP : 0, layer->name());
            msgs << net::makeLayerTreeMoveMessage(ctx, uint32_t(id), uint32_t(parentId), 0); // on top of its parent
            uint8_t flags = 0;
            if (group && !group->passThroughMode()) flags |= DP_MSG_LAYER_ATTRIBUTES_FLAGS_ISOLATED;
            if (layer->alphaChannelDisabled()) flags |= DP_MSG_LAYER_ATTRIBUTES_FLAGS_CLIP;
            msgs << net::makeLayerAttributesMessage(ctx, uint32_t(id), 0, flags, layer->opacity(),
                                                    uint8_t(IkSessionMirror::blendModeFor(layer->compositeOpId())));
            if (!layer->visible()) hidden.insert(id, true);
            if (group) {
                add(n, id);
                continue;
            }
            // Pixel content (with masks; vector, fill and file layers rendered).
            KisPaintDeviceSP dev = layer->projection();
            if (!dev) continue;
            if (*dev->colorSpace() != *rgb) {
                dev = new KisPaintDevice(*dev);
                dev->convertTo(rgb);
            }
            const QRect bounds = dev->exactBounds() & QRect(0, 0, W, H);
            QByteArray bytes;
            QImage img;
            for (int y = bounds.top() - bounds.top() % kChunk; y <= bounds.bottom(); y += kChunk) {
                for (int x = bounds.left() - bounds.left() % kChunk; x <= bounds.right(); x += kChunk) {
                    const int w = qMin(kChunk, W - x), h = qMin(kChunk, H - y);
                    bytes.resize(w * h * 4);
                    dev->readBytes(reinterpret_cast<quint8 *>(bytes.data()), x, y, w, h);
                    if (!chunkToImage(bytes, w, h, img)) continue;
                    net::makePutImageMessagesCompat(msgs, ctx, uint32_t(id), DP_BLEND_MODE_REPLACE, x, y, img, false);
                }
            }
        }
    };
    add(image->root(), 0);

    drawdance::DrawContext dc = drawdance::DrawContextPool::acquire();
    DP_CanvasState *cs = DP_canvas_state_new();
    for (const net::Message &m : msgs) {
        DP_CanvasState *next = DP_canvas_state_handle(cs, dc.get(), nullptr, m.get());
        if (next) {
            DP_canvas_state_decref(cs);
            cs = next;
        } else {
            qWarning("IKriate: Drawpile export: %s", DP_error());
        }
    }
    if (!hidden.isEmpty()) {
        DP_TransientCanvasState *tcs = DP_transient_canvas_state_new(cs);
        DP_canvas_state_decref(cs);
        applyHidden(DP_transient_canvas_state_transient_layer_props(tcs, 0), hidden);
        cs = DP_transient_canvas_state_persist(tcs);
    }

    // Projects are SQLite databases written by the project code (DP_save
    // only writes the other formats).
    const QByteArray pathBytes = path.toUtf8();
    const DP_SaveResult result =
        type == DP_SAVE_IMAGE_PROJECT
        ? DP_SaveResult(DP_project_save_error_to_save_result(
              DP_project_save_state(cs, pathBytes.constData(), DP_image_write_project_thumbnail, nullptr)))
        : DP_save(cs, dc.get(), type, pathBytes.constData(), nullptr, nullptr, nullptr);
    DP_canvas_state_decref(cs);
    if (result != DP_SAVE_RESULT_SUCCESS) {
        if (error) {
            *error = result == DP_SAVE_RESULT_BAD_DIMENSIONS ? i18n("The image is too large for this format.")
                                                             : i18n("The file could not be written: %1",
                                                                    QString::fromUtf8(DP_error()));
        }
        return false;
    }
    return true;
}
