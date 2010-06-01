/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2007 Andreas Pakulat <apaku@gmx.de>                        *
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

#include "svncheckoutjob.h"
#include "svncheckoutjob_p.h"

#include <QMutexLocker>

#include <klocale.h>
#include <kdebug.h>
#include <ThreadWeaver.h>

#include "kdevsvncpp/client.hpp"
#include "kdevsvncpp/path.hpp"

#include <vcs/vcslocation.h>
#include <QFileInfo>

SvnInternalCheckoutJob::SvnInternalCheckoutJob( SvnJobBase* parent )
    : SvnInternalJobBase( parent )
{
}

bool SvnInternalCheckoutJob::isValid() const
{
    QMutexLocker l( m_mutex );
    return m_sourceRepository.isValid() && m_destinationDirectory.isLocalFile() && QFileInfo(m_destinationDirectory.upUrl().toLocalFile()).exists();
}

void SvnInternalCheckoutJob::run()
{
    initBeforeRun();

    svn::Client cli(m_ctxt);
    try
    {
        bool recurse = ( recursion() == KDevelop::IBasicVersionControl::Recursive );
        KUrl desturl = KUrl( source().repositoryServer() );
        desturl.cleanPath(KUrl::SimplifyDirSeparators);
        QByteArray srcba = desturl.url( KUrl::RemoveTrailingSlash ).toUtf8();
        KUrl destdir = KUrl(QFileInfo( destination().upUrl().toLocalFile() ).canonicalFilePath());
        destdir.addPath( destination().fileName() );
        QByteArray destba = destdir.toLocalFile().toUtf8();
        kDebug(9510) << srcba << destba << recurse;
        cli.checkout( srcba.data(), svn::Path( destba.data() ), svn::Revision::HEAD, recurse );
    }catch( svn::ClientException ce )
    {
        kDebug(9510) << "Exception while checking out: "
                << source().repositoryServer()
                << QString::fromUtf8( ce.message() );
        setErrorMessage( QString::fromUtf8( ce.message() ) );
        m_success = false;
    }
}


void SvnInternalCheckoutJob::setMapping( const KDevelop::VcsLocation & sourceRepository, const KUrl & destinationDirectory, KDevelop::IBasicVersionControl::RecursionMode recursion )
{
    QMutexLocker l( m_mutex );
    m_sourceRepository = sourceRepository;
    m_destinationDirectory = destinationDirectory;
    m_recursion = recursion;
}

KDevelop::VcsLocation SvnInternalCheckoutJob::source() const
{
    QMutexLocker l( m_mutex );
    return m_sourceRepository;
}

KDevelop::IBasicVersionControl::RecursionMode SvnInternalCheckoutJob::recursion() const
{
    QMutexLocker l( m_mutex );
    return m_recursion;
}

KUrl SvnInternalCheckoutJob::destination() const
{
    QMutexLocker l( m_mutex );
    return m_destinationDirectory;
}

SvnCheckoutJob::SvnCheckoutJob( KDevSvnPlugin* parent )
    : SvnJobBase( parent, KDevelop::OutputJob::Silent )
{
    setType( KDevelop::VcsJob::Import );
    m_job = new SvnInternalCheckoutJob( this );
    setObjectName(i18n("Subversion Checkout"));
}

QVariant SvnCheckoutJob::fetchResults()
{
    return QVariant();
}

void SvnCheckoutJob::start()
{
    if (!m_job->isValid() ) {
        internalJobFailed( m_job );
        setErrorText( i18n( "Not enough information to checkout" ) );
    } else {
        kDebug(9510) << "checking out: " << m_job->source().repositoryServer();
        ThreadWeaver::Weaver::instance()->enqueue( m_job );
    }
}

SvnInternalJobBase* SvnCheckoutJob::internalJob() const
{
    return m_job;
}

void SvnCheckoutJob::setMapping( const KDevelop::VcsLocation & sourceRepository, const KUrl & destinationDirectory, KDevelop::IBasicVersionControl::RecursionMode recursion )
{
    if( status() == KDevelop::VcsJob::JobNotStarted ) {
        m_job->setMapping(sourceRepository, destinationDirectory, recursion);
    }
}


#include "svncheckoutjob.moc"
#include "svncheckoutjob_p.moc"
