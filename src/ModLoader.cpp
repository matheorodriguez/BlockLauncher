#include "ModLoader.h"
#include "Net.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>

QString loaderDisplayName(Loader l)
{
    switch (l) {
    case Loader::Fabric: return "Fabric";
    case Loader::Quilt: return "Quilt";
    case Loader::Forge: return "Forge";
    case Loader::NeoForge: return "NeoForge";
    }
    return {};
}

QString loaderKey(Loader l)
{
    switch (l) {
    case Loader::Fabric: return "fabric";
    case Loader::Quilt: return "quilt";
    case Loader::Forge: return "forge";
    case Loader::NeoForge: return "neoforge";
    }
    return {};
}

Loader loaderFromKey(const QString& k)
{
    if (k == "quilt") return Loader::Quilt;
    if (k == "forge") return Loader::Forge;
    if (k == "neoforge") return Loader::NeoForge;
    return Loader::Fabric;
}

namespace {

QString profileIdFor(Loader l, const QString& mcVersion, const QString& loaderVersion)
{
    return loaderKey(l) + "-loader-" + loaderVersion + "-" + mcVersion;
}

#if defined(Q_OS_WIN)
constexpr auto kCpSep = ";";
#else
constexpr auto kCpSep = ":";
#endif

// ============================================================================
//  Fabric / Quilt — API « meta », simple et fiable.
// ============================================================================
QString fabricLikeBase(Loader l) { return l == Loader::Quilt ? "https://meta.quiltmc.org/v3" : "https://meta.fabricmc.net/v2"; }

QStringList fetchFabricLikeVersions(QNetworkAccessManager& nam, Loader l, const QString& mcVersion)
{
    const QByteArray raw = httpGet(nam, fabricLikeBase(l) + "/versions/loader/" + mcVersion);
    const auto arr = QJsonDocument::fromJson(raw).array();
    QStringList out;
    for (const auto& e : arr) out << e.toObject()["loader"].toObject()["version"].toString();
    return out;
}

QString installFabricLike(QNetworkAccessManager& nam, Loader l, const QString& mcVersion, const QString& loaderVersion,
                          const Progress& progress)
{
    progress("Téléchargement du profil " + loaderDisplayName(l) + "…", 0, 0);
    const QString url = fabricLikeBase(l) + "/versions/loader/" + mcVersion + "/" + loaderVersion + "/profile/json";
    QJsonObject v = QJsonDocument::fromJson(httpGet(nam, url)).object();
    if (v.isEmpty()) throw Error{"Profil " + loaderDisplayName(l) + " introuvable pour Minecraft " + mcVersion + "."};

    const QString id = profileIdFor(l, mcVersion, loaderVersion);
    v["id"] = id;
    if (!v.contains("inheritsFrom")) v["inheritsFrom"] = mcVersion;
    Game::writeJsonFile(Game::versionJsonPath(id), v);

    Game::downloadLibraries(nam, v["libraries"].toArray(), progress, "Bibliothèques " + loaderDisplayName(l));
    Game::writeModMeta(id, mcVersion, loaderKey(l), loaderVersion);
    Game::markComplete(id);
    return id;
}

// ============================================================================
//  Forge / NeoForge — installateur officiel rejoué localement (bibliothèques +
//  « processeurs »). Reproduit ce que font MultiMC/Prism Launcher ; support du
//  nouveau format (1.13+, avec processeurs) et de l'ancien (≤ 1.12.2).
// ============================================================================
QStringList parseMavenMetadataVersions(const QByteArray& xml)
{
    QStringList out;
    QXmlStreamReader r(xml);
    while (!r.atEnd() && !r.hasError()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("version")) out << r.readElementText();
    }
    return out;
}

