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

#ifndef SVNLOGJOB_H
#define SVNLOGJOB_H

#include "svnjobbase.h"

#include <QVariant>

#include <kurl.h>

#include <vcs/vcsrevision.h>
#include <vcs/vcsevent.h>

class SvnInternalLogJob;

class SvnLogJob : public SvnJobBase
{
    Q_OBJECT
public:
    SvnLogJob( KDevSvnPlugin* parent );
    QVariant fetchResults();
    void start();
    SvnInternalJobBase* internalJob() const;

    void setLocation( const KUrl& location );
    void setLimit( int limit );
    void setEndRevision( const KDevelop::VcsRevision& rev );
    void setStartRevision( const KDevelop::VcsRevision& rev );
private slots:
    void logEventReceived( const KDevelop::VcsEvent& );
private:
    SvnInternalLogJob* m_job;
    QList<QVariant> m_eventList;

};

#endif
