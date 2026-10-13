#pragma once
// Thème « Fluent » sombre inspiré de Windows 11.
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QString>

namespace Fluent {

inline const QColor Accent("#60cdff");
inline const QColor AccentHover("#5bbde9");
inline const QColor TextPrimary(255, 255, 255);
inline const QColor TextSecondary(255, 255, 255, 200);
inline const QColor TextTertiary(255, 255, 255, 140);

// Glyphes « Segoe Fluent Icons » (même code dans Segoe MDL2 Assets sous Windows 10)
namespace Glyph {
inline constexpr char16_t Home = 0xE80F;
inline constexpr char16_t Library = 0xE8F1;
inline constexpr char16_t Settings = 0xE713;
inline constexpr char16_t Play = 0xE768;
inline constexpr char16_t Contact = 0xE77B;
inline constexpr char16_t Folder = 0xE8B7;
inline constexpr char16_t Download = 0xE896;
inline constexpr char16_t Memory = 0xE964;
inline constexpr char16_t Code = 0xE943;
inline constexpr char16_t Key = 0xE192;
inline constexpr char16_t Close = 0xE8BB;
inline constexpr char16_t Check = 0xE73E;
inline constexpr char16_t Error = 0xEA39;
inline constexpr char16_t Info = 0xE946;
inline constexpr char16_t ChevronDown = 0xE70D;
inline constexpr char16_t Refresh = 0xE72C;
inline constexpr char16_t Search = 0xE721;
inline constexpr char16_t Cube = 0xF158;
inline constexpr char16_t Color = 0xE790;
inline constexpr char16_t SignOut = 0xF3B1;
inline constexpr char16_t Puzzle = 0xEA86;
inline constexpr char16_t Delete = 0xE74D;
inline constexpr char16_t Add = 0xE710;
}

inline QFont iconFont(int px)
{
    QFont f;
    f.setFamilies({"Segoe Fluent Icons", "Segoe MDL2 Assets"});
    f.setPixelSize(px);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

inline QIcon glyphIcon(char16_t glyph, const QColor& color, int px = 16)
{
    QPixmap pm(px * 2, px * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(iconFont(px));
    p.setPen(color);
    p.drawText(QRect(0, 0, px, px), Qt::AlignCenter, QString(QChar(glyph)));
    return QIcon(pm);
}

inline QString styleSheet(bool mica)
{
    QString qss = R"(
* { color: #ffffff; font-size: 14px; }
QToolTip { background: #2c2c2c; color: #fff; border: 1px solid rgba(255,255,255,0.1); padding: 6px; border-radius: 4px; }

QMainWindow, #central { background: %BG%; }
#content { background: %CONTENT%; border-top-left-radius: 8px;
           border-top: 1px solid rgba(255,255,255,0.06); border-left: 1px solid rgba(255,255,255,0.06); }
QStackedWidget, QScrollArea, QScrollArea > QWidget > QWidget, #page { background: transparent; border: none; }

QLabel { background: transparent; }
QLabel#appTitle { font-size: 12px; color: rgba(255,255,255,0.79); }
QLabel#pageTitle { font-size: 28px; font-weight: 600; }
QLabel#sectionTitle { font-size: 14px; font-weight: 600; }
QLabel#secondary { font-size: 12px; color: rgba(255,255,255,0.62); }
QLabel#badge { font-size: 11px; color: rgba(255,255,255,0.79); background: rgba(255,255,255,0.06);
               border: 1px solid rgba(255,255,255,0.08); border-radius: 4px; padding: 1px 6px; }

QFrame#card { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.075); border-radius: 8px; }
QFrame#row  { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.075); border-radius: 6px; }

QFrame#infoBar { border-radius: 6px; border: 1px solid rgba(255,255,255,0.08); }
QFrame#infoBar[sev="info"]    { background: rgba(255,255,255,0.05); }
QFrame#infoBar[sev="success"] { background: #393d1b; }
QFrame#infoBar[sev="error"]   { background: #442726; }

QPushButton { background: rgba(255,255,255,0.061); border: 1px solid rgba(255,255,255,0.093);
              border-radius: 4px; padding: 6px 14px; min-height: 20px; }
QPushButton:hover { background: rgba(255,255,255,0.084); }
QPushButton:pressed { background: rgba(255,255,255,0.033); color: rgba(255,255,255,0.79); }
QPushButton:disabled { background: rgba(255,255,255,0.042); color: rgba(255,255,255,0.36); }
QPushButton#accent { background: #60cdff; color: #000000; border: 1px solid rgba(255,255,255,0.08); font-weight: 600; }
QPushButton#accent:hover { background: #5bbde9; }
QPushButton#accent:pressed { background: #56acd4; color: rgba(0,0,0,0.55); }
QPushButton#accent:disabled { background: rgba(255,255,255,0.16); color: rgba(255,255,255,0.53); }
QPushButton#subtle { background: transparent; border: none; }
QPushButton#subtle:hover { background: rgba(255,255,255,0.06); }

QLineEdit { background: rgba(255,255,255,0.061); border: 1px solid rgba(255,255,255,0.08);
            border-bottom: 1px solid rgba(255,255,255,0.54); border-radius: 4px; padding: 6px 10px;
            selection-background-color: #0078d4; }
QLineEdit:hover { background: rgba(255,255,255,0.084); }
QLineEdit:focus { background: rgba(30,30,30,0.85); border-bottom: 2px solid #60cdff; }

QComboBox { background: rgba(255,255,255,0.061); border: 1px solid rgba(255,255,255,0.093);
            border-radius: 4px; padding: 6px 34px 6px 12px; min-height: 20px; }
QComboBox:hover { background: rgba(255,255,255,0.084); }
QComboBox:disabled { color: rgba(255,255,255,0.36); }
QComboBox::drop-down { border: none; width: 0px; }
QComboBox::down-arrow { image: none; }
QComboBox QAbstractItemView { background: #2c2c2c; border: 1px solid rgba(255,255,255,0.1); border-radius: 8px;
                              padding: 4px; outline: 0; }
QComboBox QAbstractItemView::item { min-height: 32px; padding: 0 10px; border-radius: 4px; }
QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected { background: rgba(255,255,255,0.06); }

QSlider::groove:horizontal { height: 4px; background: rgba(255,255,255,0.54); border-radius: 2px; }
QSlider::sub-page:horizontal { background: #60cdff; border-radius: 2px; }
QSlider::handle:horizontal { background: #60cdff; border: 5px solid #454545; width: 10px; height: 10px;
                             margin: -8px 0; border-radius: 10px; }
QSlider::handle:horizontal:hover { border-width: 4px; }

QProgressBar { background: rgba(255,255,255,0.1); border: none; border-radius: 2px; max-height: 4px; min-height: 4px; }
QProgressBar::chunk { background: #60cdff; border-radius: 2px; }

QListWidget { background: transparent; border: none; outline: 0; }
QListWidget::item, QListWidget::item:selected, QListWidget::item:hover { background: transparent; border: none; }

QScrollBar:vertical { background: transparent; width: 8px; margin: 2px; }
QScrollBar::handle:vertical { background: rgba(255,255,255,0.3); border-radius: 2px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.5); }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)";
    qss.replace("%BG%", mica ? "transparent" : "#202020");
    qss.replace("%CONTENT%", mica ? "rgba(58,58,58,0.30)" : "#272727");
    return qss;
}

} // namespace Fluent
