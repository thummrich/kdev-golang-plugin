/* KDevelop CMake Support
 *
 * Copyright 2006 Matt Rogers <mattr@kde.org>
 * Copyright 2007-2009 Aleix Pol <aleixpol@kde.org>
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

#include "cmakemanager.h"

#include <QList>
#include <QVector>
#include <QDomDocument>
#include <QDir>
#include <QQueue>

#include <kpluginfactory.h>
#include <kpluginloader.h>
#include <KAboutData>
#include <KDialog>
#include <kparts/mainwindow.h>
#include <KUrl>
#include <KAction>
#include <KMessageBox>
#include <ktexteditor/document.h>
#include <KStandardDirs>

#include <interfaces/icore.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/iproject.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/contextmenuextension.h>
#include <interfaces/context.h>
#include <project/projectmodel.h>
#include <project/importprojectjob.h>
#include <project/helper.h>
#include <language/duchain/parsingenvironment.h>
#include <language/duchain/indexedstring.h>
#include <language/duchain/duchain.h>
#include <language/duchain/dumpchain.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/declaration.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchainutils.h>
#include <language/codecompletion/codecompletion.h>
#include <language/codegen/applychangeswidget.h>

#include "cmakemodelitems.h"
#include "cmakenavigationwidget.h"
#include "cmakecachereader.h"
#include "cmakeastvisitor.h"
#include "cmakeprojectvisitor.h"
#include "cmakeexport.h"
#include "cmakecodecompletionmodel.h"
#include "cmakeutils.h"
#include "cmaketypes.h"
#include "parser/cmakeparserutils.h"
#include "icmakebuilder.h"
#include "icmakedocumentation.h"

#ifdef CMAKEDEBUGVISITOR
#include "cmakedebugvisitor.h"
#endif

#include "ui_cmakepossibleroots.h"
#include <language/editor/editorintegrator.h>
#include <language/duchain/smartconverter.h>
#include <language/duchain/use.h>
#include <ktexteditor/smartinterface.h>
#include <interfaces/idocumentation.h>
#include "cmakeprojectdata.h"
#include <cmakeconfig.h>

#include <language/highlighting/codehighlighting.h>
#include <interfaces/iruncontroller.h>

using namespace KDevelop;

K_PLUGIN_FACTORY(CMakeSupportFactory, registerPlugin<CMakeManager>(); )
K_EXPORT_PLUGIN(CMakeSupportFactory(KAboutData("kdevcmakemanager","kdevcmake", ki18n("CMake Manager"), "0.1", ki18n("Support for managing CMake projects"), KAboutData::License_GPL)))

namespace {

QString fetchBuildDir(KDevelop::IProject* project)
{
    Q_ASSERT(project);
    return CMake::currentBuildDir(project).toLocalFile(KUrl::AddTrailingSlash);
}

QString fetchInstallDir(KDevelop::IProject* project)
{
    Q_ASSERT(project);
    return CMake::currentInstallDir(project).toLocalFile(KUrl::AddTrailingSlash);
}

inline QString replaceBuildDir(QString in, QString buildDir)
{
    return in.replace("#[bin_dir]", buildDir);
}

inline  QString replaceInstallDir(QString in, QString installDir)
{
    return in.replace("#[install_dir]", installDir);
}

KUrl::List resolveSystemDirs(KDevelop::IProject* project, const QStringList& dirs)
{
    QString buildDir = fetchBuildDir(project);
    QString installDir = fetchInstallDir(project);

    KUrl::List newList;
    foreach(const QString& _s, dirs)
    {
        QString s=_s;
        if(s.startsWith(QString::fromUtf8("#[bin_dir]")))
        {
            s= replaceBuildDir(s, buildDir);
        }
        else if(s.startsWith(QString::fromUtf8("#[install_dir]")))
        {
            s= replaceInstallDir(s, installDir);
        }
//         kDebug(9042) << "resolving" << _s << "to" << s;
        newList.append(KUrl(s));
    }
    return newList;
}

}

CMakeManager::CMakeManager( QObject* parent, const QVariantList& )
    : KDevelop::IPlugin( CMakeSupportFactory::componentData(), parent )
{
    KDEV_USE_EXTENSION_INTERFACE( KDevelop::IBuildSystemManager )
    KDEV_USE_EXTENSION_INTERFACE( KDevelop::IProjectFileManager )
    KDEV_USE_EXTENSION_INTERFACE( KDevelop::ILanguageSupport )

    m_highlight = new KDevelop::CodeHighlighting(this);

    new CodeCompletion(this, new CMakeCodeCompletionModel(this), name());
    
    connect(ICore::self()->projectController(), SIGNAL(projectClosing(KDevelop::IProject*)), SLOT(projectClosing(KDevelop::IProject*)));
}

CMakeManager::~CMakeManager()
{}

KUrl CMakeManager::buildDirectory(KDevelop::ProjectBaseItem *item) const
{
    KUrl ret;
    if(item) {
        bool isroot = false;
        if (ProjectFolderItem* projectFolderItem = dynamic_cast<ProjectFolderItem*>(item)) {
            if(projectFolderItem->isProjectRoot()) {
                isroot = true;
            }
        }
        if (isroot) {
            ret=CMake::currentBuildDir(item->project());
        }
        else {
            ret=buildDirectory(dynamic_cast<CMakeFolderItem*>(item->parent()));
            CMakeFolderItem *fi=dynamic_cast<CMakeFolderItem*>(item);
            if(fi)
                ret.addPath(fi->buildDir());
        }
    }
    return ret;
}

KDevelop::ReferencedTopDUContext CMakeManager::initializeProject(KDevelop::IProject* project, const KUrl& baseUrl)
{
    QPair<VariableMap,QStringList> initials = CMakeParserUtils::initialVariables();
    m_modulePathPerProject[project]=initials.first["CMAKE_MODULE_PATH"];
    m_macrosPerProject[project].clear();
    m_varsPerProject[project]=initials.first;
    m_varsPerProject[project].insert("CMAKE_SOURCE_DIR", QStringList(baseUrl.toLocalFile(KUrl::RemoveTrailingSlash)));

    KSharedConfig::Ptr cfg = project->projectConfiguration();
    KConfigGroup group(cfg.data(), "CMake");
    if(group.hasKey("CMakeDir"))
    {
        QStringList l;
        foreach(const QString &path, group.readEntry("CMakeDir", QStringList()) )
        {
            if( QFileInfo(path).exists() )
            {
                m_modulePathPerProject[project] << path;
                l << path;
            }
        }
        if( !l.isEmpty() )
            group.writeEntry("CMakeDir", l);
        else
            group.writeEntry("CMakeDir", m_modulePathPerProject[project]);
    }
    else
        group.writeEntry("CMakeDir", m_modulePathPerProject[project]);

    
    KDevelop::ReferencedTopDUContext buildstrapContext;
    {
        KUrl buildStrapUrl = baseUrl;
        buildStrapUrl.addPath("buildstrap");
        DUChainWriteLocker lock(DUChain::lock());
        
        buildstrapContext = DUChain::self()->chainForDocument(buildStrapUrl);
        
        if(buildstrapContext) {
            buildstrapContext->clearLocalDeclarations();
            buildstrapContext->clearImportedParentContexts();
            buildstrapContext->deleteChildContextsRecursively();
        }else{
            IndexedString idxpath(buildStrapUrl);
            buildstrapContext=new TopDUContext(idxpath, SimpleRange(0,0, 0,0),
                                               new ParsingEnvironmentFile(idxpath));
            DUChain::self()->addDocumentChain(buildstrapContext);
        }
        
        Q_ASSERT(buildstrapContext);
    }
    ReferencedTopDUContext ref=buildstrapContext;
    foreach(const QString& script, initials.second)
    {
        ref = includeScript(CMakeProjectVisitor::findFile(script, m_modulePathPerProject[project], QStringList()),
                              project, ref);
    }
    
    return ref;
}

KDevelop::ProjectFolderItem* CMakeManager::import( KDevelop::IProject *project )
{
    CMakeFolderItem* m_rootItem=0;
    KUrl cmakeInfoFile(project->projectFileUrl());
    cmakeInfoFile = cmakeInfoFile.upUrl();
    cmakeInfoFile.addPath("CMakeLists.txt");

    KUrl folderUrl=project->folder();
    kDebug(9042) << "file is" << cmakeInfoFile.toLocalFile();

    if ( !cmakeInfoFile.isLocalFile() )
    {
        kWarning() << "error. not a local file. CMake support doesn't handle remote projects";
    }
    else
    {
        KSharedConfig::Ptr cfg = project->projectConfiguration();
        KConfigGroup group(cfg.data(), "CMake");

        if(group.hasKey("ProjectRootRelative"))
        {
            QString relative=CMake::projectRootRelative(project);
            folderUrl.cd(relative);
        }
        else
        {
            KDialog chooseRoot;
            QWidget *e=new QWidget(&chooseRoot);
            Ui::CMakePossibleRoots ui;
            ui.setupUi(e);
            chooseRoot.setMainWidget(e);
            for(KUrl aux=folderUrl; QFile::exists(aux.toLocalFile()+"/CMakeLists.txt"); aux=aux.upUrl())
                ui.candidates->addItem(aux.toLocalFile());

            if(ui.candidates->count()>1)
            {
                connect(ui.candidates, SIGNAL(itemActivated(QListWidgetItem*)), &chooseRoot,SLOT(accept()));
                ui.candidates->setMinimumSize(384,192);
                int a=chooseRoot.exec();
                if(!a || !ui.candidates->currentItem())
                {
                    return 0;
                }
                KUrl choice=KUrl(ui.candidates->currentItem()->text());
                CMake::setProjectRootRelative(project, KUrl::relativeUrl(folderUrl, choice));
                folderUrl=choice;
            }
            else
            {
                CMake::setProjectRootRelative(project, "./");
            }
        }

        m_rootItem = new CMakeFolderItem(project, folderUrl.url(), QString(), 0 );
        m_rootItem->setProjectRoot(true);

        KUrl cachefile=buildDirectory(m_rootItem);
        if( cachefile.isEmpty() ) {
            CMake::checkForNeedingConfigure(m_rootItem);
        }
        cachefile.addPath("CMakeCache.txt");
        m_projectCache[project]=readCache(cachefile);
        
        m_watchers[project] = new KDirWatch(project);
        m_watchers[project]->addDir(folderUrl.path());
        m_watchers[project]->disconnect( SIGNAL(dirty(QString) ), this, SLOT(dirtyFile(QString)));
        connect(m_watchers[project], SIGNAL(dirty(QString)), this, SLOT(dirtyFile(QString)));
        Q_ASSERT(m_rootItem->rowCount()==0);
    }
    return m_rootItem;
}


KDevelop::ReferencedTopDUContext CMakeManager::includeScript(const QString& file,
                                                        KDevelop::IProject * project, ReferencedTopDUContext parent)
{
    m_watchers[project]->addFile(file);
    return CMakeParserUtils::includeScript( file, parent, &m_varsPerProject[project], &m_macrosPerProject[project], project->folder().toLocalFile(KUrl::RemoveTrailingSlash), &m_projectCache[project], m_modulePathPerProject[project]);
}

QMutex rxFileFilterMutex; //We have to use a mutex-lock to protect static regular ex
static QRegExp rxFileFilter("\\w*~$|\\w*\\.bak$"); ///@todo This filter should be configurable, and filtering should be done on a manager-independent level

QSet<QString> filterFiles(const QStringList& orig)
{
    QMutexLocker lock(&rxFileFilterMutex);
    
    QSet<QString> ret;
    foreach(const QString& str, orig)
    {
        if(rxFileFilter.indexIn(str)<0)
            ret.insert(str);
    }
    return ret;
}

QList<KDevelop::ProjectFolderItem*> CMakeManager::parse( KDevelop::ProjectFolderItem* item )
{
    QList<KDevelop::ProjectFolderItem*> folderList;
    CMakeFolderItem* folder = dynamic_cast<CMakeFolderItem*>( item );

    KUrl subroot=item->project()->folder();
    m_watchers[item->project()]->addDir(item->url().toLocalFile());
    if(folder && folder->type()==KDevelop::ProjectBaseItem::BuildFolder)
    {
        Q_ASSERT(folder->rowCount()==0);

        kDebug(9042) << "parse:" << folder->url();
        KUrl cmakeListsPath(folder->url());
        cmakeListsPath.addPath("CMakeLists.txt");

        CMakeFileContent f = CMakeListsParser::readCMakeFile(cmakeListsPath.toLocalFile());
        
        if(f.isEmpty())
        {
            kDebug() << "There is no" << cmakeListsPath;
        }
        else
        {
            
            KDevelop::ReferencedTopDUContext curr;
            if(item==item->project()->projectItem())
                curr=initializeProject(item->project(), item->project()->projectItem()->url());
            else
                curr=folder->formerParent()->topDUContext();
            
            kDebug(9042) << "Adding cmake: " << cmakeListsPath << " to the model";

            m_watchers[item->project()]->addFile(cmakeListsPath.toLocalFile());
            QString binDir=KUrl::relativePath(folder->project()->projectItem()->url().toLocalFile(), folder->url().toLocalFile());
            if(binDir.startsWith("./"))
                binDir=binDir.remove(0, 2);
            
            CMakeProjectData data;
            
            //Im not sure if we want to save taht, it might be a lot of data,
            //but can be useful when regenerating.
            data.vm=m_varsPerProject[item->project()];
            data.mm=m_macrosPerProject[item->project()];
            QString currentBinDir=data.vm.value("CMAKE_BINARY_DIR")[0]+binDir;

            data.vm.insert("CMAKE_CURRENT_BINARY_DIR", QStringList(currentBinDir));
            data.vm.insert("CMAKE_CURRENT_LIST_FILE", QStringList(cmakeListsPath.toLocalFile(KUrl::RemoveTrailingSlash)));
            data.vm.insert("CMAKE_CURRENT_SOURCE_DIR", QStringList(folder->url().toLocalFile(KUrl::RemoveTrailingSlash)));

            kDebug(9042) << "currentBinDir" << KUrl(data.vm.value("CMAKE_BINARY_DIR")[0]) << data.vm.value("CMAKE_CURRENT_BINARY_DIR");

        #ifdef CMAKEDEBUGVISITOR
            CMakeAstDebugVisitor dv;
            dv.walk(cmakeListsPath.toLocalFile(), f, 0);
        #endif
            
            {
                CMakeProjectVisitor v(folder->url().toLocalFile(KUrl::RemoveTrailingSlash), curr);
                v.setCacheValues(&m_projectCache[item->project()]);
                v.setVariableMap(&data.vm);
                v.setMacroMap(&data.mm);
                v.setModulePath(m_modulePathPerProject[item->project()]);
                v.setDefinitions(folder->definitions());
                v.walk(f, 0);
                
                folder->setTopDUContext(v.context());
                data.projectName=v.projectName();
                data.subdirectories=v.subdirectories();
                data.definitions=v.definitions();
                data.includeDirectories=v.includeDirectories();
                data.targets=v.targets();
                data.properties=v.properties();
                
                QList<Target>::iterator it=data.targets.begin(), itEnd=data.targets.end();
                for(; it!=itEnd; ++it)
                {
                    it->files=v.resolveDependencies(it->files);
                }
            }
            data.vm.remove("CMAKE_CURRENT_LIST_FILE");
            data.vm.remove("CMAKE_CURRENT_SOURCE_DIR");
            data.vm.remove("CMAKE_CURRENT_BINARY_DIR");
            
            m_varsPerProject[item->project()]=data.vm;
            m_macrosPerProject[item->project()]=data.mm;

            /*{
            kDebug() << "dumpiiiiiing" << folder->url();
            KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
            KDevelop::DumpChain dump;
            dump.dump( v.context(), false);
            }*/

            if(folder->text()=="/" && !data.projectName.isEmpty())
            {
                folder->setText(data.projectName);
            }

            QStringList alreadyAdded;
            foreach (const Subdirectory& subf, data.subdirectories)
            {
                if(subf.name.isEmpty() || alreadyAdded.contains(subf.name)) //empty case would not be necessary if we didn't parse the wrong lines
                    continue;
                
                KUrl path(subf.name);
                if(path.isRelative())
                {
                    KUrl pp=KUrl(folder->url());
                    pp.addPath(subf.name);
                    path=pp;
                }
                path.adjustPath(KUrl::AddTrailingSlash);

                kDebug(9042) << "Found subdir " << path << "which should be into" << subroot;
                if(subroot.isParentOf(path) || path.isParentOf(subroot))
                {
                    alreadyAdded.append(subf.name);
                    CMakeFolderItem* parent=folder;
                    if(path.upUrl()!=folder->url())
                        parent=0;

                    CMakeFolderItem* a = new CMakeFolderItem( folder->project(), subf.name, subf.build_dir, parent );
                    kDebug() << "folder: " << a << a->index();
                    a->setUrl(path);
                    a->setDefinitions(data.definitions);
                    folderList.append( a );
                    
                    if(!parent) {
                        m_pending[path]=a;
                        a->setFormerParent(folder);
                    }

                    DescriptorAttatched* datt=static_cast<DescriptorAttatched*>(a);
                    datt->setDescriptor(subf.desc);
                }
            }

    //         if(folderList.isEmpty() && path.isParentOf(item->url()))
    //             kDebug() << "poor guess";

            QStringList directories;
            directories += folder->url().toLocalFile(KUrl::RemoveTrailingSlash);
            directories += currentBinDir;

            foreach(const QString& s, data.includeDirectories)
            {
                QString dir(s);
                if(!s.startsWith("#["))
                {
                    if(KUrl( s ).isRelative())
                    {
                        KUrl path=folder->url();
                        path.addPath(s);
                        dir=path.toLocalFile();
                    }

                    KUrl simp(dir); //We use this to simplify dir
                    simp.cleanPath();
                    dir=simp.toLocalFile();
                }

                kDebug() << "converting " << s << dir;
                if(!directories.contains(dir))
                    directories.append(dir);
            }
            folder->setIncludeDirectories(directories);
