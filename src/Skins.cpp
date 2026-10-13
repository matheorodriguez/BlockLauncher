#include "Skins.h"
#include "Net.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace {
const QString kProfileUrl = "https://api.minecraftservices.com/minecraft/profile";
const QString kSkinsUrl = "https://api.minecraftservices.com/minecraft/profile/skins";
const QString kActiveSkinUrl = "https://api.minecraftservices.com/minecraft/profile/skins/active";
const QString kActiveCapeUrl = "https://api.minecraftservices.com/minecraft/profile/capes/active";

QNetworkRequest authedRequest(const QString& url, const QString& accessToken)
{
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Authorization", "Bearer " + accessToken.toUtf8());
    return req;
}
} // namespace

namespace Skins {

MinecraftProfile fetchProfile(QNetworkAccessManager& nam, const QString& accessToken)
{
    const auto res = httpSend(nam, authedRequest(kProfileUrl, accessToken), "GET");
    if (res.status != 200) throw Error{"Impossible de récupérer le profil Minecraft."};
    const auto o = QJsonDocument::fromJson(res.body).object();

    MinecraftProfile p;
    p.name = o["name"].toString();
    p.uuid = o["id"].toString();
    for (const auto& s : o["skins"].toArray()) {
        const auto so = s.toObject();
        p.skins << SkinEntry{so["id"].toString(), so["url"].toString(), so["variant"].toString(),
                             so["state"].toString()};
    }
    for (const auto& c : o["capes"].toArray()) {
        const auto co = c.toObject();
        p.capes << CapeEntry{co["id"].toString(), co["url"].toString(), co["alias"].toString(),
                             co["state"].toString()};
    }
    return p;
}

void uploadSkin(QNetworkAccessManager& nam, const QString& accessToken, const QString& filePath,
                const QString& variant)
{
    auto* file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        throw Error{"Impossible de lire le fichier : " + QDir::toNativeSeparators(filePath)};
    }

    auto* multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart variantPart;
    variantPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"variant\""));
    variantPart.setBody(variant.toUtf8());
    multiPart->append(variantPart);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QString("form-data; name=\"file\"; filename=\"%1\"")
                                    .arg(QFileInfo(filePath).fileName())));
    file->setParent(multiPart);
    filePart.setBodyDevice(file);
    multiPart->append(filePart);

    QNetworkRequest req = authedRequest(kSkinsUrl, accessToken);
    req.setTransferTimeout(30000);

    QNetworkReply* reply = nam.post(req, multiPart);
    multiPart->setParent(reply);
    if (!reply->isFinished()) {
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString errStr = reply->errorString();
    const bool netErr = reply->error() != QNetworkReply::NoError;
    reply->deleteLater();
    if (status < 200 || status >= 300)
        throw Error{QString("Échec du changement de skin (%1).").arg(netErr ? errStr : QString::number(status))};
}

void resetSkin(QNetworkAccessManager& nam, const QString& accessToken)
{
    const auto res = httpSend(nam, authedRequest(kActiveSkinUrl, accessToken), "DELETE");
    if (res.status != 200 && res.status != 204) throw Error{"Impossible de réinitialiser le skin."};
}

void setActiveCape(QNetworkAccessManager& nam, const QString& accessToken, const QString& capeId)
{
    HttpResult res;
    if (capeId.isEmpty()) {
        res = httpSend(nam, authedRequest(kActiveCapeUrl, accessToken), "DELETE");
    } else {
        QNetworkRequest req = authedRequest(kActiveCapeUrl, accessToken);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        res = httpSend(nam, req, "PUT", QJsonDocument(QJsonObject{{"capeId", capeId}}).toJson(QJsonDocument::Compact));
    }
    if (res.status != 200 && res.status != 204) throw Error{"Impossible de changer de cape."};
}

} // namespace Skins
