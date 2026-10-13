#pragma once
#include <QString>
#include <QUrl>
#include <functional>

// Compte Microsoft / Minecraft Java connecté.
struct Account {
    QString name;          // pseudo Minecraft
    QString uuid;          // UUID du profil (sans tirets)
    QString accessToken;   // jeton Minecraft (≈ 24 h)
    QString refreshToken;  // jeton Microsoft pour renouveler la session
    qint64 expiresAt = 0;  // epoch en secondes
    bool valid() const { return !uuid.isEmpty() && !refreshToken.isEmpty(); }
};

namespace Auth {

// Port local qui reçoit la redirection OAuth. Il doit correspondre à l'URI
// de redirection déclarée dans l'application Azure.
inline constexpr quint16 kPort = 5173;
inline const char* kRedirectUri = "http://localhost:5173/callback";

// Connexion complète : ouvre le navigateur, attend la redirection puis
// enchaîne Microsoft → Xbox Live → XSTS → Minecraft. À appeler dans un thread de travail.
Account loginInteractive(const QString& clientId, const std::function<void(const QUrl&)>& openBrowser);

// Renouvelle la session si le jeton Minecraft expire bientôt.
Account refreshIfNeeded(const QString& clientId, const Account& acc);

}
