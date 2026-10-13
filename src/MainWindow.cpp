#include "Config.h"
#include "MainWindow.h"
#include "ModLoader.h"
#include "Mods.h"
#include "Net.h"
#include "Skins.h"
#include "Style.h"
#include "Update.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QThread>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

using namespace Fluent;

// ============================================================================
//  Contrôles Fluent personnalisés
// ============================================================================

static QFont uiFont(int px, int weight = QFont::Normal, bool display = false)
{
    QFont f;
    f.setFamilies(display ? QStringList{"Segoe UI Variable Display", "Segoe UI"}
                          : QStringList{"Segoe UI Variable Text", "Segoe UI"});
    f.setPixelSize(px);
    f.setWeight(QFont::Weight(weight));
    return f;
}

// Élément de navigation avec la petite « pilule » d'accent de Windows 11.
class NavButton : public QAbstractButton {
public:
    NavButton(char16_t glyph, const QString& text, QWidget* parent = nullptr)
        : QAbstractButton(parent), m_glyph(glyph)
    {
        setText(text);
        setCheckable(true);
        setAutoExclusive(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFixedHeight(40);
    }
    QSize sizeHint() const override { return {200, 40}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(4, 2, -4, -2);
        int alpha = isChecked() ? 15 : (underMouse() ? 10 : 0);
        if (isDown()) alpha = 7;
        if (alpha) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, alpha));
            p.drawRoundedRect(r, 4, 4);
        }
        if (isChecked()) {
            p.setBrush(Accent);
            p.drawRoundedRect(QRectF(r.left(), r.center().y() - 8, 3, 16), 1.5, 1.5);
        }
        p.setPen(TextPrimary);
        p.setFont(iconFont(16));
        p.drawText(QRectF(r.left() + 14, r.top(), 18, r.height()), Qt::AlignCenter, QString(QChar(m_glyph)));
        p.setFont(uiFont(14));
        p.drawText(QRectF(r.left() + 46, r.top(), r.width() - 50, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                   text());
    }

private:
    char16_t m_glyph;
};

// Interrupteur « ToggleSwitch » animé.
class ToggleSwitch : public QAbstractButton {
public:
    explicit ToggleSwitch(QWidget* parent = nullptr) : QAbstractButton(parent)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFixedSize(40, 20);
        m_anim.setDuration(150);
        m_anim.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_pos = v.toReal();
            update();
        });
        QObject::connect(this, &QAbstractButton::toggled, this, [this](bool on) {
            m_anim.stop();
            m_anim.setStartValue(m_pos);
            m_anim.setEndValue(on ? 1.0 : 0.0);
            m_anim.start();
        });
    }
    void setCheckedNoAnim(bool on)
    {
        setChecked(on);
        m_anim.stop();
        m_pos = on ? 1.0 : 0.0;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF track = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool on = isChecked();
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(underMouse() ? AccentHover : Accent);
        } else {
            p.setPen(QPen(QColor(255, 255, 255, 155), 1));
            p.setBrush(QColor(0, 0, 0, underMouse() ? 40 : 25));
        }
        p.drawRoundedRect(track, 10, 10);
        const qreal knob = underMouse() ? 14 : 12;
        const QPointF c(10 + m_pos * 20, height() / 2.0);
        p.setPen(Qt::NoPen);
        p.setBrush(on ? QColor(0, 0, 0) : QColor(255, 255, 255, 200));
        p.drawEllipse(c, knob / 2, knob / 2);
    }

private:
    QVariantAnimation m_anim;
    qreal m_pos = 0;
};

// ComboBox avec le chevron Fluent.
class FluentCombo : public QComboBox {
public:
    explicit FluentCombo(QWidget* parent = nullptr) : QComboBox(parent)
    {
        setItemDelegate(new QStyledItemDelegate(this));
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void paintEvent(QPaintEvent* e) override
    {
        QComboBox::paintEvent(e);
        QPainter p(this);
        p.setFont(iconFont(10));
        p.setPen(isEnabled() ? TextSecondary : TextTertiary);
        p.drawText(QRect(width() - 30, 0, 20, height()), Qt::AlignCenter, QString(QChar(Glyph::ChevronDown)));
    }
};

// Bannière d'accueil : paysage en blocs dessiné à la main (aucune image externe).
class HeroBanner : public QWidget {
public:
    explicit HeroBanner(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setPlayer(const QString& name)
    {
        m_name = name;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = rect();
        QPainterPath clip;
        clip.addRoundedRect(r, 8, 8);
        p.setClipPath(clip);
        p.setRenderHint(QPainter::Antialiasing, false);

        // Ciel au coucher du soleil
        QLinearGradient sky(0, 0, 0, r.height());
        sky.setColorAt(0.0, QColor("#16224a"));
        sky.setColorAt(0.55, QColor("#5a4a8a"));
        sky.setColorAt(1.0, QColor("#f2a46b"));
        p.fillRect(r, sky);

        const int b = 12; // taille d'un bloc
        // Soleil carré
        const int sx = int(r.width() * 0.70) / b * b, sy = int(r.height() * 0.30) / b * b;
        p.fillRect(QRect(sx - 6, sy - 6, b * 4 + 12, b * 4 + 12), QColor(255, 214, 120, 60));
        p.fillRect(QRect(sx, sy, b * 4, b * 4), QColor("#ffd27a"));

        // Nuages
        auto cloud = [&](int cx, int cy, int w) {
            p.fillRect(QRect(cx, cy, w * b, b), QColor(255, 255, 255, 150));
            p.fillRect(QRect(cx + b, cy - b, (w - 2) * b, b), QColor(255, 255, 255, 150));
        };
        cloud(int(r.width() * 0.18) / b * b, 36, 7);
        cloud(int(r.width() * 0.48) / b * b, 60, 5);
        cloud(int(r.width() * 0.86) / b * b, 30, 6);

        // Collines lointaines puis proches
        const int cols = int(r.width()) / b + 1;
        for (int i = 0; i < cols; ++i) {
            const double x = i;
            const int hFar = int(5 + 2.5 * std::sin(x * 0.11) + 1.6 * std::sin(x * 0.037 + 2.0));
            const int top = int(r.height()) - hFar * b - b * 4;
            p.fillRect(QRect(i * b, top, b, int(r.height()) - top), QColor("#3d3a6b"));
        }
        for (int i = 0; i < cols; ++i) {
            const double x = i;
            const int h = int(3 + 1.8 * std::sin(x * 0.17 + 1.0) + 1.2 * std::sin(x * 0.05 + 0.4));
            const int top = int(r.height()) - h * b;
            p.fillRect(QRect(i * b, top, b, b), QColor("#5fae4c"));
            p.fillRect(QRect(i * b, top + b, b, int(r.height()) - top), QColor("#7a5434"));
            if ((i * 7) % 5 == 0) p.fillRect(QRect(i * b, top + b * 2, b, b), QColor("#6b4a2e"));
            if ((i * 3) % 4 == 0) p.fillRect(QRect(i * b, top, b, 4), QColor("#79c763"));
        }

        // Voile pour la lisibilité du texte
        QLinearGradient veil(0, 0, r.width() * 0.7, 0);
        veil.setColorAt(0, QColor(0, 0, 0, 150));
        veil.setColorAt(1, QColor(0, 0, 0, 0));
        p.fillRect(r, veil);

        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(TextPrimary);
        p.setFont(uiFont(32, QFont::DemiBold, true));
        const QString title = m_name.isEmpty() ? QStringLiteral("Bienvenue !") : "Bon retour, " + m_name;
        p.drawText(QRectF(32, 32, r.width() - 64, 44), Qt::AlignLeft | Qt::AlignVCenter, title);
        p.setPen(TextSecondary);
        p.setFont(uiFont(15));
        p.drawText(QRectF(32, 78, r.width() - 64, 24), Qt::AlignLeft | Qt::AlignVCenter,
                   m_name.isEmpty() ? QStringLiteral("Connecte ton compte Microsoft pour commencer à jouer.")
                                    : QStringLiteral("Prêt pour une nouvelle aventure ?"));
    }

private:
    QString m_name;
};

// Pastille du compte en bas de la barre latérale.
class AccountChip : public QAbstractButton {
public:
    explicit AccountChip(QWidget* parent = nullptr) : QAbstractButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFixedHeight(56);
    }
    void setAccount(const QString& name)
    {
        m_name = name;
        update();
    }
    QSize sizeHint() const override { return {200, 56}; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(4, 4, -4, -4);
        if (underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, isDown() ? 7 : 10));
            p.drawRoundedRect(r, 4, 4);
        }
        const QRectF av(r.left() + 8, r.center().y() - 16, 32, 32);
        p.setPen(Qt::NoPen);
        p.setBrush(m_name.isEmpty() ? QColor(255, 255, 255, 30) : Accent);
        p.drawEllipse(av);
        if (m_name.isEmpty()) {
            p.setPen(TextPrimary);
            p.setFont(iconFont(14));
            p.drawText(av, Qt::AlignCenter, QString(QChar(Glyph::Contact)));
        } else {
            p.setPen(Qt::black);
            p.setFont(uiFont(14, QFont::DemiBold));
            p.drawText(av, Qt::AlignCenter, m_name.left(1).toUpper());
        }
        p.setPen(TextPrimary);
        p.setFont(uiFont(13, QFont::DemiBold));
        p.drawText(QRectF(av.right() + 12, r.top() + 8, r.width() - 60, 18), Qt::AlignLeft | Qt::AlignVCenter,
                   m_name.isEmpty() ? QStringLiteral("Non connecté") : m_name);
        p.setPen(TextTertiary);
        p.setFont(uiFont(11));
        p.drawText(QRectF(av.right() + 12, r.top() + 26, r.width() - 60, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   m_name.isEmpty() ? QStringLiteral("Clique pour te connecter") : QStringLiteral("Compte Microsoft"));
    }

private:
    QString m_name;
};

// Icône arrondie (mod ou modloader) : affiche une image téléchargée une fois disponible,
// sinon un glyphe/des initiales sur un fond coloré en attendant.
class ModIcon : public QWidget {
public:
    explicit ModIcon(int size = 40, QWidget* parent = nullptr) : QWidget(parent) { setFixedSize(size, size); }

