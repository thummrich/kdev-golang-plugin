/***************************************************************************
 *   Copyright (C) 2008 by Andreas Pakulat <apaku@gmx.de                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "openprojectdialog.h"
#include "openprojectpage.h"
#include "projectinfopage.h"

#include <QtCore/QFileInfo>
#include <QtCore/QDir>

#include <kpagewidgetmodel.h>
#include <kmessagebox.h>
#include <klocale.h>
#include <KColorScheme>

#include <kio/netaccess.h>
#include <kio/udsentry.h>
#include <kio/job.h>

#include <kdebug.h>

#include "core.h"
#include "uicontroller.h"
#include "mainwindow.h"
#include "shellextension.h"

namespace KDevelop
{

OpenProjectDialog::OpenProjectDialog( const KUrl& startUrl, QWidget* parent )
    : KAssistantDialog( parent )
{
    resize(QSize(700, 500));
    
    QWidget* page = new OpenProjectPage( startUrl, this );
    connect( page, SIGNAL( urlSelected( const KUrl& ) ), this, SLOT( validateOpenUrl( const KUrl& ) ) );
    openPage = addPage( page, "Select Directory/Project File" );
    page = new ProjectInfoPage( this );
    connect( page, SIGNAL( projectNameChanged( const QString& ) ), this, SLOT( validateProjectName( const QString& ) ) );
    connect( page, SIGNAL( projectManagerChanged( const QString& ) ), this, SLOT( validateProjectManager( const QString& ) ) );
    projectInfoPage = addPage( page, "Project Information" );
    setValid( openPage, false );
    setValid( projectInfoPage, false);
    setAppropriate( projectInfoPage, false );
    showButton( KDialog::Help, false );
}

void OpenProjectDialog::validateOpenUrl( const KUrl& url )
{
    bool isDir = false;
    QString extension;
    bool isValid = false;

    if( url.isLocalFile() )
    {
        QFileInfo info( url.toLocalFile() );
        isValid = info.exists();
        if ( isValid ) {
            isDir = QFileInfo( url.toLocalFile() ).isDir();
            extension = QFileInfo( url.toLocalFile() ).suffix();
        }
    } else 
    {
        KIO::UDSEntry entry;
        isValid = KIO::NetAccess::stat( url, entry, Core::self()->uiControllerInternal()->defaultMainWindow() );
        if ( isValid ) {
            isDir = entry.isDir();
            extension = QFileInfo( entry.stringValue( KIO::UDSEntry::UDS_NAME ) ).suffix();
        }
    }

    if ( isValid ) {
        // reset header
        openPage->setHeader(QString());
    } else {
        // report error
        KColorScheme scheme(palette().currentColorGroup());
        const QString errorMsg = i18n("Selected URL is invalid.");
        QString msgTemplate;
        if ( layoutDirection() == Qt::LeftToRight ) {
            msgTemplate = QString(
                "<table width='100%' cellpadding='0' cellspacing='0'>"
                    "<tr>"
                        "<td align='left'>%1</td>"
                        "<td align='right'><font color='%3'>%2</font></td>"
                    "</tr>"
                "</table>"
            );
        } else {
            msgTemplate = QString(
                "<table width='100%' cellpadding='0' cellspacing='0'>"
                    "<tr>"
                        "<td align='left'><font color='%3'>%2</font></td>"
                        "<td align='right'>%1</td>"
                    "</tr>"
                "</table>"
            );
        }
        openPage->setHeader(
            msgTemplate
                .arg(openPage->name())
                .arg(errorMsg)
                .arg(scheme.foreground(KColorScheme::NegativeText).color().name())
        );
        setAppropriate( projectInfoPage, false );
        setAppropriate( openPage, true );
        setValid( openPage, false );
        return;
    }

    if( isDir || extension != ShellExtension::getInstance()->projectFileExtension() ) 
    {
        setAppropriate( projectInfoPage, true );
        m_url = url;
        if( !isDir )
        {
             m_url = m_url.upUrl();
        }
        ProjectInfoPage* page = qobject_cast<ProjectInfoPage*>( projectInfoPage->widget() );
        if( page )
        {
            page->setProjectName( m_url.fileName() );
            OpenProjectPage* page2 = qobject_cast<OpenProjectPage*>( openPage->widget() );
            if( page2 )
            {
                // Default manager
                page->setProjectManager( "Generic Project Manager" );
                // clear the filelist
                m_fileList.clear();

                if( isDir ) {
                    // If a dir was selected fetch all files in it
                    KIO::ListJob* job = KIO::listDir( m_url );
                    connect( job, SIGNAL(entries(KIO::Job*, const KIO::UDSEntryList&)), 
                                  SLOT(storeFileList(KIO::Job*, const KIO::UDSEntryList&)));
                    KIO::NetAccess::synchronousRun( job, Core::self()->uiController()->activeMainWindow() );
                } else {
                    // Else we'lll just take the given file
                    m_fileList << url.fileName();
                }
                // Now find a manager for the file(s) in our filelist.
                bool managerFound = false;
                foreach( const QString& manager, page2->projectFilters().keys() )
                {
                    foreach( const QString& filterexp, page2->projectFilters()[manager] )
                    {
                        if( !m_fileList.filter( QRegExp( filterexp, Qt::CaseSensitive, QRegExp::Wildcard ) ).isEmpty() ) 
                        {
                            managerFound = true;
                            break;
                        }
                    }
                    if( managerFound )
                    {
                        page->setProjectManager( manager );
                        break;
                    }
                }
            }
        }
        m_url.addPath( m_url.fileName()+'.'+ShellExtension::getInstance()->projectFileExtension() );
    } else
    {
        setAppropriate( projectInfoPage, false );
        m_url = url;
    }
    validateProjectInfo();
    setValid( openPage, true );
}

void OpenProjectDialog::validateProjectName( const QString& name )
{
    m_projectName = name;
    validateProjectInfo();
}

void OpenProjectDialog::validateProjectInfo()
{
    setValid( projectInfoPage, (!projectName().isEmpty() && !projectManager().isEmpty()) );
}

void OpenProjectDialog::validateProjectManager( const QString& manager )
{
    m_projectManager = manager;
    validateProjectInfo();
}

KUrl OpenProjectDialog::projectFileUrl()
{
    return m_url;
}

QString OpenProjectDialog::projectName()
{
    return m_projectName;
}

QString OpenProjectDialog::projectManager()
{
    return m_projectManager;
}

void OpenProjectDialog::storeFileList(KIO::Job*, const KIO::UDSEntryList& list)
{
    foreach( const KIO::UDSEntry& entry, list )
    {
        QString name = entry.stringValue( KIO::UDSEntry::UDS_NAME );
        if( name != "." && name != ".." && !entry.isDir() )
        {
            m_fileList << name;
        }
    }
}

}

#include "openprojectdialog.moc"

