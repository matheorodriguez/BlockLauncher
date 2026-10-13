#include "MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QOperatingSystemVersion>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("BlockLauncher");
    QApplication::setApplicationName("BlockLauncher");
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QFont font;
    font.setFamilies({"Segoe UI Variable Text", "Segoe UI"});
    font.setPixelSize(14);
    QApplication::setFont(font);

    QPalette pal;
    pal.setColor(QPalette::Window, QColor("#202020"));
    pal.setColor(QPalette::Base, QColor("#2c2c2c"));
    pal.setColor(QPalette::Text, Qt::white);
    pal.setColor(QPalette::WindowText, Qt::white);
    pal.setColor(QPalette::ButtonText, Qt::white);
    pal.setColor(QPalette::Highlight, QColor("#0078d4"));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    QApplication::setPalette(pal);

    // Mica : uniquement Windows 11 22H2 (build 22621) et plus, désactivable dans Paramètres.
    bool mica = false;
#ifdef Q_OS_WIN
    const auto os = QOperatingSystemVersion::current();
    mica = os.majorVersion() >= 10 && os.microVersion() >= 22621 && QSettings().value("mica", true).toBool();
#endif

    MainWindow w(mica);
    w.show();
    return app.exec();
}
