/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QVariant>

#include <kpluginfactory.h>

#include <KisDocument.h>
#include <KisImportExportFilter.h>
#include <kis_image.h>

#include "ikriate/online/IkDrawpileFiles.h"

/// Drawpile projects and canvases (.dppr, .dpcs) and QOI images.
class KisDrawpileImport : public KisImportExportFilter
{
    Q_OBJECT
public:
    KisDrawpileImport(QObject *parent, const QVariantList &)
        : KisImportExportFilter(parent)
    {
    }

    KisImportExportErrorCode convert(KisDocument *document, QIODevice *io,
                                     KisPropertiesConfigurationSP = KisPropertiesConfigurationSP()) override
    {
        // Drawpile reads projects with SQLite, which needs a real file: use
        // the file itself when it is one, else a copy.
        QString path = filename();
        QTemporaryDir dir;
        if (path.isEmpty() || !QFileInfo(path).isFile()) {
            const QString suffix = mimeType() == QByteArray("application/x-drawpile-project") ? QStringLiteral("dppr")
                : mimeType() == QByteArray("application/x-drawpile-canvas")                  ? QStringLiteral("dpcs")
                                                                                                 : QStringLiteral("qoi");
            path = dir.filePath(QStringLiteral("import.") + suffix);
            QFile copy(path);
            if (!dir.isValid() || !copy.open(QIODevice::WriteOnly) || copy.write(io->readAll()) < 0) {
                return ImportExportCodes::CannotCreateFile;
            }
        }
        QString error;
        KisImageSP image = IkDrawpileFiles::load(path, &error);
        if (!image) {
            warnFile << "Drawpile import:" << error;
            return ImportExportCodes::FileFormatIncorrect;
        }
        document->setCurrentImage(image);
        return ImportExportCodes::OK;
    }
};

K_PLUGIN_FACTORY_WITH_JSON(KisDrawpileImportFactory, "krita_drawpile_import.json", registerPlugin<KisDrawpileImport>();)

#include "kis_drawpile_import.moc"
