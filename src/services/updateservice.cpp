#include "services/updateservice.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QObject>
#include <QMessageBox>
#include <QLibraryInfo>
#include <QUrlQuery>
#include <QSettings>
#include <QApplication>
#include <QJSEngine>
#include "build_number.h"
#include "dialogs/updatedialog.h"
#include "version.h"
#include "release.h"
#include "mainwindow.h"

UpdateService::UpdateService(QObject *parent) :
        QObject(parent) {
}

void UpdateService::checkForUpdates(MainWindow *mainWindow, UpdateMode updateMode) {
    this->mainWindow = mainWindow;
    this->updateMode = updateMode;

    QSettings settings;
    if (updateMode != Manual) {
        settings.setValue("LastUpdateCheck", QDateTime::currentDateTime());
    }

    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
    connect(manager, SIGNAL(finished(QNetworkReply *)),
            this, SLOT(onResult(QNetworkReply *)));

    bool isDebug;

#ifdef QT_NO_DEBUG
    isDebug = false;
#else
    isDebug = true;
#endif

    // there are troubles with https by default on different platforms,
    // so we are using http everywhere for now
    QUrl url("http://www.qownnotes.org/api/v1/last_release/QOwnNotes/" +
             QString(PLATFORM) + ".json");

    QUrlQuery q;
    q.addQueryItem("b", QString::number(BUILD));
    q.addQueryItem("v", QString(VERSION));
    q.addQueryItem("d", QString(__DATE__) + " " + QString(__TIME__));
    q.addQueryItem("um", QString::number(updateMode));
    q.addQueryItem("debug", QString::number(isDebug));

    if (!settings.value("appMetrics/disableTracking").toBool() ||
            !settings.value("appMetrics/disableAppHeartbeat").toBool()) {
        q.addQueryItem("cid", settings.value("PiwikClientId").toString());
    }

#if (QT_VERSION >= QT_VERSION_CHECK(5, 4, 0))
    q.addQueryItem("r", QString(RELEASE) + " (" +
                        QSysInfo::buildCpuArchitecture() + ")");

    QString operatingSystem = QSysInfo::prettyProductName();
    if (!operatingSystem.contains(QSysInfo::currentCpuArchitecture())) {
        operatingSystem += " (" + QSysInfo::currentCpuArchitecture() + ")";
    }

    q.addQueryItem("o", operatingSystem);
#else
    q.addQueryItem("r", QString(RELEASE) );
#endif

    url.setQuery(q);

    manager->get(QNetworkRequest(url));
}

void UpdateService::onResult(QNetworkReply *reply) {
    // abort if reply was null
    if (reply == NULL) {
        return;
    }

    // abort if there was an error
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << __func__ << " - 'reply error': " << reply->error();
        return;
    }

    QString allData = reply->readAll();
    if (allData.isEmpty()) {
        return;
    }

    // we have to add [], so the string can be parsed as JSON
    QString data = QString("[") + allData + QString("]");

    QJSEngine engine;
    QJSValue result = engine.evaluate(data);

    // get the information if we should update our app
    bool shouldUpdate =
            (!result.property("0").isNull()) ?
            result.property("0").property("should_update").toBool() : false;

    // check if we should update our app
    if (shouldUpdate) {
        // get the release url
        QString releaseUrl = result.property("0").property("release").property(
                "assets").property("0").property(
                "browser_download_url").toString();

        // get the release version string
        QString releaseVersionString = result.property("0").property(
                "release_version_string").toString();

        // get the release build number
        int releaseBuildNumber = result.property("0").property(
                "release_build_number").toInt();

        // get the changes html
        QString changesHtml = result.property("0").property(
                "changes_html").toString();

        // show the update available button
        mainWindow->showUpdateAvailableButton(releaseVersionString);

        bool showUpdateDialog = true;

        // do some more checks for non manual update requests
        if (updateMode != UpdateService::Manual) {
            QSettings settings;
            QString skipVersion = settings.value("skipVersion").toString();

            // check if this version should be skipped
            if (releaseVersionString == skipVersion) {
                showUpdateDialog = false;
            } else if (updateMode == UpdateService::Periodic) {
                // check if the update dialog is already open
                showUpdateDialog = !UpdateDialog::isUpdateDialogOpen();
            }

            // check if the update dialog was disabled
            if (showUpdateDialog) {
                showUpdateDialog = !settings.value
                        ("disableAutomaticUpdateDialog").toBool();
            }
        }

        if (showUpdateDialog) {
            // open the update dialog
            UpdateDialog *dialog = new UpdateDialog(
                    0, changesHtml, releaseUrl,
                    releaseVersionString,
                    releaseBuildNumber);
            dialog->exec();
        }
    } else {
        mainWindow->hideUpdateAvailableButton();

        if (this->updateMode == UpdateService::Manual) {
            QMessageBox::information(
                    0, tr("No updates"),
                    tr("There are no updates available.<br /><strong>%1"
                               "</strong> is the latest version.")
                            .arg(QString(VERSION)));
        }
    }
}
