/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2007 Andreas Pakulat <apaku@gmx.de>                         *
 *   Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>                         *
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

#include "svninternaljobbase.h"

#include <QMutex>
#include <QDateTime>
#include <QCoreApplication>

#include <iostream>

#include <ThreadWeaver.h>
#include <klocale.h>
#include <kdebug.h>

extern "C" {
#include <svn_auth.h>
}

#include <vcs/vcsrevision.h>

#include <svnjobbase.h>
#include "kdevsvncpp/context.hpp"
#include "kdevsvncpp/apr.hpp"
#include "kdevsvncpp/revision.hpp"

SvnInternalJobBase::SvnInternalJobBase( SvnJobBase* parent )
    : ThreadWeaver::Job( parent ), m_ctxt( new svn::Context() ),
      m_guiSemaphore( 0 ), m_mutex( new QMutex() ), m_killMutex( new QMutex() ),
      m_success( true ), sendFirstDelta( false ), killed( false )
{
    m_ctxt->setListener(this);
    connect( this, SIGNAL( failed( ThreadWeaver::Job* ) ),
             parent, SLOT( internalJobFailed( ThreadWeaver::Job* ) ), Qt::QueuedConnection );
    connect( this, SIGNAL( done( ThreadWeaver::Job* ) ),
             parent, SLOT( internalJobDone( ThreadWeaver::Job* ) ), Qt::QueuedConnection );
    connect( this, SIGNAL( started( ThreadWeaver::Job* ) ),
             parent, SLOT( internalJobStarted( ThreadWeaver::Job* ) ), Qt::QueuedConnection );
}

SvnInternalJobBase::~SvnInternalJobBase()
{
    m_ctxt->setListener(0);
    delete m_ctxt;
    m_ctxt = 0;
}

bool SvnInternalJobBase::contextGetLogin( const std::string& realm,
                        std::string& username, std::string& password,
                        bool& maySave )
{

    emit needLogin( QString::fromUtf8( realm.c_str() )  );
    m_guiSemaphore.acquire( 1 );
    QMutexLocker l(m_mutex);
    if( m_login_username.isEmpty() || m_login_password.isEmpty() )
        return false;
    username = std::string( m_login_username.toUtf8() );
    password = std::string( m_login_password.toUtf8() );
    maySave = this->m_maySave;
    return true;
}

