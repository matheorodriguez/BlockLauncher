#pragma once
#include "Auth.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

class QNetworkAccessManager;

struct VersionEntry {
    QString id, type, url;
    bool isNull() const { return id.isEmpty(); }
};

struct LaunchOptions {
    int ramGb = 4;
    QString extraJvmArgs;
};

// Profil moddé installé (Fabric, Quilt, Forge, NeoForge…).
struct ModProfile {
    QString id;            // identifiant de version (ex: fabric-loader-0.15.11-1.20.4)
    QString mcVersion;     // version Minecraft de base
    QString loader;        // "fabric", "quilt", "forge" ou "neoforge"
    QString loaderVersion;
};

// status, fait, total (total = 0 → progression indéterminée)
using Progress = std::function<void(const QString&, qint64, qint64)>;

namespace Game {

QString rootDir();                              // %APPDATA%/.blocklauncher
QList<VersionEntry> fetchManifest(QNetworkAccessManager& nam);
QList<VersionEntry> cachedManifest();           // dernière liste connue (hors ligne)
QStringList installedVersions();
bool isInstalled(const QString& id);

// Télécharge (ou vérifie) le jeu, les bibliothèques, les ressources et Java.
void install(QNetworkAccessManager& nam, const VersionEntry& v, const Progress& progress);

// Lance le jeu ; renvoie le PID.
qint64 launch(const QString& id, const Account& acc, const LaunchOptions& opts);

// --- Profils moddés -------------------------------------------------------
QList<ModProfile> installedModProfiles();
// Toutes les versions installées (vanilla d'abord, loader vide ; puis les profils moddés).
QList<ModProfile> allProfiles();
QString instanceDir(const QString& id);         // dossier de jeu isolé (mods/saves) d'un profil moddé
bool isModded(const QString& id);
void removeProfile(const QString& id);          // supprime un profil (moddé ou vanilla installé)

// --- Primitives bas niveau, réutilisées par ModLoader.cpp pour installer les modloaders ---
QString versionDir(const QString& id);
QString versionJsonPath(const QString& id);
QString versionJarPath(const QString& id);
QString librariesDir();
QString javaExeForVersion(const QString& mcVersionId); // java/javaw correspondant à une version vanilla installée
QString mavenPath(const QString& coord);        // groupe:artefact:version[:classifier][@ext] -> chemin relatif .jar
QJsonObject readVersionJson(const QString& id);
void writeJsonFile(const QString& path, const QJsonObject& obj);
void writeModMeta(const QString& id, const QString& mcVersion, const QString& loader, const QString& loaderVersion);
void downloadLibraries(QNetworkAccessManager& nam, const QJsonArray& libraries, const Progress& progress,
                       const QString& label);
void extractArchive(const QString& archivePath, const QString& destDir);
QString jarMainClass(const QString& jarPath);
void runBlocking(const QString& exe, const QStringList& args, const QString& workDir);
void markComplete(const QString& id);

}