    void setPixmap(const QPixmap& pm)
    {
        m_pixmap = pm;
        update();
    }
    void setGlyph(char16_t glyph, const QColor& bg, const QColor& fg = TextPrimary)
    {
        m_pixmap = QPixmap();
        m_text.clear();
        m_glyph = glyph;
        m_bg = bg;
        m_fg = fg;
        update();
    }
    void setLetters(const QString& text, const QColor& bg, const QColor& fg = Qt::white)
    {
        m_pixmap = QPixmap();
        m_text = text;
        m_bg = bg;
        m_fg = fg;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), 8, 8);
        p.setClipPath(clip);
        if (!m_pixmap.isNull()) {
            const QPixmap scaled = m_pixmap.scaled(size() * devicePixelRatioF(), Qt::KeepAspectRatioByExpanding,
                                                    Qt::SmoothTransformation);
            p.drawPixmap(QRect(QPoint(0, 0), size()),
                        scaled.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            return;
        }
        p.fillRect(rect(), m_bg);
        p.setPen(m_fg);
        if (!m_text.isEmpty()) {
            p.setFont(uiFont(qRound(height() * 0.34), QFont::DemiBold));
            p.drawText(rect(), Qt::AlignCenter, m_text);
        } else {
            p.setFont(iconFont(qRound(height() * 0.45)));
            p.drawText(rect(), Qt::AlignCenter, QString(QChar(m_glyph)));
        }
    }

private:
    QPixmap m_pixmap;
    QString m_text;
    char16_t m_glyph = Glyph::Cube;
    QColor m_bg = QColor(255, 255, 255, 20);
    QColor m_fg = TextPrimary;
};

// Télécharge une icône (de mod) en tâche de fond et l'applique au widget cible si celui-ci
// existe encore (il peut avoir été détruit entre-temps si la liste a été rafraîchie).
static void loadModIcon(QNetworkAccessManager* net, const QString& url, QPointer<ModIcon> icon)
{
    if (!net || url.isEmpty() || icon.isNull()) return;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockLauncher/1.0");
    QNetworkReply* r = net->get(req);
    QObject::connect(r, &QNetworkReply::finished, net, [r, icon] {
        r->deleteLater();
        if (icon.isNull() || r->error() != QNetworkReply::NoError) return;
        QPixmap pm;
        if (pm.loadFromData(r->readAll())) icon->setPixmap(pm);
    });
}

// Télécharge une texture de cape et en extrait le panneau visible (dos), pour un petit aperçu.
static void loadCapeThumb(QNetworkAccessManager* net, const QString& url, QPointer<ModIcon> icon)
{
    if (!net || url.isEmpty() || icon.isNull()) return;
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "BlockLauncher/1.0");
    QNetworkReply* r = net->get(req);
    QObject::connect(r, &QNetworkReply::finished, net, [r, icon] {
        r->deleteLater();
        if (icon.isNull() || r->error() != QNetworkReply::NoError) return;
        QImage img;
        if (!img.loadFromData(r->readAll())) return;
        icon->setPixmap(QPixmap::fromImage(img.copy(QRect(1, 1, 10, 16))));
    });
}

// Compose une vue de face « paper doll » pixelisée à partir d'une texture de skin (64x64 ou
// 64x32 historique), sans dépendance de rendu 3D.
static QPixmap renderSkinFront(const QImage& src, bool slim)
{
    QImage skin = src.convertToFormat(QImage::Format_ARGB32);
    const int armW = slim ? 3 : 4;
    const bool full = skin.height() >= 64;

    QImage canvas(16, 32, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.drawImage(QRect(4, 0, 8, 8), skin, QRect(8, 8, 8, 8));   // tête
    p.drawImage(QRect(4, 0, 8, 8), skin, QRect(40, 8, 8, 8));  // chapeau (overlay)
    p.drawImage(QRect(4, 8, 8, 12), skin, QRect(20, 20, 8, 12)); // torse

    const QImage rightArm = skin.copy(QRect(44, 20, armW, 12));
    p.drawImage(QRect(12, 8, armW, 12), rightArm);
    const QImage leftArm = full ? skin.copy(QRect(36, 52, armW, 12)) : rightArm.mirrored(true, false);
    p.drawImage(QRect(0, 8, armW, 12), leftArm);

    const QImage rightLeg = skin.copy(QRect(4, 20, 4, 12));
    p.drawImage(QRect(4, 20, 4, 12), rightLeg);
    const QImage leftLeg = full ? skin.copy(QRect(20, 52, 4, 12)) : rightLeg.mirrored(true, false);
    p.drawImage(QRect(8, 20, 4, 12), leftLeg);
    p.end();

    return QPixmap::fromImage(canvas.scaled(canvas.size() * 8, Qt::KeepAspectRatio, Qt::FastTransformation));
}

// Couleur distinctive par modloader, pour les avatars et badges de la page Mods.
static QColor loaderColor(const QString& key)
{
    if (key == "quilt") return QColor("#9B59E0");
    if (key == "forge") return QColor("#5C7CFA");
    if (key == "neoforge") return QColor("#F2793A");
    return QColor("#DEAA00"); // fabric
}

static QString loaderInitials(const QString& key)
{
    if (key == "quilt") return "Qu";
    if (key == "forge") return "Fo";
    if (key == "neoforge") return "Ne";
    return "Fa";
}

static QString formatCount(qint64 n)
{
    if (n >= 1000000) return QString::number(n / 1000000.0, 'f', 1) + "M";
    if (n >= 1000) return QString::number(n / 1000.0, 'f', 1) + "k";
    return QString::number(n);
}

static QLabel* glyphLabel(char16_t glyph, int px = 20, const QColor& color = TextPrimary)
{
    auto* l = new QLabel(QString(QChar(glyph)));
    l->setFont(iconFont(px));
    l->setStyleSheet(QString("color: %1;").arg(color.name(QColor::HexArgb)));
    l->setFixedWidth(px + 12);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

static QLabel* label(const QString& text, const char* objectName = nullptr)
{
    auto* l = new QLabel(text);
    if (objectName) l->setObjectName(objectName);
    return l;
}

static QIcon makeAppIcon()
{
    QPixmap pm(128, 128);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF top(64, 14), left(14, 40), right(114, 40), mid(64, 66), bl(14, 92), br(114, 92), bot(64, 118);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#9be3ff"));
    p.drawPolygon(QPolygonF{top, right, mid, left});
    p.setBrush(QColor("#3aa0d8"));
    p.drawPolygon(QPolygonF{left, mid, bot, bl});
    p.setBrush(QColor("#1d6fa5"));
    p.drawPolygon(QPolygonF{mid, right, br, bot});
    return QIcon(pm);
}

// ============================================================================
//  Fenêtre principale
// ============================================================================

namespace {
enum Page {
    HomePage = 0,
    VersionsPage = 1,
    ModsPage = 2,
    TexturesPage = 3,
    InstancesPage = 4,
    SkinPage = 5,
    SettingsPage = 6
};

QString typeLabel(const QString& t)
{
    if (t == "release") return "Version finale";
    if (t == "snapshot") return "Snapshot";
    if (t == "old_beta") return "Bêta";
    if (t == "old_alpha") return "Alpha";
    return t;
}
}

MainWindow::MainWindow(bool mica, QWidget* parent) : QMainWindow(parent), m_mica(mica)
{
    m_net = new QNetworkAccessManager(this);
    setWindowTitle("Block Launcher");
    setWindowIcon(makeAppIcon());
    setMinimumSize(980, 640);
    resize(1120, 720);

#ifdef Q_OS_WIN
    if (m_mica) setAttribute(Qt::WA_TranslucentBackground);
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    int corner = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &corner, sizeof corner);
    if (m_mica) {
        int backdrop = 2; // DWMSBT_MAINWINDOW (Mica)
        DwmSetWindowAttribute(hwnd, 38 /*DWMWA_SYSTEMBACKDROP_TYPE*/, &backdrop, sizeof backdrop);
        MARGINS margins{-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }
#endif

    qApp->setStyleSheet(Fluent::styleSheet(m_mica));

    auto* central = new QWidget;
    central->setObjectName("central");
    auto* rootLay = new QHBoxLayout(central);
    rootLay->setContentsMargins(0, 0, 0, 0);
    rootLay->setSpacing(0);
    rootLay->addWidget(buildSidebar());

    auto* content = new QFrame;
    content->setObjectName("content");
    auto* contentLay = new QVBoxLayout(content);
    contentLay->setContentsMargins(36, 24, 36, 24);
    contentLay->setSpacing(12);

    // Barre d'information (InfoBar)
    m_infoBar = new QFrame;
    m_infoBar->setObjectName("infoBar");
    auto* infoLay = new QHBoxLayout(m_infoBar);
    infoLay->setContentsMargins(14, 8, 6, 8);
    m_infoIcon = new QLabel;
    m_infoIcon->setFont(iconFont(16));
    m_infoText = new QLabel;
    m_infoText->setWordWrap(true);
    auto* infoClose = new QPushButton;
    infoClose->setObjectName("subtle");
    infoClose->setIcon(glyphIcon(Glyph::Close, TextSecondary, 12));
    infoClose->setFixedSize(32, 32);
    connect(infoClose, &QPushButton::clicked, m_infoBar, &QWidget::hide);
    infoLay->addWidget(m_infoIcon);
    infoLay->addSpacing(8);
    infoLay->addWidget(m_infoText, 1);
    infoLay->addWidget(infoClose);
    m_infoBar->hide();
    m_infoTimer = new QTimer(this);
    m_infoTimer->setSingleShot(true);
    connect(m_infoTimer, &QTimer::timeout, m_infoBar, &QWidget::hide);

    m_pages = new QStackedWidget;
    m_pages->addWidget(buildHome());
    m_pages->addWidget(buildVersions());
    m_pages->addWidget(buildMods());
    m_pages->addWidget(buildTextures());
    m_pages->addWidget(buildInstances());
    m_pages->addWidget(buildSkin());
    m_pages->addWidget(buildSettings());

    contentLay->addWidget(m_infoBar);
    contentLay->addWidget(m_pages, 1);
    rootLay->addWidget(content, 1);
    setCentralWidget(central);

    loadSettings();
    refreshAccountUi();
    setBusy(false);
    goTo(HomePage);
    loadVersions();
    refreshModProfiles();
    refreshTexProfiles();
    refreshInstances();
    refreshSkin();
    checkUpdates(true);
}

// ---------------------------------------------------------------------------
QWidget* MainWindow::buildSidebar()
{
    auto* side = new QWidget;
    side->setFixedWidth(280);
    auto* lay = new QVBoxLayout(side);
    lay->setContentsMargins(4, 14, 4, 8);
    lay->setSpacing(2);

    auto* titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(14, 0, 0, 14);
    auto* logo = new QLabel;
    logo->setPixmap(makeAppIcon().pixmap(18, 18));
    titleRow->addWidget(logo);
    titleRow->addSpacing(8);
    titleRow->addWidget(label("Block Launcher", "appTitle"));
    titleRow->addStretch();
    lay->addLayout(titleRow);

    const QList<QPair<char16_t, QString>> items{{Glyph::Home, "Accueil"},
                                                {Glyph::Library, "Versions"},
                                                {Glyph::Puzzle, "Mods"},
                                                {Glyph::Color, "Textures"},
                                                {Glyph::Folder, "Instances"},
                                                {Glyph::Contact, "Skin"}};
    for (int i = 0; i < items.size(); ++i) {
        auto* b = new NavButton(items[i].first, items[i].second);
        connect(b, &QAbstractButton::clicked, this, [this, i] { goTo(i); });
        m_nav << b;
        lay->addWidget(b);
    }
    lay->addStretch();

    auto* settingsBtn = new NavButton(Glyph::Settings, "Paramètres");
    connect(settingsBtn, &QAbstractButton::clicked, this, [this] { goTo(SettingsPage); });
    m_nav << settingsBtn;
    lay->addWidget(settingsBtn);

    m_chip = new AccountChip;
    connect(m_chip, &QAbstractButton::clicked, this, [this] { goTo(SettingsPage); });
    lay->addWidget(m_chip);
    return side;
}

QWidget* MainWindow::buildHome()
{
    auto* page = new QWidget;
    page->setObjectName("page");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(16);
    lay->addWidget(label("Accueil", "pageTitle"));

    m_hero = new HeroBanner;
    lay->addWidget(m_hero, 1);

    auto* card = new QFrame;
    card->setObjectName("card");
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(20, 16, 20, 16);
    cardLay->setSpacing(12);

    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    row->addWidget(glyphLabel(Glyph::Cube, 22));
    auto* texts = new QVBoxLayout;
    texts->setSpacing(0);
    texts->addWidget(label("Version du jeu", "sectionTitle"));
    texts->addWidget(label("Téléchargée automatiquement si besoin", "secondary"));
    row->addLayout(texts);
    row->addStretch();

    m_versionCombo = new FluentCombo;
    m_versionCombo->setMinimumWidth(220);
    m_versionCombo->setMaxVisibleItems(14);
    row->addWidget(m_versionCombo);

    m_playBtn = new QPushButton("  Jouer");
    m_playBtn->setObjectName("accent");
    m_playBtn->setIcon(glyphIcon(Glyph::Play, Qt::black, 14));
    m_playBtn->setMinimumSize(150, 40);
    m_playBtn->setCursor(Qt::PointingHandCursor);
    connect(m_playBtn, &QPushButton::clicked, this, [this] { play(m_versionCombo->currentData().toString()); });
    row->addWidget(m_playBtn);
    cardLay->addLayout(row);

    auto* statusRow = new QHBoxLayout;
    m_status = label("", "secondary");
    m_statusCount = label("", "secondary");
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_statusCount);
    cardLay->addLayout(statusRow);

    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    cardLay->addWidget(m_progress);

    lay->addWidget(card);
    return page;
}

QWidget* MainWindow::buildVersions()
{
    auto* page = new QWidget;
    page->setObjectName("page");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(16);
    lay->addWidget(label("Versions", "pageTitle"));

    auto* bar = new QHBoxLayout;
    bar->setSpacing(10);
    m_search = new QLineEdit;
    m_search->setPlaceholderText("Rechercher une version");
    m_search->addAction(glyphIcon(Glyph::Search, TextSecondary, 14), QLineEdit::TrailingPosition);
    m_search->setFixedWidth(300);
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this] { refreshVersionList(); });
    bar->addWidget(m_search);
    bar->addStretch();

    m_snapToggle = new ToggleSwitch;
    connect(m_snapToggle, &QAbstractButton::toggled, this, [this](bool on) {
        QSettings().setValue("showSnapshots", on);
        refreshVersionList();
        refreshVersionCombo();
    });
    bar->addWidget(label("Snapshots", "secondary"));
    bar->addWidget(m_snapToggle);
    bar->addSpacing(8);

    auto* refresh = new QPushButton("  Actualiser");
    refresh->setIcon(glyphIcon(Glyph::Refresh, TextPrimary, 14));
    connect(refresh, &QPushButton::clicked, this, [this] { loadVersions(); });
    bar->addWidget(refresh);
    lay->addLayout(bar);

    m_list = new QListWidget;
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    lay->addWidget(m_list, 1);
    return page;
}

