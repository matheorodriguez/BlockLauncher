#include "Auth.h"
#include "Net.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <memory>

namespace {

const QString kAuthorizeUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize";
const QString kTokenUrl = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
const QString kScope = "XboxLive.signin offline_access";

QString b64url(const QByteArray& d)
{
    return QString::fromLatin1(d.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray randomBytes(int n)
{
    QByteArray b(n, 0);
    for (int i = 0; i < n; ++i) b[i] = char(QRandomGenerator::system()->bounded(256));
    return b;
}

QJsonObject parseObj(const HttpResult& r)
{
    return QJsonDocument::fromJson(r.body).object();
}

HttpResult postJson(QNetworkAccessManager& nam, const QString& url, const QJsonObject& body)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Accept", "application/json");
    return httpSend(nam, req, "POST", QJsonDocument(body).toJson(QJsonDocument::Compact));
}

HttpResult postForm(QNetworkAccessManager& nam, const QString& url, const QUrlQuery& form)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    return httpSend(nam, req, "POST", form.query(QUrl::FullyEncoded).toUtf8());
}

void replyHtml(QTcpSocket& s, int code, const QString& html)
{
    const QByteArray body = html.toUtf8();
    QByteArray head = "HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " Not Found") +
                      "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    s.write(head + body);
    s.waitForBytesWritten(2000);
    s.disconnectFromHost();
    if (s.state() != QAbstractSocket::UnconnectedState) s.waitForDisconnected(1000);
}

QString resultPage(bool ok)
{
    const QString msg = ok ? QStringLiteral("Connexion réussie ✓<br><small>Tu peux fermer cet onglet et retourner dans Block Launcher.</small>")
                           : QStringLiteral("La connexion a échoué.<br><small>Retourne dans Block Launcher pour plus de détails.</small>");
    return QStringLiteral(
               "<!doctype html><meta charset=utf-8><title>Block Launcher</title>"
               "<body style=\"margin:0;height:100vh;display:grid;place-items:center;background:#202020;"
               "color:#fff;font-family:'Segoe UI Variable Display','Segoe UI',sans-serif;font-size:22px;text-align:center\">"
               "<div style=\"background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.08);"
               "border-radius:8px;padding:32px 48px;line-height:1.6\">%1</div></body>")
        .arg(msg);
}

QString waitForCode(QTcpServer& server, const QString& state)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5 * 60 * 1000) {
        if (!server.waitForNewConnection(250)) continue;
        std::unique_ptr<QTcpSocket> s(server.nextPendingConnection());
        if (!s) continue;

        QByteArray req;
        while (!req.contains("\r\n") && s->waitForReadyRead(3000)) req += s->readAll();
        const QList<QByteArray> parts = req.split(' ');
        if (parts.size() < 2) continue;

        const QUrl url(QStringLiteral("http://localhost") + QString::fromLatin1(parts[1]));
        if (url.path() != QLatin1String("/callback")) {
            replyHtml(*s, 404, QString());
            continue;
        }
        const QUrlQuery q(url);
        const bool ok = q.hasQueryItem("code") && q.queryItemValue("state") == state;
        replyHtml(*s, 200, resultPage(ok));

        if (q.hasQueryItem("error"))
            throw Error{"Connexion annulée : " + q.queryItemValue("error_description", QUrl::FullyDecoded)};
        if (!ok) throw Error{"Réponse de connexion invalide."};
        return q.queryItemValue("code", QUrl::FullyDecoded);
    }
    throw Error{"Délai de connexion dépassé (5 minutes)."};
}

QString xstsErrorMessage(qint64 xerr)
{
    switch (xerr) {
    case 2148916233LL: return "Ce compte Microsoft n'a pas de profil Xbox. Crée-en un sur xbox.com puis réessaie.";
    case 2148916235LL: return "Xbox Live n'est pas disponible dans ton pays.";
    case 2148916236LL:
    case 2148916237LL: return "Ce compte doit d'abord terminer une vérification d'âge sur xbox.com.";
    case 2148916238LL: return "Compte enfant : un adulte doit l'ajouter à une famille Microsoft avant de jouer.";
    default: return QString("Autorisation Xbox refusée (code %1).").arg(xerr);
    }
}