QStringList fetchForgeLikeVersions(QNetworkAccessManager& nam, Loader l, const QString& mcVersion)
{
    const QString url = l == Loader::NeoForge
                            ? "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml"
                            : "https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml";
    const QStringList all = parseMavenMetadataVersions(httpGet(nam, url));
    QStringList out;
    if (l == Loader::NeoForge) {
        // NeoForge réutilise le schéma de version de Minecraft en abandonnant le préfixe « 1. »
        // (ex : Minecraft 1.20.4 -> versions NeoForge "20.4.x").
        const QString shortMc = mcVersion.startsWith("1.") ? mcVersion.mid(2) : mcVersion;
        for (const auto& ver : all)
            if (ver.startsWith(shortMc + ".")) out << ver;
    } else {
        for (const auto& ver : all)
            if (ver.startsWith(mcVersion + "-")) out << ver.mid(mcVersion.size() + 1);
    }
    std::reverse(out.begin(), out.end()); // la métadonnée liste du plus ancien au plus récent
    return out;
}

// Copie dans libraries/ les bibliothèques déjà fournies localement par l'installateur
// (dossier « maven/ » embarqué), puis télécharge normalement le reste.
void installForgeLibraries(QNetworkAccessManager& nam, const QJsonArray& libraries, const QString& extractDir,
                           const Progress& progress, const QString& label)
{
    for (const auto& l : libraries) {
        const auto o = l.toObject();
        const auto artifact = o["downloads"].toObject()["artifact"].toObject();
        QString path = artifact["path"].toString();
        if (path.isEmpty()) path = Game::mavenPath(o["name"].toString());
        if (path.isEmpty() || !artifact["url"].toString().isEmpty()) continue;
        const QString dst = Game::librariesDir() + "/" + path;
        if (QFileInfo::exists(dst)) continue;
        const QString src = extractDir + "/maven/" + path;
        if (QFileInfo::exists(src)) {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            QFile::copy(src, dst);
        }
    }
    Game::downloadLibraries(nam, libraries, progress, label);
}

// Résout une coordonnée Maven entre crochets ("[groupe:artefact:version[:classifier][@ext]]")
// en chemin absolu dans le dossier libraries/.
QString resolveMavenBracket(const QString& coord)
{
    QString c = coord;
    QString ext = "jar";
    if (c.contains('@')) {
        const auto parts = c.split('@');
        c = parts[0];
        ext = parts[1];
    }
    QString path = Game::mavenPath(c);
    if (ext != "jar" && path.size() > 4) path = path.left(path.size() - 3) + ext;
    return QDir::toNativeSeparators(Game::librariesDir() + "/" + path);
}