QWidget* MainWindow::buildMods()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* page = new QWidget;
    page->setObjectName("page");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 12, 12);
    lay->setSpacing(16);
    lay->addWidget(label("Mods", "pageTitle"));

    // --- Installer un modloader ---------------------------------------------------
    auto* card = new QFrame;
    card->setObjectName("card");
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(20, 16, 20, 16);
    cardLay->setSpacing(12);
    cardLay->addWidget(label("Installer un modloader", "sectionTitle"));
    cardLay->addWidget(
        label("Fabric, Quilt, Forge ou NeoForge, pour la version de Minecraft de ton choix.", "secondary"));

    auto* row = new QHBoxLayout;
    row->setSpacing(10);
    m_modMcCombo = new FluentCombo;
    m_modMcCombo->setMinimumWidth(140);
    m_modLoaderCombo = new FluentCombo;
    m_modLoaderCombo->addItem("Fabric", "fabric");
    m_modLoaderCombo->addItem("Quilt", "quilt");
    m_modLoaderCombo->addItem("Forge", "forge");
    m_modLoaderCombo->addItem("NeoForge", "neoforge");
    m_modLoaderVerCombo = new FluentCombo;
    m_modLoaderVerCombo->setMinimumWidth(170);
    m_modLoaderVerCombo->setMaxVisibleItems(14);

    connect(m_modMcCombo, &QComboBox::currentIndexChanged, this, [this] { refreshModLoaderVersions(); });
    connect(m_modLoaderCombo, &QComboBox::currentIndexChanged, this, [this] { refreshModLoaderVersions(); });

    auto* installBtn = new QPushButton("  Installer");
    installBtn->setObjectName("accent");
    installBtn->setIcon(glyphIcon(Glyph::Download, Qt::black, 14));
    connect(installBtn, &QPushButton::clicked, this, [this] { installModLoader(); });

    row->addWidget(label("Minecraft", "secondary"));
    row->addWidget(m_modMcCombo);
    row->addWidget(label("Modloader", "secondary"));
    row->addWidget(m_modLoaderCombo);
    row->addWidget(label("Version", "secondary"));
    row->addWidget(m_modLoaderVerCombo, 1);
    row->addWidget(installBtn);
    cardLay->addLayout(row);
    lay->addWidget(card);

    lay->addWidget(label("Profils installés", "sectionTitle"));
    m_modProfileList = new QListWidget;
    m_modProfileList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_modProfileList->setSelectionMode(QAbstractItemView::NoSelection);
    m_modProfileList->setFocusPolicy(Qt::NoFocus);
    m_modProfileList->setMinimumHeight(68);
    m_modProfileList->setMaximumHeight(220);
    lay->addWidget(m_modProfileList);

    // --- Parcourir / installer des mods --------------------------------------------
    lay->addWidget(label("Ajouter des mods", "sectionTitle"));
    auto* bar = new QHBoxLayout;
    bar->setSpacing(10);
    m_modProfileCombo = new FluentCombo;
    m_modProfileCombo->setMinimumWidth(220);
    connect(m_modProfileCombo, &QComboBox::currentIndexChanged, this, [this] { refreshInstalledMods(); });
    m_modSearch = new QLineEdit;
    m_modSearch->setPlaceholderText("Rechercher un mod (ex : Sodium, JEI…)");
    m_modSearch->addAction(glyphIcon(Glyph::Search, TextSecondary, 14), QLineEdit::TrailingPosition);
    connect(m_modSearch, &QLineEdit::returnPressed, this, [this] { searchMods(); });
    auto* searchBtn = new QPushButton("Rechercher");
    connect(searchBtn, &QPushButton::clicked, this, [this] { searchMods(); });
    bar->addWidget(label("Profil", "secondary"));
    bar->addWidget(m_modProfileCombo);
    bar->addWidget(m_modSearch, 1);
    bar->addWidget(searchBtn);
    lay->addLayout(bar);

    m_modResultsList = new QListWidget;
    m_modResultsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_modResultsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_modResultsList->setFocusPolicy(Qt::NoFocus);
    m_modResultsList->setMinimumHeight(160);
    m_modResultsList->setMaximumHeight(300);
    lay->addWidget(m_modResultsList);

    lay->addWidget(label("Mods installés pour ce profil", "sectionTitle"));
    m_modInstalledList = new QListWidget;
    m_modInstalledList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_modInstalledList->setSelectionMode(QAbstractItemView::NoSelection);
    m_modInstalledList->setFocusPolicy(Qt::NoFocus);
    m_modInstalledList->setMinimumHeight(48);
    lay->addWidget(m_modInstalledList);
    lay->addStretch();

    scroll->setWidget(page);
    return scroll;
}

QWidget* MainWindow::buildTextures()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* page = new QWidget;
    page->setObjectName("page");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 12, 12);
    lay->setSpacing(16);
    lay->addWidget(label("Textures", "pageTitle"));

    auto* bar = new QHBoxLayout;
    bar->setSpacing(10);
    m_texProfileCombo = new FluentCombo;
    m_texProfileCombo->setMinimumWidth(200);
    connect(m_texProfileCombo, &QComboBox::currentIndexChanged, this, [this] { refreshInstalledTextures(); });

    m_texTypeCombo = new FluentCombo;
    m_texTypeCombo->addItem("Packs de textures", 0);
    m_texTypeCombo->addItem("Shaders", 1);
    connect(m_texTypeCombo, &QComboBox::currentIndexChanged, this, [this] {
        m_texResultsList->clear();
        refreshInstalledTextures();
    });

    m_texSearch = new QLineEdit;
    m_texSearch->setPlaceholderText("Rechercher (ex : Faithful, Complementary…)");
    m_texSearch->addAction(glyphIcon(Glyph::Search, TextSecondary, 14), QLineEdit::TrailingPosition);
    connect(m_texSearch, &QLineEdit::returnPressed, this, [this] { searchTextures(); });
    auto* searchBtn = new QPushButton("Rechercher");
    connect(searchBtn, &QPushButton::clicked, this, [this] { searchTextures(); });

    bar->addWidget(label("Profil", "secondary"));
    bar->addWidget(m_texProfileCombo);
    bar->addWidget(m_texTypeCombo);
    bar->addWidget(m_texSearch, 1);
    bar->addWidget(searchBtn);
    lay->addLayout(bar);

    m_texResultsList = new QListWidget;
    m_texResultsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_texResultsList->setSelectionMode(QAbstractItemView::NoSelection);
    m_texResultsList->setFocusPolicy(Qt::NoFocus);
    m_texResultsList->setMinimumHeight(200);
    m_texResultsList->setMaximumHeight(340);
    lay->addWidget(m_texResultsList);

    lay->addWidget(label("Installés pour ce profil", "sectionTitle"));
    m_texInstalledList = new QListWidget;
    m_texInstalledList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_texInstalledList->setSelectionMode(QAbstractItemView::NoSelection);
    m_texInstalledList->setFocusPolicy(Qt::NoFocus);
    m_texInstalledList->setMinimumHeight(48);
    lay->addWidget(m_texInstalledList);
    lay->addStretch();

    scroll->setWidget(page);
    return scroll;
}

