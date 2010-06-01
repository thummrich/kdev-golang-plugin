/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2007 Andreas Pakulat <apaku@gmx.de>                         *
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

#include "svncopyjob.h"
#include "svncopyjob_p.h"

#include <QMutexLocker>

#include <klocale.h>
#include <kdebug.h>
#include <ThreadWeaver.h>

#include "kdevsvncpp/client.hpp"
#include "kdevsvncpp/path.hpp"

SvnInternalCopyJob::SvnInternalCopyJob( SvnJobBase* parent )
    : SvnInternalJobBase( parent )
{
}

void SvnInternalCopyJob::run()
{
    initBeforeRun();

    svn::Client cli(m_ctxt);
    try
    {
        QByteArray srcba = sourceLocation().toLocalFile( KUrl::RemoveTrailingSlash ).toUtf8();
        QByteArray dstba = destinationLocation().toLocalFile( KUrl::RemoveTrailingSlash ).toUtf8();
        cli.copy( svn::Path( srcba.data() ), svn::Revision(), svn::Path( dstba.data() ) );
    }catch( svn::ClientException ce )
    {
        kDebug(9510) << "Exception while copying file: "
                << sourceLocation() << "to" << destinationLocation()
                << QString::fromUtf8( ce.message() );
        setErrorMessage( QString::fromUtf8( ce.message() ) );
        m_success = false;
    }
}


void SvnInternalCopyJob::setDestinationLocation( const KUrl& url )
{
    QMutexLocker l( m_mutex );
    m_destinationLocation = url;
}

KUrl SvnInternalCopyJob::destinationLocation() const
{
    QMutexLocker l( m_mutex );
    return m_destinationLocation;
}

void SvnInternalCopyJob::setSourceLocation( const KUrl& url )
{
    QMutexLocker l( m_mutex );
    m_sourceLocation = url;
}

KUrl SvnInternalCopyJob::sourceLocation() const
{
    QMutexLocker l( m_mutex );
    return m_sourceLocation;
}

SvnCopyJob::SvnCopyJob( KDevSvnPlugin* parent )
    : SvnJobBase( parent, KDevelop::OutputJob::Silent )
{
    setType( KDevelop::VcsJob::Copy );
    m_job = new SvnInternalCopyJob( this );
    setObjectName(i18n("Subversion Copy"));
}

QVariant SvnCopyJob::fetchResults()
{
    return QVariant();
}

void SvnCopyJob::start()
{
    if( m_job->sourceLocation().isEmpty() || m_job->destinationLocation().isEmpty() )
    {
        internalJobFailed( m_job );
        setErrorText( i18n( "Not enough information to copy file" ) );
    }else
    {
        kDebug(9510) << "copying url:" << m_job->sourceLocation() << "to url" << m_job->destinationLocation();
        ThreadWeaver::Weaver::instance()->enqueue( m_job );
    }
}

SvnInternalJobBase* SvnCopyJob::internalJob() const
{
    return m_job;
}

void SvnCopyJob::setDestinationLocation( const KUrl& url )
{
    if( status() == KDevelop::VcsJob::JobNotStarted )
        m_job->setDestinationLocation( url );
}

void SvnCopyJob::setSourceLocation( const KUrl& url )
{
    if( status() == KDevelop::VcsJob::JobNotStarted )
        m_job->setSourceLocation( url );
}


#include "svncopyjob.moc"
#include "svncopyjob_p.moc"
