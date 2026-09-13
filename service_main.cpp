/*
 * T14FinishService entry point
 * ----------------------------
 * MAINTENANCE GUIDE:
 *   - Running this executable with NO arguments starts the background TCP service
 *     on localhost. This is the mode used by systemd and Qt Creator when testing
 *     the service itself.
 *   - Running this executable WITH arguments delegates those arguments to the
 *     installed `t14-finish` management/client script. This makes commands such as
 *       ./T14FinishService ai
 *       ./T14FinishService deadline
 *     behave the same way as `t14-finish ai` and `t14-finish deadline`.
 *   - The shell helper intentionally owns user-editable management features such
 *     as MOTD editing and AI instruction files. The C++ background service owns
 *     timing, weather, sunset, cache, and socket responses.
 *   - If you change the localhost protocol, update both FinishService::handleCommand()
 *     and scripts/t14-finish.
 */

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

#include "finishservice.h"

namespace {

// Resolve the installed helper without hard-coding the account name.  The install
// script normally places the helper in ~/.local/bin/t14-finish.
QString helperPath()
{
    return QDir::homePath() + QStringLiteral("/.local/bin/t14-finish");
}

// Client/management mode.  We forward stdin/stdout/stderr so interactive commands
// such as `motd edit` and `ai-edit` still work naturally from a terminal.
int runHelperCommand(const QStringList &arguments)
{
    const QString helper = helperPath();
    if (!QFileInfo::exists(helper)) {
        QTextStream err(stderr);
        err << "ERROR: " << helper << " is not installed.\n"
            << "Run the T14FinishService install.sh first, or use the installed t14-finish command.\n";
        return 2;
    }

    QProcess process;
    process.setProgram(helper);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.start();

    if (!process.waitForStarted()) {
        QTextStream err(stderr);
        err << "ERROR: could not start " << helper << ": " << process.errorString() << "\n";
        return 2;
    }

    process.waitForFinished(-1);
    if (process.exitStatus() != QProcess::NormalExit)
        return 1;
    return process.exitCode();
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("T14FinishService");
    QCoreApplication::setApplicationVersion("1.0-rc1");

    // Any argument means the user is invoking the executable as a command-line
    // client/management tool rather than asking it to become another server copy.
    // This also avoids the old behavior where `./T14FinishService ai` silently
    // attempted to start a second service and failed if the configured port was occupied.
    if (app.arguments().size() > 1)
        return runHelperCommand(app.arguments().mid(1));

    // No arguments: normal long-running background-service mode.
    FinishService service;
    if (!service.start()) {
        QTextStream err(stderr);
        err << service.startError() << "\n";
        return 1;
    }

    return app.exec();
}