//             kDebug(9042) << "setting include directories: " << folder->url() << directories << "result: " << folder->includeDirectories();
            folder->setDefinitions(data.definitions);

            foreach ( const Target& t, data.targets)
            {
                QStringList files=t.files;
                QString outputName=t.name;
                if(data.properties[TargetProperty].contains(t.name) && data.properties[TargetProperty][t.name].contains("OUTPUT_NAME"))
                    outputName=data.properties[TargetProperty][t.name]["OUTPUT_NAME"].first();
                
                QString path;
                switch(t.type)
                {
                    case Target::Library:
                        path=data.vm.value("CMAKE_LIBRARY_OUTPUT_DIRECTORY").join(QString());
                        break;
                    case Target::Executable:
                        path=data.vm.value("CMAKE_RUNTIME_OUTPUT_DIRECTORY").join(QString());
                        break;
                    case Target::Custom:
                        break;
                }
                
                KUrl resolvedPath;
                if(!path.isEmpty())
                    resolvedPath=resolveSystemDirs(folder->project(), QStringList(path)).first();
                
                KDevelop::ProjectTargetItem* targetItem = 0;
                switch(t.type)
                {
                    case Target::Library:
                        targetItem = new CMakeLibraryTargetItem( item->project(), t.name,
                                                                 folder, t.declaration, outputName, resolvedPath);
                        break;
                    case Target::Executable:
                        targetItem = new CMakeExecutableTargetItem( item->project(), t.name,
                                                                    folder, t.declaration, outputName, resolvedPath);
                        break;
                    case Target::Custom:
                        targetItem = new CMakeCustomTargetItem( item->project(), t.name,
                                                                folder, t.declaration, outputName );
                        break;
                }
                DescriptorAttatched* datt=dynamic_cast<DescriptorAttatched*>(targetItem);
                datt->setDescriptor(t.desc);

                foreach( const QString & sFile, t.files)
                {
                    if(sFile.isEmpty())
                        continue;

                    KUrl sourceFile(sFile);
                    if(sourceFile.isRelative()) {
                        sourceFile = folder->url();
                        sourceFile.adjustPath( KUrl::RemoveTrailingSlash );
                        sourceFile.addPath( sFile );
                    }
                    
                    new KDevelop::ProjectFileItem( item->project(), sourceFile, targetItem );
                    kDebug(9042) << "..........Adding:" << sourceFile;
                }
            }

        }
    }

    QStringList entriesL = QDir( item->url().toLocalFile() ).entryList( QDir::AllEntries | QDir::NoDotAndDotDot );
    QSet<QString> entries = filterFiles(entriesL);
    foreach( const QString& entry, entries )
    {
        if( item->hasFileOrFolder( entry ) )
            continue;

        KUrl fileurl = item->url();
        fileurl.addPath( entry );

        if( QFileInfo( fileurl.toLocalFile() ).isDir() )
        {
            KUrl cache=fileurl;
            cache.addPath("CMakeCache.txt");
            fileurl.adjustPath(KUrl::AddTrailingSlash);
            if(!QFile::exists(cache.toLocalFile())
                && !CMake::allBuildDirs(item->project()).contains(fileurl.toLocalFile(KUrl::RemoveTrailingSlash)))
            {
                if(m_pending.contains(fileurl))
                    item->appendRow(m_pending.take(fileurl));
                else if(subroot.isParentOf(fileurl))
                {
                    //if it's not subparent, we don't add it at all
                    ProjectFolderItem* fitem=new ProjectFolderItem( item->project(), fileurl, item );
                    folderList.append(fitem);
                }
            }
        }
        else if ( subroot.isParentOf(fileurl) )
        {
            //if it's not subparent, we don't add it at all
            new KDevelop::ProjectFileItem( item->project(), fileurl, item );
        }
    }

    return folderList;
}

