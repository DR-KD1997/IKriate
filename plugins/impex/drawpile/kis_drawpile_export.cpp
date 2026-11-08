/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <QFile>
#include <QTemporaryDir>
#include <QVariant>

#include <kpluginfactory.h>

#include <KisDocument.h>
#include <KisExportCheckRegistry.h>
#include <KisImportExportFilter.h>
#include <KoColorModelStandardIds.h>
#include <kis_image.h>

#include "ikriate/online/IkDrawpileFiles.h"

/// Drawpile projects and canvases (.dppr, .dpcs) and QOI images.
class KisDrawpileExport : public KisImportExportFilter
{
    Q_OBJECT
public:
    KisDrawpileExport(QObject *parent, const QVariantList &)
        : KisImportExportFilter(parent)
    {
    }

    bool flat() const { return mimeType() == QByteArray("image/qoi"); }

    QString suffix() const
    {
        return mimeType() == QByteArray("application/x-drawpile-project") ? QStringLiteral("dppr")
            : mimeType() == QByteArray("application/x-drawpile-canvas")  ? QStringLiteral("dpcs")
                                                                             : QStringLiteral("qoi");
    }

    KisImportExportErrorCode convert(KisDocument *document, QIODevice *io,
                                     KisPropertiesConfigurationSP = KisPropertiesConfigurationSP()) override
    {
        // Drawpile writes to a path (SQLite for projects): write a temporary
        // file and copy it into the destination.
        QTemporaryDir dir;
        if (!dir.isValid()) return ImportExportCodes::CannotCreateFile;
        const QString path = dir.filePath(QStringLiteral("export.") + suffix());
        QString error;
        if (!IkDrawpileFiles::save(document->savingImage(), path, &error)) {
            warnFile << "Drawpile export:" << error;
            return ImportExportCodes::Failure;
        }
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return ImportExportCodes::FileNotExist;
        if (io->write(f.readAll()) < 0) return ImportExportCodes::ErrorWhileWriting;
        return ImportExportCodes::OK;
    }

    void initializeCapabilities() override
    {
        const KisExportCheckBase::Level layers = flat() ? KisExportCheckBase::PARTIALLY : KisExportCheckBase::SUPPORTED;
        addCapability(KisExportCheckRegistry::instance()->get("MultiLayerCheck")->create(layers));
        addCapability(KisExportCheckRegistry::instance()->get("NodeTypeCheck/KisGroupLayer")->create(layers));
        addCapability(KisExportCheckRegistry::instance()->get("LayerOpacityCheck")->create(layers));
        addCapability(KisExportCheckRegistry::instance()->get("sRGBProfileCheck")->create(KisExportCheckBase::SUPPORTED));
        addCapability(KisExportCheckRegistry::instance()->get("ColorModelHomogenousCheck")->create(KisExportCheckBase::SUPPORTED));
        QList<QPair<KoID, KoID>> models;
        models << QPair<KoID, KoID>(RGBAColorModelID, Integer8BitsColorDepthID);
        addSupportedColorModels(models, flat() ? QStringLiteral("QOI") : QStringLiteral("Drawpile"));
    }
};

K_PLUGIN_FACTORY_WITH_JSON(KisDrawpileExportFactory, "krita_drawpile_export.json", registerPlugin<KisDrawpileExport>();)

#include "kis_drawpile_export.moc"
