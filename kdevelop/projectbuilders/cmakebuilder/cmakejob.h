/* KDevelop CMake Support
 *
 * Copyright 2006-2007 Andreas Pakulat <apaku@gmx.de>
 * Copyright 2008 Hamish Rodda <rodda@kde.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef CMAKEJOB_H
#define CMAKEJOB_H

#include <outputview/outputjob.h>
#include <QProcess>
#include <QString>

namespace KDevelop {
class IProject;
class ProjectBaseItem;
class CommandExecutor;
}

class KUrl;

class CMakeJob: public KDevelop::OutputJob
{
    Q_OBJECT

public:
    enum ErrorTypes
    {
        NoProjectError = UserDefinedError,
        FailedError
    };

    CMakeJob(QObject* parent = 0);
    
    void setProject(KDevelop::IProject* project);

    virtual void start();
    
protected:
    bool doKill();

private Q_SLOTS:
    void slotFailed( QProcess::ProcessError );
    void slotCompleted();

private:
    QStringList cmakeArguments( KDevelop::IProject* project );
    KUrl buildDir( KDevelop::IProject* project );
    QString cmakeBinary( KDevelop::IProject* project );

    KDevelop::IProject* m_project;
    KDevelop::CommandExecutor* m_executor;
    bool m_killed;
};

#endif // CMAKEJOB_H
