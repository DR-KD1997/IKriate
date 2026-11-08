/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkSessionMirror.h"

extern "C" {
#include <dpengine/canvas_state.h>
#include <dpengine/layer_content.h>
#include <dpengine/layer_group.h>
#include <dpengine/layer_list.h>
#include <dpengine/layer_props.h>
#include <dpengine/layer_props_list.h>
#include <dpengine/pixels.h>
#include <dpengine/tile.h>
#include <dpmsg/blend_mode.h>
#include <dpmsg/messages.h>
}

#include "libclient/canvas/canvasmodel.h"
#include "libclient/canvas/layerlist.h"
#include "libclient/canvas/paintengine.h"
#include "libclient/document.h"
#include "libclient/net/client.h"
#include "libclient/net/message.h"

#include <QImage>

#include <klocalizedstring.h>

#include <KisDocument.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>
#include <kis_group_layer.h>
#include <kis_image.h>
#include <kis_node.h>
#include <kis_paint_device.h>
#include <kis_paint_layer.h>

namespace
{
constexpr int kTile = DP_TILE_SIZE;

quint64 tileKey(int layerId, int tx, int ty)
{
    return (quint64(quint32(layerId)) << 32) | (quint64(quint16(tx)) << 16) | quint64(quint16(ty));
}

/// FNV-1a over the tile bytes.
quint64 hashBytes(const QByteArray &bytes)
{
    quint64 h = 1469598103934665603ULL;
    const uchar *p = reinterpret_cast<const uchar *>(bytes.constData());
    for (int i = 0, n = bytes.size(); i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

QString layerTitle(DP_LayerProps *lp)
{
    size_t len = 0;
    const char *t = DP_layer_props_title(lp, &len);
    return t ? QString::fromUtf8(t, int(len)) : QString();
}

quint8 toByteOpacity(uint16_t o)
{
    return quint8(qBound(0, (int(o) * 255 + DP_BIT15 / 2) / DP_BIT15, 255));
}

bool sharedNode(KisNode *n)
{
    return n && (n->inherits("KisPaintLayer") || n->inherits("KisGroupLayer"));
}

/// Converts one Drawpile tile (premultiplied 15 bit, may be null) into
/// Krita's RGBA 8 layout (B, G, R, A unpremultiplied), w x h of it.
void tileToKrita(DP_Tile *tile, int w, int h, QByteArray &out)
{
    out.resize(w * h * 4);
    if (!tile) {
        out.fill(0);
        return;
    }
    static thread_local QVector<DP_UPixel8> row;
    row.resize(kTile * kTile);
    DP_pixels15_to_8_unpremultiply(row.data(), DP_tile_pixels(tile), kTile * kTile);
    char *dst = out.data();
    for (int y = 0; y < h; ++y) {
        memcpy(dst + y * w * 4, row.constData() + y * kTile, size_t(w) * 4);
    }
}

/// Krita's RGBA 8 bytes as a premultiplied ARGB32 image (Drawpile's PutImage).
QImage kritaToImage(const QByteArray &bytes, int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    const uchar *src = reinterpret_cast<const uchar *>(bytes.constData());
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const uchar *p = src + (y * w + x) * 4;
            const int a = p[3];
            line[x] = qRgba((p[2] * a + 127) / 255, (p[1] * a + 127) / 255, (p[0] * a + 127) / 255, a);
        }
    }
    return img;
}

struct BlendPair {
    int dp;
    const QString *krita;
};
}

// ---------------------------------------------------------------- blend modes