QWidget* MainWindow::buildInstances()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* page = new QWidget;
    page->setObjectName("page");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 12, 12);
    lay->setSpacing(16);

    auto* headerRow = new QHBoxLayout;
    headerRow->addWidget(label("Instances", "pageTitle"));
    headerRow->addStretch();
    auto* refresh = new QPushButton("  Actualiser");
    refresh->setIcon(glyphIcon(Glyph::Refresh, TextPrimary, 14));
    connect(refresh, &QPushButton::clicked, this, [this] { refreshInstances(); });
    headerRow->addWidget(refresh);
    lay->addLayout(headerRow);
    lay->addWidget(
        label("Un profil par version/modloader installé, avec ses mods, packs de textures et shaders.", "secondary"));

    m_instancesList = new QVBoxLayout;
    m_instancesList->setSpacing(12);
    lay->addLayout(m_instancesList);
    lay->addStretch();

    scroll->setWidget(page);
    return scroll;
}

QWidget* MainWindow::buildSkin()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* page = new QWidget;
    page->setObjectName("page");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 12, 12);
    lay->setSpacing(16);

    auto* headerRow = new QHBoxLayout;
    headerRow->addWidget(label("Skin", "pageTitle"));
    headerRow->addStretch();
    auto* refresh = new QPushButton("  Actualiser");
    refresh->setIcon(glyphIcon(Glyph::Refresh, TextPrimary, 14));
    connect(refresh, &QPushButton::clicked, this, [this] { refreshSkin(); });
    headerRow->addWidget(refresh);
    lay->addLayout(headerRow);

    auto* card = new QFrame;
    card->setObjectName("card");
    auto* cardLay = new QHBoxLayout(card);
    cardLay->setContentsMargins(24, 20, 24, 20);
    cardLay->setSpacing(24);

    m_skinPreview = new QLabel;
    m_skinPreview->setFixedSize(128, 256);
    m_skinPreview->setAlignment(Qt::AlignCenter);
    cardLay->addWidget(m_skinPreview);

    auto* right = new QVBoxLayout;
    right->setSpacing(10);
    m_skinVariantLabel = label("Connecte-toi pour gérer ton skin.", "secondary");
    m_skinVariantLabel->setWordWrap(true);
    right->addWidget(m_skinVariantLabel);
    right->addStretch();

    auto* variantRow = new QHBoxLayout;
    variantRow->addWidget(label("Modèle pour le prochain envoi", "secondary"));
    m_skinVariantCombo = new FluentCombo;
    m_skinVariantCombo->addItem("Classique (Steve)", "classic");
    m_skinVariantCombo->addItem("Fin (Alex)", "slim");
    variantRow->addWidget(m_skinVariantCombo);
    variantRow->addStretch();
    right->addLayout(variantRow);

    auto* btnRow = new QHBoxLayout;
    auto* chooseBtn = new QPushButton("  Choisir un fichier…");
    chooseBtn->setObjectName("accent");
    chooseBtn->setIcon(glyphIcon(Glyph::Folder, Qt::black, 14));
    connect(chooseBtn, &QPushButton::clicked, this, [this] {
        if (!m_account.valid()) {
            showInfo("Connecte-toi avec ton compte Microsoft dans Paramètres.", Sev::Info);
            return;
        }
        const QString path =
            QFileDialog::getOpenFileName(this, "Choisir un skin", QString(), "Images PNG (*.png)");
        if (!path.isEmpty()) changeSkin(path, m_skinVariantCombo->currentData().toString());
    });
    btnRow->addWidget(chooseBtn);
    auto* resetBtn = new QPushButton("Réinitialiser");
    connect(resetBtn, &QPushButton::clicked, this, [this] { resetSkinAction(); });
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    right->addLayout(btnRow);

    cardLay->addLayout(right, 1);
    lay->addWidget(card);

    lay->addWidget(label("Capes", "sectionTitle"));
    lay->addWidget(label("Les capes obtenues (événements, migration, promotions…) apparaissent ici.", "secondary"));
    m_capeList = new QListWidget;
    m_capeList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_capeList->setSelectionMode(QAbstractItemView::NoSelection);
    m_capeList->setFocusPolicy(Qt::NoFocus);
    m_capeList->setMinimumHeight(120);
    lay->addWidget(m_capeList);
    lay->addStretch();

    scroll->setWidget(page);
    return scroll;
}

QFrame* MainWindow::settingsCard(char16_t glyph, const QString& title, const QString& desc, QWidget* control,
                                 QLabel** descOut)
{
    auto* card = new QFrame;
    card->setObjectName("card");
    card->setMinimumHeight(68);
    auto* lay = new QHBoxLayout(card);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(14);
    lay->addWidget(glyphLabel(glyph, 18));
    auto* texts = new QVBoxLayout;
    texts->setSpacing(1);
    texts->addWidget(label(title));
    auto* d = label(desc, "secondary");
    d->setWordWrap(true);
    texts->addWidget(d);
    lay->addLayout(texts, 1);
    if (control) lay->addWidget(control);
    if (descOut) *descOut = d;
    return card;
}

QWidget* MainWindow::buildSettings()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* page = new QWidget;
    page->setObjectName("page");
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 12, 12);
    lay->setSpacing(4);
    lay->addWidget(label("Paramètres", "pageTitle"));
    lay->addSpacing(12);

    auto section = [&](const QString& t) {
        lay->addSpacing(14);
        lay->addWidget(label(t, "sectionTitle"));
        lay->addSpacing(4);
    };

    // --- Compte
    section("Compte");
    m_accountBtn = new QPushButton;
    m_accountBtn->setMinimumWidth(140);
    connect(m_accountBtn, &QPushButton::clicked, this, [this] { m_account.valid() ? logout() : login(); });
    lay->addWidget(settingsCard(Glyph::Contact, "Compte Microsoft", "", m_accountBtn, &m_accountDesc));

    // --- Jeu
    section("Jeu");
    auto* ramBox = new QWidget;
    auto* ramLay = new QHBoxLayout(ramBox);
    ramLay->setContentsMargins(0, 0, 0, 0);
    m_ram = new QSlider(Qt::Horizontal);
    m_ram->setRange(2, 16);
    m_ram->setFixedWidth(200);
    m_ram->setCursor(Qt::PointingHandCursor);
    m_ramLabel = new QLabel;
    m_ramLabel->setFixedWidth(48);
    m_ramLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_ram, &QSlider::valueChanged, this, [this](int v) {
        m_ramLabel->setText(QString("%1 Go").arg(v));
        QSettings().setValue("ramGb", v);
    });
    ramLay->addWidget(m_ram);
    ramLay->addWidget(m_ramLabel);
    lay->addWidget(settingsCard(Glyph::Memory, "Mémoire allouée",
                                "Quantité de RAM maximale donnée au jeu (4 Go suffisent en général)", ramBox));

    m_jvmArgs = new QLineEdit;
    m_jvmArgs->setFixedWidth(280);
    m_jvmArgs->setPlaceholderText("-XX:+UseG1GC");
    connect(m_jvmArgs, &QLineEdit::editingFinished, this, [this] { QSettings().setValue("jvmArgs", m_jvmArgs->text()); });
    lay->addWidget(settingsCard(Glyph::Code, "Arguments Java", "Options avancées passées à la machine virtuelle Java",
                                m_jvmArgs));

    auto* openDir = new QPushButton("Ouvrir");
    connect(openDir, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Game::rootDir())); });
    lay->addWidget(settingsCard(Glyph::Folder, "Dossier du jeu", QDir::toNativeSeparators(Game::rootDir()), openDir));

    m_closeToggle = new ToggleSwitch;
    connect(m_closeToggle, &QAbstractButton::toggled, this, [](bool on) { QSettings().setValue("closeOnLaunch", on); });
    lay->addWidget(settingsCard(Glyph::Play, "Fermer le launcher au démarrage du jeu",
                                "Libère un peu de mémoire pendant que tu joues", m_closeToggle));

    // --- Avancé
    section("Avancé");
    m_clientId = new QLineEdit;
    m_clientId->setFixedWidth(300);
    m_clientId->setPlaceholderText("00000000-0000-0000-0000-000000000000");
    connect(m_clientId, &QLineEdit::editingFinished, this,
            [this] { QSettings().setValue("clientId", m_clientId->text().trimmed()); });
    lay->addWidget(settingsCard(Glyph::Key, "ID client Azure",
                                "Identifiant de ton application Microsoft Entra, nécessaire pour la connexion (voir "
                                "README). Pré-rempli automatiquement si un fichier client_id.txt est présent à côté "
                                "de l'exécutable.",
                                m_clientId));

    m_micaToggle = new ToggleSwitch;
    connect(m_micaToggle, &QAbstractButton::toggled, this, [this](bool on) {
        QSettings().setValue("mica", on);
        showInfo("L'effet Mica sera appliqué au prochain démarrage du launcher.", Sev::Info);
    });
    lay->addWidget(settingsCard(Glyph::Color, "Effet Mica",
                                "Fond translucide de Windows 11 (22H2 ou plus récent). Nécessite un redémarrage.",
                                m_micaToggle));

    // --- Mises à jour
    section("Mises à jour");
    m_updateBtn = new QPushButton("Vérifier");
    m_updateBtn->setMinimumWidth(130);
    connect(m_updateBtn, &QPushButton::clicked, this, [this] {
        if (!m_latestUpdate.version.isEmpty()) installUpdate();
        else checkUpdates(false);
    });
    lay->addWidget(settingsCard(Glyph::Download, "Block Launcher",
                                QString("Version actuelle : %1").arg(BLOCKLAUNCHER_VERSION), m_updateBtn,
                                &m_updateDesc));

    lay->addSpacing(18);
    auto* about = label("Block Launcher " BLOCKLAUNCHER_VERSION " · Projet non officiel, non affilié à Mojang Studios ni à Microsoft.",
                        "secondary");
    lay->addWidget(about);
    lay->addStretch();

    scroll->setWidget(page);
    return scroll;
}

