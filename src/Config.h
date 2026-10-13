#pragma once
#include <QCoreApplication>
#include <QFile>
#include <QString>

// Permet à un installateur (ou à un déploiement en entreprise) de préconfigurer l'ID client
// Azure sans que l'utilisateur ait à le saisir : il suffit de déposer un fichier
// "client_id.txt" (une seule ligne, le GUID) à côté de l'exécutable.
namespace Config {

inline QString fileClientId()
{
    QFile f(QCoreApplication::applicationDirPath() + "/client_id.txt");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

}