QString IkSessionMirror::compositeOpFor(int blendMode)
{
    // Drawpile has an alpha-preserving and an alpha-affecting variant of most
    // modes; Krita's layer modes are the alpha-affecting kind. Both map to
    // the same Krita mode (the case labels are the preserving variants).
    DP_BlendMode affecting, preserving;
    if (blendMode != DP_BLEND_MODE_NORMAL && blendMode != DP_BLEND_MODE_ERASE
        && DP_blend_mode_alpha_preserve_pair(blendMode, &affecting, &preserving) && blendMode == int(affecting)) {
        blendMode = int(preserving);
    }
    switch (blendMode) {
    case DP_BLEND_MODE_MULTIPLY: return COMPOSITE_MULT;
    case DP_BLEND_MODE_DIVIDE: return COMPOSITE_DIVIDE;
    case DP_BLEND_MODE_BURN: return COMPOSITE_BURN;
    case DP_BLEND_MODE_DODGE: return COMPOSITE_DODGE;
    case DP_BLEND_MODE_DARKEN: return COMPOSITE_DARKEN;
    case DP_BLEND_MODE_LIGHTEN: return COMPOSITE_LIGHTEN;
    case DP_BLEND_MODE_SUBTRACT: return COMPOSITE_SUBTRACT;
    case DP_BLEND_MODE_ADD: return COMPOSITE_LINEAR_DODGE;
    case DP_BLEND_MODE_SCREEN: return COMPOSITE_SCREEN;
    case DP_BLEND_MODE_OVERLAY: return COMPOSITE_OVERLAY;
    case DP_BLEND_MODE_HARD_LIGHT: return COMPOSITE_HARD_LIGHT;
    case DP_BLEND_MODE_SOFT_LIGHT: return COMPOSITE_SOFT_LIGHT_PHOTOSHOP;
    case DP_BLEND_MODE_LINEAR_BURN: return COMPOSITE_LINEAR_BURN;
    case DP_BLEND_MODE_LINEAR_LIGHT: return COMPOSITE_LINEAR_LIGHT;
    case DP_BLEND_MODE_HUE: return COMPOSITE_HUE;
    case DP_BLEND_MODE_SATURATION: return COMPOSITE_SATURATION;
    case DP_BLEND_MODE_LUMINOSITY: return COMPOSITE_LUMINIZE;
    case DP_BLEND_MODE_COLOR: return COMPOSITE_COLOR;
    case DP_BLEND_MODE_VIVID_LIGHT: return COMPOSITE_VIVID_LIGHT;
    case DP_BLEND_MODE_PIN_LIGHT: return COMPOSITE_PIN_LIGHT;
    case DP_BLEND_MODE_DIFFERENCE: return COMPOSITE_DIFF;
    case DP_BLEND_MODE_DARKER_COLOR: return COMPOSITE_DARKER_COLOR;
    case DP_BLEND_MODE_LIGHTER_COLOR: return COMPOSITE_LIGHTER_COLOR;
    case DP_BLEND_MODE_BEHIND: return COMPOSITE_BEHIND;
    case DP_BLEND_MODE_GREATER: return COMPOSITE_GREATER;
    case DP_BLEND_MODE_ERASE: return COMPOSITE_ERASE;
    default: return COMPOSITE_OVER;
    }
}

int IkSessionMirror::blendModeFor(const QString &op)
{
    static const QHash<QString, int> map = {
        {COMPOSITE_MULT, DP_BLEND_MODE_MULTIPLY},     {COMPOSITE_DIVIDE, DP_BLEND_MODE_DIVIDE},
        {COMPOSITE_BURN, DP_BLEND_MODE_BURN},         {COMPOSITE_DODGE, DP_BLEND_MODE_DODGE},
        {COMPOSITE_DARKEN, DP_BLEND_MODE_DARKEN},     {COMPOSITE_LIGHTEN, DP_BLEND_MODE_LIGHTEN},
        {COMPOSITE_SUBTRACT, DP_BLEND_MODE_SUBTRACT}, {COMPOSITE_LINEAR_DODGE, DP_BLEND_MODE_ADD},
        {COMPOSITE_ADD, DP_BLEND_MODE_ADD},           {COMPOSITE_SCREEN, DP_BLEND_MODE_SCREEN},
        {COMPOSITE_OVERLAY, DP_BLEND_MODE_OVERLAY},   {COMPOSITE_HARD_LIGHT, DP_BLEND_MODE_HARD_LIGHT},
        {COMPOSITE_SOFT_LIGHT_PHOTOSHOP, DP_BLEND_MODE_SOFT_LIGHT},
        {COMPOSITE_LINEAR_BURN, DP_BLEND_MODE_LINEAR_BURN},
        {COMPOSITE_LINEAR_LIGHT, DP_BLEND_MODE_LINEAR_LIGHT},
        {COMPOSITE_HUE, DP_BLEND_MODE_HUE},           {COMPOSITE_SATURATION, DP_BLEND_MODE_SATURATION},
        {COMPOSITE_LUMINIZE, DP_BLEND_MODE_LUMINOSITY}, {COMPOSITE_COLOR, DP_BLEND_MODE_COLOR},
        {COMPOSITE_VIVID_LIGHT, DP_BLEND_MODE_VIVID_LIGHT}, {COMPOSITE_PIN_LIGHT, DP_BLEND_MODE_PIN_LIGHT},
        {COMPOSITE_DIFF, DP_BLEND_MODE_DIFFERENCE},   {COMPOSITE_DARKER_COLOR, DP_BLEND_MODE_DARKER_COLOR},
        {COMPOSITE_LIGHTER_COLOR, DP_BLEND_MODE_LIGHTER_COLOR}, {COMPOSITE_BEHIND, DP_BLEND_MODE_BEHIND},
        {COMPOSITE_GREATER, DP_BLEND_MODE_GREATER},   {COMPOSITE_ERASE, DP_BLEND_MODE_ERASE},
    };
    const int mode = map.value(op, DP_BLEND_MODE_NORMAL);
    // A Krita layer in multiply etc. also covers where the layers below are
    // transparent: Drawpile's alpha-affecting variant.
    DP_BlendMode affecting, preserving;
    if (mode != DP_BLEND_MODE_ERASE && DP_blend_mode_alpha_preserve_pair(mode, &affecting, &preserving)) {
        return int(affecting);
    }
    return mode;
}