bool CMakeManager::reload(KDevelop::ProjectFolderItem* folder)
{
    CMakeFolderItem* item=dynamic_cast<CMakeFolderItem*>(folder);
    if ( !item ) {
        QStandardItem* it = folder;
        while(!item && it->parent()) {
            it = it->parent();
            item = dynamic_cast<CMakeFolderItem*>(it);
        }
    }

    if (!item || item == item->project()->projectItem()) {
        folder->project()->reloadModel();
    } else {
        CMakeFolderItem* former=item->formerParent();
        QString buildDir=item->buildDir();
        ProjectFolderItem* parent=static_cast<ProjectFolderItem*>(item->parent());
        KUrl url=item->url();
        IProject* project=item->project();

        parent->removeRow(item->row());
        CMakeFolderItem* fi=new CMakeFolderItem(project, url.toLocalFile(), buildDir, 0);

        fi->setFormerParent(former);
        reimport(fi, parent->url());
    }
    return true;
}

QList<KDevelop::ProjectTargetItem*> CMakeManager::targets() const
{
    QList<KDevelop::ProjectTargetItem*> ret;
    foreach(IProject* p, m_watchers.keys())
    {
        ret+=p->projectItem()->targetList();
    }
    return ret;
}


KUrl::List CMakeManager::includeDirectories(KDevelop::ProjectBaseItem *item) const
{
    CMakeFolderItem* folder=0;
//     kDebug(9042) << "Querying inc dirs for " << item;
    while(!folder && item)
    {
        folder = dynamic_cast<CMakeFolderItem*>( item );
        item = static_cast<KDevelop::ProjectBaseItem*>(item->parent());
//         kDebug(9042) << "Looking for a folder: " << (folder ? folder->url() : KUrl()) << item;
    }
    Q_ASSERT(folder);

//     kDebug(9042) << "Include directories! -- before" << folder->includeDirectories();
    KUrl::List l = resolveSystemDirs(folder->project(), folder->includeDirectories());
//     kDebug(9042) << "Include directories!" << l;
    return l;
}