// Microsoft → Xbox Live → XSTS → Minecraft → profil
Account finish(QNetworkAccessManager& nam, const QString& msToken, const QString& refreshToken)
{
    // 1. Xbox Live
    auto xblRes = postJson(nam, "https://user.auth.xboxlive.com/user/authenticate",
                           QJsonObject{{"Properties", QJsonObject{{"AuthMethod", "RPS"},
                                                                  {"SiteName", "user.auth.xboxlive.com"},
                                                                  {"RpsTicket", "d=" + msToken}}},
                                       {"RelyingParty", "http://auth.xboxlive.com"},
                                       {"TokenType", "JWT"}});
    if (xblRes.status != 200) throw Error{"Échec de l'authentification Xbox Live."};
    const auto xbl = parseObj(xblRes);
    const QString xblToken = xbl["Token"].toString();
    const QString uhs = xbl["DisplayClaims"].toObject()["xui"].toArray().at(0).toObject()["uhs"].toString();

    // 2. XSTS
    auto xstsRes = postJson(nam, "https://xsts.auth.xboxlive.com/xsts/authorize",
                            QJsonObject{{"Properties", QJsonObject{{"SandboxId", "RETAIL"},
                                                                   {"UserTokens", QJsonArray{xblToken}}}},
                                        {"RelyingParty", "rp://api.minecraftservices.com/"},
                                        {"TokenType", "JWT"}});
    const auto xsts = parseObj(xstsRes);
    if (xstsRes.status == 401) throw Error{xstsErrorMessage(qint64(xsts["XErr"].toDouble()))};
    if (xstsRes.status != 200) throw Error{"Échec de l'autorisation XSTS."};

    // 3. Minecraft
    auto mcRes = postJson(nam, "https://api.minecraftservices.com/authentication/login_with_xbox",
                          QJsonObject{{"identityToken", "XBL3.0 x=" + uhs + ";" + xsts["Token"].toString()}});
    if (mcRes.status == 403)
        throw Error{"Mojang refuse cet ID client Azure. Les nouvelles applications doivent être approuvées "
                    "pour l'API Minecraft (formulaire : aka.ms/mce-reviewappid)."};
    if (mcRes.status != 200) throw Error{QString("Échec de connexion aux services Minecraft (%1).").arg(mcRes.status)};
    const auto mc = parseObj(mcRes);

    Account acc;
    acc.accessToken = mc["access_token"].toString();
    acc.expiresAt = QDateTime::currentSecsSinceEpoch() + mc["expires_in"].toInt(86400);
    acc.refreshToken = refreshToken;

    // 4. Profil
    QNetworkRequest req{QUrl("https://api.minecraftservices.com/minecraft/profile")};
    req.setRawHeader("Authorization", "Bearer " + acc.accessToken.toUtf8());
    auto profRes = httpSend(nam, req, "GET");
    if (profRes.status == 404) throw Error{"Ce compte ne possède pas Minecraft : Java Edition."};
    if (profRes.status != 200) throw Error{"Impossible de récupérer le profil Minecraft."};
    const auto prof = parseObj(profRes);
    acc.uuid = prof["id"].toString();
    acc.name = prof["name"].toString();
    return acc;
}

} // namespace

namespace Auth {

Account loginInteractive(const QString& clientId, const std::function<void(const QUrl&)>& openBrowser)
{
    if (clientId.trimmed().isEmpty())
        throw Error{"Renseigne d'abord ton ID client Azure dans Paramètres (voir README)."};

    const QString verifier = b64url(randomBytes(32));
    const QString challenge = b64url(QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256));
    const QString state = b64url(randomBytes(16));

    QUrl url(kAuthorizeUrl);
    QUrlQuery q;
    q.addQueryItem("client_id", clientId.trimmed());
    q.addQueryItem("response_type", "code");
    q.addQueryItem("redirect_uri", kRedirectUri);
    q.addQueryItem("scope", kScope);
    q.addQueryItem("code_challenge", challenge);
    q.addQueryItem("code_challenge_method", "S256");
    q.addQueryItem("state", state);
    q.addQueryItem("prompt", "select_account");
    url.setQuery(q);

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, kPort))
        throw Error{QString("Le port %1 est déjà utilisé, ferme l'application qui l'occupe.").arg(kPort)};

    openBrowser(url);
    const QString code = waitForCode(server, state);
    server.close();

    QNetworkAccessManager nam;
    QUrlQuery form;
    form.addQueryItem("client_id", clientId.trimmed());
    form.addQueryItem("scope", kScope);
    form.addQueryItem("code", QString::fromUtf8(QUrl::toPercentEncoding(code)));
    form.addQueryItem("redirect_uri", kRedirectUri);
    form.addQueryItem("grant_type", "authorization_code");
    form.addQueryItem("code_verifier", verifier);
    auto res = postForm(nam, kTokenUrl, form);
    const auto tok = parseObj(res);
    if (res.status != 200)
        throw Error{"Échec de l'échange du code Microsoft : " + tok["error_description"].toString().section('\n', 0, 0)};

    return finish(nam, tok["access_token"].toString(), tok["refresh_token"].toString());
}

Account refreshIfNeeded(const QString& clientId, const Account& acc)
{
    if (acc.expiresAt - QDateTime::currentSecsSinceEpoch() > 10 * 60) return acc;
    if (clientId.trimmed().isEmpty()) throw Error{"ID client Azure manquant dans Paramètres."};

    QNetworkAccessManager nam;
    QUrlQuery form;
    form.addQueryItem("client_id", clientId.trimmed());
    form.addQueryItem("scope", kScope);
    form.addQueryItem("refresh_token", QString::fromUtf8(QUrl::toPercentEncoding(acc.refreshToken)));
    form.addQueryItem("grant_type", "refresh_token");
    auto res = postForm(nam, kTokenUrl, form);
    if (res.status != 200) throw Error{"Ta session a expiré, reconnecte-toi dans Paramètres."};
    const auto tok = parseObj(res);
    return finish(nam, tok["access_token"].toString(),
                  tok["refresh_token"].toString().isEmpty() ? acc.refreshToken : tok["refresh_token"].toString());
}

} // namespace Auth
