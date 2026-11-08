/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKSESSIONMIRROR_H
#define IKSESSIONMIRROR_H

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRegion>
#include <QSet>
#include <QTimer>
#include <QVector>

#include <kis_types.h>

#include "libclient/drawdance/canvasstate.h"

class KisDocument;
class Document;

namespace canvas
{
class CanvasModel;
}

/**
 * Keeps a Krita document and a Drawpile canvas (drawdance) the same.
 *
 * Inbound, layers and pixels that changed in the shared canvas are written
 * into the Krita layers (without undo commands). Outbound, pixels and layer
 * changes made with Krita's tools are compared against what the shared canvas
 * last had and sent as Drawpile messages: changed 64 px tiles as PutImage
 * (replace), layer creation, deletion, order and attributes as layer messages.
 *
 * Changes are exchanged only while Krita's image is idle (between strokes),
 * under the image's barrier lock, so neither side writes while the other is
 * painting. Outbound changes are always flushed before inbound ones are
 * applied, so nothing drawn locally is overwritten before it was sent.
 *
 * Only paint layers and groups are shared; other layer types and masks stay
 * local. Layer visibility and alpha lock are local in Drawpile as well.
 */
class IkSessionMirror : public QObject
{
    Q_OBJECT
public:
    IkSessionMirror(Document *doc, QObject *parent = nullptr);
    ~IkSessionMirror() override;

    /// Starts mirroring into @p document (its image is replaced by the
    /// shared canvas on the first update).
    void attach(KisDocument *document);
    void detach();
    KisDocument *document() const;

    /// Builds a Krita image from a Drawpile canvas (also used to import
    /// Drawpile files).
    static KisImageSP imageFromCanvasState(const drawdance::CanvasState &cs, const QString &name);

    /// Krita composite op for a Drawpile blend mode and back.
    static QString compositeOpFor(int blendMode);
    static int blendModeFor(const QString &compositeOp);

    /// True if the last outbound flush or inbound update did anything (for
    /// tests and the status line).
    int pendingLocalTiles() const;

Q_SIGNALS:
    /// The shared canvas now has a size (the Krita document can be shown).
    void ready();

private Q_SLOTS:
    void scheduleTick();
    void tick();
    void imageUpdated(const QRect &rect);

private:
    struct NodeInfo {
        int id {0};
        int parentId {0};
        bool group {false};
        QString name;
        quint8 opacity {255};
        int blend {1};
        bool isolated {true};
        bool clip {false};
        QVector<int> children; ///< bottom to top
        bool operator==(const NodeInfo &o) const
        {
            return id == o.id && parentId == o.parentId && group == o.group && name == o.name
                && opacity == o.opacity && blend == o.blend && isolated == o.isolated && clip == o.clip
                && children == o.children;
        }
        bool operator!=(const NodeInfo &o) const { return !(*this == o); }
    };
    using Snapshot = QHash<int, NodeInfo>; ///< id 0 is the root

    canvas::CanvasModel *canvas() const;
    Snapshot dpSnapshot(const drawdance::CanvasState &cs) const;
    Snapshot kritaSnapshot(QSet<KisNode *> *unmapped) const;

    void flushLocal();
    void flushLocalStructure();
    void flushLocalPixels();
    void applyRemote(const drawdance::CanvasState &cs);
    void applyRemoteStructure(const drawdance::CanvasState &cs, const Snapshot &snap);
    void applyRemotePixels(const drawdance::CanvasState &cs, bool everything);

    quint64 baseHash(int layerId, int tx, int ty);
    void readKritaTile(KisPaintDeviceSP dev, int x, int y, int w, int h, QByteArray &out) const;
    void writeKritaTile(KisPaintDeviceSP dev, const drawdance::CanvasState &cs, void *tile, int x, int y, int w, int h,
                        QByteArray &scratch);

    QPointer<Document> m_doc;
    QPointer<KisDocument> m_document;
    KisImageWSP m_image;
    QTimer m_timer;
    QTimer m_heartbeat;
    drawdance::CanvasState m_mirrored; ///< what the Krita image shows
    Snapshot m_base;                   ///< layer structure both sides agreed on
    QHash<int, KisNode *> m_nodes;     ///< Drawpile layer id -> Krita node
    QHash<KisNode *, int> m_ids;
    QVector<KisNodeSP> m_keep;         ///< keeps mapped nodes alive while mapped
    QHash<quint64, quint64> m_hashes;  ///< (layer, tile) -> hash of the Krita pixels both sides agreed on
    QSet<quint64> m_justSent;          ///< tiles sent but not yet echoed back
    QRegion m_dirty;                   ///< image areas Krita updated since the last flush
    QSet<int> m_newLayers;             ///< local layers created this flush (scan whole layer)
    QElapsedTimer m_awaitStructure;    ///< running while sent layer changes are not yet applied
    bool m_ready {false};
    bool m_writing {false};
    int m_pendingTiles {0};
};

#endif // IKSESSIONMIRROR_H