QHash< QString, QString > CMakeManager::defines(KDevelop::ProjectBaseItem *item ) const
{
    CMakeFolderItem* folder=0;
    kDebug(9042) << "Querying defines dirs for " << item;
    while(!folder)
    {
        folder = dynamic_cast<CMakeFolderItem*>( item );
        item = static_cast<KDevelop::ProjectBaseItem*>(item->parent());
//         kDebug(9042) << "Looking for a folder: " << folder << item;
    }
    Q_ASSERT(folder);

    return folder->definitions();
}

KDevelop::IProjectBuilder * CMakeManager::builder(KDevelop::ProjectFolderItem *) const
{
    IPlugin* i = core()->pluginController()->pluginForExtension( "org.kdevelop.ICMakeBuilder" );
    Q_ASSERT(i);
    ICMakeBuilder* _builder = i->extension<ICMakeBuilder>();
    Q_ASSERT(_builder );
    return _builder ;
}

/*void CMakeProjectManager::parseOnly(KDevelop::IProject* project, const KUrl &url)
{
    kDebug(9042) << "Looking for" << url << " to regenerate";

    KUrl cmakeListsPath(url);
    cmakeListsPath.addPath("CMakeLists.txt");

    VariableMap *vm=&m_varsPerProject[project];
    MacroMap *mm=&m_macrosPerProject[project];

    CMakeFileContent f = CMakeListsParser::readCMakeFile(cmakeListsPath.toLocalFile());
    if(f.isEmpty())
    {
        kDebug() << "There is no" << cmakeListsPath;
        return;
    }

    QString currentBinDir=KUrl::relativeUrl(project->projectItem()->url(), url);
    vm->insert("CMAKE_CURRENT_BINARY_DIR", QStringList(vm->value("CMAKE_BINARY_DIR")[0]+currentBinDir));
    vm->insert("CMAKE_CURRENT_LIST_FILE", QStringList(cmakeListsPath.toLocalFile(KUrl::RemoveTrailingSlash)));
    vm->insert("CMAKE_CURRENT_SOURCE_DIR", QStringList(url.toLocalFile(KUrl::RemoveTrailingSlash)));
    CMakeProjectVisitor v(url.toLocalFile(), missingtopcontext);
    v.setCacheValues(m_projectCache[project]);
    v.setVariableMap(vm);
    v.setMacroMap(mm);
    v.setModulePath(m_modulePathPerProject[project]);
    v.walk(f, 0);
    vm->remove("CMAKE_CURRENT_LIST_FILE");
    vm->remove("CMAKE_CURRENT_SOURCE_DIR");
    vm->remove("CMAKE_CURRENT_BINARY_DIR");
}*/

