/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGifWriter.h"

#include <QFile>
#include <QImage>
#include <QVector>
#include <algorithm>
#include <cstring>

#include <gif_lib.h>

namespace
{
/// 5 bits per channel: the colour space the palette is chosen in.
inline int key15(QRgb c)
{
    return ((qRed(c) >> 3) << 10) | ((qGreen(c) >> 3) << 5) | (qBlue(c) >> 3);
}

struct Box {
    int begin, end; // into the colour list
    int lo[3], hi[3];
    qint64 count;
    int longest() const
    {
        int a = 0;
        for (int i = 1; i < 3; ++i) {
            if (hi[i] - lo[i] > hi[a] - lo[a]) a = i;
        }
        return a;
    }
    int span() const { return hi[longest()] - lo[longest()]; }
};

struct Entry {
    int c[3];
    qint64 n;
};

void shrink(Box &b, const QVector<Entry> &e)
{
    for (int i = 0; i < 3; ++i) {
        b.lo[i] = 255;
        b.hi[i] = 0;
    }
    b.count = 0;
    for (int k = b.begin; k < b.end; ++k) {
        for (int i = 0; i < 3; ++i) {
            b.lo[i] = qMin(b.lo[i], e[k].c[i]);
            b.hi[i] = qMax(b.hi[i], e[k].c[i]);
        }
        b.count += e[k].n;
    }
}

/// Median cut over the image's histogram; returns the palette and the
/// palette index for every 15-bit colour present.
void palette(const QImage &img, QVector<QRgb> &pal, QVector<quint8> &lookup)
{
    QVector<qint64> hist(32768, 0);
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            ++hist[key15(line[x])];
        }
    }
    QVector<Entry> e;
    QVector<int> keys;
    for (int k = 0; k < 32768; ++k) {
        if (hist[k]) {
            e.append({{((k >> 10) & 31) * 255 / 31, ((k >> 5) & 31) * 255 / 31, (k & 31) * 255 / 31}, hist[k]});
            keys.append(k);
        }
    }
    // Keep each entry's key next to it while sorting.
    QVector<int> order(e.size());
    for (int i = 0; i < order.size(); ++i) order[i] = i;

    QVector<Box> boxes;
    Box all{0, int(e.size()), {0, 0, 0}, {0, 0, 0}, 0};
    QVector<Entry> se = e;
    shrink(all, se);
    boxes.append(all);
    while (boxes.size() < 256) {
        // Split the box with the most pixels times the widest range.
        int best = -1;
        qint64 bestScore = 0;
        for (int i = 0; i < boxes.size(); ++i) {
            if (boxes[i].end - boxes[i].begin < 2) continue;
            const qint64 score = boxes[i].count * qint64(boxes[i].span() + 1);
            if (score > bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best < 0) break;
        Box b = boxes[best];
        const int axis = b.longest();
        std::sort(order.begin() + b.begin, order.begin() + b.end,
                  [&](int x, int y) { return e[x].c[axis] < e[y].c[axis]; });
        for (int k = b.begin; k < b.end; ++k) se[k] = e[order[k]];
        // The split point where half the pixels are on each side.
        qint64 half = b.count / 2, acc = 0;
        int mid = b.begin;
        while (mid < b.end - 1 && acc + se[mid].n <= half) acc += se[mid++].n;
        if (mid == b.begin) mid = b.begin + 1;
        Box l{b.begin, mid, {0, 0, 0}, {0, 0, 0}, 0}, r{mid, b.end, {0, 0, 0}, {0, 0, 0}, 0};
        shrink(l, se);
        shrink(r, se);
        boxes[best] = l;
        boxes.append(r);
    }

    pal.clear();
    lookup.fill(0, 32768);
    for (int i = 0; i < boxes.size(); ++i) {
        qint64 s[3] = {0, 0, 0}, n = 0;
        for (int k = boxes[i].begin; k < boxes[i].end; ++k) {
            const Entry &x = e[order[k]];
            for (int j = 0; j < 3; ++j) s[j] += qint64(x.c[j]) * x.n;
            n += x.n;
            lookup[keys[order[k]]] = quint8(i);
        }
        n = qMax<qint64>(n, 1);
        pal.append(qRgb(int(s[0] / n), int(s[1] / n), int(s[2] / n)));
    }
}
}

