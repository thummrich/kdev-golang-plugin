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

#include "debugjob.h"
#include "debuggerplugin.h"
#include <interfaces/ilaunchconfiguration.h>
#include <util/environmentgrouplist.h>
#include <interfaces/icore.h>
#include <interfaces/iplugincontroller.h>
#include <outputview/outputmodel.h>
#include <execute/iexecuteplugin.h>
#include "debugsession.h"

using namespace GDBDebugger;
using namespace KDevelop;

DebugJob::DebugJob( GDBDebugger::CppDebuggerPlugin* p, KDevelop::ILaunchConfiguration* launchcfg, QObject* parent) 
    : KDevelop::OutputJob(parent), m_launchcfg( launchcfg )
{
    setCapabilities(Killable);

    m_session = p->createSession();
    connect(m_session, SIGNAL(applicationStandardOutputLines(QStringList)), SLOT(stderrReceived(QStringList)));
    connect(m_session, SIGNAL(applicationStandardErrorLines(QStringList)), SLOT(stdoutReceived(QStringList)));
    connect(m_session, SIGNAL(finished()), SLOT(done()) );
    
    setObjectName(launchcfg->name());
}

void DebugJob::start()
{
    KConfigGroup grp = m_launchcfg->config();
    KDevelop::EnvironmentGroupList l(KGlobal::config());
    IExecutePlugin* iface = KDevelop::ICore::self()->pluginController()->pluginForExtension("org.kdevelop.IExecutePlugin")->extension<IExecutePlugin>();
    Q_ASSERT(iface);
    QString err;
    QString executable = iface->executable( m_launchcfg, err ).toLocalFile();
    QString envgrp = iface->environmentGroup( m_launchcfg );
    
    if( !err.isEmpty() )
    {
        setError( -1 );
        setErrorText( err );
        emitResult();
        return;
    }
    
    if( envgrp.isEmpty() )
    {
        kWarning() << i18n("No environment group specified, looks like a broken "
        "configuration, please check run configuration '%1'. "
        "Using default environment group.", m_launchcfg->name() );
        envgrp = l.defaultGroup();
    }
    
    QStringList arguments = iface->arguments( m_launchcfg, err );
    if( !err.isEmpty() )
    {
        setError( -1 );
        setErrorText( err );
    }
    if( error() != 0 )
    {
        emitResult();
        return;
    }
    
    setStandardToolView(KDevelop::IOutputView::DebugView);
    setBehaviours(KDevelop::IOutputView::AllowUserClose | KDevelop::IOutputView::AutoScroll);
    setModel( new KDevelop::OutputModel(), KDevelop::IOutputView::TakeOwnership );
    setTitle(m_launchcfg->name());
    
    startOutput();
    
    m_session->startProgram( m_launchcfg );
}

bool DebugJob::doKill()
{
    kDebug();
    m_session->stopDebugger();
    return true;
}

void DebugJob::stderrReceived(const QStringList& l )
{
    if (KDevelop::OutputModel* m = model()) {
        m->appendLines( l );
    }
}

void DebugJob::stdoutReceived(const QStringList& l )
{
    if (KDevelop::OutputModel* m = model()) {
        m->appendLines( l );
    }
}

KDevelop::OutputModel* DebugJob::model()
{
    return dynamic_cast<KDevelop::OutputModel*>( KDevelop::OutputJob::model() );
}


void DebugJob::done()
{
    emitResult();
}


KillSessionJob::KillSessionJob(DebugSession *session, QObject* parent): m_session(session), KJob(parent)
{
    connect(m_session, SIGNAL(finished()), SLOT(sessionFinished()));
    setCapabilities(Killable);
}

void KillSessionJob::start()
{
    //NOOP
}

bool KillSessionJob::doKill()
{
    m_session->stopDebugger();
    return true;
}

void KillSessionJob::sessionFinished()
{
    emitResult();
}