void CMakeManager::reimport(KDevelop::ProjectFolderItem* fi, const KUrl& parent)
{
    Q_ASSERT(!isReloading(fi->project()));
    
    KJob *job=createImportJob(fi);
    job->setProperty("parent", QUrl(parent));
    
    QMutexLocker locker(&m_busyProjectsMutex);
    m_busyProjects[job]=fi;
    
    connect( job, SIGNAL( result( KJob* ) ), this, SLOT( reimportDone( KJob* ) ) );
    ICore::self()->runController()->registerJob( job );
}

void CMakeManager::reimportDone(KJob* job)
{
    QMutexLocker locker(&m_busyProjectsMutex);
    Q_ASSERT(m_busyProjects.contains(job));
    ProjectFolderItem* it=m_busyProjects[job];
    
    QUrl parentUrl=job->property("parent").toUrl();
    
    QList<ProjectFolderItem*> folders=it->project()->foldersForUrl(parentUrl);
    
    if(!folders.isEmpty()) //If it was not removed while reparsing
    {
        Q_ASSERT(folders.size()==1);
        
        folders.first()->appendRow(m_busyProjects[job]);
    }
    m_busyProjects.remove(job);
}

bool CMakeManager::isReloading(IProject* p)
{
    QMutexLocker locker(&m_busyProjectsMutex);
    if(!p->isReady())
        return true;
    
    foreach(KDevelop::ProjectFolderItem* it, m_busyProjects) {
        if(it->project()==p)
            return true;
    }
    return false;
}

void CMakeManager::dirtyFile(const QString & dirty)
{
    const KUrl dirtyFile(dirty);
    IProject* p=ICore::self()->projectController()->findProjectForUrl(dirtyFile);

    if(p && isReloading(p))
        return;
    
    if(p && dirtyFile.fileName() == "CMakeLists.txt")
    {
        QMutexLocker locker(&m_reparsingMutex); //Maybe we should have a mutex per project
        
        QList<ProjectFileItem*> files=p->filesForUrl(dirtyFile);
        kDebug(9032) << dirtyFile << "is dirty" << files.count();

        Q_ASSERT(files.count()==1);
        CMakeFolderItem *folderItem=static_cast<CMakeFolderItem*>(files.first()->parent());
        if(folderItem!=p->projectItem())
        {
#if 0
            KUrl relative=KUrl::relativeUrl(projectBaseUrl, dir);
            initializeProject(proj, dir);
            KUrl current=projectBaseUrl;
            QStringList subs=relative.toLocalFile().split("/");
            subs.append(QString());
            for(; !subs.isEmpty(); current.cd(subs.takeFirst()))
            {
                parseOnly(proj, current);
            }
#endif
            reload(folderItem);
        }
        else
        {
            reload(p->projectItem());
        }
    }
    else if(dirty.endsWith(".cmake"))
    {
        foreach(KDevelop::IProject* project, m_watchers.uniqueKeys())
        {
            if(m_watchers[project]->contains(dirty))
                reload(project->projectItem());
        }
    }
    else if(p && QFileInfo(dirty).isDir())
    {
        QList<ProjectFolderItem*> folders=p->foldersForUrl(dirty);
        Q_ASSERT(folders.isEmpty() || folders.size()==1);
        
        if(!folders.isEmpty())
        {
            QStringList entriesL = QDir(dirty).entryList( QDir::AllEntries | QDir::NoDotAndDotDot );
            QSet<QString> entries = filterFiles(entriesL);
            
            ProjectFolderItem* item=folders.first();
            KUrl folderurl = item->url();
            
            //We look for new elements
            foreach( const QString& entry, entries )
            {
                if( item->hasFileOrFolder( entry ) )
                    continue;

                KUrl fileurl = folderurl;
                fileurl.addPath( entry );

                if( QFileInfo( fileurl.toLocalFile() ).isDir() )
                {
                    KUrl cache=fileurl;
                    cache.addPath("CMakeCache.txt");
                    fileurl.adjustPath(KUrl::AddTrailingSlash);
                    if(!QFile::exists(cache.toLocalFile())
                        && !CMake::allBuildDirs(item->project()).contains(fileurl.toLocalFile(KUrl::RemoveTrailingSlash)))
                    {
                        new ProjectFolderItem( item->project(), fileurl, item );    
                    }
                }
                else
                {
                    new KDevelop::ProjectFileItem( item->project(), fileurl, item );
                }
            }
            
            //We look for removed elements
            for(int i=0; i<item->rowCount(); i++)
            {
                QStandardItem* it=item->child(i, 0);
                if(it->type()==ProjectBaseItem::Target)
                    continue;
                
                QString current=it->text();
                if(!entries.contains(current))
                {
                    KUrl fileurl = folderurl;
                    fileurl.addPath(current);
                
                    switch(it->type())
                    {
                        case ProjectBaseItem::File:
                            foreach(const ProjectFileItem* removed, p->filesForUrl(fileurl))
                                removed->parent()->removeRow(removed->row());
                            break;
                        case ProjectBaseItem::Folder:
                        case ProjectBaseItem::BuildFolder:
                            foreach(const ProjectFolderItem* removed, p->foldersForUrl(fileurl))
                                removed->parent()->removeRow(removed->row());
                            break;
                        default:
                            break;
                    }
                }
            }
        }
    }
}

