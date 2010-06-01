/* This file is part of KDevelop
    Copyright 2004 Roberto Raggi <roberto@kdevelop.org>
    Copyright 2006 Matt Rogers <mattr@kde.org>
    Copyright 2006 Hamish Rodda <rodda@kde.org>
    Copyright 2007 Andreas Pakulat <apaku@gmx.de

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
#ifndef IPROJECTFILEMANAGER_H
#define IPROJECTFILEMANAGER_H

#include <QtCore/QStringList>

#include <kurl.h>

#include "../projectexport.h"

class KJob;


namespace KDevelop
{

class IProject;
class ProjectBaseItem;
class ProjectFolderItem;
class ProjectFileItem;
class KDialogBase;

/**
 * @short An interface to project file management
 *
 * FileManager is the class you want to implement for integrating
 * a project manager in KDevelop.  For build systems, implement its
 * child class, BuildManager.
 *
 * These classes \e do \e not cause files, folders etc. to be created
 * or removed on disk.  They simply read from and write to the file(s)
 * which describe the structure (eg. CMakeLists.txt for cmake, Makefile.am for automake, etc).
 *
 * @author Roberto Raggi, Matt Rogers, Hamish Rodda
 */
class KDEVPLATFORMPROJECT_EXPORT IProjectFileManager
{
public:

    virtual ~IProjectFileManager();
    /** Features the file manager supports */
    enum Feature
    {
        None     = 0 ,     ///< This manager supports nothing
        Folders  = 1 << 0, ///< Folders are supported by the manager
        Targets  = 1 << 1, ///< Targets are supported by the manager
        Files    = 1 << 2  ///< Files are supported by the manager
    };
    Q_DECLARE_FLAGS( Features, Feature )

    /**
     * @return the Features supported by the filemanager
     */
    virtual Features features() const = 0;

    /**
     * This method initialize the model item @arg dom
     * @return The list of the sub folders
     */
    virtual QList<ProjectFolderItem*> parse(ProjectFolderItem *dom) = 0;

    /**
     * This method creates the root item from the file @arg fileName
     * @return The created item
     */
    virtual ProjectFolderItem *import(IProject *project) = 0;
    
    /**
     * This method creates an import job for the given @arg item
     *
     * The default implementation should be suitable for most needs,
     * it'll create an instance of @class ImportProjectJob
     *
     * @return a job that imports the project
     */
    virtual KJob* createImportJob(ProjectFolderItem* item);

    /**
     * Add a folder to the project and create it on disk.
     *
     * Adds the folder specified by @p folder to @p parent and modifies the
     * underlying build system if needed
     */
    virtual ProjectFolderItem* addFolder(const KUrl& folder, ProjectFolderItem *parent) = 0;


    /**
     * Add a file to a folder and create it on disk.
     *
     * Adds the file specified by @p file to the folder @p parent and modifies
     * the underlying build system if needed. The file is not added to a target
     */
    virtual ProjectFileItem* addFile(const KUrl& folder, ProjectFolderItem *parent) = 0;

    /**
     * Remove a folder from the project and remove it from disk.
     *
     * Removes the folder specified by @p folder from folder @p parent and
     * modifies the underlying build system if needed.
     */
    virtual bool removeFolder(ProjectFolderItem *folder) = 0;

    /**
     * Remove a file from the project and remove it from disk.
     *
     * Removes the file specified by @p file and
     * modifies the underlying build system if needed. If the file being
     * removed is also part of a target, it should be removed from the target as well
     */
    virtual bool removeFile(ProjectFileItem *file) = 0;

    /**
     * Rename a file in the project
     *
     * Renames the file specified by @p oldFile to @p newFile
     *
     */
    virtual bool renameFile(ProjectFileItem* oldFile,
                            const KUrl& newFile) = 0;
    /**
     * Rename a folder in the project
     *
     * Renames the folder specified by @p oldFile to @p newFile
     */
    virtual bool renameFolder(ProjectFolderItem* oldFolder,
                              const KUrl& newFolder ) = 0;

    /**
     * Reload an item in the project
     *
     * Reloads the item specified by @p item
     */
    virtual bool reload(ProjectFolderItem* item) = 0;

Q_SIGNALS:
    void folderAdded( ProjectFolderItem* folder );
    void folderRemoved( ProjectFolderItem* folder );
    void folderRenamed( const KUrl& oldFolder,
                        ProjectFolderItem* newFolder );

    void fileAdded(ProjectFileItem* file);
    void fileRemoved(ProjectFileItem* file);
    void fileRenamed(const KUrl& oldFile,
                     ProjectFileItem* newFile);

};

}
Q_DECLARE_OPERATORS_FOR_FLAGS( KDevelop::IProjectFileManager::Features )

Q_DECLARE_INTERFACE( KDevelop::IProjectFileManager, "org.kdevelop.IProjectFileManager")

#endif