// ---------------------------------------------------------------------------
void MainWindow::loadSettings()
{
    QSettings s;
    m_ram->setValue(s.value("ramGb", 4).toInt());
    m_ramLabel->setText(QString("%1 Go").arg(m_ram->value()));
    m_jvmArgs->setText(s.value("jvmArgs").toString());
    const QString defaultClientId = qEnvironmentVariable("BLOCK_CLIENT_ID", Config::fileClientId());
    m_clientId->setText(s.value("clientId", defaultClientId).toString());
    QSignalBlocker b1(m_closeToggle), b2(m_micaToggle), b3(m_snapToggle);
    m_closeToggle->setCheckedNoAnim(s.value("closeOnLaunch", false).toBool());
    m_micaToggle->setCheckedNoAnim(m_mica);
    m_snapToggle->setCheckedNoAnim(s.value("showSnapshots", false).toBool());

    s.beginGroup("account");
    m_account.name = s.value("name").toString();
    m_account.uuid = s.value("uuid").toString();
    m_account.accessToken = s.value("accessToken").toString();
    m_account.refreshToken = s.value("refreshToken").toString();
    m_account.expiresAt = s.value("expiresAt", 0).toLongLong();
}

void MainWindow::saveAccount()
{
    QSettings s;
    s.remove("account");
    if (!m_account.valid()) return;
    s.beginGroup("account");
    s.setValue("name", m_account.name);
    s.setValue("uuid", m_account.uuid);
    s.setValue("accessToken", m_account.accessToken);
    s.setValue("refreshToken", m_account.refreshToken);
    s.setValue("expiresAt", m_account.expiresAt);
}

void MainWindow::goTo(int page)
{
    m_pages->setCurrentIndex(page);
    if (page < m_nav.size()) m_nav[page]->setChecked(true);
}

void MainWindow::showInfo(const QString& msg, Sev sev)
{
    const char* key = sev == Sev::Error ? "error" : sev == Sev::Success ? "success" : "info";
    m_infoBar->setProperty("sev", key);
    m_infoBar->style()->unpolish(m_infoBar);
    m_infoBar->style()->polish(m_infoBar);
    const char16_t glyph = sev == Sev::Error ? Glyph::Error : sev == Sev::Success ? Glyph::Check : Glyph::Info;
    const QColor color = sev == Sev::Error ? QColor("#ff99a4") : sev == Sev::Success ? QColor("#6ccb5f") : Accent;
    m_infoIcon->setText(QString(QChar(glyph)));
    m_infoIcon->setStyleSheet(QString("color: %1;").arg(color.name()));
    m_infoText->setText(msg);
    m_infoBar->show();
    if (sev == Sev::Error) m_infoTimer->stop();
    else m_infoTimer->start(7000);
}

// ---------------------------------------------------------------------------
//  Tâches en arrière-plan
// ---------------------------------------------------------------------------
void MainWindow::runTask(Job job, std::function<void()> onDone, bool silent)
{
    if (m_busy) return;
    setBusy(true);
    QThread* th = QThread::create([this, job, onDone, silent] {
        const Progress progress = [this](const QString& s, qint64 d, qint64 t) {
            QMetaObject::invokeMethod(this, [this, s, d, t] { onProgress(s, d, t); }, Qt::QueuedConnection);
        };
        QString err;
        try {
            job(progress);
        } catch (const Error& e) {
            err = e.msg;
        } catch (const std::exception& e) {
            err = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(this, [this, err, onDone, silent] {
            setBusy(false);
            if (!err.isEmpty()) { if (!silent) showInfo(err, Sev::Error); }
            else if (onDone) onDone();
        }, Qt::QueuedConnection);
    });
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    th->start();
}

void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    m_playBtn->setEnabled(!busy);
    m_versionCombo->setEnabled(!busy);
    m_list->setEnabled(!busy);
    m_accountBtn->setEnabled(!busy);
    m_progress->setVisible(busy);
    m_statusCount->setVisible(busy);
    if (busy) {
        m_progress->setRange(0, 0);
        m_status->setText("Veuillez patienter…");
        m_statusCount->clear();
    } else {
        m_status->setText(m_account.valid() ? "Prêt à jouer." : "Connecte-toi dans Paramètres pour pouvoir jouer.");
    }
}

void MainWindow::onProgress(const QString& status, qint64 done, qint64 total)
{
    if (!m_busy) return;
    m_status->setText(status);
    if (total > 0) {
        m_progress->setRange(0, int(total));
        m_progress->setValue(int(done));
        m_statusCount->setText(QString("%1 / %2").arg(done).arg(total));
    } else {
        m_progress->setRange(0, 0);
        m_statusCount->clear();
    }
}

// ---------------------------------------------------------------------------
//  Versions
// ---------------------------------------------------------------------------
void MainWindow::loadVersions()
{
    if (m_versions.isEmpty()) {
        m_versions = Game::cachedManifest();
        refreshVersionCombo();
        refreshVersionList();
        refreshModMcCombo();
    }
    auto result = std::make_shared<QList<VersionEntry>>();
    runTask(
        [result](const Progress& p) {
            p("Récupération de la liste des versions…", 0, 0);
            QNetworkAccessManager nam;
            *result = Game::fetchManifest(nam);
        },
        [this, result] {
            m_versions = *result;
            refreshVersionCombo();
            refreshVersionList();
            refreshModMcCombo();
        });
}

VersionEntry MainWindow::findEntry(const QString& id) const
{
    for (const auto& v : m_versions)
        if (v.id == id) return v;
    return {};
}

void MainWindow::refreshVersionCombo()
{
    const QString current = m_versionCombo->currentData().toString();
    const QString last = QSettings().value("lastVersion").toString();
    const QStringList installed = Game::installedVersions();
    const bool snaps = m_snapToggle->isChecked();

    QSignalBlocker block(m_versionCombo);
    m_versionCombo->clear();
    const QIcon check = glyphIcon(Glyph::Check, QColor("#6ccb5f"), 12);
    QStringList added;
    // Versions installées d'abord, puis les versions disponibles
    for (const auto& id : installed) {
        m_versionCombo->addItem(check, id, id);
        added << id;
    }
    for (const auto& v : m_versions) {
        if (added.contains(v.id)) continue;
        if (v.type == "release" || (snaps && v.type == "snapshot")) {
            m_versionCombo->addItem(QIcon(), v.id, v.id);
            added << v.id;
        }
    }
    QString pick = !current.isEmpty() ? current : last;
    int idx = m_versionCombo->findData(pick);
    if (idx < 0) {
        for (const auto& v : m_versions)
            if (v.type == "release") {
                idx = m_versionCombo->findData(v.id);
                break;
            }
    }
    m_versionCombo->setCurrentIndex(qMax(0, idx));
}

void MainWindow::refreshVersionList()
{
    m_list->clear();
    const QStringList installed = Game::installedVersions();
    const QString filter = m_search->text().trimmed();
    const bool snaps = m_snapToggle->isChecked();
    int snapshotCount = 0;

    for (const auto& v : m_versions) {
        const bool inst = installed.contains(v.id);
        if (!inst) {
            if (v.type == "snapshot" && (!snaps || ++snapshotCount > 150)) continue;
            if (v.type != "release" && v.type != "snapshot") continue;
        }
        if (!filter.isEmpty() && !v.id.contains(filter, Qt::CaseInsensitive)) continue;

        auto* holder = new QWidget;
        auto* hl = new QVBoxLayout(holder);
        hl->setContentsMargins(0, 0, 8, 4);
        auto* row = new QFrame;
        row->setObjectName("row");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(16, 8, 12, 8);
        rl->setSpacing(12);
        rl->addWidget(glyphLabel(Glyph::Cube, 18, inst ? Accent : TextSecondary));
        auto* name = label(v.id);
        name->setFont(uiFont(14, QFont::DemiBold));
        rl->addWidget(name);
        auto* badge = label(typeLabel(v.type), "badge");
        badge->setFixedHeight(22);
        rl->addWidget(badge, 0, Qt::AlignVCenter);
        rl->addStretch();

        const QString id = v.id;
        if (inst) {
            rl->addWidget(glyphLabel(Glyph::Check, 12, QColor("#6ccb5f")));
            rl->addWidget(label("Installée", "secondary"));
            rl->addSpacing(8);
            auto* playBtn = new QPushButton("Jouer");
            playBtn->setObjectName("accent");
            playBtn->setMinimumWidth(110);
            connect(playBtn, &QPushButton::clicked, this, [this, id] {
                m_versionCombo->setCurrentIndex(m_versionCombo->findData(id));
                goTo(HomePage);
                play(id);
            });
            rl->addWidget(playBtn);
        } else {
            auto* instBtn = new QPushButton("  Installer");
            instBtn->setIcon(glyphIcon(Glyph::Download, TextPrimary, 14));
            instBtn->setMinimumWidth(110);
            connect(instBtn, &QPushButton::clicked, this, [this, id] { install(id); });
            rl->addWidget(instBtn);
        }
        hl->addWidget(row);

        auto* item = new QListWidgetItem(m_list);
        item->setSizeHint(QSize(0, 62));
        m_list->setItemWidget(item, holder);
    }
}

void MainWindow::install(const QString& id)
{
    const VersionEntry entry = findEntry(id);
    if (entry.isNull()) return;
    goTo(HomePage);
    runTask([entry](const Progress& p) {
        QNetworkAccessManager nam;
        Game::install(nam, entry, p);
    },
            [this, id] {
                showInfo("Minecraft " + id + " est installé.", Sev::Success);
                refreshVersionCombo();
                refreshVersionList();
                refreshTexProfiles();
                refreshInstances();
            });
}

// ---------------------------------------------------------------------------
//  Jouer
// ---------------------------------------------------------------------------
void MainWindow::play(const QString& id)
{
    if (id.isEmpty()) return;
    if (!m_account.valid()) {
        showInfo("Connecte-toi avec ton compte Microsoft pour jouer.", Sev::Info);
        goTo(SettingsPage);
        return;
    }
    const QString clientId = m_clientId->text().trimmed();
    const LaunchOptions opts{m_ram->value(), m_jvmArgs->text()};
    const VersionEntry entry = findEntry(id);
    const Account current = m_account;
    auto out = std::make_shared<Account>();

    runTask(
        [=](const Progress& p) {
            p("Vérification de la session…", 0, 0);
            const Account acc = Auth::refreshIfNeeded(clientId, current);
            const bool installed = Game::isInstalled(id);
            if (!entry.isNull()) {
                QNetworkAccessManager nam;
                try {
                    Game::install(nam, entry, p);
                } catch (const Error&) {
                    if (!installed) throw; // hors ligne : on lance la version déjà installée
                }
            } else if (!installed) {
                throw Error{"Version inconnue : " + id};
            }
            p("Lancement de Minecraft " + id + "…", 0, 0);
            Game::launch(id, acc, opts);
            *out = acc;
        },
        [this, id, out] {
            m_account = *out;
            saveAccount();
            QSettings().setValue("lastVersion", id);
            showInfo("Minecraft " + id + " démarre, bon jeu !", Sev::Success);
            refreshVersionCombo();
            refreshVersionList();
            if (m_closeToggle->isChecked()) QTimer::singleShot(2000, qApp, &QApplication::quit);
        });
}

