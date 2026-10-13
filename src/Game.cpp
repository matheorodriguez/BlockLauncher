#include "Game.h"
#include "Net.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>

namespace {

const QString kManifestUrl = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
const QString kJavaIndexUrl =
    "https://launchermeta.mojang.com/v1/products/java-runtime/2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json";
const QString kAssetsUrl = "https://resources.download.minecraft.net/";

// ---------- Plateforme ----------
QString osName()
{
#if defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_MACOS)
    return "osx";
#else
    return "linux";
#endif
}

QString cpuArch() { return QSysInfo::currentCpuArchitecture(); } // x86_64, arm64, i386
bool is64() { return cpuArch() != "i386"; }

QString javaPlatform()
{
    const QString a = cpuArch();
#if defined(Q_OS_WIN)
    return a == "arm64" ? "windows-arm64" : (a == "x86_64" ? "windows-x64" : "windows-x86");
#elif defined(Q_OS_MACOS)
    return a == "arm64" ? "mac-os-arm64" : "mac-os";
#else
    return a == "i386" ? "linux-i386" : "linux";
#endif
}

QString cpSep()
{
#if defined(Q_OS_WIN)
    return ";";
#else
    return ":";
#endif
}

// ---------- Chemins ----------
QString root() { return Game::rootDir(); }
QString versionDir(const QString& id) { return root() + "/versions/" + id; }
QString versionJsonPath(const QString& id) { return versionDir(id) + "/" + id + ".json"; }
QString versionJarPath(const QString& id) { return versionDir(id) + "/" + id + ".jar"; }
QString nativesDir(const QString& id) { return versionDir(id) + "/natives"; }
QString librariesDir() { return root() + "/libraries"; }
QString assetsDir() { return root() + "/assets"; }
QString runtimeDir(const QString& component) { return root() + "/runtime/" + component; }

QString javaExe(const QString& component)
{
#if defined(Q_OS_WIN)
    return runtimeDir(component) + "/bin/javaw.exe";
#elif defined(Q_OS_MACOS)
    return runtimeDir(component) + "/jre.bundle/Contents/Home/bin/java";
#else
    return runtimeDir(component) + "/bin/java";
#endif
}

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
        throw Error{"Impossible d'écrire " + QDir::toNativeSeparators(path)};
}

QJsonObject readJsonFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw Error{"Fichier introuvable : " + QDir::toNativeSeparators(path)};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// ---------- Règles des bibliothèques / arguments ----------
bool rulesAllow(const QJsonArray& rules)
{
    if (rules.isEmpty()) return true;
    bool allow = false;
    for (const auto& r : rules) {
        const auto o = r.toObject();
        bool match = !o.contains("features"); // démo, résolution perso, quickplay… non gérés
        if (o.contains("os")) {
            const auto os = o["os"].toObject();
            if (os.contains("name") && os["name"].toString() != osName()) match = false;
            if (os.contains("arch") && os["arch"].toString() == "x86" && is64()) match = false;
        }
        if (match) allow = o["action"].toString() == "allow";
    }
    return allow;
}

// ---------- Téléchargements parallèles ----------
struct DlItem {
    QString url, path, sha1;
    qint64 size = 0;
    bool executable = false;
};

