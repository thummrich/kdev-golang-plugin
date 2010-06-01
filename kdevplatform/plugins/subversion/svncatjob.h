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

#ifndef SVNCATJOB_H
#define SVNCATJOB_H

#include "svnjobbase.h"

#include <QVariant>

#include <kurl.h>

#include <vcs/vcsdiff.h>

namespace KDevelop
{
    class VcsRevision;
}

class SvnInternalCatJob;

class SvnCatJob : public SvnJobBase
{
    Q_OBJECT
public:
    SvnCatJob( KDevSvnPlugin* parent );
    QVariant fetchResults();
    void start();
    SvnInternalJobBase* internalJob() const;

    void setSource( const KDevelop::VcsLocation& );
    void setPegRevision( const KDevelop::VcsRevision& );
    void setSrcRevision( const KDevelop::VcsRevision& );

public slots:
    void setContent( const QString& );
private:
    SvnInternalCatJob* m_job;
    QString m_content;
};


#endif

