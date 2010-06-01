/*
* GDB Debugger Support
*
* Copyright 2006 Vladimir Prus <ghost@cs.msu.su>
* Copyright 2007 Hamish Rodda <rodda@kde.org>
* Copyright 2009 Andreas Pakulat <apaku@gmx.de>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public
* License along with this program; if not, write to the
* Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/


#include "gdblaunchconfig.h"

#include <kconfiggroup.h>
#include <kicon.h>
#include <klocale.h>
#include <kshell.h>
#include <kmessagebox.h>
#include <kparts/mainwindow.h>

#include <outputview/outputmodel.h>
#include <interfaces/ilaunchconfiguration.h>
#include <util/environmentgrouplist.h>
#include <interfaces/iproject.h>
#include <project/interfaces/iprojectbuilder.h>
#include <project/builderjob.h>
#include <interfaces/iuicontroller.h>
#include <project/interfaces/ibuildsystemmanager.h>
#include <util/executecompositejob.h>
#include <execute/iexecuteplugin.h>

#include "debugsession.h"
#include "debuggerplugin.h"

#include "ui_debuggerconfigwidget.h"
#include <interfaces/iplugincontroller.h>
#include <interfaces/icore.h>
#include "debugjob.h"

using namespace KDevelop;

GdbConfigPage::GdbConfigPage( QWidget* parent )
    : LaunchConfigurationPage(parent), ui( new Ui::DebuggerConfigWidget )
{
    ui->setupUi( this );
    ui->kcfg_gdbPath->setMode(KFile::File|KFile::ExistingOnly|KFile::LocalOnly);
    connect(ui->kcfg_asmDemangle, SIGNAL(toggled(bool)), this, SIGNAL(changed()));
    connect(ui->kcfg_breakOnLoadingLibrary, SIGNAL(toggled(bool)), this, SIGNAL(changed()));
    connect(ui->kcfg_configGdbScript, SIGNAL(textChanged(QString)), this, SIGNAL(changed()));
    //connect(ui->kcfg_dbgTerminal, SIGNAL(toggled(bool)), this, SIGNAL(changed()));
    connect(ui->kcfg_debuggingShell, SIGNAL(textChanged(QString)), this, SIGNAL(changed()));
    connect(ui->kcfg_displayStaticMembers, SIGNAL(toggled(bool)), this, SIGNAL(changed()));
    connect(ui->kcfg_gdbPath, SIGNAL(textChanged(QString)), this, SIGNAL(changed()));
    connect(ui->kcfg_runGdbScript, SIGNAL(textChanged(QString)), this, SIGNAL(changed()));
    connect(ui->kcfg_runShellScript, SIGNAL(textChanged(QString)), this, SIGNAL(changed()));
}

KIcon GdbConfigPage::icon() const
{
    return KIcon();
}

void GdbConfigPage::loadFromConfiguration( const KConfigGroup& cfg, KDevelop::IProject*  )
{
    bool block = blockSignals( true );
    ui->kcfg_gdbPath->setUrl( cfg.readEntry( GDBDebugger::gdbPathEntry, KUrl() ) );
    ui->kcfg_debuggingShell->setUrl( cfg.readEntry( GDBDebugger::debuggerShellEntry, KUrl() ) );
    ui->kcfg_configGdbScript->setUrl( cfg.readEntry( GDBDebugger::remoteGdbConfigEntry, KUrl() ) );
    ui->kcfg_runShellScript->setUrl( cfg.readEntry( GDBDebugger::remoteGdbShellEntry, KUrl() ) );
    ui->kcfg_runGdbScript->setUrl( cfg.readEntry( GDBDebugger::remoteGdbRunEntry, KUrl() ) );
    ui->kcfg_displayStaticMembers->setChecked( cfg.readEntry(GDBDebugger::staticMembersEntry, false) );
    ui->kcfg_asmDemangle->setChecked( cfg.readEntry( GDBDebugger::demangleNamesEntry, true) );
    //TODO: add ui for this
    //ui->kcfg_allowForceBP->setChecked( cfg.readEtnry( GDBDebugger::allowForcedBPEntry, true ) );
    ui->kcfg_breakOnLoadingLibrary->setChecked( cfg.readEntry( GDBDebugger::breakOnLibLoadEntry, true) );
    //ui->kcfg_dbgTerminal->setChecked( cfg.readEntry( GDBDebugger::separateTerminalEntry, false) );
    blockSignals( block );
}

void GdbConfigPage::saveToConfiguration( KConfigGroup cfg, KDevelop::IProject* ) const
{
    cfg.writeEntry(GDBDebugger::gdbPathEntry, ui->kcfg_gdbPath->url() );
    cfg.writeEntry(GDBDebugger::debuggerShellEntry, ui->kcfg_debuggingShell->url() );
    cfg.writeEntry(GDBDebugger::remoteGdbConfigEntry, ui->kcfg_configGdbScript->url() );
    cfg.writeEntry(GDBDebugger::remoteGdbShellEntry, ui->kcfg_runShellScript->url() );
    cfg.writeEntry(GDBDebugger::remoteGdbRunEntry, ui->kcfg_runGdbScript->url() );
    cfg.writeEntry(GDBDebugger::staticMembersEntry, ui->kcfg_displayStaticMembers->isChecked() );
    cfg.writeEntry(GDBDebugger::demangleNamesEntry, ui->kcfg_asmDemangle->isChecked() );
    cfg.writeEntry(GDBDebugger::breakOnLibLoadEntry, ui->kcfg_breakOnLoadingLibrary->isChecked() );
    //cfg.writeEntry(GDBDebugger::separateTerminalEntry, ui->kcfg_dbgTerminal->isChecked() );
}

QString GdbConfigPage::title() const
{
    return i18n( "GDB Configuration" );
}


GdbLauncher::GdbLauncher( GDBDebugger::CppDebuggerPlugin* p ) : m_plugin( p )
{
    factoryList << new GdbConfigPageFactory();
}

QList< KDevelop::LaunchConfigurationPageFactory* > GdbLauncher::configPages() const
{
    return factoryList;
}

QString GdbLauncher::id()
{
    return "gdb";
}

QString GdbLauncher::name() const
{
    return i18n("GDB");
}

KJob* GdbLauncher::start(const QString& launchMode, KDevelop::ILaunchConfiguration* cfg)
{
    Q_ASSERT(cfg);
    if( !cfg )
    {
        return 0;
    }
    if( launchMode == "debug" )
    {
        IExecutePlugin* iface = KDevelop::ICore::self()->pluginController()->pluginForExtension("org.kdevelop.IExecutePlugin")->extension<IExecutePlugin>();
        Q_ASSERT(iface);
        QList<KJob*> l;
        KJob* depjob = iface->dependecyJob(cfg);
        if( depjob ) 
        {
            l << depjob;
        }
        l << new GDBDebugger::DebugJob( m_plugin, cfg );
        return new KDevelop::ExecuteCompositeJob( KDevelop::ICore::self()->runController(), l );
    }
    kWarning() << "Unknown launch mode" << launchMode << "for config:" << cfg->name();
    return 0;
}

QStringList GdbLauncher::supportedModes() const
{
    return QStringList() << "debug";
}

QString GdbLauncher::description() const
{
    return i18n("Executes a Native application in GDB");
}

KDevelop::LaunchConfigurationPage* GdbConfigPageFactory::createWidget( QWidget* parent )
{
    return new GdbConfigPage( parent );
}



#include "gdblaunchconfig.moc"
