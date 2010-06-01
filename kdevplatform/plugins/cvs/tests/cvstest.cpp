/***************************************************************************
 *   Copyright 2007 Robert Gruber <rgruber@users.sourceforge.net>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "cvstest.h"

#include <qtest_kde.h>
#include <QtTest/QtTest>

#include <KUrl>
#include <kio/netaccess.h>

#include <cvsjob.h>
#include <cvsproxy.h>

#define CVSTEST_BASEDIR         "/tmp/kdevcvs_testdir/"
#define CVS_REPO                CVSTEST_BASEDIR"repo/"
#define CVS_IMPORT              CVSTEST_BASEDIR"import/"
#define CVS_TESTFILE_NAME       "testfile"
#define CVS_CHECKOUT            CVSTEST_BASEDIR"working/"

void CvsTest::initTestCase()
{
    m_proxy = new CvsProxy;

    // If the basedir for this cvs test exists from a 
    // previous run; remove it...
    if ( QFileInfo(CVSTEST_BASEDIR).exists() )
        KIO::NetAccess::del(KUrl(QString(CVSTEST_BASEDIR)), 0);

    // Now create the basic directory structure
    QDir tmpdir("/tmp");
    tmpdir.mkdir(CVSTEST_BASEDIR);
    tmpdir.mkdir(CVS_REPO);
    tmpdir.mkdir(CVS_IMPORT);
}

void CvsTest::cleanupTestCase()
{
    delete m_proxy;

    if ( QFileInfo(CVSTEST_BASEDIR).exists() )
        KIO::NetAccess::del(KUrl(QString(CVSTEST_BASEDIR)), 0);
}

void CvsTest::repoInit()
{
    // make job that creates the local repository
    CvsJob* j = new CvsJob(0);
    QVERIFY( j );
    j->setDirectory(CVSTEST_BASEDIR);
    *j << "cvs" << "-d" << CVS_REPO << "init";

    // try to start the job
    QVERIFY( j->exec() );

    //check if the CVSROOT directory in the new local repository exists now
    QVERIFY( QFileInfo(QString(CVS_REPO"/CVSROOT")).exists() );
}

void CvsTest::importTestData()
{
    // create a file so we don't import an empty dir
    QFile f(CVS_IMPORT""CVS_TESTFILE_NAME);
    if(f.open(QIODevice::WriteOnly)) {
        QTextStream input( &f );
        input << "HELLO WORLD";
    }
    f.flush();


    CvsJob* j = m_proxy->import(KUrl(CVS_IMPORT), CVS_REPO, 
                        "test", "vendor", "release", 
                        "test import message");
    QVERIFY( j );

    // try to start the job
    QVERIFY( j->exec() );

    //check if the directory has been added to the repository
    QString testdir(CVS_REPO"/test");
    QVERIFY( QFileInfo(testdir).exists() );

    //check if the file has been added to the repository
    QString testfile(CVS_REPO"/test/"CVS_TESTFILE_NAME",v");
    QVERIFY( QFileInfo(testfile).exists() );
}


void CvsTest::checkoutTestData()
{
    CvsJob* j = m_proxy->checkout(KUrl(CVS_CHECKOUT), CVS_REPO, "test");
    QVERIFY( j );

    // try to start the job
    QVERIFY( j->exec() );

    //check if the directory is there
    QString testdir(CVS_CHECKOUT);
    QVERIFY( QFileInfo(testdir).exists() );

    //check if the file is there
    QString testfile(CVS_CHECKOUT""CVS_TESTFILE_NAME);
    QVERIFY( QFileInfo(testfile).exists() );
}


void CvsTest::testInitAndImport()
{
    repoInit();
    importTestData();
    checkoutTestData();
}

QTEST_KDEMAIN(CvsTest, GUI)


#include "cvstest.moc"