// Exécute les « processeurs » de l'installateur Forge/NeoForge : chacun patch/assemble un
// morceau du client (mappings, jar patché…) en lançant un petit outil Java.
void runForgeProcessors(Loader l, const QJsonObject& installProfile, const QString& mcVersion,
                        const QString& extractDir, const QString& installerJar, const Progress& progress)
{
    const QString side = "client";

    QHash<QString, QString> data;
    const QJsonObject dataObj = installProfile["data"].toObject();
    for (auto it = dataObj.begin(); it != dataObj.end(); ++it) {
        QString v = it.value().toObject()[side].toString();
        if (v.startsWith('[') && v.endsWith(']')) v = resolveMavenBracket(v.mid(1, v.size() - 2));
        else if (v.startsWith('/')) v = QDir::toNativeSeparators(extractDir + v);
        data[it.key()] = v;
    }
    data["SIDE"] = side;
    data["MINECRAFT_JAR"] = QDir::toNativeSeparators(Game::versionJarPath(mcVersion));
    data["MINECRAFT_VERSION"] = mcVersion;
    data["ROOT"] = QDir::toNativeSeparators(extractDir);
    data["INSTALLER"] = QDir::toNativeSeparators(installerJar);
    data["LIBRARY_DIR"] = QDir::toNativeSeparators(Game::librariesDir());

    static const QRegularExpression bracketRe(R"(^\[(.+)\]$)");
    static const QRegularExpression braceRe(R"(\{([^}]+)\})");
    auto substituteArg = [&](const QString& s) -> QString {
        const auto bm = bracketRe.match(s);
        if (bm.hasMatch()) return resolveMavenBracket(bm.captured(1));
        QString out;
        qsizetype last = 0;
        auto it = braceRe.globalMatch(s);
        while (it.hasNext()) {
            const auto m = it.next();
            out += s.mid(last, m.capturedStart() - last);
            out += data.contains(m.captured(1)) ? data.value(m.captured(1)) : m.captured(0);
            last = m.capturedEnd();
        }
        return out + s.mid(last);
    };

    const QJsonArray processors = installProfile["processors"].toArray();
    const QString java = Game::javaExeForVersion(mcVersion);
    int idx = 0;
    for (const auto& entry : processors) {
        ++idx;
        const auto p = entry.toObject();
        const auto sides = p["sides"].toArray();
        if (!sides.isEmpty()) {
            bool clientOk = false;
            for (const auto& s : sides)
                if (s.toString() == side) clientOk = true;
            if (!clientOk) continue;
        }
        progress(QString("Installation de %1 (%2/%3)…").arg(loaderDisplayName(l)).arg(idx).arg(processors.size()), 0, 0);

        const QString jarPath = Game::librariesDir() + "/" + Game::mavenPath(p["jar"].toString());
        if (!QFileInfo::exists(jarPath)) throw Error{"Bibliothèque de l'installateur manquante : " + p["jar"].toString()};

        QStringList cp{QDir::toNativeSeparators(jarPath)};
        for (const auto& c : p["classpath"].toArray()) {
            const QString path = QDir::toNativeSeparators(Game::librariesDir() + "/" + Game::mavenPath(c.toString()));
            if (!cp.contains(path)) cp << path;
        }

        QStringList args{"-cp", cp.join(kCpSep), Game::jarMainClass(jarPath)};
        for (const auto& a : p["args"].toArray()) args << substituteArg(a.toString());

        Game::runBlocking(java, args, extractDir);
    }
}