void downloadAll(QNetworkAccessManager& nam, const QList<DlItem>& items, const Progress& progress,
                 const QString& label)
{
    QList<DlItem> todo;
    for (const auto& it : items) {
        QFileInfo fi(it.path);
        if (fi.exists() && (it.size <= 0 || fi.size() == it.size)) continue;
        todo << it;
    }
    if (todo.isEmpty()) return;

    constexpr int kParallel = 16;
    int next = 0, active = 0, done = 0;
    QString error;
    QEventLoop loop;
    std::function<void(int, int)> start;
    std::function<void()> pump;

    start = [&](int idx, int attempt) {
        const DlItem& it = todo[idx];
        QNetworkRequest req{QUrl(it.url)};
        req.setTransferTimeout(60000);
        req.setHeader(QNetworkRequest::UserAgentHeader, "BlockLauncher/1.0");
        QNetworkReply* r = nam.get(req);
        ++active;
        QObject::connect(r, &QNetworkReply::finished, &loop, [&, r, idx, attempt] {
            --active;
            r->deleteLater();
            const DlItem& it = todo[idx];
            bool ok = r->error() == QNetworkReply::NoError;
            QByteArray data = ok ? r->readAll() : QByteArray();
            if (ok && !it.sha1.isEmpty() &&
                QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex() != it.sha1.toLatin1())
                ok = false;
            if (ok) {
                try {
                    writeFile(it.path, data);
                    if (it.executable)
                        QFile::setPermissions(it.path, QFile::permissions(it.path) | QFileDevice::ExeOwner |
                                                           QFileDevice::ExeGroup | QFileDevice::ExeOther);
                } catch (const Error& e) {
                    if (error.isEmpty()) error = e.msg;
                }
                progress(label, ++done, todo.size());
            } else if (attempt < 3) {
                start(idx, attempt + 1);
                return;
            } else if (error.isEmpty()) {
                error = "Téléchargement échoué : " + it.url;
            }
            pump();
        });
    };
    pump = [&] {
        while (error.isEmpty() && active < kParallel && next < todo.size()) start(next++, 1);
        if (active == 0) loop.quit();
    };

    progress(label, 0, todo.size());
    pump();
    if (active > 0) loop.exec();
    if (!error.isEmpty()) throw Error{error};
}

// ---------- Bibliothèques ----------
struct Lib {
    DlItem dl;
    bool native = false;
};

QString mavenPath(const QString& name)
{
    // groupe:artefact:version[:classifier]
    const QStringList p = name.split(':');
    if (p.size() < 3) return {};
    QString path = QString(p[0]).replace('.', '/') + "/" + p[1] + "/" + p[2] + "/" + p[1] + "-" + p[2];
    if (p.size() > 3) path += "-" + p[3];
    return path + ".jar";
}

QList<Lib> collectLibraries(const QJsonObject& v)
{
    QList<Lib> libs;
    for (const auto& l : v["libraries"].toArray()) {
        const auto o = l.toObject();
        if (!rulesAllow(o["rules"].toArray())) continue;
        const auto dls = o["downloads"].toObject();

        if (dls.contains("artifact")) {
            const auto a = dls["artifact"].toObject();
            QString path = a["path"].toString();
            if (path.isEmpty()) path = mavenPath(o["name"].toString());
            libs << Lib{{a["url"].toString(), librariesDir() + "/" + path, a["sha1"].toString(),
                         qint64(a["size"].toDouble())}, false};
        }
        if (o.contains("natives")) {
            QString key = o["natives"].toObject()[osName()].toString();
            key.replace("${arch}", is64() ? "64" : "32");
            const auto c = dls["classifiers"].toObject()[key].toObject();
            if (!key.isEmpty() && !c.isEmpty())
                libs << Lib{{c["url"].toString(), librariesDir() + "/" + c["path"].toString(), c["sha1"].toString(),
                             qint64(c["size"].toDouble())}, true};
        }
    }
    return libs;
}

void extractNatives(const QList<Lib>& libs, const QString& dir)
{
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    for (const auto& l : libs) {
        if (!l.native) continue;
        QProcess p;
#if defined(Q_OS_LINUX)
        p.start("unzip", {"-o", "-q", l.dl.path, "-d", dir, "-x", "META-INF/*"});
#else
        // tar (bsdtar) sait lire les .jar/.zip sous Windows 10+ et macOS
        p.start("tar", {"-xf", l.dl.path, "-C", dir, "--exclude", "META-INF"});
#endif
        if (!p.waitForFinished(120000) || p.exitCode() != 0)
            throw Error{"Impossible d'extraire les bibliothèques natives (" + QFileInfo(l.dl.path).fileName() + ")."};
    }
}

