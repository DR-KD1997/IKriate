/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "KisGifMediaEncoderRunnable.h"
#include "KisGifWriter.h"

#include <QImage>
#include <klocalizedstring.h>
#include <kis_debug.h>

class KisGifMediaEncoderRunnable::Format : public KisMediaEncoderFormat
{
public:
    Type type() const override { return Type::GifMediaEncoder; }
    // The key FFmpeg's GIF had, so saved render settings keep working.
    QString key() const override { return QStringLiteral("libav:gif"); }
    QString title() const override { return i18n("GIF"); }
    QString extension() const override { return QStringLiteral("gif"); }
    bool supportsAudio() const override { return false; }
    QWidget *createPreferencesWidget(const QVariantMap &) const override { return nullptr; }
    void resetPreferencesWidget(QWidget *) const override {}
    QVariantMap getPreferencesFromWidget(QWidget *) const override { return QVariantMap(); }
};

KisGifMediaEncoderRunnable *KisGifMediaEncoderRunnable::create(const KisMediaEncoderWrapperSettings &settings,
                                                               QObject *parent)
{
    if (settings.format->type() == KisMediaEncoderFormat::Type::GifMediaEncoder) {
        return new KisGifMediaEncoderRunnable(settings, parent);
    }
    return nullptr;
}

void KisGifMediaEncoderRunnable::getSupportedFormats(QVector<KisMediaEncoderFormat *> &outSupportedFormats)
{
    outSupportedFormats.append(new Format());
}

KisGifMediaEncoderRunnable::KisGifMediaEncoderRunnable(const KisMediaEncoderWrapperSettings &settings,
                                                       QObject *parent)
    : KisMediaEncoderRunnable(settings, parent)
{
}

KisMediaEncoderRunnable::EncodeResult KisGifMediaEncoderRunnable::encode(QString &outErrorMessage)
{
    const QSize size = settings().outputSize;
    KisGifWriter writer;
    if (!writer.open(settings().outputFile, size.width(), size.height(), true)) {
        outErrorMessage = i18n("Could not write the GIF (%1)", writer.errorString());
        return EncodeResult::Failed;
    }
    // GIF delays are in hundredths of a second: carry the rounding over so
    // the total length stays right at any frame rate.
    const double perInstance = 100.0 / double(qMax(1, settings().outputFps));
    double due = 0.0;
    int written = 0;
    Frame frame;
    QImage image;
    while (nextFrame(frame)) {
        if (isCancelled()) return EncodeResult::Cancelled;
        if (!frame.readImage(image)) {
            warnFile << "GIF encoder: could not read" << frame.path();
            continue;
        }
        due += perInstance * frame.instances();
        const int delay = qMax(1, int(due - written + 0.5));
        written += delay;
        if (!writer.addFrame(image, delay)) {
            outErrorMessage = i18n("Could not write the GIF (%1)", writer.errorString());
            return EncodeResult::Failed;
        }
    }
    if (!writer.close()) {
        outErrorMessage = i18n("Could not write the GIF (%1)", writer.errorString());
        return EncodeResult::Failed;
    }
    return EncodeResult::Completed;
}
