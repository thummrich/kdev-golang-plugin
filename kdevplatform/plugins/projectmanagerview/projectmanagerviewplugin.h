/* This file is part of KDevelop
    Copyright 2004 Roberto Raggi <roberto@kdevelop.org>
    Copyright 2007 Andreas Pakulat <apaku@gmx.de>

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
#ifndef KDEVPROJECTMANAGERVIEWPLUGIN_H
#define KDEVPROJECTMANAGERVIEWPLUGIN_H

#include <QtCore/QPointer>
#include <interfaces/iplugin.h>
#include <QtCore/QVariant>
#include <project/builderjob.h>

class KUrl;
class ProjectBuildSetModel;

namespace KDevelop
{
class ProjectBaseItem;
class ProjectBuilder;
class ProjectFileItem;
class ProjectFolderItem;
class ProjectTargetItem;
class IProjectBuilder;
class IProject;
class ContextMenuExtension;
class Context;
}

class ProjectManagerView;

class ProjectManagerViewPlugin: public KDevelop::IPlugin
{
    Q_OBJECT
public:

public:
    ProjectManagerViewPlugin(QObject *parent, const QVariantList & = QVariantList() );
    virtual ~ProjectManagerViewPlugin();

    // Plugin methods
    virtual void unload();

    KDevelop::ContextMenuExtension contextMenuExtension( KDevelop::Context* );
public Q_SLOTS:
    void buildProjectItems();
    void installProjectItems();
    void cleanProjectItems();
 
protected Q_SLOTS:
    void closeProjects();
    void buildItemsFromContextMenu();
    void installItemsFromContextMenu();
    void cleanItemsFromContextMenu();
    void configureProjectItems();
    void pruneProjectItems();
    void buildAllProjects();
    void addItemsFromContextMenuToBuildset();
    void projectConfiguration();
    void runTargetsFromContextMenu();
    void reloadFromContextMenu();
    void createFolderFromContextMenu();
    void createFileFromContextMenu();
    void removeFromContextMenu();
    void renameItemFromContextMenu();
    void updateActionState( KDevelop::Context* ctx );
    void updateFromBuildSetChange();
private:
    static KDevelop::IProjectBuilder* getProjectBuilder( KDevelop::ProjectBaseItem* item );
    QList<KDevelop::ProjectBaseItem*> recurseAndFetchCheckedItems( KDevelop::ProjectBaseItem* item );
    void runBuilderJob( KDevelop::BuilderJob::BuildType );
    class ProjectManagerViewPluginPrivate* const d;
};

#endif