QString installForgeLike(QNetworkAccessManager& nam, Loader l, const QString& mcVersion, const QString& loaderVersion,
                         const Progress& progress)
{
    const bool neo = l == Loader::NeoForge;
    const QString fullVersion = neo ? loaderVersion : (mcVersion + "-" + loaderVersion);
    const QString groupPath = neo ? "net/neoforged/neoforge" : "net/minecraftforge/forge";
    const QString artifact = neo ? "neoforge" : "forge";
    const QString repo = neo ? "https://maven.neoforged.net/releases" : "https://maven.minecraftforge.net";
    const QString installerUrl = repo + "/" + groupPath + "/" + fullVersion + "/" + artifact + "-" + fullVersion + "-installer.jar";

    progress("Téléchargement de l'installateur " + loaderDisplayName(l) + "…", 0, 0);
    const QByteArray installerData = httpGet(nam, installerUrl);

    const QString stageDir = Game::rootDir() + "/_installers/" + loaderKey(l) + "-" + fullVersion;
    QDir().mkpath(stageDir);
    const QString installerJar = stageDir + "/installer.jar";
    {
        QFile f(installerJar);
        if (!f.open(QIODevice::WriteOnly) || f.write(installerData) != installerData.size())
            throw Error{"Impossible d'écrire l'installateur " + loaderDisplayName(l) + "."};
    }

    const QString extractDir = stageDir + "/extracted";
    progress("Extraction de l'installateur…", 0, 0);
    Game::extractArchive(installerJar, extractDir);

    QFile ipFile(extractDir + "/install_profile.json");
    if (!ipFile.open(QIODevice::ReadOnly)) throw Error{"install_profile.json introuvable dans l'installateur."};
    const QJsonObject installProfile = QJsonDocument::fromJson(ipFile.readAll()).object();

    QJsonObject versionJson;
    if (installProfile.contains("processors")) {
        // Format moderne (Minecraft 1.13+) : les processeurs assemblent le jar patché.
        QFile vf(extractDir + "/version.json");
        if (!vf.open(QIODevice::ReadOnly)) throw Error{"version.json introuvable dans l'installateur " + loaderDisplayName(l) + "."};
        versionJson = QJsonDocument::fromJson(vf.readAll()).object();

        progress("Préparation des outils d'installation " + loaderDisplayName(l) + "…", 0, 0);
        installForgeLibraries(nam, installProfile["libraries"].toArray(), extractDir, progress, "Outils d'installation");

        runForgeProcessors(l, installProfile, mcVersion, extractDir, installerJar, progress);

        progress("Téléchargement des bibliothèques de lancement…", 0, 0);
        installForgeLibraries(nam, versionJson["libraries"].toArray(), extractDir, progress,
                              "Bibliothèques " + loaderDisplayName(l));
    } else {
        // Format hérité (Minecraft ≤ 1.12.2) : simple copie du jar universel, pas de processeur.
        versionJson = installProfile.contains("versionInfo") ? installProfile["versionInfo"].toObject() : installProfile;
        const QJsonObject installSection = installProfile["install"].toObject();
        const QString mavenCoord = installSection["path"].toString();
        const QString filePath = installSection["filePath"].toString();
        if (!mavenCoord.isEmpty() && !filePath.isEmpty()) {
            const QString dst = Game::librariesDir() + "/" + Game::mavenPath(mavenCoord);
            QDir().mkpath(QFileInfo(dst).absolutePath());
            QFile::remove(dst);
            if (!QFile::copy(extractDir + "/" + filePath, dst))
                throw Error{"Impossible d'installer " + loaderDisplayName(l) + " (copie du jar universel)."};
        }
        progress("Téléchargement des bibliothèques " + loaderDisplayName(l) + "…", 0, 0);
        installForgeLibraries(nam, versionJson["libraries"].toArray(), extractDir, progress,
                              "Bibliothèques " + loaderDisplayName(l));
    }

    QString id = versionJson["id"].toString();
    if (id.isEmpty()) id = profileIdFor(l, mcVersion, loaderVersion);
    versionJson["id"] = id;
    if (!versionJson.contains("inheritsFrom")) versionJson["inheritsFrom"] = mcVersion;
    Game::writeJsonFile(Game::versionJsonPath(id), versionJson);
    Game::writeModMeta(id, mcVersion, loaderKey(l), loaderVersion);
    Game::markComplete(id);

    QDir(stageDir).removeRecursively();
    return id;
}

} // namespace

namespace ModLoader {

QStringList fetchLoaderVersions(QNetworkAccessManager& nam, Loader loader, const QString& mcVersion)
{
    if (loader == Loader::Fabric || loader == Loader::Quilt) return fetchFabricLikeVersions(nam, loader, mcVersion);
    return fetchForgeLikeVersions(nam, loader, mcVersion);
}

QString install(QNetworkAccessManager& nam, Loader loader, const QString& mcVersion, const QString& loaderVersion,
                const Progress& progress)
{
    if (!Game::isInstalled(mcVersion)) {
        VersionEntry entry;
        auto manifest = Game::cachedManifest();
        for (const auto& e : manifest)
            if (e.id == mcVersion) entry = e;
        if (entry.isNull()) {
            manifest = Game::fetchManifest(nam);
            for (const auto& e : manifest)
                if (e.id == mcVersion) entry = e;
        }
        if (entry.isNull()) throw Error{"Version Minecraft introuvable : " + mcVersion};
        Game::install(nam, entry, progress);
    }

    if (loader == Loader::Fabric || loader == Loader::Quilt) return installFabricLike(nam, loader, mcVersion, loaderVersion, progress);
    return installForgeLike(nam, loader, mcVersion, loaderVersion, progress);
}

} // namespace ModLoader