// --------------------------------------------------------------------- setup

IkSessionMirror::IkSessionMirror(Document *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &IkSessionMirror::tick);
    // Catches anything a signal missed (and layer edits, which Krita does not
    // report as image updates).
    connect(&m_heartbeat, &QTimer::timeout, this, &IkSessionMirror::scheduleTick);
}

IkSessionMirror::~IkSessionMirror()
{
    detach();
}

canvas::CanvasModel *IkSessionMirror::canvas() const
{
    return m_doc ? m_doc->canvas() : nullptr;
}

KisDocument *IkSessionMirror::document() const
{
    return m_document;
}

int IkSessionMirror::pendingLocalTiles() const
{
    return m_pendingTiles;
}

void IkSessionMirror::attach(KisDocument *document)
{
    detach();
    m_document = document;
    m_image = document ? KisImageWSP(document->image()) : KisImageWSP();
    m_mirrored = drawdance::CanvasState::null();
    m_base.clear();
    m_base.insert(0, NodeInfo());
    m_nodes.clear();
    m_ids.clear();
    m_keep.clear();
    m_hashes.clear();
    m_justSent.clear();
    m_dirty = QRegion();
    m_ready = false;
    if (KisImageSP image = m_image.toStrongRef()) {
        connect(image.data(), &KisImage::sigImageUpdated, this, &IkSessionMirror::imageUpdated);
        // The image starts as a placeholder: its own layers are replaced by
        // the shared ones.
        image->barrierLock();
        const KisNodeSP root = image->root();
        QVector<KisNodeSP> old;
        for (KisNodeSP c = root->firstChild(); c; c = c->nextSibling()) old << c;
        for (KisNodeSP c : old) image->removeNode(c);
        image->unlock();
    }
    if (canvas::CanvasModel *cm = canvas()) {
        connect(cm->paintEngine(), &canvas::PaintEngine::areaChanged, this, &IkSessionMirror::scheduleTick);
        connect(cm->paintEngine(), &canvas::PaintEngine::layersChanged, this, &IkSessionMirror::scheduleTick);
        connect(cm->paintEngine(), &canvas::PaintEngine::resized, this, &IkSessionMirror::scheduleTick);
        connect(cm->paintEngine(), &canvas::PaintEngine::tileCacheDirtyCheckNeeded, this, &IkSessionMirror::scheduleTick);
    }
    m_heartbeat.start(100);
    scheduleTick();
}

void IkSessionMirror::detach()
{
    m_timer.stop();
    m_heartbeat.stop();
    if (KisImageSP image = m_image.toStrongRef()) {
        disconnect(image.data(), nullptr, this, nullptr);
    }
    if (canvas::CanvasModel *cm = canvas()) {
        disconnect(cm->paintEngine(), nullptr, this, nullptr);
    }
    m_document = nullptr;
    m_image = KisImageWSP();
    m_keep.clear();
    m_nodes.clear();
    m_ids.clear();
}

void IkSessionMirror::scheduleTick()
{
    if (!m_timer.isActive()) m_timer.start();
}

void IkSessionMirror::imageUpdated(const QRect &rect)
{
    if (m_writing) return; // our own writes
    m_dirty += rect;
    scheduleTick();
}

// ---------------------------------------------------------------------- tick

void IkSessionMirror::tick()
{
    KisImageSP image = m_image.toStrongRef();
    canvas::CanvasModel *cm = canvas();
    if (!image || !cm) return;
    const drawdance::CanvasState cs = cm->paintEngine()->historyCanvasState();
    if (cs.isNull() || cs.width() <= 0 || cs.height() <= 0) {
        if (qEnvironmentVariableIsSet("IKRIATE_DEBUG_ONLINE")) qDebug("IKriate online: canvas not ready");
        m_timer.start(200);
        return;
    }
    // Only between strokes: the barrier lock waits for nothing and fails
    // while Krita is still painting, and keeps Krita from starting until the
    // exchange is done.
    if (!image->tryBarrierLock()) {
        m_timer.start(60);
        return;
    }
    m_writing = true;
    if (m_ready) flushLocal();
    applyRemote(cs);
    if (qEnvironmentVariableIsSet("IKRIATE_DEBUG_ONLINE")) {
        qDebug("IKriate online: tick %dx%d image %dx%d layers %d sent %d", cs.width(), cs.height(), image->width(),
               image->height(), int(m_nodes.size()), m_pendingTiles);
    }
    m_writing = false;
    image->unlock();
    if (!m_ready) {
        m_ready = true;
        Q_EMIT ready();
    }
}

