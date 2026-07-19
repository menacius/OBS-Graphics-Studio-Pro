#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace bgs::packed_title {

/* Binary, versioned container used only by .obgp files.  The ordinary .obgt
 * JSON template format deliberately does not depend on this API. */
struct SourceEntry {
    QString archive_path;
    QString kind;
    QString source_path;
    QByteArray data;
    QJsonObject metadata;
};

struct ReadResult {
    QByteArray title_json;
    QString extraction_root;
    QStringList extracted_font_paths;
    QJsonObject manifest;
};

bool has_packed_signature(const QString &path);

bool write(const QString &path,
           const QList<SourceEntry> &entries,
           const QJsonObject &manifest_metadata,
           QString *error);

bool read(const QString &path,
          const QString &extraction_base,
          ReadResult *result,
          QString *error);

} // namespace bgs::packed_title