KisGifWriter::~KisGifWriter()
{
    if (m_gif) {
        int err = 0;
        EGifCloseFile(m_gif, &err);
    }
}

bool KisGifWriter::fail(const QString &what, int code)
{
    const char *msg = GifErrorString(code);
    m_error = QStringLiteral("%1: %2").arg(what, msg ? QString::fromLatin1(msg) : QString::number(code));
    return false;
}

bool KisGifWriter::open(const QString &path, int width, int height, bool loop)
{
    int err = 0;
    m_gif = EGifOpenFileName(QFile::encodeName(path).constData(), false, &err);
    if (!m_gif) return fail(QStringLiteral("open"), err);
    m_width = width;
    m_height = height;
    EGifSetGifVersion(m_gif, true);
    if (EGifPutScreenDesc(m_gif, width, height, 8, 0, nullptr) == GIF_ERROR) {
        return fail(QStringLiteral("screen"), m_gif->Error);
    }
    if (loop) {
        static const GifByteType netscape[] = "NETSCAPE2.0";
        static const GifByteType repeat[] = {1, 0, 0}; // forever
        if (EGifPutExtensionLeader(m_gif, APPLICATION_EXT_FUNC_CODE) == GIF_ERROR
            || EGifPutExtensionBlock(m_gif, 11, netscape) == GIF_ERROR
            || EGifPutExtensionBlock(m_gif, 3, repeat) == GIF_ERROR || EGifPutExtensionTrailer(m_gif) == GIF_ERROR) {
            return fail(QStringLiteral("loop"), m_gif->Error);
        }
    }
    return true;
}

bool KisGifWriter::addFrame(const QImage &image, int delay)
{
    if (!m_gif) return false;
    QImage img = image.size() == QSize(m_width, m_height)
        ? image
        : image.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_RGB32);

    QVector<QRgb> pal;
    QVector<quint8> lookup;
    palette(img, pal, lookup);
    int bits = 1;
    while ((1 << bits) < pal.size()) ++bits;
    ColorMapObject *map = GifMakeMapObject(1 << bits, nullptr);
    if (!map) return fail(QStringLiteral("palette"), E_GIF_ERR_NOT_ENOUGH_MEM);
    for (int i = 0; i < map->ColorCount; ++i) {
        const QRgb c = i < pal.size() ? pal[i] : qRgb(0, 0, 0);
        map->Colors[i] = {GifByteType(qRed(c)), GifByteType(qGreen(c)), GifByteType(qBlue(c))};
    }

    GraphicsControlBlock gcb;
    gcb.DisposalMode = DISPOSE_DO_NOT;
    gcb.UserInputFlag = false;
    gcb.DelayTime = qMax(delay, 1);
    gcb.TransparentColor = NO_TRANSPARENT_COLOR;
    GifByteType ext[4];
    EGifGCBToExtension(&gcb, ext);
    bool ok = EGifPutExtension(m_gif, GRAPHICS_EXT_FUNC_CODE, 4, ext) != GIF_ERROR
        && EGifPutImageDesc(m_gif, 0, 0, m_width, m_height, false, map) != GIF_ERROR;
    GifFreeMapObject(map);
    if (!ok) return fail(QStringLiteral("frame"), m_gif->Error);

    QVector<GifByteType> row(m_width);
    for (int y = 0; y < m_height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < m_width; ++x) row[x] = lookup[key15(line[x])];
        if (EGifPutLine(m_gif, row.data(), m_width) == GIF_ERROR) return fail(QStringLiteral("pixels"), m_gif->Error);
    }
    return true;
}

bool KisGifWriter::close()
{
    if (!m_gif) return true;
    int err = 0;
    const int result = EGifCloseFile(m_gif, &err);
    m_gif = nullptr;
    return result == GIF_OK || fail(QStringLiteral("close"), err);
}
