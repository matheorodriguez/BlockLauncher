#include "Update.h"
#include "Net.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>

namespace {

// Compare deux versions "x.y.z" (composants manquants = 0). Renvoie vrai si a > b.
bool isNewer(const QString& a, const QString& b)
{
    const auto pa = a.split('.');
    const auto pb = b.split('.');
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int va = i < pa.size() ? pa[i].toInt() : 0;
        const int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb) return va > vb;
    }
    return false;
}

} // namespace

namespace Update {

UpdateInfo checkForUpdate(QNetworkAccessManager& nam, const QString& currentVersion)
{
    QNetworkRequest req{QUrl(QString("https://api.github.com/repos/%1/releases/latest").arg(kRepo))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    const auto res = httpSend(nam, req, "GET");
    if (res.status != 200) return {}; // pas encore de release, ou hors ligne : silencieux

    const auto o = QJsonDocument::fromJson(res.body).object();
    QString tag = o["tag_name"].toString();
    if (tag.startsWith('v') || tag.startsWith('V')) tag.remove(0, 1);
    if (tag.isEmpty() || !isNewer(tag, currentVersion)) return {};

    QString url;
    for (const auto& a : o["assets"].toArray()) {
        const auto ao = a.toObject();
        if (ao["name"].toString().endsWith(".exe", Qt::CaseInsensitive)) {
            url = ao["browser_download_url"].toString();
            break;
        }
    }
    if (url.isEmpty()) return {};

    UpdateInfo info;
    info.version = tag;
    info.downloadUrl = url;
    info.releaseNotesUrl = o["html_url"].toString();
    return info;
}

void downloadAndLaunchInstaller(QNetworkAccessManager& nam, const UpdateInfo& info, const Progress& progress)
{
    progress("Téléchargement de la mise à jour…", 0, 0);
    const QByteArray data = httpGet(nam, info.downloadUrl);

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QDir().mkpath(dir);
    const QString path = dir + "/BlockLauncherSetup-" + info.version + ".exe";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        throw Error{"Impossible d'enregistrer le programme de mise à jour."};
    f.close();

    if (!QProcess::startDetached(path, {})) throw Error{"Impossible de lancer le programme de mise à jour."};
}

} // namespace Update
