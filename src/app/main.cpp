#include <solidara/main_window.hpp>
#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDir>
#include <array>
#include <utility>
#include <QIcon>
#include <QTimer>

namespace {
void configureBundledKernelResources() {
    const QDir root(QCoreApplication::applicationDirPath() + "/../share/Solidara/opencascade");
    if (!root.exists()) return;
    const std::array<std::pair<const char*, const char*>, 5> locations{{
        {"CSF_OCCTResourcePath", "."},
        {"CSF_ShadersDirectory", "Shaders"},
        {"CSF_XSMessage", "XSMessage"},
        {"CSF_SHMessage", "SHMessage"},
        {"CSF_STEPDefaults", "XSTEPResource"}
    }};
    for (const auto& [variable, relativePath] : locations) {
        if (!qEnvironmentVariableIsSet(variable) && root.exists(relativePath))
            qputenv(variable, root.absoluteFilePath(relativePath).toUtf8());
    }
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    configureBundledKernelResources();
    QApplication::setApplicationName("Solidara");
    QApplication::setOrganizationName("Solidara");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setWindowIcon(QIcon(":/icons/solidara.png"));
    QApplication::setStyle("Fusion");
    QFile theme(":/theme.qss");
    if (theme.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(theme.readAll()));
    QCommandLineParser parser;
    parser.setApplicationDescription("Solidara — offline parametric CAD");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"smoke-test", "Exercise native UI, regeneration and local export, then exit.", "directory"});
    parser.process(app);
    solidara::MainWindow window;
    window.show();
    if (parser.isSet("smoke-test")) {
        const auto directory = parser.value("smoke-test");
        QTimer::singleShot(500, &window, [&window, directory] { window.runSmokeTest(directory); });
    }
    return app.exec();
}
