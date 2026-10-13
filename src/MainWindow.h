#pragma once
#include "Auth.h"
#include "Game.h"
#include "Mods.h"
#include "Skins.h"

#include <QMainWindow>
#include <functional>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;
class QNetworkAccessManager;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class NavButton;
class HeroBanner;
class AccountChip;
class ToggleSwitch;

class MainWindow : public QMainWindow {
public:
    explicit MainWindow(bool mica, QWidget* parent = nullptr);

private:
    enum class Sev { Info, Success, Error };
    using Job = std::function<void(const Progress&)>;

    QWidget* buildSidebar();
    QWidget* buildHome();
    QWidget* buildVersions();
    QWidget* buildMods();
    QWidget* buildTextures();
    QWidget* buildInstances();
    QWidget* buildSkin();
    QWidget* buildSettings();
    QFrame* settingsCard(char16_t glyph, const QString& title, const QString& desc, QWidget* control,
                         QLabel** descOut = nullptr);

    void goTo(int page);
    void runTask(Job job, std::function<void()> onDone = {});
    void setBusy(bool busy);
    void onProgress(const QString& status, qint64 done, qint64 total);
    void showInfo(const QString& msg, Sev sev);

    void loadVersions();
    void refreshVersionCombo();
    void refreshVersionList();
    void refreshAccountUi();
    VersionEntry findEntry(const QString& id) const;

    void play(const QString& id);
    void install(const QString& id);
    void login();
    void logout();
    void loadSettings();
    void saveAccount();

    void refreshModMcCombo();
    void refreshModLoaderVersions();
    void installModLoader();
    void refreshModProfiles();
    void refreshInstalledMods();
    void searchMods();
    void installMod(const QString& projectId, const QString& profileId);

    void refreshTexProfiles();
    void searchTextures();
    void installTexture(const QString& projectId, const QString& profileId, ContentType type);
    void refreshInstalledTextures();

    void refreshInstances();

    void refreshSkin();
    void applySkinProfileUi();
    void changeSkin(const QString& filePath, const QString& variant);
    void resetSkinAction();
    void activateCape(const QString& capeId);

    bool m_mica;
    bool m_busy = false;
    Account m_account;
    QList<VersionEntry> m_versions;

    QList<NavButton*> m_nav;
    QStackedWidget* m_pages = nullptr;
    QFrame* m_infoBar = nullptr;
    QLabel* m_infoIcon = nullptr;
    QLabel* m_infoText = nullptr;
    QTimer* m_infoTimer = nullptr;
    AccountChip* m_chip = nullptr;

    HeroBanner* m_hero = nullptr;
    QComboBox* m_versionCombo = nullptr;
    QPushButton* m_playBtn = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_statusCount = nullptr;
    QProgressBar* m_progress = nullptr;

    QLineEdit* m_search = nullptr;
    ToggleSwitch* m_snapToggle = nullptr;
    QListWidget* m_list = nullptr;

    QComboBox* m_modMcCombo = nullptr;
    QComboBox* m_modLoaderCombo = nullptr;
    QComboBox* m_modLoaderVerCombo = nullptr;
    QListWidget* m_modProfileList = nullptr;
    QComboBox* m_modProfileCombo = nullptr;
    QLineEdit* m_modSearch = nullptr;
    QListWidget* m_modResultsList = nullptr;
    QListWidget* m_modInstalledList = nullptr;
    QNetworkAccessManager* m_net = nullptr; // chargement asynchrone des icônes de mods (fil GUI)

    QComboBox* m_texProfileCombo = nullptr;
    QComboBox* m_texTypeCombo = nullptr;
    QLineEdit* m_texSearch = nullptr;
    QListWidget* m_texResultsList = nullptr;
    QListWidget* m_texInstalledList = nullptr;

    QVBoxLayout* m_instancesList = nullptr;

    QLabel* m_skinPreview = nullptr;
    QLabel* m_skinVariantLabel = nullptr;
    QComboBox* m_skinVariantCombo = nullptr;
    QListWidget* m_capeList = nullptr;
    MinecraftProfile m_mcProfile;

    QLabel* m_accountDesc = nullptr;
    QPushButton* m_accountBtn = nullptr;
    QSlider* m_ram = nullptr;
    QLabel* m_ramLabel = nullptr;
    QLineEdit* m_clientId = nullptr;
    QLineEdit* m_jvmArgs = nullptr;
    ToggleSwitch* m_closeToggle = nullptr;
    ToggleSwitch* m_micaToggle = nullptr;
};
