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

#include "plugincontrollertest.h"

#include <QtCore/QProcess>
#include <QtCore/QStringList>
#include <QDBusConnection>
#include <QDBusMessage>

#include <qtest_kde.h>
#include <kprocess.h>
#include <kglobal.h>
#include <kstandarddirs.h>
#include <kdebug.h>

#include <tests/autotestshell.h>

#include "../core.h"
#include "../plugincontroller.h"

using KDevelop::Core;
using KDevelop::PluginController;
using KDevelop::IPlugin;

using QTest::kWaitForSignal;

////////////////////// Fixture ///////////////////////////////////////////////

void PluginControllerTest::initTestCase()
{
    // This is needed so we don't have to install our test plugin, adds kdevplatform/shell/tests and builddir/lib to the KDEDIRS variable
    KGlobal::mainComponent().dirs()->addResourceDir( "module", BUILD_DIR"/lib" );
    KGlobal::mainComponent().dirs()->addResourceDir( "services", KDESRCDIR"/share/kde4/services" );

    kDebug() << "module dirs:" << KGlobal::mainComponent().dirs()->resourceDirs("module");
    QString kdedirs = "";
    QStringList env = QProcess::systemEnvironment();
    for( int i = 0; i < env.count(); i++ )
    {
        if( env[i].startsWith("KDEDIRS=") )
        {
            kdedirs = env[i].mid(8);
            kDebug() << kdedirs;
            break;
        }
    }
    kdedirs = QString(KDESRCDIR":")+kdedirs;
    KProcess p;
    p.setEnv( "KDEDIRS", kdedirs, true );
    p.setProgram( "kbuildsycoca4" );
    p.execute();

    AutoTestShell::init();
    Core::initialize( Core::NoUi );
    m_core = Core::self();
    m_pluginCtrl = m_core->pluginControllerInternal();
}

void PluginControllerTest::init()
{
}

void PluginControllerTest::cleanup()
{
}

void PluginControllerTest::pluginInfo()
{
    IPlugin* plugin = m_pluginCtrl->loadPlugin( "kdevnonguiinterface" );
    KPluginInfo kpi = m_pluginCtrl->pluginInfo( plugin );
    QCOMPARE( QString( "kdevnonguiinterface" ), kpi.pluginName() );
}

void PluginControllerTest::loadUnloadPlugin()
{
    QSignalSpy spy(m_pluginCtrl, SIGNAL(pluginLoaded(KDevelop::IPlugin*)));
    QSignalSpy spyloading(m_pluginCtrl, SIGNAL(loadingPlugin(const QString&)));
    QVERIFY(spy.isValid());
    QVERIFY(spyloading.isValid());
    m_pluginCtrl->loadPlugin( "kdevnonguiinterface" );
    QVERIFY( m_pluginCtrl->plugin( "kdevnonguiinterface" ) );

    QCOMPARE(spy.size(), 1);
    QCOMPARE(spyloading.size(), 1);

    QList<QVariant> args = spyloading.takeFirst();
    QCOMPARE( args.at(0).toString(), QString( "kdevnonguiinterface" ) );

    QSignalSpy spy2(m_pluginCtrl, SIGNAL(pluginUnloaded(KDevelop::IPlugin*)) );
    QVERIFY(spy2.isValid());
    m_pluginCtrl->unloadPlugin( "kdevnonguiinterface" );
    QVERIFY( !m_pluginCtrl->plugin( "kdevnonguiinterface" ) );

    QCOMPARE(spy2.size(), 1);
}

void PluginControllerTest::loadFromExtension()
{
    IPlugin* plugin = m_pluginCtrl->pluginForExtension( "org.kdevelop.ITestNonGuiInterface" );
    QVERIFY( plugin );
    QCOMPARE( plugin->extensions(), QStringList() << "org.kdevelop.ITestNonGuiInterface" );
}

QTEST_KDEMAIN( PluginControllerTest, GUI)
#include "plugincontrollertest.moc"