QList< KDevelop::ProjectTargetItem * > CMakeManager::targets(KDevelop::ProjectFolderItem * folder) const
{
    return folder->targetList();
}

QString CMakeManager::name() const
{
    return "CMake";
}

KDevelop::ParseJob * CMakeManager::createParseJob(const KUrl &)
{
    return 0;
}

KDevelop::ILanguage * CMakeManager::language()
{
    return core()->languageController()->language(name());
}

const KDevelop::ICodeHighlighting* CMakeManager::codeHighlighting() const
{
    return m_highlight;
}

ContextMenuExtension CMakeManager::contextMenuExtension( KDevelop::Context* context )
{
    if( context->type() != KDevelop::Context::ProjectItemContext )
        return IPlugin::contextMenuExtension( context );

    KDevelop::ProjectItemContext* ctx = dynamic_cast<KDevelop::ProjectItemContext*>( context );
    QList<KDevelop::ProjectBaseItem*> items = ctx->items();

    if( items.isEmpty() )
        return IPlugin::contextMenuExtension( context );

    m_clickedItems = items;
    ContextMenuExtension menuExt;
    if(items.count()==1 && dynamic_cast<DUChainAttatched*>(items.first()))
    {
        KAction* action = new KAction( i18n( "Jump to target definition" ), this );
        connect( action, SIGNAL( triggered() ), this, SLOT( jumpToDeclaration() ) );
        menuExt.addAction( ContextMenuExtension::ProjectGroup, action );
    }

    return menuExt;
}

void CMakeManager::jumpToDeclaration()
{
    DUChainAttatched* du=dynamic_cast<DUChainAttatched*>(m_clickedItems.first());
    if(du)
    {
        KTextEditor::Cursor c;
        KUrl url;
        {
            KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
            Declaration* decl = du->declaration().data();
            if(!decl)
                return;
            c = decl->range().start.textCursor();
            url = decl->url().toUrl();
        }

        ICore::self()->documentController()->openDocument(url, c);
    }
}

CacheValues CMakeManager::readCache(const KUrl &path) const
{
    QFile file(path.toLocalFile());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        kDebug() << "error. Could not find the file" << path;
        return CacheValues();
    }

    CacheValues ret;
    QTextStream in(&file);
    kDebug(9042) << "Reading cache:" << path;
    QStringList currentComment;
    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if(!line.isEmpty() && line[0].isLetter()) //it is a variable
        {
            CacheLine c;
            c.readLine(line);
            if(c.flag().isEmpty())
                ret[c.name()]=CacheEntry(c.value(), currentComment.join("\n"));
//             kDebug(9042) << "Cache line" << line << c.name();
        }
        else if(line.startsWith("//"))
            currentComment += line.right(line.count()-2);
    }
    return ret;
}

KDevelop::ProjectFolderItem* CMakeManager::addFolder( const KUrl& folder, KDevelop::ProjectFolderItem* parent)
{
    if ( !KDevelop::createFolder(folder) ) {
        return 0;
    }
    Q_ASSERT(QFile::exists(folder.toLocalFile()));
    KUrl lists=parent->url();
    lists.addPath("CMakeLists.txt");
    QString relative=KUrl::relativeUrl(parent->url(), folder);

    kDebug() << "Adding folder " << parent->url() << " to " << folder << " as " << relative;

    Q_ASSERT(!relative.contains("/"));
//     CMakeFileContent f = CMakeListsParser::readCMakeFile(file);

    ApplyChangesWidget e;
    e.setCaption(relative);
    e.setInformation(i18n("Create a folder called '%1'.", relative));
    e.addDocuments(IndexedString(lists));

    e.document()->insertLine(e.document()->lines(), QString("add_subdirectory(%1)").arg(relative));

    if(e.exec())
    {
        KUrl newCMakeLists(folder);
        newCMakeLists.addPath("CMakeLists.txt");

        QFile f(newCMakeLists.toLocalFile());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            KMessageBox::error(0, i18n("KDevelop - CMake Support"),
                                  i18n("Could not create the directory's CMakeLists.txt file."));
            return 0;
        }
        QTextStream out(&f);
        out << "\n";

        bool saved=e.applyAllChanges();
        if(!saved)
            KMessageBox::error(0, i18n("KDevelop - CMake Support"),
                                  i18n("Could not save the change."));
    }
    return 0;
}