// ------------------------------------------------------------------ snapshots

IkSessionMirror::Snapshot IkSessionMirror::dpSnapshot(const drawdance::CanvasState &cs) const
{
    Snapshot s;
    s.insert(0, NodeInfo());
    std::function<void(DP_LayerPropsList *, int)> walk = [&](DP_LayerPropsList *lpl, int parent) {
        const int n = DP_layer_props_list_count(lpl);
        for (int i = 0; i < n; ++i) {
            DP_LayerProps *lp = DP_layer_props_list_at_noinc(lpl, i);
            NodeInfo info;
            info.id = DP_layer_props_id(lp);
            info.parentId = parent;
            DP_LayerPropsList *children = DP_layer_props_children_noinc(lp);
            info.group = children != nullptr;
            info.name = layerTitle(lp);
            info.opacity = toByteOpacity(DP_layer_props_opacity(lp));
            info.blend = DP_layer_props_blend_mode(lp);
            info.isolated = info.group ? DP_layer_props_isolated(lp) : true;
            info.clip = DP_layer_props_clip(lp);
            s[parent].children.append(info.id);
            s.insert(info.id, info);
            if (children) walk(children, info.id);
        }
    };
    walk(DP_canvas_state_layer_props_noinc(cs.get()), 0);
    return s;
}

IkSessionMirror::Snapshot IkSessionMirror::kritaSnapshot(QSet<KisNode *> *unmapped) const
{
    Snapshot s;
    s.insert(0, NodeInfo());
    KisImageSP image = m_image.toStrongRef();
    if (!image) return s;
    std::function<void(KisNodeSP, int)> walk = [&](KisNodeSP parent, int parentId) {
        for (KisNodeSP c = parent->firstChild(); c; c = c->nextSibling()) {
            if (!sharedNode(c.data())) continue;
            const int id = m_ids.value(c.data(), 0);
            if (!id) {
                if (unmapped) unmapped->insert(c.data());
                continue;
            }
            NodeInfo info;
            info.id = id;
            info.parentId = parentId;
            KisGroupLayer *group = qobject_cast<KisGroupLayer *>(c.data());
            info.group = group != nullptr;
            info.name = c->name();
            info.opacity = c->opacity();
            info.blend = blendModeFor(c->compositeOpId());
            info.isolated = group ? !group->passThroughMode() : true;
            if (KisLayer *l = qobject_cast<KisLayer *>(c.data())) info.clip = l->alphaChannelDisabled();
            s[parentId].children.append(id);
            s.insert(id, info);
            if (group) walk(c, id);
        }
    };
    walk(image->root(), 0);
    return s;
}

// ------------------------------------------------------------------ outbound

void IkSessionMirror::flushLocal()
{
    m_newLayers.clear();
    flushLocalStructure();
    flushLocalPixels();
    m_dirty = QRegion();
}