// ---------------------------------------------------------------------------
//  Compte
// ---------------------------------------------------------------------------
void MainWindow::refreshAccountUi()
{
    const bool ok = m_account.valid();
    m_chip->setAccount(ok ? m_account.name : QString());
    m_hero->setPlayer(ok ? m_account.name : QString());
    m_accountDesc->setText(ok ? "Connecté en tant que " + m_account.name
                              : "Connecte-toi avec le compte qui possède Minecraft : Java Edition");
    m_accountBtn->setText(ok ? "Se déconnecter" : "Se connecter");
    m_accountBtn->setObjectName(ok ? "" : "accent");
    m_accountBtn->style()->unpolish(m_accountBtn);
    m_accountBtn->style()->polish(m_accountBtn);
    if (!m_busy) setBusy(false);
}

void MainWindow::login()
{
    const QString clientId = m_clientId->text().trimmed();
    QSettings().setValue("clientId", clientId);
    auto out = std::make_shared<Account>();
    runTask(
        [this, clientId, out](const Progress& p) {
            p("Termine la connexion dans ton navigateur…", 0, 0);
            *out = Auth::loginInteractive(clientId, [this](const QUrl& url) {
                QMetaObject::invokeMethod(this, [url] { QDesktopServices::openUrl(url); }, Qt::QueuedConnection);
            });
        },
        [this, out] {
            m_account = *out;
            saveAccount();
            refreshAccountUi();
            showInfo("Connecté en tant que " + m_account.name + ".", Sev::Success);
            raise();
            activateWindow();
            refreshSkin();
        });
}

void MainWindow::logout()
{
    m_account = Account{};
    saveAccount();
    refreshAccountUi();
    showInfo("Tu es déconnecté.", Sev::Info);
    refreshSkin();
}

// ---------------------------------------------------------------------------
//  Mods & modloaders
// ---------------------------------------------------------------------------
void MainWindow::refreshModMcCombo()
{
    const QString current = m_modMcCombo->currentData().toString();
    QSignalBlocker b(m_modMcCombo);
    m_modMcCombo->clear();
    for (const auto& v : m_versions)
        if (v.type == "release") m_modMcCombo->addItem(v.id, v.id);
    const int idx = m_modMcCombo->findData(current);
    m_modMcCombo->setCurrentIndex(qMax(0, idx));
    refreshModLoaderVersions();
}

void MainWindow::refreshModLoaderVersions()
{
    const QString mc = m_modMcCombo->currentData().toString();
    const Loader loader = loaderFromKey(m_modLoaderCombo->currentData().toString());
    m_modLoaderVerCombo->clear();
    if (mc.isEmpty() || m_busy) return;
    m_modLoaderVerCombo->setEnabled(false);
    m_modLoaderVerCombo->addItem("Chargement…");

    auto result = std::make_shared<QStringList>();
    runTask(
        [=](const Progress& p) {
            p("Récupération des versions " + loaderDisplayName(loader) + "…", 0, 0);
            QNetworkAccessManager nam;
            *result = ModLoader::fetchLoaderVersions(nam, loader, mc);
        },
        [this, result] {
            m_modLoaderVerCombo->clear();
            m_modLoaderVerCombo->setEnabled(true);
            for (const auto& v : *result) m_modLoaderVerCombo->addItem(v, v);
            if (result->isEmpty()) m_modLoaderVerCombo->addItem("Aucune version disponible", QString());
        });
}

void MainWindow::installModLoader()
{
    const QString mc = m_modMcCombo->currentData().toString();
    const Loader loader = loaderFromKey(m_modLoaderCombo->currentData().toString());
    const QString loaderVer = m_modLoaderVerCombo->currentData().toString();
    if (mc.isEmpty() || loaderVer.isEmpty()) {
        showInfo("Choisis une version de Minecraft et du modloader.", Sev::Info);
        return;
    }
    runTask(
        [=](const Progress& p) {
            QNetworkAccessManager nam;
            ModLoader::install(nam, loader, mc, loaderVer, p);
        },
        [this, loader] {
            showInfo(loaderDisplayName(loader) + " a été installé avec succès.", Sev::Success);
            refreshModProfiles();
            refreshTexProfiles();
            refreshInstances();
        });
}

void MainWindow::refreshModProfiles()
{
    const auto profiles = Game::installedModProfiles();

    const QString currentSel = m_modProfileCombo->currentData().toString();
    QSignalBlocker b(m_modProfileCombo);
    m_modProfileCombo->clear();
    for (const auto& pr : profiles)
        m_modProfileCombo->addItem(loaderDisplayName(loaderFromKey(pr.loader)) + " · " + pr.mcVersion, pr.id);
    const int idx = m_modProfileCombo->findData(currentSel);
    m_modProfileCombo->setCurrentIndex(qMax(0, idx));

    m_modProfileList->clear();
    for (const auto& pr : profiles) {
        auto* holder = new QWidget;
        auto* hl = new QVBoxLayout(holder);
        hl->setContentsMargins(0, 0, 8, 4);
        auto* row = new QFrame;
        row->setObjectName("row");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(16, 8, 12, 8);
        rl->setSpacing(12);
        auto* icon = new ModIcon(36);
        icon->setLetters(loaderInitials(pr.loader), loaderColor(pr.loader));
        rl->addWidget(icon);
        auto* name = label(loaderDisplayName(loaderFromKey(pr.loader)) + "  ·  " + pr.mcVersion);
        name->setFont(uiFont(14, QFont::DemiBold));
        rl->addWidget(name);
        auto* badge = label(pr.loaderVersion, "badge");
        badge->setFixedHeight(22);
        rl->addWidget(badge, 0, Qt::AlignVCenter);
        rl->addStretch();

        const QString id = pr.id;
        auto* playBtn = new QPushButton("Jouer");
        playBtn->setObjectName("accent");
        playBtn->setMinimumWidth(90);
        connect(playBtn, &QPushButton::clicked, this, [this, id] { play(id); });
        rl->addWidget(playBtn);

        auto* folderBtn = new QPushButton;
        folderBtn->setIcon(glyphIcon(Glyph::Folder, TextPrimary, 14));
        folderBtn->setToolTip("Ouvrir le dossier mods");
        connect(folderBtn, &QPushButton::clicked, this, [id] {
            QDir().mkpath(Mods::dir(id, ContentType::Mod));
            QDesktopServices::openUrl(QUrl::fromLocalFile(Mods::dir(id, ContentType::Mod)));
        });
        rl->addWidget(folderBtn);

        auto* delBtn = new QPushButton;
        delBtn->setObjectName("subtle");
        delBtn->setIcon(glyphIcon(Glyph::Delete, QColor("#ff99a4"), 14));
        delBtn->setToolTip("Supprimer ce profil");
        connect(delBtn, &QPushButton::clicked, this, [this, id] {
            Game::removeProfile(id);
            showInfo("Profil supprimé.", Sev::Info);
            refreshModProfiles();
            refreshTexProfiles();
            refreshInstances();
        });
        rl->addWidget(delBtn);

        hl->addWidget(row);
        auto* item = new QListWidgetItem(m_modProfileList);
        item->setSizeHint(QSize(0, 62));
        m_modProfileList->setItemWidget(item, holder);
    }
    refreshInstalledMods();
}

void MainWindow::refreshInstalledMods()
{
    m_modInstalledList->clear();
    const QString profileId = m_modProfileCombo->currentData().toString();
    if (profileId.isEmpty()) return;
    for (const auto& fileName : Mods::installedFiles(profileId, ContentType::Mod)) {
        auto* holder = new QWidget;
        auto* hl = new QVBoxLayout(holder);
        hl->setContentsMargins(0, 0, 8, 4);
        auto* row = new QFrame;
        row->setObjectName("row");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(16, 8, 12, 8);
        rl->setSpacing(12);
        auto* icon = new ModIcon(26);
        icon->setGlyph(Glyph::Check, QColor(108, 203, 95, 40), QColor("#6ccb5f"));
        rl->addWidget(icon);
        rl->addWidget(label(fileName), 1);
        auto* delBtn = new QPushButton;
        delBtn->setObjectName("subtle");
        delBtn->setIcon(glyphIcon(Glyph::Delete, QColor("#ff99a4"), 14));
        connect(delBtn, &QPushButton::clicked, this, [this, profileId, fileName] {
            Mods::remove(profileId, ContentType::Mod, fileName);
            refreshInstalledMods();
            refreshInstances();
        });
        rl->addWidget(delBtn);
        hl->addWidget(row);
        auto* item = new QListWidgetItem(m_modInstalledList);
        item->setSizeHint(QSize(0, 48));
        m_modInstalledList->setItemWidget(item, holder);
    }
}

void MainWindow::searchMods()
{
    const QString profileId = m_modProfileCombo->currentData().toString();
    if (profileId.isEmpty()) {
        showInfo("Installe d'abord un modloader pour pouvoir chercher des mods.", Sev::Info);
        return;
    }
    QString mcVersion, loaderStr;
    for (const auto& pr : Game::installedModProfiles())
        if (pr.id == profileId) {
            mcVersion = pr.mcVersion;
            loaderStr = pr.loader;
        }
    const QString query = m_modSearch->text().trimmed();

    auto result = std::make_shared<QList<ModSearchResult>>();
    runTask(
        [=](const Progress& p) {
            p("Recherche de mods…", 0, 0);
            QNetworkAccessManager nam;
            *result = Mods::search(nam, query, mcVersion, loaderStr, ContentType::Mod);
        },
        [this, result, profileId] {
            m_modResultsList->clear();
            for (const auto& m : *result) {
                auto* holder = new QWidget;
                auto* hl = new QVBoxLayout(holder);
                hl->setContentsMargins(0, 0, 8, 4);
                auto* row = new QFrame;
                row->setObjectName("row");
                auto* rl = new QHBoxLayout(row);
                rl->setContentsMargins(16, 10, 12, 10);
                rl->setSpacing(12);

                auto* icon = new ModIcon(42);
                icon->setGlyph(Glyph::Puzzle, QColor(255, 255, 255, 20), TextSecondary);
                loadModIcon(m_net, m.iconUrl, icon);
                rl->addWidget(icon);

                auto* texts = new QVBoxLayout;
                texts->setSpacing(1);
                auto* title = label(m.title);
                title->setFont(uiFont(14, QFont::DemiBold));
                texts->addWidget(title);
                auto* desc = label(m.description, "secondary");
                desc->setWordWrap(true);
                texts->addWidget(desc);
                rl->addLayout(texts, 1);

                auto* dlBadge = label(formatCount(m.downloads) + " dl", "badge");
                dlBadge->setFixedHeight(22);
                rl->addWidget(dlBadge, 0, Qt::AlignVCenter);

                auto* instBtn = new QPushButton("  Installer");
                instBtn->setIcon(glyphIcon(Glyph::Download, TextPrimary, 14));
                instBtn->setMinimumWidth(110);
                const QString projectId = m.projectId;
                connect(instBtn, &QPushButton::clicked, this,
                        [this, projectId, profileId] { installMod(projectId, profileId); });
                rl->addWidget(instBtn);

                hl->addWidget(row);
                auto* item = new QListWidgetItem(m_modResultsList);
                item->setSizeHint(QSize(0, 70));
                m_modResultsList->setItemWidget(item, holder);
            }
            if (result->isEmpty()) showInfo("Aucun mod trouvé pour cette recherche.", Sev::Info);
        });
}