// ---------- Ressources (sons, langues, textures) ----------
void installAssets(QNetworkAccessManager& nam, const QJsonObject& v, const Progress& progress)
{
    const auto ai = v["assetIndex"].toObject();
    const QString id = ai["id"].toString();
    const QString indexPath = assetsDir() + "/indexes/" + id + ".json";
    downloadAll(nam, {{ai["url"].toString(), indexPath, ai["sha1"].toString(), qint64(ai["size"].toDouble())}},
                progress, "Index des ressources");

    const auto index = readJsonFile(indexPath);
    const auto objects = index["objects"].toObject();
    QList<DlItem> items;
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        const auto o = it.value().toObject();
        const QString hash = o["hash"].toString();
        const QString rel = hash.left(2) + "/" + hash;
        items << DlItem{kAssetsUrl + rel, assetsDir() + "/objects/" + rel, hash, qint64(o["size"].toDouble())};
    }
    downloadAll(nam, items, progress, "Ressources du jeu");

    // Anciennes versions (≤ 1.7.2) : les ressources doivent exister sous leur vrai nom.
    const bool virt = index["virtual"].toBool();
    const bool mapRes = index["map_to_resources"].toBool();
    if (!virt && !mapRes) return;
    const QString target = mapRes ? root() + "/resources" : assetsDir() + "/virtual/" + id;
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        const QString hash = it.value().toObject()["hash"].toString();
        const QString dst = target + "/" + it.key();
        if (QFileInfo::exists(dst)) continue;
        QDir().mkpath(QFileInfo(dst).absolutePath());
        QFile::copy(assetsDir() + "/objects/" + hash.left(2) + "/" + hash, dst);
    }
}

// ---------- Java (runtime officiel de Mojang) ----------
QString javaComponent(const QJsonObject& v)
{
    const QString c = v["javaVersion"].toObject()["component"].toString();
    return c.isEmpty() ? "jre-legacy" : c;
}

void installJava(QNetworkAccessManager& nam, const QString& component, const Progress& progress)
{
    progress("Recherche de Java…", 0, 0);
    const auto all = getJson(nam, kJavaIndexUrl);
    const auto entries = all[javaPlatform()].toObject()[component].toArray();
    if (entries.isEmpty())
        throw Error{QString("Java « %1 » n'est pas disponible pour ta plateforme (%2).").arg(component, javaPlatform())};

    const auto manifest = getJson(nam, entries[0].toObject()["manifest"].toObject()["url"].toString());
    const auto files = manifest["files"].toObject();
    const QString base = runtimeDir(component);
    QList<DlItem> items;
    QList<QPair<QString, QString>> links;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const auto o = it.value().toObject();
        const QString path = base + "/" + it.key();
        const QString type = o["type"].toString();
        if (type == "directory") {
            QDir().mkpath(path);
        } else if (type == "file") {
            const auto raw = o["downloads"].toObject()["raw"].toObject();
            items << DlItem{raw["url"].toString(), path, raw["sha1"].toString(), qint64(raw["size"].toDouble()),
                            o["executable"].toBool()};
        } else if (type == "link") {
            links << qMakePair(path, o["target"].toString());
        }
    }
    downloadAll(nam, items, progress, "Installation de Java");
    for (const auto& [path, target] : links)
        if (!QFileInfo::exists(path)) QFile::link(target, path);
}

// ---------- Arguments ----------
QString substitute(QString s, const QHash<QString, QString>& vars)
{
    static const QRegularExpression re(R"(\$\{([^}]+)\})");
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += vars.contains(m.captured(1)) ? vars.value(m.captured(1)) : m.captured(0);
        last = m.capturedEnd();
    }
    return out + s.mid(last);
}

QStringList collectArgs(const QJsonArray& arr, const QHash<QString, QString>& vars)
{
    QStringList out;
    for (const auto& a : arr) {
        if (a.isString()) {
            out << substitute(a.toString(), vars);
            continue;
        }
        const auto o = a.toObject();
        if (!rulesAllow(o["rules"].toArray())) continue;
        const auto val = o["value"];
        if (val.isString()) out << substitute(val.toString(), vars);
        else
            for (const auto& x : val.toArray()) out << substitute(x.toString(), vars);
    }
    return out;
}

