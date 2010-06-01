/***************************************************************************
*   This file is part of KDevelop                                         *
*   Copyright 2009 Fabian Wiesel <fabian.wiesel@googlemail.com>           *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU Library General Public License as       *
*   published by the Free Software Foundation; either version 2 of the    *
*   License, or (at your option) any later version.                       *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU Library General Public     *
*   License along with this program; if not, write to the                 *
*   Free Software Foundation, Inc.,                                       *
*   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
***************************************************************************/

#include "svnrecursiveadd.h"
#include <memory>
#include <QtTest/QtTest>
#include <qtest_kde.h>
#include <KTempDir>
#include <KProcess>
#include <kdebug.h>
#include <kparts/part.h>
#include <kio/netaccess.h>
#include <interfaces/iplugincontroller.h>
#include <tests/autotestshell.h>
#include <tests/testcore.h>
#include <vcs/interfaces/icentralizedversioncontrol.h>
#include <vcs/vcsjob.h>

#define PATHETIC    // A little motivator to make things work right :)
#if defined(PATHETIC)
const QString vcsTestDir0("testdir0");
const QString vcsTestDir1("testdir1");
const QString vcsTest_FileName0("foo");
const QString vcsTest_FileName1("bar");
const QString keywordText("text");
#else
const QString vcsTestDir0("dvcs\t testdir");   // Directory containing whitespaces
const QString vcsTestDir1("--help");           // Starting with hyphen for command-line tools
const QString vcsTest_FileName0("foo\t bar");
const QString vcsTest_FileName1("--help");
const QString keywordText("Author:\nDate:\nCommit:\n------------------------------------------------------------------------\nr999999 | ehrman | 1989-11-09 18:53:00 +0100 (Thu, 09 Nov 1989) | 1 lines\nthe line\n");  // Text containing keywords of the various vcs-programs
#endif

const QString simpleText("It's foo!\n");
const QString simpleAltText("No, foo()! It's bar()!\n");

#define VERBOSE
#if defined(VERBOSE)
#define TRACE(X) kDebug() << X
#else
#define TRACE(X) { line = line; }
#endif

using namespace KDevelop;

void validatingExecJob(VcsJob* j, VcsJob::JobStatus status = VcsJob::JobSucceeded)
{
    QVERIFY(j);
    // Print the commmands in full, for easier bug location
#if 0
    if (QLatin1String(j->metaObject()->className()) == "DVcsJob") {
        kDebug() << "Command: \"" << ((DVcsJob*)j)->getChildproc()->program() << ((DVcsJob*)j)->getChildproc()->workingDirectory();
        kDebug() << "Output: \"" << ((DVcsJob*)j)->output();
    }
#endif

    if (!j->exec()) {
        qDebug() << "ooops, no exec";
        kDebug() << j->errorString();
        // On error, wait for key in order to allow manual state inspection
#if 0
        char c;
        std::cin.read(&c, 1);
#endif
    }

    QCOMPARE(j->status(), status);
}

void verifiedWrite(KUrl const & url, QString const & contents)
{
    QFile f(url.path());
    QVERIFY(f.open(QIODevice::WriteOnly));
    QTextStream filecontents(&f);
    filecontents << contents;
    filecontents.flush();
    f.flush();
}


void fillWorkingDirectory(QString const & dirname)
{
    QDir dir(dirname);
    //we start it after repoInit, so we still have empty dvcs repo
    QVERIFY(dir.mkdir(vcsTestDir0));
    QVERIFY(dir.cd(vcsTestDir0));
    KUrl file0(dir.absoluteFilePath(vcsTest_FileName0));
    QVERIFY(dir.mkdir(vcsTestDir1));
    QVERIFY(dir.cd(vcsTestDir1));
    KUrl file1(dir.absoluteFilePath(vcsTest_FileName1));
    verifiedWrite(file0, simpleText);
    verifiedWrite(file1, keywordText);
}

void SvnRecursiveAdd::test()
{
    KTempDir reposDir;
    KProcess cmd;
    cmd.setWorkingDirectory(reposDir.name());
    cmd << "svnadmin" << "create" << reposDir.name();
    QCOMPARE(cmd.execute(10000), 0);
    AutoTestShell::init();
    std::auto_ptr<TestCore> core(new TestCore());
    core->initialize(Core::Default);
    QList<IPlugin*> plugins = Core::self()->pluginController()->allPluginsForExtension("org.kdevelop.IBasicVersionControl");
    IBasicVersionControl* vcs = NULL;
    foreach(IPlugin* p,  plugins) {
        qDebug() << "checking plugin" << p;    
        ICentralizedVersionControl* icentr = p->extension<ICentralizedVersionControl>();
        if (!icentr)
            continue;
        if (icentr->name() == "Subversion") {
            vcs = icentr;
            break;
        }
    }
    qDebug() << "ok, got vcs" << vcs;
    QVERIFY(vcs);
    VcsLocation reposLoc;
    reposLoc.setRepositoryServer("file://" + reposDir.name());
    KTempDir checkoutDir;
    KUrl checkoutLoc = checkoutDir.name();
    kDebug() << "Checking out from " << reposLoc.repositoryServer() << " to " << checkoutLoc;
    qDebug() << "creating job";
    VcsJob* job = vcs->createWorkingCopy( reposLoc, checkoutLoc );
    validatingExecJob(job);
    qDebug() << "filling wc";
    fillWorkingDirectory(checkoutDir.name());
    KUrl addUrl = checkoutLoc;
    addUrl.addPath( vcsTestDir0 );
    kDebug() << "Recursively adding files at " << addUrl;
    validatingExecJob(vcs->add(KUrl(addUrl), IBasicVersionControl::Recursive));
    kDebug() << "Recursively reverting changes at " << addUrl;
    validatingExecJob(vcs->revert(KUrl(addUrl), IBasicVersionControl::Recursive));
}

QTEST_KDEMAIN(SvnRecursiveAdd, GUI)