bool CMakeManager::removeFolder( KDevelop::ProjectFolderItem* it)
{
    if ( !KDevelop::removeUrl(it->project(), it->url(), true) ) {
        return false;
    }
    KUrl lists=it->url().upUrl();
    lists.addPath("CMakeLists.txt");
    if(it->type()!=KDevelop::ProjectBaseItem::BuildFolder)
    {
        it->parent()->removeRow(it->row());
        return true;
    }

    ApplyChangesWidget e;
    e.setCaption(it->text());
    e.setInformation(i18n("Remove a folder called '%1'.", it->text()));
    e.addDocuments(IndexedString(lists));
    
    CMakeFolderItem* cmit=static_cast<CMakeFolderItem*>(it);
    KTextEditor::Range r=cmit->descriptor().range().textRange();
    kDebug(9042) << "For " << lists << " remove " << r;
    e.document()->removeText(r);

    if(e.exec())
    {
        bool saved=e.applyAllChanges();
        if(!saved)
            KMessageBox::error(0, i18n("KDevelop - CMake Support"),
                                  i18n("Could not save the change."));
    }
    return 0;
}

bool followUses(KTextEditor::Document* doc, SimpleRange r, const QString& name, const KUrl& lists, bool add, const QString& replace)
{
    bool ret=false;
    QString txt=doc->text(r.textRange());
    if(!add && txt.contains(name))
    {
        txt.replace(name, replace);
        doc->replaceText(r.textRange(), txt);
        ret=true;
    }
    else
    {
        KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
        KDevelop::ReferencedTopDUContext topctx=DUChain::self()->chainForDocument(lists);
        QList<Declaration*> decls;
        for(int i=0; i<topctx->usesCount(); i++)
        {
            Use u = topctx->uses()[i];

            if(!r.contains(u.m_range))
                continue; //We just want the uses in the range, not the whole file

            Declaration* d=u.usedDeclaration(topctx);

            if(d->context()->topContext()->url().toUrl()==lists)
                decls += d;
        }

        if(add && decls.isEmpty())
        {
            doc->insertText(r.textRange().start(), name);
            ret=true;
        }
        else foreach(Declaration* d, decls)
        {
            r.start=d->range().end;

            for(int lineNum = r.start.line; lineNum <= r.end.line; lineNum++)
            {
                int endParenIndex = doc->line(lineNum).indexOf(')');
                if(endParenIndex >= 0) {
                    r.end = SimpleCursor(lineNum, endParenIndex);
                    break;
                }
            }

            if(!r.isEmpty())
            {
                ret = ret || followUses(doc, r, name, lists, add, replace);
            }
        }
    }
    return ret;
}

bool CMakeManager::removeFile( KDevelop::ProjectFileItem* it)
{
    if ( !KDevelop::removeUrl(it->project(), it->url(), false) ) {
        return false;
    }

    bool ret=true;
    QList<ProjectFileItem*> files=it->project()->filesForUrl(it->url());
    QMap<ProjectTargetItem*, ProjectFileItem*> targets;

    //We loop through all the files with the same url
    foreach(ProjectFileItem* file, files)
    {
        ProjectTargetItem* target=static_cast<ProjectBaseItem*>(file->parent())->target();
        if(target) {
            targets.insert(target, file);
        } else
            file->parent()->removeRow(file->row());
    }

    //only remove after we iterated over all files, otherwise the items
    //might get deleted due to cmake files gettin reparsed
    //TODO: this still leads to crashes if more than one item in a single target gets removed.
    QMap< ProjectTargetItem*, ProjectFileItem* >::const_iterator it2 = targets.constBegin();
    while (it2 != targets.constEnd()) {
        bool res = removeFileFromTarget(it2.value(), it2.key());
        ret = ret && res;
        ++it2;
    }

    return ret;
}

bool CMakeManager::removeFileFromTarget( KDevelop::ProjectFileItem* it, KDevelop::ProjectTargetItem* target)
{
    if(it->parent()!=target)
        return false; //It is not a cmake-managed file

    CMakeFolderItem* folder=static_cast<CMakeFolderItem*>(target->parent());

    DescriptorAttatched* desc=dynamic_cast<DescriptorAttatched*>(target);
    SimpleRange r=desc->descriptor().range();
    r.start=SimpleCursor(desc->descriptor().arguments.first().range().end);

    KUrl lists=folder->url();
    lists.addPath("CMakeLists.txt");

    ApplyChangesWidget e;
    e.setCaption(it->text());
    e.setInformation(i18n("Remove a file called '%1'.", it->text()));
    e.addDocuments(IndexedString(lists));

    bool ret=followUses(e.document(), r, ' '+it->text(), lists, false, QString());
    if(ret)
    {
        if(e.exec())
        {
            e.applyAllChanges();
        }
    }
    
    return ret;
}

//This is being called from ::parse() so we shouldn't make it block the ui
KDevelop::ProjectFileItem* CMakeManager::addFile( const KUrl& url, KDevelop::ProjectFolderItem* parent)
{
    KDevelop::ProjectFileItem* created = 0;
    if ( KDevelop::createFile(url) ) {
        created = new KDevelop::ProjectFileItem( parent->project(), url, parent );
    }
    return created;
}

bool CMakeManager::addFileToTarget( KDevelop::ProjectFileItem* it, KDevelop::ProjectTargetItem* target)
{
    Q_ASSERT(!it->url().isEmpty());
    
    QSet<QString> headerExt=QSet<QString>() << ".h" << ".hpp" << ".hxx";
    foreach(const QString& ext, headerExt)
    {
        if(it->url().fileName().toLower().endsWith(ext))
            return false;
    }
    
    if(it->parent()==target)
        return true; //It already is in the target

    CMakeFolderItem* folder=static_cast<CMakeFolderItem*>(target->parent());

    DescriptorAttatched* desc=dynamic_cast<DescriptorAttatched*>(target);
    SimpleRange r=desc->descriptor().range();
    r.start=SimpleCursor(desc->descriptor().arguments.first().range().end);

    KUrl lists=folder->url();
    lists.addPath("CMakeLists.txt");

    ApplyChangesWidget e;
    e.setCaption(it->fileName());
    e.setInformation(i18n("Add a file called '%1' to target '%2'.", it->fileName(), target->text()));
    e.addDocuments(IndexedString(lists));

    QString filename=KUrl::relativeUrl(folder->url(), it->url());
    if(filename.startsWith("./"))
        filename=filename.right(filename.size()-2);
    bool ret=followUses(e.document(), r, ' '+filename, lists, true, QString());

    if(ret && e.exec())
        ret=e.applyAllChanges();
    if(!ret)
            KMessageBox::error(0, i18n("KDevelop - CMake Support"),
                                  i18n("Cannot save the change."));
    return ret;
}

