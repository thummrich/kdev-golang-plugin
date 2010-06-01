/* This file is part of KDevelop
    Copyright 2004 Roberto Raggi <roberto@kdevelop.org>
    Copyright 2007 Andreas Pakulat <apaku@gmx.de>
    Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "makebuilder.h"
#include "makeoutputmodel.h"
#include <config.h>

#include <QtCore/QStringList>

#include <project/projectmodel.h>

#include <project/interfaces/ibuildsystemmanager.h>
#include <util/commandexecutor.h>
#include <interfaces/iproject.h>
#include <interfaces/icore.h>
#include <interfaces/iplugincontroller.h>
#include <outputview/ioutputview.h>
#include <QtDesigner/QExtensionFactory>
#include <QtCore/QProcess>
#include <QtGui/QAction>
#include <QtGui/QKeySequence>

#include <kpluginfactory.h>
#include <kaboutdata.h>
#include <kpluginloader.h>
#include <kglobal.h>
#include <klocale.h>
#include <kdebug.h>
#include <kactioncollection.h>
#include <ksharedconfig.h>

#include <util/environmentgrouplist.h>

#include "makeoutputdelegate.h"
#include "makejob.h"
#include <interfaces/iruncontroller.h>

K_PLUGIN_FACTORY(MakeBuilderFactory, registerPlugin<MakeBuilder>(); )
K_EXPORT_PLUGIN(MakeBuilderFactory(KAboutData("kdevmakebuilder","kdevmakebuilder", ki18n("Make Builder"), "0.1", ki18n("Support for building Make projects"), KAboutData::License_GPL)))


MakeBuilder::MakeBuilder(QObject *parent, const QVariantList &)
    : KDevelop::IPlugin(MakeBuilderFactory::componentData(), parent)
    , m_delegate(new MakeOutputDelegate(this))
{
    KDEV_USE_EXTENSION_INTERFACE( KDevelop::IProjectBuilder )
    KDEV_USE_EXTENSION_INTERFACE( IMakeBuilder )
}

MakeBuilder::~MakeBuilder()
{
}

KJob* MakeBuilder::build( KDevelop::ProjectBaseItem *dom )
{
    return runMake( dom, MakeJob::BuildCommand );
}

KJob* MakeBuilder::clean( KDevelop::ProjectBaseItem *dom )
{
    return runMake( dom, MakeJob::CleanCommand, "clean" );
}

KJob* MakeBuilder::install( KDevelop::ProjectBaseItem *dom )
{
    return runMake( dom, MakeJob::InstallCommand, "install" );
}

void MakeBuilder::jobFinished(KJob* job)
{
    MakeJob* mj = dynamic_cast<MakeJob*>(job);

    if( !mj )
        return;
    
    if (mj->error()) 
    {
        emit failed( mj->item() );

    } else 
    {
        switch( mj->commandType() )
        {
            case MakeJob::BuildCommand:
                emit built( mj->item() );
                break;
            case MakeJob::InstallCommand:
                emit installed( mj->item() );
                break;
            case MakeJob::CleanCommand:
                emit cleaned( mj->item() );
                break;
            case MakeJob::CustomTargetCommand:
                emit makeTargetBuilt( mj->item(), mj->customTarget() );
                break;
        }
    }
}

KJob* MakeBuilder::executeMakeTarget(KDevelop::ProjectBaseItem* item,
                                    const QString& targetname )
{
    return runMake( item, MakeJob::CustomTargetCommand, targetname );
}

KJob* MakeBuilder::runMake( KDevelop::ProjectBaseItem* item, MakeJob::CommandType c,  const QString& overrideTarget )
{
    ///Running the same builder twice may result in serious problems, so kill jobs already running on the same project
    foreach(KJob* job, KDevelop::ICore::self()->runController()->currentJobs())
    {
        MakeJob* makeJob = dynamic_cast<MakeJob*>(job);
        if( makeJob && item && makeJob->item() && makeJob->item()->project() == item->project() ) {
            kDebug() << "killing running make job, due to new started build on same project";
            job->kill(KJob::EmitResult);
        }
    }
    
    MakeJob* job = new MakeJob(this, item, c, overrideTarget);
    job->setItem(item);

    connect(job, SIGNAL(finished(KJob*)), this, SLOT(jobFinished(KJob*)));
    return job;
}

MakeOutputDelegate * MakeBuilder::delegate() const
{
    return m_delegate;
}

#include "makebuilder.moc"