void MainWindow::installMod(const QString& projectId, const QString& profileId)
{
    QString mcVersion, loaderStr;
    for (const auto& pr : Game::installedModProfiles())
        if (pr.id == profileId) {
            mcVersion = pr.mcVersion;
            loaderStr = pr.loader;
        }

    auto fileName = std::make_shared<QString>();
    runTask(
        [=](const Progress& p) {
            QNetworkAccessManager nam;
            const auto versions = Mods::versionsFor(nam, projectId, mcVersion, loaderStr, ContentType::Mod);
            if (versions.isEmpty()) throw Error{"Aucune version compatible de ce mod n'a été trouvée."};
            const ModFile file = versions.first();
            *fileName = file.fileName;
            Mods::install(nam, file, profileId, ContentType::Mod, p);
        },
        [this, fileName] {
            showInfo(*fileName + " a été installé.", Sev::Success);
            refreshInstalledMods();
            refreshInstances();
        });
}

// ---------------------------------------------------------------------------
//  Packs de textures & shaders
// ---------------------------------------------------------------------------
void MainWindow::refreshTexProfiles()
{
    const auto profiles = Game::allProfiles();
    const QString currentSel = m_texProfileCombo->currentData().toString();
    QSignalBlocker b(m_texProfileCombo);
    m_texProfileCombo->clear();
    for (const auto& pr : profiles) {
        const QString text =
            pr.loader.isEmpty() ? pr.id : (loaderDisplayName(loaderFromKey(pr.loader)) + "  ·  " + pr.mcVersion);
        m_texProfileCombo->addItem(text, pr.id);
    }
    const int idx = m_texProfileCombo->findData(currentSel);
    m_texProfileCombo->setCurrentIndex(qMax(0, idx));
    refreshInstalledTextures();
}

void MainWindow::searchTextures()
{
    const QString profileId = m_texProfileCombo->currentData().toString();
    if (profileId.isEmpty()) {
        showInfo("Installe d'abord une version de Minecraft.", Sev::Info);
        return;
    }
    QString mcVersion;
    for (const auto& pr : Game::allProfiles())
        if (pr.id == profileId) mcVersion = pr.mcVersion;
    const ContentType type = m_texTypeCombo->currentData().toInt() == 1 ? ContentType::Shader : ContentType::ResourcePack;
    const QString query = m_texSearch->text().trimmed();

    auto result = std::make_shared<QList<ModSearchResult>>();
    runTask(
        [=](const Progress& p) {
            p("Recherche…", 0, 0);
            QNetworkAccessManager nam;
            *result = Mods::search(nam, query, mcVersion, QString(), type);
        },
        [this, result, profileId, type] {
            m_texResultsList->clear();
            for (const auto& m : *result) {
                auto* holder = new QWidget;
                auto* hl = new QVBoxLayout(holder);
                hl->setContentsMargins(0, 0, 8, 4);
                auto* row = new QFrame;
                row->setObjectName("row");
                auto* rl = new QHBoxLayout(row);
                rl->setContentsMargins(16, 10, 12, 10);
                rl->setSpacing(12);

                auto* icon = new ModIcon(42);
                icon->setGlyph(Glyph::Color, QColor(255, 255, 255, 20), TextSecondary);
                loadModIcon(m_net, m.iconUrl, icon);
                rl->addWidget(icon);

                auto* texts = new QVBoxLayout;
                texts->setSpacing(1);
                auto* title = label(m.title);
                title->setFont(uiFont(14, QFont::DemiBold));
                texts->addWidget(title);
                auto* desc = label(m.description, "secondary");
                desc->setWordWrap(true);
                texts->addWidget(desc);
                rl->addLayout(texts, 1);

                auto* dlBadge = label(formatCount(m.downloads) + " dl", "badge");
                dlBadge->setFixedHeight(22);
                rl->addWidget(dlBadge, 0, Qt::AlignVCenter);

                auto* instBtn = new QPushButton("  Installer");
                instBtn->setIcon(glyphIcon(Glyph::Download, TextPrimary, 14));
                instBtn->setMinimumWidth(110);
                const QString projectId = m.projectId;
                connect(instBtn, &QPushButton::clicked, this,
                        [this, projectId, profileId, type] { installTexture(projectId, profileId, type); });
                rl->addWidget(instBtn);

                hl->addWidget(row);
                auto* item = new QListWidgetItem(m_texResultsList);
                item->setSizeHint(QSize(0, 70));
                m_texResultsList->setItemWidget(item, holder);
            }
            if (result->isEmpty()) showInfo("Aucun résultat pour cette recherche.", Sev::Info);
        });
}

void MainWindow::installTexture(const QString& projectId, const QString& profileId, ContentType type)
{
    QString mcVersion;
    for (const auto& pr : Game::allProfiles())
        if (pr.id == profileId) mcVersion = pr.mcVersion;

    auto fileName = std::make_shared<QString>();
    runTask(
        [=](const Progress& p) {
            QNetworkAccessManager nam;
            const auto versions = Mods::versionsFor(nam, projectId, mcVersion, QString(), type);
            if (versions.isEmpty()) throw Error{"Aucune version compatible n'a été trouvée."};
            const ModFile file = versions.first();
            *fileName = file.fileName;
            Mods::install(nam, file, profileId, type, p);
        },
        [this, fileName] {
            showInfo(*fileName + " a été installé.", Sev::Success);
            refreshInstalledTextures();
            refreshInstances();
        });
}

void MainWindow::refreshInstalledTextures()
{
    m_texInstalledList->clear();
    const QString profileId = m_texProfileCombo->currentData().toString();
    if (profileId.isEmpty()) return;
    const ContentType type = m_texTypeCombo->currentData().toInt() == 1 ? ContentType::Shader : ContentType::ResourcePack;
    for (const auto& fileName : Mods::installedFiles(profileId, type)) {
        auto* holder = new QWidget;
        auto* hl = new QVBoxLayout(holder);
        hl->setContentsMargins(0, 0, 8, 4);
        auto* row = new QFrame;
        row->setObjectName("row");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(16, 8, 12, 8);
        rl->setSpacing(12);
        auto* icon = new ModIcon(26);
        icon->setGlyph(Glyph::Check, QColor(108, 203, 95, 40), QColor("#6ccb5f"));
        rl->addWidget(icon);
        rl->addWidget(label(fileName), 1);
        auto* delBtn = new QPushButton;
        delBtn->setObjectName("subtle");
        delBtn->setIcon(glyphIcon(Glyph::Delete, QColor("#ff99a4"), 14));
        connect(delBtn, &QPushButton::clicked, this, [this, profileId, type, fileName] {
            Mods::remove(profileId, type, fileName);
            refreshInstalledTextures();
            refreshInstances();
        });
        rl->addWidget(delBtn);
        hl->addWidget(row);
        auto* item = new QListWidgetItem(m_texInstalledList);
        item->setSizeHint(QSize(0, 48));
        m_texInstalledList->setItemWidget(item, holder);
    }
}

// ---------------------------------------------------------------------------
//  Instances — vue d'ensemble de chaque profil installé
// ---------------------------------------------------------------------------
void MainWindow::refreshInstances()
{
    QLayoutItem* item;
    while ((item = m_instancesList->takeAt(0))) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    const auto profiles = Game::allProfiles();
    if (profiles.isEmpty()) {
        m_instancesList->addWidget(label("Aucune version installée pour le moment.", "secondary"));
        return;
    }

    for (const auto& pr : profiles) {
        const bool modded = !pr.loader.isEmpty();
        auto* card = new QFrame;
        card->setObjectName("card");
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(18, 14, 18, 14);
        cl->setSpacing(10);

        auto* top = new QHBoxLayout;
        top->setSpacing(12);
        auto* icon = new ModIcon(36);
        if (modded) icon->setLetters(loaderInitials(pr.loader), loaderColor(pr.loader));
        else icon->setGlyph(Glyph::Cube, QColor(255, 255, 255, 20), TextSecondary);
        top->addWidget(icon);

        auto* name = label(modded ? (loaderDisplayName(loaderFromKey(pr.loader)) + "  ·  " + pr.mcVersion) : pr.id);
        name->setFont(uiFont(15, QFont::DemiBold));
        top->addWidget(name);
        top->addStretch();

        const QString id = pr.id;
        auto* playBtn = new QPushButton("Jouer");
        playBtn->setObjectName("accent");
        playBtn->setMinimumWidth(90);
        connect(playBtn, &QPushButton::clicked, this, [this, id] { play(id); });
        top->addWidget(playBtn);

        auto* folderBtn = new QPushButton;
        folderBtn->setIcon(glyphIcon(Glyph::Folder, TextPrimary, 14));
        folderBtn->setToolTip("Ouvrir le dossier du jeu");
        connect(folderBtn, &QPushButton::clicked, this, [id] {
            const QString d = Game::instanceDir(id);
            QDir().mkpath(d);
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        });
        top->addWidget(folderBtn);
        cl->addLayout(top);

        auto addContentRow = [&](const QString& rowLabel, ContentType type) {
            const auto files = Mods::installedFiles(id, type);
            auto* row = new QHBoxLayout;
            row->setSpacing(8);
            auto* lbl = label(QString("%1 (%2)").arg(rowLabel).arg(files.size()), "secondary");
            lbl->setFixedWidth(160);
            row->addWidget(lbl);
            auto* names = label(files.isEmpty() ? "aucun" : files.join(", "), "secondary");
            names->setWordWrap(true);
            row->addWidget(names, 1);
            auto* openBtn = new QPushButton;
            openBtn->setObjectName("subtle");
            openBtn->setIcon(glyphIcon(Glyph::Folder, TextSecondary, 12));
            openBtn->setFixedSize(28, 28);
            connect(openBtn, &QPushButton::clicked, this, [id, type] {
                const QString d = Mods::dir(id, type);
                QDir().mkpath(d);
                QDesktopServices::openUrl(QUrl::fromLocalFile(d));
            });
            row->addWidget(openBtn);
            cl->addLayout(row);
        };

        if (modded) addContentRow("Mods", ContentType::Mod);
        addContentRow("Packs de textures", ContentType::ResourcePack);
        addContentRow("Shaders", ContentType::Shader);

        m_instancesList->addWidget(card);
    }
}