// ---------- Résolution des chaînes « inheritsFrom » (profils moddés) ----------
// Fusionne récursivement un json de version avec son parent : les bibliothèques et les
// arguments sont concaténés, les autres champs sont écrasés par l'enfant. `jarIdOut`
// reçoit l'identifiant de la version qui possède réellement le .jar client (l'ancêtre racine).
QJsonObject resolveVersionJson(const QString& id, QString* jarIdOut = nullptr, int depth = 0)
{
    if (depth > 8) throw Error{"Chaîne de version trop profonde : " + id};
    const QJsonObject v = readJsonFile(versionJsonPath(id));
    const QString inherits = v["inheritsFrom"].toString();
    if (inherits.isEmpty()) {
        if (jarIdOut) *jarIdOut = id;
        return v;
    }

    QString parentJarId;
    QJsonObject merged = resolveVersionJson(inherits, &parentJarId, depth + 1);
    if (jarIdOut) *jarIdOut = parentJarId;

    for (auto it = v.begin(); it != v.end(); ++it) {
        if (it.key() == "libraries" || it.key() == "arguments" || it.key() == "inheritsFrom") continue;
        merged[it.key()] = it.value();
    }
    QJsonArray libs = merged["libraries"].toArray();
    for (const auto& l : v["libraries"].toArray()) libs.append(l);
    merged["libraries"] = libs;

    if (v.contains("arguments") || merged.contains("arguments")) {
        const QJsonObject parentArgs = merged["arguments"].toObject();
        const QJsonObject childArgs = v["arguments"].toObject();
        QJsonObject margs;
        for (const char* k : {"jvm", "game"}) {
            QJsonArray arr = parentArgs[QLatin1String(k)].toArray();
            for (const auto& a : childArgs[QLatin1String(k)].toArray()) arr.append(a);
            margs[QLatin1String(k)] = arr;
        }
        merged["arguments"] = margs;
    }
    merged["id"] = v["id"];
    return merged;
}

// ---------- Extraction générique d'archive (.jar/.zip) ----------
// Réutilise `tar` (présent sur Windows 10+/macOS) ou `unzip` (Linux), comme extractNatives().
void extractArchiveImpl(const QString& archivePath, const QString& destDir)
{
    QDir(destDir).removeRecursively();
    QDir().mkpath(destDir);
    QProcess p;
#if defined(Q_OS_LINUX)
    p.start("unzip", {"-o", "-q", archivePath, "-d", destDir});
#else
    p.start("tar", {"-xf", archivePath, "-C", destDir});
#endif
    if (!p.waitForFinished(180000) || p.exitCode() != 0)
        throw Error{"Impossible d'extraire l'archive : " + QFileInfo(archivePath).fileName()};
}

} // namespace

namespace Game {

QString rootDir()
{
    static const QString dir = [] {
#if defined(Q_OS_WIN)
        QString base = qEnvironmentVariable("APPDATA");
        if (base.isEmpty()) base = QDir::homePath();
#else
        QString base = QDir::homePath();
#endif
        const QString d = QDir::cleanPath(base + "/.blocklauncher");
        QDir().mkpath(d);
        return d;
    }();
    return dir;
}

static QList<VersionEntry> parseManifest(const QJsonObject& m)
{
    QList<VersionEntry> list;
    for (const auto& v : m["versions"].toArray()) {
        const auto o = v.toObject();
        list << VersionEntry{o["id"].toString(), o["type"].toString(), o["url"].toString()};
    }
    return list;
}

QList<VersionEntry> fetchManifest(QNetworkAccessManager& nam)
{
    const QByteArray raw = httpGet(nam, kManifestUrl);
    writeFile(root() + "/version_manifest.json", raw);
    return parseManifest(QJsonDocument::fromJson(raw).object());
}

QList<VersionEntry> cachedManifest()
{
    QFile f(root() + "/version_manifest.json");
    if (!f.open(QIODevice::ReadOnly)) return {};
    return parseManifest(QJsonDocument::fromJson(f.readAll()).object());
}

bool isInstalled(const QString& id)
{
    if (!QFileInfo::exists(versionJsonPath(id)) || !QFileInfo::exists(versionDir(id) + "/.complete")) return false;
    QString jarId;
    try {
        resolveVersionJson(id, &jarId);
    } catch (...) {
        return false;
    }
    return QFileInfo::exists(versionJarPath(jarId));
}

QStringList installedVersions()
{
    QStringList out;
    const auto dirs = QDir(root() + "/versions").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& d : dirs)
        if (isInstalled(d) && !QFileInfo::exists(versionDir(d) + "/blmodloader.json")) out << d;
    return out;
}