void SvnInternalJobBase::contextNotify( const char* path, svn_wc_notify_action_t action,
                    svn_node_kind_t kind, const char* mimetype,
                    svn_wc_notify_state_t contentState,
                    svn_wc_notify_state_t propState, svn_revnum_t rev )
{
    QString notifyString;
    switch( action ){
        case svn_wc_notify_add:
            notifyString = i18nc( "A file was marked to be added to svn", "Added %1", QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_delete:
            notifyString = i18nc( "A file was marked for deletion from svn", "Deleted %1", QString::fromUtf8( path ) );
            break;
        // various update notifications
        case svn_wc_notify_update_delete:
            notifyString = i18nc( "A file was deleted during an svn update operation", "Deleted %1", QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_update_add:
            notifyString = i18nc( "A file was added during an svn update operation", "Added %1", QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_update_update:
         /* If this is an inoperative dir change, do no notification.
            An inoperative dir change is when a directory gets closed
            without any props having been changed. */
            if (! ((kind == svn_node_dir)
                    && ((propState == svn_wc_notify_state_inapplicable)
                    || (propState == svn_wc_notify_state_unknown)
                    || (propState == svn_wc_notify_state_unchanged)))) {

                if (kind == svn_node_file) {
                    if (contentState == svn_wc_notify_state_conflicted)
                        notifyString = "Conflict On File";
                    else if (contentState == svn_wc_notify_state_merged)
                        notifyString = "File Merged";
                    else if (contentState == svn_wc_notify_state_changed)
                        notifyString = "File Updated";
                }

                if (propState == svn_wc_notify_state_conflicted)
                    notifyString += " Conflict On Property";
                else if (propState == svn_wc_notify_state_merged)
                    notifyString += " Properties Merged";
                else if (propState == svn_wc_notify_state_changed)
                    notifyString += " Properties Updated";
                else
                    notifyString += ' ';

                if (! ((contentState == svn_wc_notify_state_unchanged
                        || contentState == svn_wc_notify_state_unknown)
                        && (propState == svn_wc_notify_state_unchanged
                        || propState == svn_wc_notify_state_unknown)))
                    notifyString += QString( " " ) + QString::fromUtf8( path );

            }
            break;

        case svn_wc_notify_update_completed:
            // The last notification in an update (including updates of externals).
            notifyString = i18n("Revision %1", rev );
            break;
        case svn_wc_notify_update_external:
            notifyString = i18n("Updating externals: %1", QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_status_completed:
            break;
        case svn_wc_notify_status_external:
            break;
        // various commit notifications
        case svn_wc_notify_commit_modified:
            notifyString = i18n( "Sending %1", QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_commit_added:
            if( mimetype ){
                notifyString = i18n( "Adding %1 using mimetype %2.", QString::fromUtf8( path ), mimetype );
            } else {
                notifyString = i18n( "Adding %1.", QString::fromUtf8( path ) );
            }
            break;
        case svn_wc_notify_commit_deleted:
            notifyString = i18n( "Deleting %1.", QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_commit_replaced:
            notifyString = i18n( "Replacing %1.", QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_commit_postfix_txdelta:
            if ( sendFirstDelta ) {
                sendFirstDelta = false;
                notifyString=i18n("Transmitting file data ");
            } else {
                notifyString='.';
            }
            break;
        case svn_wc_notify_blame_revision:
            notifyString = i18n( "Blame finished for revision %1, path %2", rev, QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_revert:
            notifyString = i18n( "Reverted working copy %1", QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_failed_revert:
            notifyString = i18n( "Reverting failed on working copy %1", QString::fromUtf8( path ) );
            break;
        case svn_wc_notify_copy:
            notifyString = i18n( "Copied %1", QString::fromUtf8( path ) );
            break;
        default:
            break;
    }
    emit showNotification( QString::fromUtf8( path ), notifyString );
}

bool SvnInternalJobBase::contextCancel()
{
    QMutexLocker lock( m_killMutex );
    return killed;
}

bool SvnInternalJobBase::contextGetLogMessage( std::string& msg )
{
    emit needCommitMessage();
    m_guiSemaphore.acquire( 1 );
    QMutexLocker l( m_mutex );
    QByteArray ba = m_commitMessage.toUtf8();
    msg = std::string( ba.data() );
    return true;
}

void SvnInternalJobBase::initBeforeRun()
{
    connect( this, SIGNAL( needCommitMessage() ),
             parent(), SLOT( askForCommitMessage() ), Qt::QueuedConnection );
    connect( this, SIGNAL( needLogin( const QString& ) ),
             parent(), SLOT( askForLogin( const QString& ) ), Qt::QueuedConnection );
    connect( this, SIGNAL( needSslServerTrust( const QStringList&,
                           const QString&, const QString&, const QString&,
                           const QString&, const QString&, const QString& ) ),
             parent(), SLOT( askForSslServerTrust( const QStringList&,
                             const QString&, const QString&, const QString&,
                             const QString&, const QString&, const QString& ) ), Qt::QueuedConnection );
    connect( this, SIGNAL( showNotification( const QString&, const QString& ) ),
             parent(), SLOT( showNotification( const QString&, const QString& ) ), Qt::QueuedConnection );
    connect( this, SIGNAL( needSslClientCert( const QString& ) ),
             parent(), SLOT( askForSslClientCert( const QString& ) ), Qt::QueuedConnection );
    connect( this, SIGNAL( needSslClientCertPassword( const QString& ) ),
             parent(), SLOT( askForSslClientCertPassword( const QString& ) ), Qt::QueuedConnection );
}

svn::ContextListener::SslServerTrustAnswer SvnInternalJobBase::contextSslServerTrustPrompt(
        const svn::ContextListener::SslServerTrustData& data,
        apr_uint32_t& acceptedFailures )
{

    std::string host = data.hostname;
    std::string print = data.fingerprint;
    std::string from = data.validFrom;
    std::string until = data.validUntil;
    std::string issue = data.issuerDName;
    std::string realm = data.realm;
    acceptedFailures = data.failures;
    QStringList failures;
    if( data.failures & SVN_AUTH_SSL_NOTYETVALID )
    {
        failures << i18n("Certificate is not yet valid.");
    }
    if( data.failures & SVN_AUTH_SSL_EXPIRED )
    {
        failures << i18n("Certificate has expired.");
    }
    if( data.failures & SVN_AUTH_SSL_CNMISMATCH )
    {
        failures << i18n("Certificate's CN (hostname) doesn't match the remote hostname.");
    }
    if( data.failures & SVN_AUTH_SSL_UNKNOWNCA )
    {
        failures << i18n("Certificate authority is unknown.");
    }
    if( data.failures & SVN_AUTH_SSL_NOTYETVALID )
    {
        failures << i18n("Other unknown error.");
    }
    emit needSslServerTrust( failures,
                                     QString::fromUtf8( host.c_str() ),
                                     QString::fromUtf8( print.c_str() ),
                                     QString::fromUtf8( from.c_str() ),
                                     QString::fromUtf8( until.c_str() ),
                                     QString::fromUtf8( issue.c_str() ),
                                     QString::fromUtf8( realm.c_str() ) );
    m_guiSemaphore.acquire(1);
    QMutexLocker l(m_mutex);
    return m_trustAnswer;
}

bool SvnInternalJobBase::contextSslClientCertPrompt( std::string& cert )
{
    emit needSslClientCert( QString::fromUtf8( cert.c_str() ) );
    m_guiSemaphore.acquire( 1 );
    return true;
}

bool SvnInternalJobBase::contextSslClientCertPwPrompt( std::string& pw, const std::string& realm,
                                    bool& maySave )
{
    Q_UNUSED(pw);
    Q_UNUSED(maySave);
    emit needSslClientCertPassword( QString::fromUtf8( realm.c_str() ) );
    m_guiSemaphore.acquire( 1 );
    return false;
}

bool SvnInternalJobBase::success() const
{
    return m_success;
}

svn::Revision SvnInternalJobBase::createSvnCppRevisionFromVcsRevision( const KDevelop::VcsRevision& revision )
{
    svn::Revision rev;
    QVariant value = revision.revisionValue();
    switch( revision.revisionType() )
    {
        case KDevelop::VcsRevision::Special:
        {
            if( value.canConvert<KDevelop::VcsRevision::RevisionSpecialType>() )
            {
                KDevelop::VcsRevision::RevisionSpecialType specialtype =
                        value.value<KDevelop::VcsRevision::RevisionSpecialType>();
                switch( specialtype )
                {
                    case KDevelop::VcsRevision::Head:
                        rev = svn::Revision( svn::Revision::HEAD );
                        break;
                    case KDevelop::VcsRevision::Working:
                        rev = svn::Revision( svn::Revision::WORKING );
                        break;
                    case KDevelop::VcsRevision::Base:
                        rev = svn::Revision( svn::Revision::BASE );
                        break;
                    case KDevelop::VcsRevision::Previous:
                        rev = svn::Revision( svn_opt_revision_previous );
                        break;
                    case KDevelop::VcsRevision::Start:
                        rev = svn::Revision( svn::Revision::START );
                        break;
                    default:
                        break;
                }
            }
            break;
        }
        case KDevelop::VcsRevision::GlobalNumber:
        case KDevelop::VcsRevision::FileNumber:
        {
            bool ok;
            qlonglong number = value.toLongLong(&ok);
            if( ok )
            {
                rev = svn::Revision( number );
            }
            break;
        }
        case KDevelop::VcsRevision::Date:
        {
            QDateTime dt = value.toDateTime();
            if( dt.isValid() )
            {
                rev = svn::Revision( dt.toTime_t() );
            }
            break;
        }
        default:
            break;
    }
    return rev;
}


void SvnInternalJobBase::setErrorMessage( const QString& msg )
{
    QMutexLocker lock( m_mutex );
    m_errorMessage = msg;
}

QString SvnInternalJobBase::errorMessage() const
{
    QMutexLocker lock( m_mutex );
    return m_errorMessage;
}

void SvnInternalJobBase::kill()
{
    QMutexLocker lock( m_killMutex );
    killed = true;
}



#include "svninternaljobbase.moc"