QWidget* CMakeManager::specialLanguageObjectNavigationWidget(const KUrl& url, const KDevelop::SimpleCursor& position)
{
    KDevelop::TopDUContextPointer top= TopDUContextPointer(KDevelop::DUChain::self()->chainForDocument(url));
    Declaration *decl=0;
    QString htmlDoc;
    if(top)
    {
        int useAt=top->findUseAt(position);
        if(useAt>=0)
        {
            Use u=top->uses()[useAt];
            decl=u.usedDeclaration(top->topContext());
        }
    }

    CMakeNavigationWidget* doc=0;
    if(decl)
    {
        doc=new CMakeNavigationWidget(top, decl);
    }
    else
    {
        const IDocument* d=ICore::self()->documentController()->documentForUrl(url);
        const KTextEditor::Document* e=d->textDocument();
        KTextEditor::Cursor start=position.textCursor(), end=position.textCursor(), step(0,1);
        for(QChar i=e->character(start); i.isLetter() || i=='_'; i=e->character(start-=step))
        {}
        start+=step;
        
        for(QChar i=e->character(end); i.isLetter() || i=='_'; i=e->character(end+=step))
        {}
        
        QString id=e->text(KTextEditor::Range(start, end));
        ICMakeDocumentation* docu=CMake::cmakeDocumentation();
        if( docu )
        {
            KSharedPtr<IDocumentation> desc=docu->description(id, url);
            if(!desc.isNull())
            {
                doc=new CMakeNavigationWidget(top, desc);
            }
        }
    }
    
    return doc;
}

QPair<QString, QString> CMakeManager::cacheValue(KDevelop::IProject* project, const QString& id) const
{
    QPair<QString, QString> ret;
    if(project==0 && !m_projectCache.keys().isEmpty())
    {
        project=m_projectCache.keys().first();
    }
    
    kDebug() << "cache value " << id << project << (m_projectCache.contains(project) && m_projectCache[project].contains(id));
    if(m_projectCache.contains(project) && m_projectCache[project].contains(id))
    {
        const CacheEntry& e=m_projectCache[project].value(id);
        ret.first=e.value;
        ret.second=e.doc;
    }
    return ret;
}


bool CMakeManager::renameFile(ProjectFileItem* it, const KUrl& newUrl)
{
    QList<ProjectFileItem*> files=it->project()->filesForUrl(it->url());
    
    QList<ProjectTargetItem*> targets;
    
    //We loop through all the files with the same url
    foreach(ProjectFileItem* file, files)
    {
        ProjectTargetItem* t=static_cast<ProjectBaseItem*>(file->parent())->target();
        if(t)
            targets+=t;
    }
        
    if(targets.isEmpty())
    {
        return KDevelop::renameUrl(it->project(), it->url(), newUrl);
    }
    
    ApplyChangesWidget e;
    e.setCaption(it->text());
    e.setInformation(i18n("Remove a file called '%1'.", it->text()));
    
    bool ret=false;
    foreach(ProjectTargetItem* target, targets)
    {
        CMakeFolderItem* folder=static_cast<CMakeFolderItem*>(target->parent());

        DescriptorAttatched* desc=dynamic_cast<DescriptorAttatched*>(target);
        SimpleRange r=desc->descriptor().range();
        r.start=SimpleCursor(desc->descriptor().arguments.first().range().end);

        KUrl lists=folder->url();
        lists.addPath("CMakeLists.txt");
        e.addDocuments(IndexedString(lists));
        
        QString newName=KUrl::relativePath(it->url().upUrl().path(), newUrl.path());
        if(newName.startsWith("./"))
            newName.remove(0,2);
        bool hasChanges = followUses(e.document(), r, ' '+it->text(), lists, false, ' '+newName);
        ret = ret || hasChanges;
    }

    if(ret && e.exec())
    {
        bool ret=e.applyAllChanges();
        if(ret)
            ret=KDevelop::renameUrl(it->project(), it->url(), newUrl);
    }

    return ret;
}

bool CMakeManager::renameFolder(ProjectFolderItem* _it, const KUrl& newUrl)
{
    if(_it->type()!=KDevelop::ProjectBaseItem::BuildFolder)
    {
        return KDevelop::renameUrl(_it->project(), _it->url(), newUrl);
    }
    CMakeFolderItem* it=static_cast<CMakeFolderItem*>(_it);
    KUrl lists=it->formerParent()->url();
    lists.addPath("CMakeLists.txt");
    QString newName=KUrl::relativePath(lists.upUrl().path(), newUrl.path());
    if(newName.startsWith("./"))
        newName.remove(0,2);

    ApplyChangesWidget e;
    e.setCaption(it->text());
    e.setInformation(i18n("Rename a folder called '%1'.", it->text()));
    e.addDocuments(IndexedString(lists));
    
    CMakeFolderItem* cmit=static_cast<CMakeFolderItem*>(it);
    KTextEditor::Range r=cmit->descriptor().argRange().textRange();
    kDebug(9042) << "For " << lists << " rename " << r;
    
    e.document()->replaceText(r, newName);
    

    bool ret=e.exec();
    if(ret)
    {
        ret=e.applyAllChanges();
        if(ret)
            ret=KDevelop::renameUrl(it->project(), it->url(), newUrl);
    }
    return ret;
}

void CMakeManager::projectClosing(IProject* p)
{
    m_modulePathPerProject.remove(p);
    m_varsPerProject.remove(p); 
    m_macrosPerProject.remove(p);
    m_watchers.remove(p);
    m_projectCache.remove(p);
}

#include "cmakemanager.moc"
