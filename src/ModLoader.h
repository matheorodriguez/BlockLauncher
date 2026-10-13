#pragma once
#include "Game.h"
#include <QString>
#include <QStringList>

class QNetworkAccessManager;

enum class Loader { Fabric, Quilt, Forge, NeoForge };

QString loaderDisplayName(Loader l);
QString loaderKey(Loader l);           // "fabric" / "quilt" / "forge" / "neoforge"
Loader loaderFromKey(const QString& key);

namespace ModLoader {

// Versions du modloader disponibles pour une version Minecraft donnée (la plus récente en premier).
QStringList fetchLoaderVersions(QNetworkAccessManager& nam, Loader loader, const QString& mcVersion);

// Installe (si besoin) la version Minecraft vanilla correspondante, puis le modloader choisi.
// Renvoie l'identifiant du profil de version créé, utilisable avec Game::launch().
QString install(QNetworkAccessManager& nam, Loader loader, const QString& mcVersion, const QString& loaderVersion,
                const Progress& progress);

}