void install(QNetworkAccessManager& nam, const VersionEntry& entry, const Progress& progress)
{
    const QString id = entry.id;
    progress("Préparation de " + id + "…", 0, 0);

    const QByteArray raw = httpGet(nam, entry.url);
    const auto v = QJsonDocument::fromJson(raw).object();
    if (v.contains("inheritsFrom")) throw Error{"Les versions moddées ne sont pas encore prises en charge."};
    writeFile(versionJsonPath(id), raw);

    // Jeu + bibliothèques
    const auto client = v["downloads"].toObject()["client"].toObject();
    QList<DlItem> items{{client["url"].toString(), versionJarPath(id), client["sha1"].toString(),
                         qint64(client["size"].toDouble())}};
    const auto libs = collectLibraries(v);
    for (const auto& l : libs) items << l.dl;
    downloadAll(nam, items, progress, "Bibliothèques");

    installAssets(nam, v, progress);
    installJava(nam, javaComponent(v), progress);

    progress("Finalisation…", 0, 0);
    extractNatives(libs, nativesDir(id));
    writeFile(versionDir(id) + "/.complete", "ok");
}

qint64 launch(const QString& id, const Account& acc, const LaunchOptions& opts)
{
    QString jarId;
    const auto v = resolveVersionJson(id, &jarId);
    const QString java = javaExe(javaComponent(v));
    if (!QFileInfo::exists(java)) throw Error{"Java est introuvable, réinstalle cette version."};

    QStringList cp;
    for (const auto& l : collectLibraries(v))
        if (!l.native && !cp.contains(l.dl.path)) cp << QDir::toNativeSeparators(l.dl.path);
    cp << QDir::toNativeSeparators(versionJarPath(jarId));

    const auto ai = v["assetIndex"].toObject();
    const QString assetId = ai["id"].toString();
    const QString gameAssets = (assetId == "legacy" || assetId == "pre-1.6")
                                   ? assetsDir() + "/virtual/" + assetId : assetsDir();

    // Les profils moddés (id != jarId) utilisent un dossier de jeu isolé, pour que leurs
    // mods/saves n'interfèrent pas avec le jeu vanilla ni avec d'autres modpacks.
    const QString gameDir = (id != jarId) ? (root() + "/instances/" + id) : root();
    QDir().mkpath(gameDir);

    const QHash<QString, QString> vars{
        {"auth_player_name", acc.name},
        {"version_name", id},
        {"game_directory", QDir::toNativeSeparators(gameDir)},
        {"assets_root", QDir::toNativeSeparators(assetsDir())},
        {"game_assets", QDir::toNativeSeparators(gameAssets)},
        {"assets_index_name", assetId},
        {"auth_uuid", acc.uuid},
        {"auth_access_token", acc.accessToken},
        {"auth_session", "token:" + acc.accessToken + ":" + acc.uuid},
        {"auth_xuid", "0"},
        {"clientid", ""},
        {"user_type", "msa"},
        {"user_properties", "{}"},
        {"version_type", v["type"].toString()},
        {"natives_directory", QDir::toNativeSeparators(nativesDir(id))},
        {"library_directory", QDir::toNativeSeparators(librariesDir())},
        {"classpath_separator", cpSep()},
        {"classpath", cp.join(cpSep())},
        {"launcher_name", "BlockLauncher"},
        {"launcher_version", "1.0"},
    };

    QStringList args{QString("-Xmx%1G").arg(opts.ramGb), "-Xms512M"};
    if (!opts.extraJvmArgs.trimmed().isEmpty()) args << QProcess::splitCommand(opts.extraJvmArgs);

    if (v.contains("arguments")) {
        const auto a = v["arguments"].toObject();
        args << collectArgs(a["jvm"].toArray(), vars);
        args << v["mainClass"].toString();
        args << collectArgs(a["game"].toArray(), vars);
    } else {
        // Format ancien (≤ 1.12)
        args << substitute("-Djava.library.path=${natives_directory}", vars) << "-cp" << vars["classpath"];
        args << v["mainClass"].toString();
        for (const auto& part : v["minecraftArguments"].toString().split(' ', Qt::SkipEmptyParts))
            args << substitute(part, vars);
    }

    qint64 pid = 0;
    if (!QProcess::startDetached(java, args, gameDir, &pid)) throw Error{"Impossible de démarrer Java."};
    return pid;
}

