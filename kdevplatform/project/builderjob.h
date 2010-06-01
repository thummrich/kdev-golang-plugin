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

#ifndef BUILDERJOB_H
#define BUILDERJOB_H

#include <QtCore/QList>

#include <kcompositejob.h>

#include "projectexport.h"

class KConfigGroup;

namespace KDevelop
{
class ProjectBaseItem;
class IProject;

/**
 * Allows to build a list of project items or projects sequentially, where
 * failing to build one item in the list will fail the whole job.
 */
class KDEVPLATFORMPROJECT_EXPORT BuilderJob : public KCompositeJob
{
    Q_OBJECT
public:
    /**
     * Defines what action to do on the Project builder
     */
    enum BuildType 
    {
        Build /**< Build the selected items */,
        Prune /**< Prune the selected items */,
        Configure /**< Configure the selected items */,
        Install /**< Install the selected items */,
        Clean /**< Clean the selected items */
    };
    
    /** 
     * Creates a Builder job
     */
    BuilderJob();

    /**
     * Allows to easily schedule building a couple of @p items using the
     * method identified by @p type
     *
     * @param type the build method to use
     * @param items the project items to add
     */
    void addItems( BuildType type, const QList<KDevelop::ProjectBaseItem*>& items );

    /**
     * Allows to easily schedule building a couple of @p projects using the
     * method identified by @p type
     *
     * @param type the build method to use
     * @param projects the projects to add
     */
    void addProjects( BuildType type, const QList<KDevelop::IProject*>& projects );

    /**
     * Allows to add a single @p item to the end of the list. The item will be 
     * built using the method identified by @p type
     *
     * @param item The item to add to the list
     * @param type The build method to be used for the item
     */
    void addItem( BuildType type, ProjectBaseItem* item );
    
    /**
     * Allows to choose between stopping and failing the composite job
     * when the first item could not be built, or building all items
     * The default for this is true.
     * @param stopOnFail if set to true this job will stop and fail when the first 
     *                   item in the list cannot be build
     */
    void setStopOnFail( bool stopOnFail );
    
    /**
     * Find out whether this builderjob stops building items on the first failed
     * item.
     * @returns true if this job stops and fails when the first subjob failed
     */
    bool stopOnFail() const;

    /**
     * Starts this job
     */
    void start();
protected Q_SLOTS:
    /**
     * @internal slot to handle the result from subjobs
     */
    virtual void slotResult( KJob* );
private:
    class BuilderJobPrivate* const d;
    friend class BuilderJobPrivate;
};

}

#endif
