// SPDX-FileCopyrightText: 2026 IKriate contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IKriate: drawdance's ZIP interface on QuaZip, which Krita already uses for
// .kra and .ora files (instead of KArchive or libzip).
extern "C" {
#include "zip_archive.h"
#include <dpcommon/common.h>
#include <dpcommon/conversions.h>
}
#include <QByteArray>
#include <QString>
#include <quazip.h>
#include <quazipfile.h>
#include <quazipnewinfo.h>
#include <zlib.h>

struct DP_ZipReaderFile {
    QByteArray data;
};

extern "C" DP_ZipReader *DP_zip_reader_new(const char *path)
{
    QuaZip *zip = new QuaZip(QString::fromUtf8(path));
    if (zip->open(QuaZip::mdUnzip)) {
        return reinterpret_cast<DP_ZipReader *>(zip);
    }
    DP_error_set("Error opening '%s' (zip error %d)", path, zip->getZipError());
    delete zip;
    return nullptr;
}

extern "C" void DP_zip_reader_free(DP_ZipReader *zr)
{
    QuaZip *zip = reinterpret_cast<QuaZip *>(zr);
    if (zip) {
        zip->close();
        delete zip;
    }
}

extern "C" DP_ZipReaderFile *DP_zip_reader_read_file(DP_ZipReader *zr, const char *path)
{
    QuaZip *zip = reinterpret_cast<QuaZip *>(zr);
    if (!zip->setCurrentFile(QString::fromUtf8(path))) {
        return nullptr;
    }
    QuaZipFile file(zip);
    if (!file.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    DP_ZipReaderFile *zrf = new DP_ZipReaderFile{file.readAll()};
    file.close();
    return zrf;
}

extern "C" size_t DP_zip_reader_file_size(DP_ZipReaderFile *zrf)
{
    return size_t(zrf->data.size());
}

extern "C" void *DP_zip_reader_file_content(DP_ZipReaderFile *zrf)
{
    return reinterpret_cast<void *>(zrf->data.data());
}

extern "C" void DP_zip_reader_file_free(DP_ZipReaderFile *zrf)
{
    delete zrf;
}

extern "C" DP_ZipWriter *DP_zip_writer_new(const char *path)
{
    QuaZip *zip = new QuaZip(QString::fromUtf8(path));
    if (zip->open(QuaZip::mdCreate)) {
        return reinterpret_cast<DP_ZipWriter *>(zip);
    }
    DP_error_set("Error creating '%s' (zip error %d)", path, zip->getZipError());
    delete zip;
    return nullptr;
}

extern "C" void DP_zip_writer_free_abort(DP_ZipWriter *zw)
{
    QuaZip *zip = reinterpret_cast<QuaZip *>(zw);
    zip->close();
    delete zip;
}

extern "C" bool DP_zip_writer_free_finish(DP_ZipWriter *zw)
{
    QuaZip *zip = reinterpret_cast<QuaZip *>(zw);
    zip->close();
    const bool ok = zip->getZipError() == UNZ_OK;
    if (!ok) {
        DP_error_set("Error closing zip archive (zip error %d)", zip->getZipError());
    }
    delete zip;
    return ok;
}

static bool write_entry(QuaZip *zip, const QString &name, const void *buffer, size_t size, bool deflate)
{
    QuaZipFile file(zip);
    if (!file.open(QIODevice::WriteOnly, QuaZipNewInfo(name), nullptr, 0, deflate ? Z_DEFLATED : 0,
                   deflate ? Z_DEFAULT_COMPRESSION : 0)) {
        return false;
    }
    bool ok = true;
    if (size > 0) {
        ok = file.write(static_cast<const char *>(buffer), qint64(size)) == qint64(size);
    }
    file.close();
    return ok && file.getZipError() == UNZ_OK;
}

extern "C" bool DP_zip_writer_add_dir(DP_ZipWriter *zw, const char *path)
{
    QuaZip *zip = reinterpret_cast<QuaZip *>(zw);
    QString name = QString::fromUtf8(path);
    if (!name.endsWith(QLatin1Char('/'))) {
        name += QLatin1Char('/');
    }
    if (write_entry(zip, name, nullptr, 0, false)) {
        return true;
    }
    DP_error_set("Error creating directory '%s' in zip", path);
    return false;
}

extern "C" bool DP_zip_writer_add_file(DP_ZipWriter *zw, const char *path, const void *buffer, size_t size,
                                       bool deflate, bool take_buffer)
{
    QuaZip *zip = reinterpret_cast<QuaZip *>(zw);
    const bool ok = write_entry(zip, QString::fromUtf8(path), buffer, size, deflate);
    if (take_buffer) {
        DP_free(const_cast<void *>(buffer));
    }
    if (!ok) {
        DP_error_set("Error storing '%s' in zip", path);
    }
    return ok;
}
