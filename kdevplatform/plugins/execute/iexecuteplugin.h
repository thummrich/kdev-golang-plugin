/* KDevelop
 *
 * Copyright 2009 Andreas Pakulat <apaku@gmx.de>
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

#ifndef IEXECUTEPLUGIN_H
#define IEXECUTEPLUGIN_H

#include <QtCore/QString>

namespace KDevelop
{
class ILaunchConfiguration;
}

class KJob;
class KUrl;
class QStringList;

/**
@author Andreas Pakulat
*/

class IExecutePlugin
{
public:
    virtual ~IExecutePlugin() {}

    virtual KUrl executable( KDevelop::ILaunchConfiguration*, QString& ) const = 0;
    virtual QStringList arguments( KDevelop::ILaunchConfiguration*, QString& ) const = 0;
    virtual KUrl workingDirectory( KDevelop::ILaunchConfiguration* ) const = 0;
    virtual KJob* dependecyJob( KDevelop::ILaunchConfiguration* ) const = 0;
    virtual QString environmentGroup( KDevelop::ILaunchConfiguration* ) const = 0;
    virtual bool useTerminal( KDevelop::ILaunchConfiguration* ) const = 0;
    virtual QString nativeAppConfigTypeId() const = 0;
};

Q_DECLARE_INTERFACE( IExecutePlugin, "org.kdevelop.IExecutePlugin" )

#endif