void IkSessionMirror::flushLocalStructure()
{
    canvas::CanvasModel *cm = canvas();
    KisImageSP image = m_image.toStrongRef();
    if (!cm || !image) return;
    const uint8_t ctx = cm->localUserId();
    net::MessageList msgs;

    // Layers made with Krita's tools: give them ids and create them.
    QSet<KisNode *> unmapped;
    kritaSnapshot(&unmapped);
    if (!unmapped.isEmpty()) {
        // In tree order, so groups exist before what is in them.
        QVector<KisNode *> order;
        std::function<void(KisNodeSP)> collect = [&](KisNodeSP parent) {
            for (KisNodeSP c = parent->firstChild(); c; c = c->nextSibling()) {
                if (!sharedNode(c.data())) continue;
                if (unmapped.contains(c.data())) order << c.data();
                if (c->inherits("KisGroupLayer")) collect(c);
            }
        };
        collect(image->root());
        QSet<int> taken;
        for (auto it = m_ids.constBegin(); it != m_ids.constEnd(); ++it) taken.insert(it.value());
        const QVector<int> ids = cm->layerlist()->getAvailableLayerIds(order.size(), &taken);
        for (int i = 0; i < order.size() && i < ids.size(); ++i) {
            KisNode *n = order[i];
            const int id = ids[i];
            const bool group = n->inherits("KisGroupLayer");
            m_ids.insert(n, id);
            m_nodes.insert(id, n);
            m_keep << KisNodeSP(n);
            m_newLayers.insert(id);
            msgs << net::makeLayerTreeCreateMessage(ctx, uint32_t(id), 0, 0, 0,
                                                    group ? DP_MSG_LAYER_TREE_CREATE_FLAGS_GROUP : 0, n->name());
            NodeInfo def;
            def.id = id;
            def.group = group;
            def.name = n->name();
            m_base.insert(id, def); // created with default attributes, at the top of the root
            m_base[0].children.append(id);
        }
    }

    const Snapshot k = kritaSnapshot(nullptr);

    // Deleted layers (not those inside a deleted group).
    for (auto it = m_base.constBegin(); it != m_base.constEnd(); ++it) {
        if (it.key() == 0 || k.contains(it.key())) continue;
        const int parent = it->parentId;
        if (parent != 0 && !k.contains(parent)) continue;
        msgs << net::makeLayerTreeDeleteMessage(ctx, uint32_t(it.key()), 0);
    }

    // Attributes.
    for (auto it = k.constBegin(); it != k.constEnd(); ++it) {
        if (it.key() == 0) continue;
        const NodeInfo &now = it.value();
        const NodeInfo was = m_base.value(it.key());
        if (now.name != was.name) msgs << net::makeLayerRetitleMessage(ctx, uint32_t(now.id), now.name);
        if (now.opacity != was.opacity || now.blend != was.blend || now.isolated != was.isolated || now.clip != was.clip) {
            uint8_t flags = 0;
            if (now.group && now.isolated) flags |= DP_MSG_LAYER_ATTRIBUTES_FLAGS_ISOLATED;
            if (now.clip) flags |= DP_MSG_LAYER_ATTRIBUTES_FLAGS_CLIP;
            msgs << net::makeLayerAttributesMessage(ctx, uint32_t(now.id), 0, flags, now.opacity, uint8_t(now.blend));
        }
    }

    // Order: every group whose children changed is rebuilt bottom to top
    // (a move with no sibling puts the layer on top of its parent).
    for (auto it = k.constBegin(); it != k.constEnd(); ++it) {
        const NodeInfo was = m_base.value(it.key());
        bool reparented = false;
        for (int c : it->children) {
            if (m_base.value(c).parentId != it.key()) reparented = true;
        }
        if (it->children == was.children && !reparented) continue;
        for (int c : it->children) {
            msgs << net::makeLayerTreeMoveMessage(ctx, uint32_t(c), uint32_t(it.key()), 0);
        }
    }

    // Forget deleted nodes.
    for (auto it = m_base.constBegin(); it != m_base.constEnd(); ++it) {
        if (it.key() != 0 && !k.contains(it.key())) {
            KisNode *n = m_nodes.take(it.key());
            m_ids.remove(n);
            for (int i = m_keep.size() - 1; i >= 0; --i) {
                if (m_keep[i].data() == n) m_keep.remove(i);
            }
        }
    }

    if (!msgs.isEmpty()) {
        msgs.prepend(net::makeUndoPointMessage(ctx));
        m_doc->client()->sendCommands(msgs.size(), msgs.constData());
        m_base = k; // assume the server accepts; a rejection rolls back and is mirrored
        // The shared canvas applies them a moment later (on its own thread):
        // until then it still shows the old layers, which must not be
        // mirrored back over the new ones.
        m_awaitStructure.start();
    }
}

quint64 IkSessionMirror::baseHash(int layerId, int tx, int ty)
{
    const quint64 key = tileKey(layerId, tx, ty);
    auto it = m_hashes.constFind(key);
    if (it != m_hashes.constEnd()) return it.value();
    // Never written: what the mirrored canvas has there.
    QByteArray bytes;
    const int x = tx * kTile, y = ty * kTile;
    const int w = qMin(kTile, m_mirrored.width() - x), h = qMin(kTile, m_mirrored.height() - y);
    DP_Tile *tile = nullptr;
    if (!m_mirrored.isNull()) {
        const drawdance::LayerSearchResult r = m_mirrored.searchLayer(layerId, true);
        if (const drawdance::LayerContent *lc = std::get_if<drawdance::LayerContent>(&r.data)) {
            tile = DP_layer_content_tile_at_noinc(lc->get(), tx, ty);
        }
    }
    tileToKrita(tile, qMax(1, w), qMax(1, h), bytes);
    const quint64 h64 = hashBytes(bytes);
    m_hashes.insert(key, h64);
    return h64;
}

