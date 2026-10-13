#pragma once
#include <QList>
#include <QString>

class QNetworkAccessManager;

struct SkinEntry {
    QString id, url, variant, state; // variant: CLASSIC/SLIM ; state: ACTIVE/INACTIVE
};

struct CapeEntry {
    QString id, url, alias, state;
};

struct MinecraftProfile {
    QString name, uuid;
    QList<SkinEntry> skins;
    QList<CapeEntry> capes;
};

// Gestion du skin et des capes via l'API officielle (api.minecraftservices.com), avec le
// jeton d'accès Minecraft du compte connecté (Account::accessToken).
namespace Skins {

MinecraftProfile fetchProfile(QNetworkAccessManager& nam, const QString& accessToken);

// Change le skin actif à partir d'un fichier PNG local (64x64). variant : "classic" ou "slim".
void uploadSkin(QNetworkAccessManager& nam, const QString& accessToken, const QString& filePath,
                const QString& variant);

// Réinitialise le skin par défaut (Steve/Alex).
void resetSkin(QNetworkAccessManager& nam, const QString& accessToken);

// Active une cape déjà possédée, ou la retire si capeId est vide.
void setActiveCape(QNetworkAccessManager& nam, const QString& accessToken, const QString& capeId);

}
