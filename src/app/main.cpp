#include <QApplication>
#include <QCoreApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>

#include "app/mainwindow.h"

namespace {

// 绿黑「雷达」主题：与 EaiLidar Viewer 同一套配色（点云/目标同款绿 #00DC78）。
void applyRadarTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    const QColor black(0, 0, 0);
    const QColor green(0, 220, 120);
    const QColor greenDim(0, 90, 50);

    QPalette pal;
    pal.setColor(QPalette::Window, black);
    pal.setColor(QPalette::WindowText, green);
    pal.setColor(QPalette::Base, black);
    pal.setColor(QPalette::AlternateBase, black);
    pal.setColor(QPalette::Text, green);
    pal.setColor(QPalette::Button, black);
    pal.setColor(QPalette::ButtonText, green);
    pal.setColor(QPalette::BrightText, Qt::white);
    pal.setColor(QPalette::Highlight, green);
    pal.setColor(QPalette::HighlightedText, black);
    pal.setColor(QPalette::ToolTipBase, black);
    pal.setColor(QPalette::ToolTipText, green);
    pal.setColor(QPalette::Link, green);
    pal.setColor(QPalette::PlaceholderText, greenDim);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, greenDim);
    pal.setColor(QPalette::Disabled, QPalette::Text, greenDim);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, greenDim);
    app.setPalette(pal);

    app.setStyleSheet(QStringLiteral(R"(
QMainWindow, QWidget {
    background-color: #000000;
    color: #00DC78;
}
QToolBar, QStatusBar {
    background-color: #000000;
    border: none;
    spacing: 4px;
}
QDockWidget { background-color: #000000; }
QPushButton {
    background-color: #000000;
    color: #00DC78;
    border: 1px solid #00DC78;
    border-radius: 2px;
    padding: 4px 12px;
    min-height: 30px;
    max-height: 30px;
}
QPushButton:hover { background-color: #00DC78; color: #000000; }
QPushButton:pressed { background-color: #00a85a; color: #000000; }
QPushButton:disabled { color: #005A32; border-color: #005A32; background-color: #000000; }
QCheckBox {
    background-color: #000000;
    color: #00DC78;
    spacing: 8px;
    border: 1px solid #00DC78;
    border-radius: 2px;
    padding: 6px 10px;
    outline: none;
    min-height: 26px;
    max-height: 26px;
}
QCheckBox::indicator { width: 16px; height: 16px; border: none; background-color: #000000; }
QCheckBox::indicator:checked { background-color: #00DC78; }
QCheckBox:disabled { color: #005A32; border-color: #005A32; }
QComboBox, QSpinBox, QDoubleSpinBox {
    background-color: #000000;
    color: #00DC78;
    border: 1px solid #00DC78;
    border-radius: 2px;
    padding: 2px 6px;
    min-height: 34px;
    max-height: 34px;
}
QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {
    color: #005A32; border-color: #005A32;
}
QComboBox::drop-down { border: none; width: 18px; }
QComboBox::down-arrow {
    image: none; width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 6px solid #00DC78;
    margin-right: 5px;
}
QComboBox QAbstractItemView {
    background-color: #000000; color: #00DC78;
    border: 1px solid #00DC78;
    selection-background-color: #00DC78; selection-color: #000000; outline: none;
}
QComboBox QLineEdit { background-color: #000000; color: #00DC78; border: none; padding: 1px; }
QSpinBox::up-button, QDoubleSpinBox::up-button,
QSpinBox::down-button, QDoubleSpinBox::down-button {
    background-color: #000000; border: none; width: 14px;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: none; width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-bottom: 6px solid #00DC78;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: none; width: 0; height: 0;
    border-left: 4px solid transparent;
    border-right: 4px solid transparent;
    border-top: 6px solid #00DC78;
}
QTableWidget {
    background-color: #000000; color: #00DC78;
    gridline-color: #005A32; border: 1px solid #00DC78;
    selection-background-color: #00DC78; selection-color: #000000;
}
QHeaderView::section {
    background-color: #000000; color: #00DC78;
    border: none; border-right: 1px solid #005A32;
    border-bottom: 1px solid #00DC78; padding: 3px;
}
QToolTip { background-color: #000000; color: #00DC78; border: 1px solid #00DC78; padding: 2px 6px; }
)"));
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("LD2451 Radar Viewer");
    app.setOrganizationName("LD2451");

    applyRadarTheme(app);

    ld2451::MainWindow window;
    window.show();
    if (QCoreApplication::arguments().contains(QStringLiteral("--demo"))) {
        QTimer::singleShot(0, &window, [&window] { window.startDemo(); });
    }
    return app.exec();
}
