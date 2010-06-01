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

#ifndef VCSITEMEVENTMODEL_H
#define VCSITEMEVENTMODEL_H


#include <QtCore/QAbstractTableModel>
#include "../vcsexport.h"

template <typename T> class QList;


namespace KDevelop
{
class VcsItemEvent;

class KDEVPLATFORMVCS_EXPORT VcsItemEventModel : public QAbstractTableModel
{
Q_OBJECT
public:
    VcsItemEventModel( QObject* parent );
    ~VcsItemEventModel();
    int rowCount( const QModelIndex& parent = QModelIndex() ) const;
    int columnCount( const QModelIndex& parent = QModelIndex() ) const;
    QVariant data( const QModelIndex&, int role = Qt::DisplayRole ) const;
    QVariant headerData( int, Qt::Orientation, int role = Qt::DisplayRole ) const;
    void addItemEvents( const QList<KDevelop::VcsItemEvent>& );
    KDevelop::VcsItemEvent itemEventForIndex( const QModelIndex& ) const;
    void clear();
private:
    class VcsItemEventModelPrivate* const d;
};
}

#endif
