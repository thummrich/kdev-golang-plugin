/***************************************************************************
 *   Copyright 2008 Evgeniy Ivanov <powerfox@kde.ru>                       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifndef COMMITLOGMODEL_H
#define COMMITLOGMODEL_H

#include <QtCore/QAbstractItemModel>
#include <QtCore/QList>
#include <QtCore/QStringList>
#include <dvcs/dvcsevent.h>

// namespace KDevelop
// {
//     class VcsRevision;
// }

class QStringList;

class CommitLogModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    CommitLogModel(const QList<DVcsEvent> revisions, QObject* parent = 0);
    ~CommitLogModel() {};

    QVariant data(const QModelIndex &index, int role) const;
    Qt::ItemFlags flags(const QModelIndex& index) const;
    QVariant headerData(int s, Qt::Orientation o, int role = Qt::DisplayRole) const;
    QModelIndex index(int r, int c, const QModelIndex& par = QModelIndex()) const;
    QModelIndex parent(const QModelIndex& index) const;
    int rowCount(const QModelIndex& par = QModelIndex()) const;
    int columnCount(const QModelIndex&) const;
    int branchCount(const int) const {return branchCnt;}
    QList<int>getProperties(const int i) const {return revs[i].getProperties();}

private:
    QStringList headerInfo;
    QList<DVcsEvent> revs;

    int rowCnt;
    int branchCnt;
};


#endif