// ---------------------------------------------------------------------------
//  Profils moddés
// ---------------------------------------------------------------------------
QList<ModProfile> installedModProfiles()
{
    QList<ModProfile> out;
    const auto dirs = QDir(root() + "/versions").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& id : dirs) {
        const QString metaPath = versionDir(id) + "/blmodloader.json";
        if (!QFileInfo::exists(metaPath) || !isInstalled(id)) continue;
        QFile f(metaPath);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const auto o = QJsonDocument::fromJson(f.readAll()).object();
        out << ModProfile{id, o["mcVersion"].toString(), o["loader"].toString(), o["loaderVersion"].toString()};
    }
    return out;
}

bool isModded(const QString& id)
{
    return QFileInfo::exists(versionDir(id) + "/blmodloader.json");
}

QList<ModProfile> allProfiles()
{
    QList<ModProfile> out;
    for (const auto& id : installedVersions()) out << ModProfile{id, id, QString(), QString()};
    out << installedModProfiles();
    return out;
}

QString instanceDir(const QString& id)
{
    return isModded(id) ? root() + "/instances/" + id : root();
}

void removeProfile(const QString& id)
{
    QDir(versionDir(id)).removeRecursively();
    if (isModded(id)) QDir(root() + "/instances/" + id).removeRecursively();
}

// ---------------------------------------------------------------------------
//  Primitives bas niveau (utilisées par ModLoader.cpp)
// ---------------------------------------------------------------------------
QString versionDir(const QString& id) { return ::versionDir(id); }
QString versionJsonPath(const QString& id) { return ::versionJsonPath(id); }
QString versionJarPath(const QString& id) { return ::versionJarPath(id); }
QString librariesDir() { return ::librariesDir(); }
QString mavenPath(const QString& coord) { return ::mavenPath(coord); }
QJsonObject readVersionJson(const QString& id) { return readJsonFile(versionJsonPath(id)); }

QString javaExeForVersion(const QString& mcVersionId)
{
    const auto v = readJsonFile(::versionJsonPath(mcVersionId));
    return javaExe(javaComponent(v));
}

void writeJsonFile(const QString& path, const QJsonObject& obj)
{
    writeFile(path, QJsonDocument(obj).toJson());
}

void writeModMeta(const QString& id, const QString& mcVersion, const QString& loader, const QString& loaderVersion)
{
    QJsonObject o;
    o["mcVersion"] = mcVersion;
    o["loader"] = loader;
    o["loaderVersion"] = loaderVersion;
    writeJsonFile(versionDir(id) + "/blmodloader.json", o);
}

void downloadLibraries(QNetworkAccessManager& nam, const QJsonArray& libraries, const Progress& progress,
                       const QString& label)
{
    QJsonObject tmp;
    tmp["libraries"] = libraries;
    QList<DlItem> items;
    for (const auto& l : collectLibraries(tmp))
        if (!l.dl.url.isEmpty()) items << l.dl;
    downloadAll(nam, items, progress, label);
}

void extractArchive(const QString& archivePath, const QString& destDir) { extractArchiveImpl(archivePath, destDir); }

QString jarMainClass(const QString& jarPath)
{
    const QString tmp = jarPath + ".manifest_tmp";
    extractArchiveImpl(jarPath, tmp);
    QFile f(tmp + "/META-INF/MANIFEST.MF");
    QString result;
    if (f.open(QIODevice::ReadOnly)) {
        QString content = QString::fromUtf8(f.readAll());
        content.replace("\r\n", "\n");
        content.replace("\n ", ""); // dépliage des lignes repliées du manifeste
        for (const auto& line : content.split('\n'))
            if (line.startsWith("Main-Class:")) {
                result = line.mid(11).trimmed();
                break;
            }
    }
    QDir(tmp).removeRecursively();
    if (result.isEmpty()) throw Error{"Main-Class introuvable dans " + QFileInfo(jarPath).fileName()};
    return result;
}

void runBlocking(const QString& exe, const QStringList& args, const QString& workDir)
{
    QProcess p;
    p.setWorkingDirectory(workDir);
    p.start(exe, args);
    if (!p.waitForFinished(600000)) throw Error{"Le processus d'installation ne répond pas : " + exe};
    if (p.exitCode() != 0) {
        const QString out = QString::fromUtf8(p.readAllStandardOutput() + p.readAllStandardError());
        throw Error{"Échec de l'installation (" + QFileInfo(exe).fileName() + ") :\n" + out.right(2000)};
    }
}

void markComplete(const QString& id) { writeFile(versionDir(id) + "/.complete", "ok"); }

} // namespace Game
