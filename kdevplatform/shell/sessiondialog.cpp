/* This file is part of KDevelop
Copyright 2008 Andreas Pakulat <apaku@gmx.de>

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

#include "sessiondialog.h"
#include "ui_sessiondialog.h"

#include <kcolorscheme.h>
#include <kglobalsettings.h>

#include "core.h"
#include "sessioncontroller.h"
#include "session.h"

namespace KDevelop
{

const QString newSessionName = "New Session";

SessionModel::SessionModel( QObject* parent )
    : QAbstractListModel( parent )
{
}

int SessionModel::rowCount( const QModelIndex& parent ) const
{
    if( parent.isValid() )
        return 0;
    return Core::self()->sessionController()->sessionNames().count();
}

QVariant SessionModel::headerData( int, Qt::Orientation, int ) const
{
    return QVariant();
}

QVariant SessionModel::data( const QModelIndex& idx, int role ) const
{
    if( !idx.isValid() || idx.row() < 0 || idx.row() >= rowCount() 
        || ( role != Qt::DisplayRole && role != Qt::BackgroundRole && role != Qt::EditRole && role != Qt::FontRole ) )
    {
        return QVariant();
    }
    const Session* s = Core::self()->sessionController()->sessions().at( idx.row() );
    if( role == Qt::DisplayRole )
    {
        return s->description();
    } else if( role == Qt::EditRole )
    {
        return s->name();
    } else if( role == Qt::FontRole )
    {
        QFont f = KGlobalSettings::generalFont();
        if( Core::self()->activeSession()->name() == s->name() )
        {
            f.setBold( true );
        }
        return QVariant::fromValue( f );
    } else 
    {
        if( Core::self()->activeSession()->name() == s->name() )
        {
            return KColorScheme( QPalette::Active ).background( KColorScheme::ActiveBackground );
        } else
        {
            return KColorScheme( QPalette::Active ).background( KColorScheme::NormalBackground );
        }
    }
}

Qt::ItemFlags SessionModel::flags( const QModelIndex& idx ) const
{
    if( !idx.isValid() || idx.row() < 0 || idx.row() >= rowCount() )
    {
        return 0;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool SessionModel::setData( const QModelIndex& idx, const QVariant& value, int role )
{
    if( !idx.isValid() || idx.row() < 0 || idx.row() >= rowCount() || role != Qt::EditRole || !value.isValid() || !value.canConvert( QVariant::String ) )
    {
        return false;
    }
    QString sname = Core::self()->sessionController()->sessionNames().at( idx.row() );
    Session* s = Core::self()->sessionController()->session( sname );
    s->setName( value.toString() );
    emit dataChanged( idx, idx );
    return true;
}

void SessionModel::addSession()
{
    beginInsertRows( QModelIndex(), rowCount(), rowCount() );
    Core::self()->sessionController()->createSession( newSessionName );
    endInsertRows();
}

void SessionModel::deleteSessions( const QList<QModelIndex>& indexes )
{
    if( indexes.isEmpty() )
    {
        return;
    }
    QStringList deleteSessions;
    int startRow = rowCount(), endRow = -1;
    foreach( const QModelIndex& idx, indexes )
    {
        QString sname = Core::self()->sessionController()->sessionNames().at( idx.row() );
        if( !idx.isValid() || idx.row() < 0 || idx.row() >= rowCount()
            || Core::self()->sessionController()->session( sname ) == Core::self()->activeSession() )
        {
            continue;
        }
        if( idx.row() < startRow )
            startRow = idx.row();
        if( idx.row() > endRow )
            endRow = idx.row();
        deleteSessions << sname;
    }
    beginRemoveRows( QModelIndex(), startRow, endRow );
    foreach( const QString& sname, deleteSessions )
    {
        Core::self()->sessionController()->deleteSession( sname );
    }
    endInsertRows();
}

void SessionModel::activateSession( const QModelIndex& idx )
{
    if( !idx.isValid() || idx.row() < 0 || idx.row() >= rowCount() )
    {
        return;
    }
    QStringList sessionList = Core::self()->sessionController()->sessionNames();
    QString sname = sessionList.at( idx.row() );
    QString aname = Core::self()->activeSession()->name();
    if( sname == aname )
    {
        return;
    }
    int activerow = sessionList.indexOf( aname );
    Core::self()->sessionController()->loadSession( sname );
    emit dataChanged( index( activerow, 0, QModelIndex() ), index( activerow, 0, QModelIndex() ) );
    emit dataChanged( idx, idx );
}

void SessionModel::cloneSession( const QModelIndex& idx )
{
    if( !idx.isValid() || idx.row() < 0 || idx.row() >= rowCount() )
    {
        return; 
    }

    beginInsertRows( QModelIndex(), rowCount(), rowCount() );

    Core::self()->sessionController()->cloneSession(
            Core::self()->sessionController()->sessions().at( idx.row() )->id()
        );
    endInsertRows();
}

SessionDialog::SessionDialog( QWidget* parent )
    : KDialog( parent ), m_ui( new Ui::SessionDialog ), m_model( new SessionModel( this ) )
{
    setButtons( KDialog::Close );
    setCaption( i18n( "Configure Sessions" ) );
    m_ui->setupUi( mainWidget() );
    m_ui->sessionList->setModel( m_model );
    connect( m_ui->newButton, SIGNAL(clicked()), this, SLOT(createSession()) );
    connect( m_ui->deleteButton, SIGNAL(clicked()), this, SLOT(deleteSession()) );
    connect( m_ui->activateButton, SIGNAL(clicked()), this, SLOT(activateSession()) );
    connect( m_ui->cloneButton, SIGNAL(clicked()), this, SLOT(cloneSession()) );
    connect( m_ui->sessionList->selectionModel(), SIGNAL( selectionChanged( const QItemSelection&, const QItemSelection& ) ), 
             this, SLOT( enableButtons( const QItemSelection&, const QItemSelection& ) ) );
    connect( m_ui->sessionList->selectionModel(), SIGNAL( currentChanged( const QModelIndex&, const QModelIndex& ) ),
             this, SLOT( enableButtons( const QModelIndex&, const QModelIndex& ) ) );
    connect( m_model, SIGNAL( rowsRemoved( const QModelIndex&, int, int ) ),
             this, SLOT( enableButtons() ) );
    enableButtons( m_ui->sessionList->selectionModel()->selection(), QItemSelection() );
    enableButtons();
}

void SessionDialog::enableButtons()
{
    m_ui->activateButton->setEnabled( m_model->rowCount() > 1 );
    m_ui->deleteButton->setEnabled( m_model->rowCount() > 1 );
}

void SessionDialog::enableButtons( const QModelIndex& current, const QModelIndex& previous )
{
    Q_UNUSED( previous );
    if( m_model->data( current ).toString() == Core::self()->activeSession()->name() )
    {
        m_ui->activateButton->setEnabled( false );
        m_ui->deleteButton->setEnabled( false );
    } else 
    {
        m_ui->activateButton->setEnabled( true );
        m_ui->deleteButton->setEnabled( true );
    }
}

void SessionDialog::enableButtons( const QItemSelection& selected, const QItemSelection& )
{
    m_ui->deleteButton->setEnabled( !selected.isEmpty() );
    m_ui->activateButton->setEnabled( !selected.isEmpty() );
    m_ui->cloneButton->setEnabled( !selected.isEmpty() );
    QString activeName = Core::self()->activeSession()->name();
    foreach( const QModelIndex& idx, m_ui->sessionList->selectionModel()->selectedRows() )
    {
        if( m_model->data( idx ).toString() == activeName )
        {
            m_ui->deleteButton->setEnabled( false );
            m_ui->activateButton->setEnabled( false );
            break;
        }
    }
}

void SessionDialog::createSession()
{
    m_model->addSession();
    m_ui->sessionList->edit( m_model->index( m_model->rowCount() - 1, 0, QModelIndex() ) );
    m_ui->deleteButton->setEnabled( true );
    m_ui->activateButton->setEnabled( true );
}

void SessionDialog::deleteSession()
{
    m_model->deleteSessions( m_ui->sessionList->selectionModel()->selectedRows() );
}

void SessionDialog::activateSession()
{
    m_model->activateSession( m_ui->sessionList->selectionModel()->selectedRows().at( 0 ) );
}

void SessionDialog::cloneSession()
{
    m_model->cloneSession( m_ui->sessionList->selectionModel()->selectedRows().at( 0 ) );
    m_ui->sessionList->edit( m_model->index( m_model->rowCount() - 1, 0, QModelIndex() ) );
}

}

