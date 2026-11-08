/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGIFWRITER_H
#define KISGIFWRITER_H

#include <QString>

class QImage;
struct GifFileType;

/**
 * Writes an animated GIF with giflib (already a dependency of the GIF
 * import/export plugin), so rendering animations to GIF needs no FFmpeg.
 * Each frame gets its own 256-colour palette (median cut), no dithering:
 * drawn art keeps clean edges and flat areas.
 */
class KisGifWriter
{
public:
    KisGifWriter() = default;
    ~KisGifWriter();
    Q_DISABLE_COPY_MOVE(KisGifWriter)

    /// @p loop: repeat forever.
    bool open(const QString &path, int width, int height, bool loop);
    /// @p image is scaled to the size given to open() if it differs; @p delay
    /// in hundredths of a second.
    bool addFrame(const QImage &image, int delay);
    bool close();
    QString errorString() const { return m_error; }

private:
    bool fail(const QString &what, int code);
    GifFileType *m_gif = nullptr;
    int m_width = 0;
    int m_height = 0;
    QString m_error;
};

#endif // KISGIFWRITER_H
