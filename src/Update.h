#pragma once
#include "Game.h" // Progress
#include <QString>

class QNetworkAccessManager;

struct UpdateInfo {
    QString version;         // ex : "1.1.0" (vide = pas de mise à jour disponible)
    QString downloadUrl;     // asset .exe de l'installateur
    QString releaseNotesUrl;
};

// Vérifie les mises à jour via les GitHub Releases du dépôt officiel du projet.
namespace Update {

inline constexpr const char* kRepo = "matheorodriguez/BlockLauncher";

// Renvoie un UpdateInfo vide (version vide) si aucune mise à jour n'est disponible, si le
// dépôt n'a pas encore de release, ou en cas d'erreur réseau.
UpdateInfo checkForUpdate(QNetworkAccessManager& nam, const QString& currentVersion);

// Télécharge l'installateur de la mise à jour et le lance (détaché). L'appelant doit
// fermer l'application ensuite pour que l'installateur puisse remplacer les fichiers.
void downloadAndLaunchInstaller(QNetworkAccessManager& nam, const UpdateInfo& info, const Progress& progress);

}
