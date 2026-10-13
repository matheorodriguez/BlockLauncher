#pragma once
#include "Game.h"
#include <QList>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

// Type de contenu téléchargeable depuis Modrinth et installé dans le dossier de jeu d'un profil.
enum class ContentType { Mod, ResourcePack, Shader };

QString contentTypeLabel(ContentType t);  // "mod" / "pack de ressources" / "shader"
QString contentTypeFacet(ContentType t);  // project_type Modrinth : "mod" / "resourcepack" / "shader"
QString contentSubdir(ContentType t);     // "mods" / "resourcepacks" / "shaderpacks"
QString contentFilePattern(ContentType t); // "*.jar" / "*.zip"

struct ModSearchResult {
    QString projectId, slug, title, description, iconUrl;
    qint64 downloads = 0;
};

struct ModFile {
    QString versionId, versionNumber, fileName, url, sha1;
};

// Navigation et téléchargement de contenu (mods, packs de ressources, shaders) pour un profil
// installé (vanilla ou moddé — voir Game::ModProfile / Game::allProfiles()).
namespace Mods {

QList<ModSearchResult> search(QNetworkAccessManager& nam, const QString& query, const QString& mcVersion,
                              const QString& loaderKey, ContentType type, int limit = 24);

// Versions compatibles avec une version de Minecraft / un modloader (la plus récente en premier).
// `loaderKey` n'est pris en compte que pour les mods (les packs/shaders ne sont pas filtrés par loader).
QList<ModFile> versionsFor(QNetworkAccessManager& nam, const QString& projectId, const QString& mcVersion,
                           const QString& loaderKey, ContentType type);

QString dir(const QString& profileId, ContentType type);
void install(QNetworkAccessManager& nam, const ModFile& file, const QString& profileId, ContentType type,
            const Progress& progress);
QStringList installedFiles(const QString& profileId, ContentType type);
void remove(const QString& profileId, ContentType type, const QString& fileName);

}
