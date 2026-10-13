#pragma once
// Petits utilitaires HTTP bloquants, à utiliser uniquement dans un thread de travail.
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

struct Error { QString msg; };

struct HttpResult {
    int status = 0;
    QByteArray body;
    QString error;
};

inline HttpResult httpSend(QNetworkAccessManager& nam, QNetworkRequest req,
                           const QByteArray& verb, const QByteArray& body = {})
{
    req.setTransferTimeout(30000);
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockLauncher/1.0");
    QNetworkReply* r = nullptr;
    if (verb == "GET") r = nam.get(req);
    else if (verb == "POST") r = nam.post(req, body);
    else if (verb == "PUT") r = nam.put(req, body);
    else if (verb == "DELETE") r = nam.deleteResource(req);
    else r = nam.sendCustomRequest(req, verb, body);
    if (!r->isFinished()) {
        QEventLoop loop;
        QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
    HttpResult res;
    res.status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    res.body = r->readAll();
    if (r->error() != QNetworkReply::NoError) res.error = r->errorString();
    delete r;
    return res;
}

inline QByteArray httpGet(QNetworkAccessManager& nam, const QString& url)
{
    auto res = httpSend(nam, QNetworkRequest(QUrl(url)), "GET");
    if (res.status < 200 || res.status >= 300)
        throw Error{QString("Téléchargement échoué : %1 (%2)")
                        .arg(url, res.error.isEmpty() ? QString::number(res.status) : res.error)};
    return res.body;
}

inline QJsonObject getJson(QNetworkAccessManager& nam, const QString& url)
{
    const auto doc = QJsonDocument::fromJson(httpGet(nam, url));
    if (!doc.isObject()) throw Error{"Réponse JSON invalide : " + url};
    return doc.object();
}
