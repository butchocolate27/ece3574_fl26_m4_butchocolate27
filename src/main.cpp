#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>

#include <cstdio>

namespace {

void confirm_exit(QWidget* parent) {
    const auto answer = QMessageBox::question(
        parent,
        "Exit",
        "Exit AI Workspace?",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (answer == QMessageBox::Yes) {
        parent->close();
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription("Responsive AI Workspace");
    parser.addHelpOption();
    const QCommandLineOption delay_option(
        "build-delay-ms", "Extra build time per document in milliseconds.", "N", "0");
    parser.addOption(delay_option);
    parser.process(app);
    bool valid_delay = false;
    const int delay_ms = parser.value(delay_option).toInt(&valid_delay);
    if (!valid_delay || delay_ms < 0) {
        std::fprintf(stderr, "--build-delay-ms requires a non-negative integer.\n");
        return 1;
    }

    aiws::gui::MainWindow window;
    window.set_build_delay_ms(delay_ms);
    window.set_exit_callback(&confirm_exit); // Set up window
    window.show();

    return app.exec();
}