void IkSessionMirror::readKritaTile(KisPaintDeviceSP dev, int x, int y, int w, int h, QByteArray &out) const
{
    out.resize(w * h * 4);
    dev->readBytes(reinterpret_cast<quint8 *>(out.data()), x, y, w, h);
}

void IkSessionMirror::flushLocalPixels()
{
    canvas::CanvasModel *cm = canvas();
    KisImageSP image = m_image.toStrongRef();
    if (!cm || !image || m_mirrored.isNull()) return;
    const uint8_t ctx = cm->localUserId();
    const bool compat = m_doc->isCompatibilityMode();
    const int W = image->width(), H = image->height();
    net::MessageList msgs;
    QByteArray bytes;
    m_pendingTiles = 0;

    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        KisPaintLayer *layer = qobject_cast<KisPaintLayer *>(it.value());
        if (!layer) continue;
        const int id = it.key();
        QRegion region = m_dirty;
        if (m_newLayers.contains(id)) region += layer->paintDevice()->exactBounds();
        region &= QRect(0, 0, W, H);
        if (region.isEmpty()) continue;
        QSet<quint64> seen;
        for (const QRect &r : region) {
            for (int ty = r.top() / kTile; ty <= r.bottom() / kTile; ++ty) {
                for (int tx = r.left() / kTile; tx <= r.right() / kTile; ++tx) {
                    const quint64 key = tileKey(id, tx, ty);
                    if (seen.contains(key)) continue;
                    seen.insert(key);
                    const int x = tx * kTile, y = ty * kTile;
                    const int w = qMin(kTile, W - x), h = qMin(kTile, H - y);
                    if (w <= 0 || h <= 0) continue;
                    readKritaTile(layer->paintDevice(), x, y, w, h, bytes);
                    const quint64 now = hashBytes(bytes);
                    if (now == baseHash(id, tx, ty)) continue;
                    net::makePutImageMessagesCompat(msgs, ctx, uint32_t(id), DP_BLEND_MODE_REPLACE, x, y,
                                                    kritaToImage(bytes, w, h), compat);
                    m_hashes.insert(key, now);
                    m_justSent.insert(key);
                    ++m_pendingTiles;
                }
            }
        }
    }
    if (!msgs.isEmpty()) {
        if (m_newLayers.isEmpty()) msgs.prepend(net::makeUndoPointMessage(ctx));
        m_doc->client()->sendCommands(msgs.size(), msgs.constData());
    }
}

// ------------------------------------------------------------------- inbound

void IkSessionMirror::applyRemote(const drawdance::CanvasState &cs)
{
    KisImageSP image = m_image.toStrongRef();
    if (!image) return;
    bool everything = m_mirrored.isNull();
    if (image->width() != cs.width() || image->height() != cs.height()) {
        // Not undoable on purpose: the shared canvas decides the size.
        image->unlock();
        image->resizeImage(QRect(0, 0, cs.width(), cs.height()));
        image->waitForDone();
        image->barrierLock();
        everything = true;
        m_hashes.clear();
    }
    if (everything || DP_canvas_state_layer_props_noinc(cs.get()) != DP_canvas_state_layer_props_noinc(m_mirrored.get())) {
        const Snapshot snap = dpSnapshot(cs);
        if (m_awaitStructure.isValid()) {
            if (snap == m_base || m_awaitStructure.elapsed() > 3000) m_awaitStructure.invalidate();
        }
        if (everything || (snap != m_base && !m_awaitStructure.isValid())) applyRemoteStructure(cs, snap);
    }
    applyRemotePixels(cs, everything);
    // The canvas background is not a layer in Drawpile: shown as the colour
    // behind Krita's layers.
    DP_Tile *bg = DP_canvas_state_background_tile_noinc(cs.get());
    if (everything || bg != DP_canvas_state_background_tile_noinc(m_mirrored.get())) {
        QColor c(Qt::transparent);
        if (bg && !DP_tile_blank(bg)) {
            const DP_UPixel8 px = DP_pixel8_unpremultiply(DP_pixel15_to_8(DP_tile_pixel_at(bg, 0, 0)));
            c = QColor::fromRgba(px.color); // B, G, R, A in memory: 0xAARRGGBB

        }
        image->setDefaultProjectionColor(KoColor(c, KoColorSpaceRegistry::instance()->rgb8()));
        image->refreshGraphAsync();
    }
    m_mirrored = cs;
}

