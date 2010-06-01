/***************************************************************************
 *   Copyright 2008 Andreas Pakulat <apaku@gmx.de>                         *
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

#include "sessioncontrollertest.h"

#include <qtest_kde.h>
#include <tests/autotestshell.h>

#include <kglobal.h>
#include <kdebug.h>
#include <kio/netaccess.h>
#include <kconfiggroup.h>

#include "../core.h"
#include "../sessioncontroller.h"
#include "../session.h"
#include "../uicontroller.h"

Q_DECLARE_METATYPE( KDevelop::ISession* )

using KDevelop::SessionController;
using KDevelop::ISession;
using KDevelop::AutoTestShell;
using KDevelop::Session;
using KDevelop::Core;

using QTest::kWaitForSignal;

//////////////////// Helper Functions ////////////////////////////////////////

void verifySessionDir( Session* s, bool exists = true )
{
    QString sessiondir = SessionController::sessionDirectory() + "/" + s->id().toString();
    if( exists ) 
    {
        kDebug() << "checking existing session" << sessiondir;
        QVERIFY( QFileInfo( sessiondir ).exists() );
        QVERIFY( QFileInfo( sessiondir ).isDir() );
        QVERIFY( QFileInfo( sessiondir+"/sessionrc" ).exists() );
        KSharedConfig::Ptr cfg = KSharedConfig::openConfig( sessiondir+"/sessionrc" );
        QCOMPARE( s->name(), cfg->group("").readEntry( Session::cfgSessionNameEntry, "" ) );
    } else {
        kDebug() << "checking not-existing dir: " << sessiondir;
        QVERIFY( !QFileInfo( sessiondir ).exists() );
    }
}

////////////////////// Fixture ///////////////////////////////////////////////

void SessionControllerTest::initTestCase()
{
    AutoTestShell::init();
    Core::initialize( 0, KDevelop::Core::NoUi );
    m_core = Core::self();
    qRegisterMetaType<KDevelop::ISession*>();
}

void SessionControllerTest::init()
{
    m_sessionCtrl = m_core->sessionController();
}

void SessionControllerTest::cleanupTestCase()
{
    foreach( const QString& name, m_sessionCtrl->sessionNames() )
    {
        m_sessionCtrl->deleteSession( name );
    }
    // Need to cleanup this directory manually, because SessionController (rightfully) doesn't
    // allow to delete the active session
    Session* s = static_cast<Session*>( Core::self()->activeSession() );
    KIO::NetAccess::del( SessionController::sessionDirectory() + "/" + s->id().toString(), 0 );
    KGlobal::config()->group( SessionController::cfgSessionGroup() ).deleteEntry( SessionController::cfgActiveSessionEntry() );
    KGlobal::config()->group( SessionController::cfgSessionGroup() ).sync();
}

void SessionControllerTest::createSession_data()
{
    QTest::addColumn<QString>( "sessionName" );
    QTest::newRow("SimpleName") << "TestSession";
    QTest::newRow("NonLetterChars") << "Test%$Session";
    QTest::newRow("NonAsciiChars") << QString::fromUtf8("TöstSession");
}

void SessionControllerTest::createSession()
{
    QFETCH(QString, sessionName);
    int sessionCount = m_sessionCtrl->sessionNames().count();
    Session* s = m_sessionCtrl->createSession( sessionName );
    QVERIFY( m_sessionCtrl->sessionNames().contains( sessionName )  );
    QCOMPARE( sessionCount+1, m_sessionCtrl->sessionNames().count() );
    verifySessionDir( s );
}

void SessionControllerTest::loadSession()
{
    const QString sessionName = "TestSession2";
    m_sessionCtrl->createSession( sessionName );
    ISession* s = m_sessionCtrl->activeSession();
    m_sessionCtrl->loadSession( sessionName );
    QEXPECT_FAIL("", "expecting a changed active session", Continue);
    QCOMPARE( s, m_sessionCtrl->activeSession());
    KConfigGroup grp = KGlobal::config()->group( SessionController::cfgSessionGroup() );
    QCOMPARE( grp.readEntry( SessionController::cfgActiveSessionEntry(), "default" ), sessionName );
}

void SessionControllerTest::renameSession()
{
    const QString sessionName = "TestSession4";
    const QString newSessionName = "TestOtherSession4";
    KDevelop::Session *s = m_sessionCtrl->createSession( sessionName );
    QCOMPARE( sessionName, s->name() );
    verifySessionDir( s );
    QSignalSpy spy(s, SIGNAL(nameChanged(const QString&, const QString&)));
    s->setName( newSessionName );
    QCOMPARE( newSessionName, s->name() );
    
    QCOMPARE( spy.size(), 1 );
    QList<QVariant> arguments = spy.takeFirst();

    QCOMPARE( sessionName, arguments.at(1).toString() );
    QCOMPARE( newSessionName, arguments.at(0).toString() );

    verifySessionDir( s );
}

void SessionControllerTest::canRenameActiveSession()
{
    const QString sessionName = "TestSession5";
    const QString newSessionName = "TestOtherSession5";
    KDevelop::Session *s = m_sessionCtrl->createSession( sessionName );
    QCOMPARE( sessionName, s->name() );
    m_sessionCtrl->loadSession( sessionName );
    QSignalSpy spy(s, SIGNAL(nameChanged(const QString&, const QString&)));
    s->setName( newSessionName );
    QCOMPARE( newSessionName, s->name() );
    
    QCOMPARE( spy.size(), 1 );
    QList<QVariant> arguments = spy.takeFirst();

    QCOMPARE( sessionName, arguments.at(1).toString() );
    QCOMPARE( newSessionName, arguments.at(0).toString() );

    verifySessionDir( s );
}

void SessionControllerTest::deleteSession()
{
    const QString sessionName = "TestSession3";
    int sessionCount = m_sessionCtrl->sessionNames().count();
    Session* s = m_sessionCtrl->createSession( sessionName );
    QCOMPARE( sessionCount+1, m_sessionCtrl->sessionNames().count() );
    verifySessionDir( s );
    QSignalSpy spy(m_sessionCtrl, SIGNAL(sessionDeleted(const QString&)));
    m_sessionCtrl->deleteSession( sessionName );
    QCOMPARE( sessionCount, m_sessionCtrl->sessionNames().count() );

    QCOMPARE(spy.size(), 1);
    QList<QVariant> arguments = spy.takeFirst();

    QString emittedSession = arguments.at(0).toString();
    QCOMPARE( sessionName, emittedSession );

    verifySessionDir( s, false );
}

void SessionControllerTest::cloneSession()
{
    QString sessionName = "CloneableSession";
    QString testgrp = "TestGroup";
    QString testentry = "TestEntry";
    QString testval = "TestValue";
    int sessionCount = m_sessionCtrl->sessionNames().count();
    m_sessionCtrl->createSession( sessionName );
    Session* s = m_sessionCtrl->session( sessionName );
    s->config()->group( testgrp ).writeEntry( testentry, testval );
    s->config()->sync();
    QCOMPARE( sessionCount+1, m_sessionCtrl->sessionNames().count() );
    QVERIFY( m_sessionCtrl->session( sessionName ) );
    
    QString newSession = m_sessionCtrl->cloneSession( sessionName );
    QVERIFY( m_sessionCtrl->session( newSession ) );
    QCOMPARE( sessionCount+2, m_sessionCtrl->sessionNames().count() );
    Session* news = m_sessionCtrl->session( newSession );
    QCOMPARE( testval, news->config()->group( testgrp ).readEntry( testentry, "" ) );
    QCOMPARE( i18n( "Copy of %1", sessionName ), news->name() );

    verifySessionDir( news );


}

void SessionControllerTest::readFromConfig()
{
    ISession* s = Core::self()->activeSession();
    KConfigGroup grp( s->config(), "TestGroup" );
    grp.writeEntry( "TestEntry", "Test1" );
    KConfigGroup grp2( s->config(), "TestGroup" );
    QCOMPARE(grp.readEntry( "TestEntry", "" ), QString( "Test1" ) );
}

QTEST_KDEMAIN( SessionControllerTest, GUI)
#include "sessioncontrollertest.moc"
