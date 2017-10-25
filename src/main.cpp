/*
 * Copyright (C) 2017 ~ 2017 Deepin Technology Co., Ltd.
 *
 * Author:     zccrs <zccrs@live.com>
 *
 * Maintainer: zccrs <zhangjide@deepin.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QDebug>
#include <DLog>

#ifdef ENABLE_GUI
#include <DApplication>
#include <DTitlebar>
#include <DThemeManager>

#include "mainwindow.h"

#include <pwd.h>

DWIDGET_USE_NAMESPACE
#else
#include <QCoreApplication>
#endif

#include "helper.h"
#include "dglobal.h"
#include "clonejob.h"
#include "commandlineparser.h"

bool Global::isOverride = true;
bool Global::disableMD5CheckForDimFile = false;
bool Global::disableLoopDevice = true;
bool Global::fixBoot = false;
#ifdef ENABLE_GUI
bool Global::isTUIMode = false;
#else
bool Global::isTUIMode = true;
#endif

int Global::bufferSize = 1024 * 1024;
int Global::compressionLevel = 0;
int Global::debugLevel = 1;

DCORE_USE_NAMESPACE

inline static bool isTUIMode(int argc, char *argv[])
{
#ifndef ENABLE_GUI
    Q_UNUSED(argc)
    Q_UNUSED(argv)

    return true;
#endif

    if (qEnvironmentVariableIsEmpty("DISPLAY"))
        return true;

    const QByteArrayList in_tui_args = {
        "--tui", "-i", "--info", "--dim-info", "--to-serial-url",
        "--from-serial-url", "-f", "--fix-boot", "-v", "--version",
        "-h", "--help", "--re-checksum"
    };

    for (int i = 1; i < argc; ++i)
        if (in_tui_args.contains(argv[i]))
            return true;

    return false;
}

static QString logFormat = "[%{time}{yyyy-MM-dd, HH:mm:ss.zzz}] [%{type:-7}] [%{file}=>%{function}: %{line}] %{message}\n";

int main(int argc, char *argv[])
{
    QCoreApplication *a;

    if (isTUIMode(argc, argv)) {
        Global::isTUIMode = true;

        a = new QCoreApplication(argc, argv);
    }
#ifdef ENABLE_GUI
    else {
        ConsoleAppender *consoleAppender = new ConsoleAppender;
        consoleAppender->setFormat(logFormat);

        RollingFileAppender *rollingFileAppender = new RollingFileAppender("/tmp/.deepin-clone.log");
        rollingFileAppender->setFormat(logFormat);
        rollingFileAppender->setLogFilesLimit(5);
        rollingFileAppender->setDatePattern(RollingFileAppender::DailyRollover);

        logger->registerAppender(consoleAppender);
        logger->registerAppender(rollingFileAppender);

        if (qEnvironmentVariableIsSet("PKEXEC_UID")) {
            quint32 pkexec_uid = qgetenv("PKEXEC_UID").toUInt();
            const QDir user_home(getpwuid(pkexec_uid)->pw_dir);

            QFile pam_file(user_home.absoluteFilePath(".pam_environment"));

            if (pam_file.open(QIODevice::ReadOnly)) {
                while (!pam_file.atEnd()) {
                    const QByteArray &line = pam_file.readLine();

                    if (line.startsWith("QT_SCALE_FACTOR")) {
                        const QByteArrayList &list = line.split('=');

                        if (list.count() == 2) {
                            qputenv("QT_SCALE_FACTOR", list.last());
                        }
                    }
                }

                pam_file.close();
            }
        }

        DApplication::loadDXcbPlugin();
        DApplication *app = new DApplication(argc, argv);

        if (!qApp->setSingleInstance("_deepin_clone_")) {
            qCritical() << "As well as the process is running";

            return -1;
        }

        if (!app->loadTranslator()) {
            dError("Load translator failed");
        }

        app->setApplicationDisplayName(QObject::tr("Deepin Clone"));
        app->setApplicationDescription(QObject::tr("Deepin Clone is a tool to backup and restore in deepin. "
                                                   "It supports to clone, backup and restore disk or partition and other functions."));
        app->setTheme("light");
        a = app;
    }
#endif

    a->setApplicationName("deepin-clone");
    a->setApplicationVersion("1.0.0");
    a->setOrganizationName("deepin");

    CommandLineParser parser;

    QFile arguments_file("/lib/live/mount/medium/.tmp/deepin-clone.arguments");
    QStringList arguments;

    bool load_arg_from_file = arguments_file.exists() && !Global::isTUIMode && !a->arguments().contains("--tui");

    if (load_arg_from_file) {
        arguments.append(a->arguments().first());

        if (!arguments_file.open(QIODevice::ReadOnly)) {
            qCritical() << "Open \"/lib/live/mount/medium/.tmp/deepin-clone.arguments\" failed, error:" << arguments_file.errorString();
        } else {
            while (!arguments_file.atEnd()) {
                const QString &arg = QString::fromUtf8(arguments_file.readLine().trimmed());

                if (!arg.isEmpty())
                    arguments.append(arg);
            }

            arguments_file.close();
            arguments_file.remove();
        }

        qDebug() << arguments;
    } else {
        arguments = a->arguments();
    }

    parser.process(arguments);

    ConsoleAppender *consoleAppender = new ConsoleAppender;
    consoleAppender->setFormat(logFormat);

    RollingFileAppender *rollingFileAppender = new RollingFileAppender(parser.logFile());
    rollingFileAppender->setFormat(logFormat);
    rollingFileAppender->setLogFilesLimit(5);
    rollingFileAppender->setDatePattern(RollingFileAppender::DailyRollover);

    logger->registerCategoryAppender("deepin.ghost", consoleAppender);
    logger->registerCategoryAppender("deepin.ghost", rollingFileAppender);

    parser.parse();

    if (load_arg_from_file) {
        dCDebug("Load arguments from \"%s\"", qPrintable(arguments_file.fileName()));
    }

    dCInfo("Application command line: %s", qPrintable(arguments.join(' ')));

    if (Global::debugLevel == 0) {
        QLoggingCategory::setFilterRules("deepin.ghost.debug=false");
    }

    if (Global::isTUIMode) {
        if (!parser.target().isEmpty()) {
            CloneJob job;

            QObject::connect(&job, &QThread::finished, a, &QCoreApplication::quit);

            job.start(parser.source(), parser.target());

            return a->exec();
        }
    }
#ifdef ENABLE_GUI
    else {
        if (!parser.isSetOverride())
            Global::isOverride = true;

        if (!parser.isSetDebug())
            Global::debugLevel = 2;

        MainWindow *window = new MainWindow;

        window->setFixedSize(860, 660);
        window->setStyleSheet(DThemeManager::instance()->getQssForWidget("main", window));
        window->setWindowIcon(QIcon::fromTheme("deepin-clone"));
        window->titlebar()->setWindowFlags(Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint
                                           | Qt::WindowSystemMenuHint | Qt::WindowTitleHint);
        window->titlebar()->setIcon(window->windowIcon().pixmap(24));
        window->titlebar()->setTitle(QString());
        window->show();

        qApp->setProductIcon(window->windowIcon().pixmap(128));

        if (!parser.source().isEmpty()) {
            window->startWithFile(parser.source(), parser.target());
        }

        QObject::connect(a, &QCoreApplication::aboutToQuit, window, &MainWindow::deleteLater);
    }
#endif

    return Global::isTUIMode ? qApp->exec() : a->exec();
}
