/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KISGIFMEDIAENCODERRUNNABLE_H
#define KISGIFMEDIAENCODERRUNNABLE_H

#include "KisMediaEncoderWrapper.h"

/// Animated GIF through giflib (KisGifWriter), for builds without FFmpeg.
class KisGifMediaEncoderRunnable : public KisMediaEncoderRunnable
{
public:
    static KisGifMediaEncoderRunnable *create(const KisMediaEncoderWrapperSettings &settings,
                                              QObject *parent = nullptr);
    static void getSupportedFormats(QVector<KisMediaEncoderFormat *> &outSupportedFormats);

protected:
    KisGifMediaEncoderRunnable(const KisMediaEncoderWrapperSettings &settings, QObject *parent);
    EncodeResult encode(QString &outErrorMessage) override;

private:
    class Format;
};

#endif