// ---------------------------------------------------------------------------
//  Skin & capes
// ---------------------------------------------------------------------------
void MainWindow::refreshSkin()
{
    if (!m_account.valid()) {
        m_skinPreview->setPixmap(QPixmap());
        m_skinVariantLabel->setText("Connecte-toi avec ton compte Microsoft dans Paramètres pour gérer ton skin.");
        m_capeList->clear();
        m_mcProfile = MinecraftProfile{};
        return;
    }
    const QString clientId = m_clientId->text().trimmed();
    const Account current = m_account;
    auto profile = std::make_shared<MinecraftProfile>();
    auto accOut = std::make_shared<Account>();
    runTask(
        [=](const Progress& p) {
            p("Récupération du profil…", 0, 0);
            const Account acc = Auth::refreshIfNeeded(clientId, current);
            QNetworkAccessManager nam;
            *profile = Skins::fetchProfile(nam, acc.accessToken);
            *accOut = acc;
        },
        [this, profile, accOut] {
            m_account = *accOut;
            saveAccount();
            m_mcProfile = *profile;
            applySkinProfileUi();
        });
}

void MainWindow::applySkinProfileUi()
{
    SkinEntry active;
    for (const auto& s : m_mcProfile.skins)
        if (s.state == "ACTIVE") active = s;

    m_skinVariantLabel->setText(active.url.isEmpty()
                                    ? "Aucun skin personnalisé détecté."
                                    : (active.variant == "SLIM" ? "Modèle actif : fin (Alex)"
                                                                 : "Modèle actif : classique (Steve)"));
    const QString variantKey = active.variant == "SLIM" ? "slim" : "classic";
    m_skinVariantCombo->setCurrentIndex(qMax(0, m_skinVariantCombo->findData(variantKey)));

    m_skinPreview->setPixmap(QPixmap());
    if (!active.url.isEmpty()) {
        QNetworkRequest req{QUrl(active.url)};
        req.setHeader(QNetworkRequest::UserAgentHeader, "BlockLauncher/1.0");
        QNetworkReply* r = m_net->get(req);
        const bool slim = active.variant == "SLIM";
        QPointer<QLabel> preview(m_skinPreview);
        connect(r, &QNetworkReply::finished, this, [r, slim, preview] {
            r->deleteLater();
            if (preview.isNull() || r->error() != QNetworkReply::NoError) return;
            QImage img;
            if (img.loadFromData(r->readAll())) preview->setPixmap(renderSkinFront(img, slim));
        });
    }

    m_capeList->clear();

    // Option « Aucune cape ».
    {
        const bool noneActive = std::none_of(m_mcProfile.capes.begin(), m_mcProfile.capes.end(),
                                             [](const CapeEntry& c) { return c.state == "ACTIVE"; });
        auto* holder = new QWidget;
        auto* hl = new QVBoxLayout(holder);
        hl->setContentsMargins(0, 0, 8, 4);
        auto* row = new QFrame;
        row->setObjectName("row");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(16, 8, 12, 8);
        rl->setSpacing(12);
        auto* icon = new ModIcon(32);
        icon->setGlyph(Glyph::Close, QColor(255, 255, 255, 20), TextSecondary);
        rl->addWidget(icon);
        rl->addWidget(label("Aucune cape"), 1);
        if (noneActive) {
            rl->addWidget(glyphLabel(Glyph::Check, 14, QColor("#6ccb5f")));
            rl->addWidget(label("Actif", "secondary"));
        } else {
            auto* btn = new QPushButton("Retirer la cape");
            connect(btn, &QPushButton::clicked, this, [this] { activateCape(QString()); });
            rl->addWidget(btn);
        }
        hl->addWidget(row);
        auto* item = new QListWidgetItem(m_capeList);
        item->setSizeHint(QSize(0, 56));
        m_capeList->setItemWidget(item, holder);
    }

    for (const auto& c : m_mcProfile.capes) {
        const bool active2 = c.state == "ACTIVE";
        auto* holder = new QWidget;
        auto* hl = new QVBoxLayout(holder);
        hl->setContentsMargins(0, 0, 8, 4);
        auto* row = new QFrame;
        row->setObjectName("row");
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(16, 8, 12, 8);
        rl->setSpacing(12);

        auto* icon = new ModIcon(32);
        icon->setGlyph(Glyph::Color, QColor(255, 255, 255, 20), TextSecondary);
        loadCapeThumb(m_net, c.url, icon);
        rl->addWidget(icon);
        rl->addWidget(label(c.alias.isEmpty() ? "Cape" : c.alias), 1);

        const QString capeId = c.id;
        if (active2) {
            rl->addWidget(glyphLabel(Glyph::Check, 14, QColor("#6ccb5f")));
            rl->addWidget(label("Actif", "secondary"));
        } else {
            auto* btn = new QPushButton("Équiper");
            btn->setObjectName("accent");
            connect(btn, &QPushButton::clicked, this, [this, capeId] { activateCape(capeId); });
            rl->addWidget(btn);
        }
        hl->addWidget(row);
        auto* item = new QListWidgetItem(m_capeList);
        item->setSizeHint(QSize(0, 56));
        m_capeList->setItemWidget(item, holder);
    }

    if (m_mcProfile.capes.isEmpty()) {
        auto* empty = label("Tu ne possèdes aucune cape pour le moment.", "secondary");
        auto* item = new QListWidgetItem(m_capeList);
        item->setSizeHint(QSize(0, 28));
        m_capeList->setItemWidget(item, empty);
    }
}

void MainWindow::changeSkin(const QString& filePath, const QString& variant)
{
    if (!m_account.valid()) return;
    const QString clientId = m_clientId->text().trimmed();
    const Account current = m_account;
    auto accOut = std::make_shared<Account>();
    runTask(
        [=](const Progress& p) {
            p("Envoi du skin…", 0, 0);
            const Account acc = Auth::refreshIfNeeded(clientId, current);
            QNetworkAccessManager nam;
            Skins::uploadSkin(nam, acc.accessToken, filePath, variant);
            *accOut = acc;
        },
        [this, accOut] {
            m_account = *accOut;
            saveAccount();
            showInfo("Ton skin a été mis à jour.", Sev::Success);
            refreshSkin();
        });
}

void MainWindow::resetSkinAction()
{
    if (!m_account.valid()) return;
    const QString clientId = m_clientId->text().trimmed();
    const Account current = m_account;
    auto accOut = std::make_shared<Account>();
    runTask(
        [=](const Progress& p) {
            const Account acc = Auth::refreshIfNeeded(clientId, current);
            QNetworkAccessManager nam;
            Skins::resetSkin(nam, acc.accessToken);
            *accOut = acc;
        },
        [this, accOut] {
            m_account = *accOut;
            saveAccount();
            showInfo("Skin réinitialisé.", Sev::Info);
            refreshSkin();
        });
}

void MainWindow::activateCape(const QString& capeId)
{
    if (!m_account.valid()) return;
    const QString clientId = m_clientId->text().trimmed();
    const Account current = m_account;
    auto accOut = std::make_shared<Account>();
    runTask(
        [=](const Progress& p) {
            const Account acc = Auth::refreshIfNeeded(clientId, current);
            QNetworkAccessManager nam;
            Skins::setActiveCape(nam, acc.accessToken, capeId);
            *accOut = acc;
        },
        [this, accOut, capeId] {
            m_account = *accOut;
            saveAccount();
            showInfo(capeId.isEmpty() ? "Cape retirée." : "Cape équipée.", Sev::Success);
            refreshSkin();
        });
}

// ---------------------------------------------------------------------------
//  Mise à jour automatique
// ---------------------------------------------------------------------------
void MainWindow::checkUpdates(bool silent)
{
    if (m_busy) return; // vérification silencieuse au démarrage : ne gêne jamais une tâche en cours
    if (m_updateDesc)
        m_updateDesc->setText(QString("Version actuelle : %1 — vérification…").arg(BLOCKLAUNCHER_VERSION));
    if (m_updateBtn) m_updateBtn->setEnabled(false);

    auto info = std::make_shared<UpdateInfo>();
    runTask(
        [info](const Progress&) {
            QNetworkAccessManager nam;
            *info = Update::checkForUpdate(nam, BLOCKLAUNCHER_VERSION);
        },
        [this, info, silent] {
            m_latestUpdate = *info;
            if (m_updateBtn) m_updateBtn->setEnabled(true);
            if (m_latestUpdate.version.isEmpty()) {
                if (m_updateDesc)
                    m_updateDesc->setText(QString("Version actuelle : %1 — à jour.").arg(BLOCKLAUNCHER_VERSION));
                if (m_updateBtn) {
                    m_updateBtn->setText("Vérifier");
                    m_updateBtn->setObjectName("");
                }
            } else {
                if (m_updateDesc)
                    m_updateDesc->setText(QString("Version actuelle : %1 — nouvelle version %2 disponible !")
                                              .arg(BLOCKLAUNCHER_VERSION, m_latestUpdate.version));
                if (m_updateBtn) {
                    m_updateBtn->setText("  Mettre à jour");
                    m_updateBtn->setIcon(glyphIcon(Glyph::Download, Qt::black, 14));
                    m_updateBtn->setObjectName("accent");
                }
                showInfo("Une nouvelle version (" + m_latestUpdate.version + ") de Block Launcher est disponible.",
                         Sev::Info);
            }
            if (m_updateBtn) {
                m_updateBtn->style()->unpolish(m_updateBtn);
                m_updateBtn->style()->polish(m_updateBtn);
            }
        },
        silent);
}

void MainWindow::installUpdate()
{
    if (m_latestUpdate.version.isEmpty()) return;
    const UpdateInfo info = m_latestUpdate;
    runTask(
        [info](const Progress& p) {
            QNetworkAccessManager nam;
            Update::downloadAndLaunchInstaller(nam, info, p);
        },
        [this] {
            showInfo("Mise à jour lancée : le launcher va se fermer pour l'installer.", Sev::Info);
            QTimer::singleShot(1500, qApp, &QApplication::quit);
        });
}
