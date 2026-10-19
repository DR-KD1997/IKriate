/*
 *  SPDX-FileCopyrightText: 2014 Boudewijn Rempt <boud@valdyas.org>
 *  SPDX-FileCopyrightText: 2022 L. E. Segovia <amy@amyspark.me>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_about_application.h"

#include <KAboutData>
#include <KLocalizedString>
#include <QFile>
#include <QStandardPaths>

#include <kis_debug.h>
#include <kis_global.h>

#include "kis_splash_screen.h"
#include "ui_wdgaboutapplication.h"
#include <KisPortingUtils.h>

class Q_DECL_HIDDEN WdgAboutApplication : public QWidget, public Ui::WdgAboutApplication
{
public:
    WdgAboutApplication(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setupUi(this);
    }
};

KisAboutApplication::KisAboutApplication(QWidget *parent)
    : KoDialog(parent)
{
    // IKRIATE-REBRAND: skip (this dialog credits the Krita project by name)
    setWindowTitle(i18n("About IKriate"));
    setButtons(KoDialog::Close);

    WdgAboutApplication *wdgTab = new WdgAboutApplication(this);

    KisSplashScreen *splash = new KisSplashScreen(true);
    splash->setWindowFlags(Qt::Widget);
    splash->displayLinks(true);

    wdgTab->aboutTab->layout()->addWidget(splash);

    QString authors = i18n("<html>"
                          "<head/>"
                          "<body>"
                          "<h1 align=\"center\">Created By</h1></p>"
                          "<p>IKriate is an independent, unofficial derivative of Krita and is not "
                          "affiliated with or endorsed by the Krita Foundation. It is built on the work of "
                          "the Krita developers:</p>"
                          "<p>");

    QFile fileDevelopers(":/developers.txt");
    Q_ASSERT(fileDevelopers.exists());
    if (fileDevelopers.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream developersText(&fileDevelopers);
        KisPortingUtils::setUtf8OnStream(developersText);
        authors.append(developersText.readAll().split("\n", Qt::SkipEmptyParts).join(", "));
    }
    authors.append(".</p></body></html>");
    wdgTab->lblAuthors->setText(authors);

    // Translators
    // TODO: move to KisApplication after string freeze is lifted
    KAboutData aboutData(KAboutData::applicationData());
    if (aboutData.translators().isEmpty()) {
        aboutData.setTranslator(i18nc("NAME OF TRANSLATORS", "Your names"),
                                i18nc("EMAIL OF TRANSLATORS", "Your emails"));

    }

    QString translatorHtml = i18n(
        "<html>"
        "<head/>"
        "<body>"
        "<h1 align=\"center\"><b>Translators</b></h1>"
        "<p><ul>");

    Q_FOREACH (const KAboutPerson &person, aboutData.translators()) {
        translatorHtml.append(QString("<li>%1</li>").arg(person.name()));
    }

    translatorHtml.append("<ul></p>");
    translatorHtml.append(
        i18n("<p>KDE is translated into many languages thanks to the work of the "
             "translation teams all over the world.</p><p>For more information on KDE "
             "internationalization visit <a href=\"http://l10n.kde.org\">http://l10n."
             "kde.org</a></p>"));
    translatorHtml.append("</body></html>");

    wdgTab->lblTranslators->setText(translatorHtml);

    // IKriate has no sponsors of its own; Krita's sponsors supported Krita,
    // not this derivative, so their logos are not shown. Credit the origin instead.
    QString origin = i18n(
        "<html><head/><body>"
        "<h1 align=\"center\">Based on Krita</h1>"
        "<p>IKriate is a modified version of Krita, the free and open source painting "
        "application developed by the Krita community and the Krita Foundation.</p>"
        "<p>IKriate is an independent project. It is not affiliated with, sponsored by or "
        "endorsed by the Krita Foundation, and \"Krita\" is used here only to identify the "
        "software IKriate is derived from.</p>"
        "<p>Please report problems with IKriate to the IKriate project, not to the Krita developers: "
        "<a href=\"https://github.com/DR-KD1997/IKriate/issues\">github.com/DR-KD1997/IKriate</a></p>"
        "<p>To learn about the original application, visit "
        "<a href=\"https://krita.org\">krita.org</a>.</p>"
        "</body></html>");
    wdgTab->lblKickstarter->setText(origin);
    wdgTab->tabWidget->setTabText(wdgTab->tabWidget->indexOf(wdgTab->lblKickstarter->parentWidget()),
                                  i18nc("About dialog tab", "Origin"));

    QString credits = i18n("<html>"
                          "<head/>"
                          "<body>"
                          "<h1 align=\"center\">Thanks To</h1>"
                          "<p>");

    QFile fileCredits(":/credits.txt");
    Q_ASSERT(fileCredits.exists());
    if (fileCredits.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream creditsText(&fileCredits);
        KisPortingUtils::setUtf8OnStream(creditsText);

        Q_FOREACH (const QString &credit, creditsText.readAll().split('\n', Qt::SkipEmptyParts)) {

            if (credit.contains(":")) {
                QList<QString> creditSplit = credit.split(':');
                credits.append(creditSplit.at(0));
                credits.append(" (<i>" + creditSplit.at(1) + "</i>)");
                credits.append(", ");
            }
        }
        credits.chop(2);
    }
    credits.append(i18n(".</p><p><i>For supporting the development of Krita, which IKriate is based on, with advice, icons, brush sets and more.</i></p></body></html>"));

    wdgTab->lblCredits->setText(credits);

    QString license = i18n("<html>"
                           "<head/>"
                           "<body>"
                           "<h1 align=\"center\"><b>Your Rights</b></h1>"
                           "<p>IKriate is released under the GNU General Public License (version 3 or any later version), "
                           "like Krita, the application it is based on.</p>"
                           "<p>This license grants people a number of freedoms:</p>"
                           "<ul>"
                           "<li>You are free to use IKriate, for any purpose</li>"
                           "<li>You are free to distribute IKriate</li>"
                           "<li>You can study how IKriate works and change it</li>"
                           "<li>You can distribute changed versions of IKriate</li>"
                           "</ul>"
                           "<p>The source code of this version is available at "
                           "<a href=\"https://github.com/DR-KD1997/IKriate\">github.com/DR-KD1997/IKriate</a>.</p>"
                           "<h1 align=\"center\">Your artwork</h1>"
                           "<p>What you create with IKriate is your sole property. All your artwork is free for you to use as you like.</p>"
                           "<p>That means that IKriate can be used commercially, for any purpose. There are no restrictions whatsoever.</p>"
                           "<p>The GNU GPL license guarantees you this freedom. Nobody is ever permitted to take it away, in contrast "
                           "to trial or educational versions of commercial software that will forbid your work in commercial situations.</p>"
                           "<br/><hr/><pre>");

    QFile licenseFile(":/LICENSE");
    Q_ASSERT(licenseFile.exists());
    if (licenseFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream licenseText(&licenseFile);
        KisPortingUtils::setUtf8OnStream(licenseText);
        license.append(licenseText.readAll());
    }
    license.append("</pre></body></html>");
    wdgTab->lblLicense->setText(license);

    QFile thirdPartyFile(":/libraries.txt");
    if (thirdPartyFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream thirdPartyText(&thirdPartyFile);
        KisPortingUtils::setUtf8OnStream(thirdPartyText);

        QString thirdPartyHtml = i18n("<html>"
                                      "<head/>"
                                      "<body>"
                                      "<h1 align=\"center\"><b>Third-party Libraries used by IKriate</b></h1>"
                                      "<p>IKriate is built on the following free software libraries:</p><p><ul>");

        Q_FOREACH (const QString &lib, thirdPartyText.readAll().split('\n', Qt::SkipEmptyParts)) {

            if (!lib.startsWith("#")) {
                QStringList parts = lib.split(',');
                if (parts.size() >= 3) {
                    thirdPartyHtml.append(QString("<li><a href=\"%2\">%1</a>: %3</li>").arg(parts[0], parts[1], parts[2]));
                }
            }
        }
        thirdPartyHtml.append("<ul></p></body></html>");
        wdgTab->lblThirdParty->setText(thirdPartyHtml);
    }

    setMainWidget(wdgTab);
    setMinimumSize(sizeHint());
    Q_ASSERT(layout());
    layout()->setSizeConstraint(QLayout::SetFixedSize);
}
