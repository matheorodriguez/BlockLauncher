#include "Mods.h"
#include "Net.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

QString contentTypeLabel(ContentType t)
{
    switch (t) {
    case ContentType::Mod: return "mod";
    case ContentType::ResourcePack: return "pack de ressources";
    case ContentType::Shader: return "shader";
    }
    return {};
}

QString contentTypeFacet(ContentType t)
{
    switch (t) {
    case ContentType::Mod: return "mod";
    case ContentType::ResourcePack: return "resourcepack";
    case ContentType::Shader: return "shader";
    }
    return {};
}

QString contentSubdir(ContentType t)
{
    switch (t) {
    case ContentType::Mod: return "mods";
    case ContentType::ResourcePack: return "resourcepacks";
    case ContentType::Shader: return "shaderpacks";
    }
    return {};
}

QString contentFilePattern(ContentType t) { return t == ContentType::Mod ? "*.jar" : "*.zip"; }

namespace {

const QString kApiBase = "https://api.modrinth.com/v2";

QString jsonArrayParam(const QStringList& items)
{
    QStringList quoted;
    for (const auto& i : items) quoted << ("\"" + i + "\"");
    return "[" + quoted.join(',') + "]";
}

} // namespace

namespace Mods {

QList<ModSearchResult> search(QNetworkAccessManager& nam, const QString& query, const QString& mcVersion,
                              const QString& loaderKey, ContentType type, int limit)
{
    QUrl url(kApiBase + "/search");
    QUrlQuery q;
    q.addQueryItem("query", query);
    q.addQueryItem("limit", QString::number(limit));
    QStringList facets{"[\"project_type:" + contentTypeFacet(type) + "\"]"};
    if (type == ContentType::Mod && !loaderKey.isEmpty()) facets << ("[\"categories:" + loaderKey + "\"]");
    if (!mcVersion.isEmpty()) facets << ("[\"versions:" + mcVersion + "\"]");
    q.addQueryItem("facets", "[" + facets.join(',') + "]");
    url.setQuery(q);

    const auto doc = QJsonDocument::fromJson(httpGet(nam, url.toString()));
    QList<ModSearchResult> out;
    for (const auto& h : doc.object()["hits"].toArray()) {
        const auto o = h.toObject();
        out << ModSearchResult{o["project_id"].toString(), o["slug"].toString(), o["title"].toString(),
                               o["description"].toString(), o["icon_url"].toString(),
                               qint64(o["downloads"].toDouble())};
    }
    return out;
}

QList<ModFile> versionsFor(QNetworkAccessManager& nam, const QString& projectId, const QString& mcVersion,
                           const QString& loaderKey, ContentType type)
{
    QUrl url(kApiBase + "/project/" + projectId + "/version");
    QUrlQuery q;
    if (type == ContentType::Mod && !loaderKey.isEmpty()) q.addQueryItem("loaders", jsonArrayParam({loaderKey}));
    if (!mcVersion.isEmpty()) q.addQueryItem("game_versions", jsonArrayParam({mcVersion}));
    url.setQuery(q);

    const auto arr = QJsonDocument::fromJson(httpGet(nam, url.toString())).array();
    QList<ModFile> out;
    for (const auto& v : arr) {
        const auto o = v.toObject();
        const auto files = o["files"].toArray();
        QJsonObject best = files.isEmpty() ? QJsonObject() : files[0].toObject();
        for (const auto& fv : files)
            if (fv.toObject()["primary"].toBool()) best = fv.toObject();
        if (best.isEmpty()) continue;
        out << ModFile{o["id"].toString(), o["version_number"].toString(), best["filename"].toString(),
                       best["url"].toString(), best["hashes"].toObject()["sha1"].toString()};
    }
    return out;
}

QString dir(const QString& profileId, ContentType type) { return Game::instanceDir(profileId) + "/" + contentSubdir(type); }

void install(QNetworkAccessManager& nam, const ModFile& file, const QString& profileId, ContentType type,
            const Progress& progress)
{
    progress("Téléchargement de " + file.fileName + "…", 0, 0);
    const QByteArray data = httpGet(nam, file.url);
    const QString d = dir(profileId, type);
    QDir().mkpath(d);
    QFile f(d + "/" + file.fileName);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        throw Error{"Impossible d'écrire " + file.fileName};
}

QStringList installedFiles(const QString& profileId, ContentType type)
{
    return QDir(dir(profileId, type)).entryList(QStringList{contentFilePattern(type)}, QDir::Files, QDir::Name);
}

void remove(const QString& profileId, ContentType type, const QString& fileName)
{
    QFile::remove(dir(profileId, type) + "/" + fileName);
}

} // namespace Mods