void IkSessionMirror::applyRemoteStructure(const drawdance::CanvasState &cs, const Snapshot &snap)
{
    Q_UNUSED(cs);
    KisImageSP image = m_image.toStrongRef();
    const KoColorSpace *rgb = KoColorSpaceRegistry::instance()->rgb8();

    // Remove what the shared canvas no longer has.
    for (auto it = m_nodes.begin(); it != m_nodes.end();) {
        if (!snap.contains(it.key())) {
            KisNodeSP n(it.value());
            m_ids.remove(it.value());
            for (int i = m_keep.size() - 1; i >= 0; --i) {
                if (m_keep[i] == n) m_keep.remove(i);
            }
            if (n->parent()) image->removeNode(n);
            it = m_nodes.erase(it);
        } else {
            ++it;
        }
    }

    // Create, configure and order, top-down from the root.
    std::function<void(int, KisNodeSP)> build = [&](int parentId, KisNodeSP parentNode) {
        const QVector<int> &children = snap.value(parentId).children;
        KisNodeSP below; // previous child (bottom to top)
        for (int id : children) {
            const NodeInfo &info = snap.value(id);
            KisNodeSP node(m_nodes.value(id));
            if (node && bool(qobject_cast<KisGroupLayer *>(node.data())) != info.group) {
                // Type changed (should not happen): replace it.
                m_ids.remove(node.data());
                if (node->parent()) image->removeNode(node);
                node = nullptr;
            }
            if (!node) {
                if (info.group) node = new KisGroupLayer(image, info.name, info.opacity);
                else node = new KisPaintLayer(image, info.name, info.opacity, rgb);
                m_nodes.insert(id, node.data());
                m_ids.insert(node.data(), id);
                m_keep << node;
            }
            if (node->name() != info.name) node->setName(info.name);
            if (node->opacity() != info.opacity) node->setOpacity(info.opacity);
            const QString op = compositeOpFor(info.blend);
            if (node->compositeOpId() != op) {
                if (KisLayer *l = qobject_cast<KisLayer *>(node.data())) l->setCompositeOpId(op);
            }
            if (KisGroupLayer *g = qobject_cast<KisGroupLayer *>(node.data())) {
                if (g->passThroughMode() == info.isolated) g->setPassThroughMode(!info.isolated);
            }
            if (KisLayer *l = qobject_cast<KisLayer *>(node.data())) {
                if (l->alphaChannelDisabled() != info.clip) l->disableAlphaChannel(info.clip);
            }
            // Place it right above the previous one (or at the bottom).
            if (!node->parent()) {
                if (below) image->addNode(node, parentNode, below);
                else image->addNode(node, parentNode, quint32(0));
            } else if (node->parent() != parentNode || node->prevSibling() != below) {
                if (below) image->moveNode(node, parentNode, below);
                else image->moveNode(node, parentNode, quint32(0));
            }
            if (info.group) build(id, node);
            below = node;
        }
    };
    build(0, image->root());
    m_base = snap;
}

void IkSessionMirror::writeKritaTile(KisPaintDeviceSP dev, const drawdance::CanvasState &, void *tile, int x, int y, int w,
                                     int h, QByteArray &scratch)
{
    tileToKrita(static_cast<DP_Tile *>(tile), w, h, scratch);
    dev->writeBytes(reinterpret_cast<const quint8 *>(scratch.constData()), x, y, w, h);
}

void IkSessionMirror::applyRemotePixels(const drawdance::CanvasState &cs, bool everything)
{
    KisImageSP image = m_image.toStrongRef();
    const int W = cs.width(), H = cs.height();
    const int cols = (W + kTile - 1) / kTile, rows = (H + kTile - 1) / kTile;
    QByteArray scratch;
    QVector<QPair<KisNodeSP, QRect>> dirty;

    for (auto it = m_nodes.constBegin(); it != m_nodes.constEnd(); ++it) {
        KisPaintLayer *layer = qobject_cast<KisPaintLayer *>(it.value());
        if (!layer) continue;
        const int id = it.key();
        const drawdance::LayerSearchResult now = cs.searchLayer(id, true);
        const drawdance::LayerContent *lc = std::get_if<drawdance::LayerContent>(&now.data);
        if (!lc) continue;
        DP_LayerContent *prevLc = nullptr;
        if (!everything && !m_mirrored.isNull()) {
            const drawdance::LayerSearchResult was = m_mirrored.searchLayer(id, true);
            if (const drawdance::LayerContent *p = std::get_if<drawdance::LayerContent>(&was.data)) prevLc = p->get();
            if (prevLc == lc->get()) continue; // unchanged layer
        }
        QRect changed;
        for (int ty = 0; ty < rows; ++ty) {
            for (int tx = 0; tx < cols; ++tx) {
                DP_Tile *t = DP_layer_content_tile_at_noinc(lc->get(), tx, ty);
                const quint64 key = tileKey(id, tx, ty);
                if (prevLc) {
                    DP_Tile *p = DP_layer_content_tile_at_noinc(prevLc, tx, ty);
                    if (p == t) continue;
                } else if (!t && everything) {
                    continue; // new layers start transparent
                }
                const int x = tx * kTile, y = ty * kTile;
                const int w = qMin(kTile, W - x), h = qMin(kTile, H - y);
                writeKritaTile(layer->paintDevice(), cs, t, x, y, w, h, scratch);
                m_hashes.insert(key, hashBytes(scratch));
                m_justSent.remove(key);
                changed |= QRect(x, y, w, h);
            }
        }
        if (!changed.isEmpty()) dirty << qMakePair(KisNodeSP(layer), changed);
    }
    if (everything) {
        image->refreshGraphAsync();
    } else {
        for (auto &d : dirty) d.first->setDirty(d.second);
    }
}

// --------------------------------------------------------------------- import

KisImageSP IkSessionMirror::imageFromCanvasState(const drawdance::CanvasState &cs, const QString &name)
{
    if (cs.isNull() || cs.width() <= 0 || cs.height() <= 0) return KisImageSP();
    const KoColorSpace *rgb = KoColorSpaceRegistry::instance()->rgb8();
    KisImageSP image = new KisImage(nullptr, cs.width(), cs.height(), rgb, name);
    const int W = cs.width(), H = cs.height();
    const int cols = (W + kTile - 1) / kTile, rows = (H + kTile - 1) / kTile;
    QByteArray scratch;

    std::function<void(DP_LayerPropsList *, DP_LayerList *, KisNodeSP)> build =
        [&](DP_LayerPropsList *lpl, DP_LayerList *ll, KisNodeSP parent) {
            const int n = DP_layer_props_list_count(lpl);
            for (int i = 0; i < n; ++i) {
                DP_LayerProps *lp = DP_layer_props_list_at_noinc(lpl, i);
                DP_LayerListEntry *lle = DP_layer_list_at_noinc(ll, i);
                const QString title = layerTitle(lp);
                const quint8 opacity = toByteOpacity(DP_layer_props_opacity(lp));
                KisLayerSP layer;
                if (DP_layer_list_entry_is_group(lle)) {
                    KisGroupLayerSP g = new KisGroupLayer(image, title, opacity);
                    g->setPassThroughMode(!DP_layer_props_isolated(lp));
                    image->addNode(g, parent);
                    build(DP_layer_props_children_noinc(lp), DP_layer_group_children_noinc(DP_layer_list_entry_group_noinc(lle)), g);
                    layer = g;
                } else {
                    KisPaintLayerSP p = new KisPaintLayer(image, title, opacity, rgb);
                    DP_LayerContent *lc = DP_layer_list_entry_content_noinc(lle);
                    for (int ty = 0; ty < rows; ++ty) {
                        for (int tx = 0; tx < cols; ++tx) {
                            DP_Tile *t = DP_layer_content_tile_at_noinc(lc, tx, ty);
                            if (!t) continue;
                            const int x = tx * kTile, y = ty * kTile;
                            const int w = qMin(kTile, W - x), h = qMin(kTile, H - y);
                            tileToKrita(t, w, h, scratch);
                            p->paintDevice()->writeBytes(reinterpret_cast<const quint8 *>(scratch.constData()), x, y, w, h);
                        }
                    }
                    image->addNode(p, parent);
                    layer = p;
                }
                layer->setCompositeOpId(compositeOpFor(DP_layer_props_blend_mode(lp)));
                layer->setVisible(!DP_layer_props_hidden(lp));
                if (DP_layer_props_clip(lp)) layer->disableAlphaChannel(true);
            }
        };
    build(DP_canvas_state_layer_props_noinc(cs.get()), DP_canvas_state_layers_noinc(cs.get()), image->root());

    // The canvas background (Drawpile keeps it apart from the layers).
    if (DP_Tile *bg = DP_canvas_state_background_tile_noinc(cs.get())) {
        if (!DP_tile_blank(bg)) {
            KisPaintLayerSP b = new KisPaintLayer(image, i18n("Background"), OPACITY_OPAQUE_U8, rgb);
            for (int ty = 0; ty < rows; ++ty) {
                for (int tx = 0; tx < cols; ++tx) {
                    const int x = tx * kTile, y = ty * kTile;
                    const int w = qMin(kTile, W - x), h = qMin(kTile, H - y);
                    tileToKrita(bg, w, h, scratch);
                    b->paintDevice()->writeBytes(reinterpret_cast<const quint8 *>(scratch.constData()), x, y, w, h);
                }
            }
            image->addNode(b, image->root(), quint32(0));
        }
    }
    image->refreshGraphAsync();
    image->waitForDone();
    return image;
}
