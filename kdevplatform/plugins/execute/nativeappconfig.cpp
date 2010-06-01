/*  This file is part of KDevelop
    Copyright 2009 Andreas Pakulat <apaku@gmx.de>

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
#include "nativeappconfig.h"

#include <klocale.h>
#include <kdebug.h>
#include <kicon.h>

#include <interfaces/icore.h>
#include <interfaces/iprojectcontroller.h>
#include <interfaces/ilaunchconfiguration.h>

#include <project/projectmodel.h>

#include "nativeappjob.h"
#include <interfaces/iproject.h>
#include <project/interfaces/ibuildsystemmanager.h>
#include <project/interfaces/iprojectbuilder.h>
#include <project/builderjob.h>
#include <kmessagebox.h>
#include <interfaces/iuicontroller.h>
#include <util/executecompositejob.h>
#include <kparts/mainwindow.h>
#include <interfaces/iplugincontroller.h>

#include "executeplugin.h"
#include <util/kdevstringhandler.h>
#include <util/environmentgrouplist.h>
#include <project/projectitemlineedit.h>

KIcon NativeAppConfigPage::icon() const
{
    return KIcon("system-run");
}

//TODO: Make sure to auto-add the executable target to the dependencies when its used.

void NativeAppConfigPage::loadFromConfiguration(const KConfigGroup& cfg, KDevelop::IProject* project ) 
{
    bool b = blockSignals( true );
    if( cfg.readEntry( ExecutePlugin::isExecutableEntry, false ) ) 
    {
        executableRadio->setChecked( true );
        executablePath->setUrl( cfg.readEntry( ExecutePlugin::executableEntry, KUrl() ) );
    } else 
    {
        if( project )
        {
            projectTarget->setBaseItem( project->projectItem() );
        } else {
            projectTarget->setBaseItem( 0 );
        }
        projectTargetRadio->setChecked( true );
        projectTarget->setItemPath( cfg.readEntry( ExecutePlugin::projectTargetEntry, QStringList() ) );
    }
    arguments->setText( cfg.readEntry( ExecutePlugin::argumentsEntry, "" ) );
    workingDirectory->setUrl( cfg.readEntry( ExecutePlugin::workingDirEntry, KUrl() ) );
    environment->setCurrentProfile( cfg.readEntry( ExecutePlugin::environmentGroupEntry, "default" ) );
    //TODO: Implement external terminal support
    //runInTerminal->setChecked( cfg.readEntry( ExecutePlugin::useTerminalEntry, false ) );
    QVariantList deps = KDevelop::stringToQVariant( cfg.readEntry( ExecutePlugin::dependencyEntry, QString() ) ).toList();
    QStringList strDeps;
    foreach( const QVariant& dep, deps ) {
        QListWidgetItem* item = new QListWidgetItem( KDevelop::joinWithEscaping( dep.toStringList(), '/', '\\' ), dependencies );
        item->setData( Qt::UserRole, dep );
    }
    dependencyAction->setCurrentIndex( dependencyAction->findData( cfg.readEntry( ExecutePlugin::dependencyActionEntry, "Nothing" ) ) );
    blockSignals( b );
}

NativeAppConfigPage::NativeAppConfigPage( QWidget* parent ) 
    : LaunchConfigurationPage( parent )
{
    setupUi(this);    
    //Setup data info for combobox
    dependencyAction->setItemData(0, "Nothing" );
    dependencyAction->setItemData(1, "Build" );
    dependencyAction->setItemData(2, "Install" );
    dependencyAction->setItemData(3, "SudoInstall" );

    addDependency->setIcon( KIcon("list-add") );
    removeDependency->setIcon( KIcon("list-remove") );
    moveDepUp->setIcon( KIcon("go-up") );
    moveDepDown->setIcon( KIcon("go-down") );
    
    //Set workingdirectory widget to ask for directories rather than files
    workingDirectory->setMode(KFile::Directory | KFile::ExistingOnly | KFile::LocalOnly);

    KDevelop::EnvironmentGroupList env( KGlobal::config() );
    environment->addItems( env.groups() );


    //connect signals to changed signal
    connect( projectTarget, SIGNAL(textChanged(const QString&)), SIGNAL(changed()) );
    connect( projectTargetRadio, SIGNAL(toggled(bool)), SIGNAL(changed()) );
    connect( executableRadio, SIGNAL(toggled(bool)), SIGNAL(changed()) );
    connect( executablePath->lineEdit(), SIGNAL(textEdited(const QString&)), SIGNAL(changed()) );
    connect( executablePath, SIGNAL(urlSelected(const KUrl&)), SIGNAL(changed()) );
    connect( arguments, SIGNAL(textEdited(const QString&)), SIGNAL(changed()) );
    connect( workingDirectory, SIGNAL(urlSelected(const KUrl&)), SIGNAL(changed()) );
    connect( workingDirectory->lineEdit(), SIGNAL(textEdited(const QString&)), SIGNAL(changed()) );
    connect( environment, SIGNAL(currentIndexChanged(int)), SIGNAL(changed()) );
    connect( addDependency, SIGNAL(clicked(bool)), SLOT(addDep()) );
    connect( addDependency, SIGNAL(clicked(bool)), SIGNAL(changed()) );
    connect( removeDependency, SIGNAL(clicked(bool)), SIGNAL(changed()) );
    connect( removeDependency, SIGNAL(clicked(bool)), SLOT(removeDep()) );
    connect( moveDepDown, SIGNAL(clicked(bool)), SIGNAL(changed()) );
    connect( moveDepUp, SIGNAL(clicked(bool)), SIGNAL(changed()) );
    connect( moveDepDown, SIGNAL(clicked(bool)), SLOT(moveDependencyDown()) );
    connect( moveDepUp, SIGNAL(clicked(bool)), SLOT(moveDependencyUp()) );
    connect( dependencies->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)), SLOT(checkActions(QItemSelection,QItemSelection)) );
    connect( dependencyAction, SIGNAL(currentIndexChanged(int)), SIGNAL(changed()) );
    //TODO: Implement external terminal support
    //connect( runInTerminal, SIGNAL(toggled(bool)), SIGNAL(changed()) );
    connect( dependencyAction, SIGNAL(currentIndexChanged(int)), SLOT(activateDeps(int)) );
    connect( targetDependency, SIGNAL(textChanged(QString)), SLOT(depEdited(QString)));
}


void NativeAppConfigPage::depEdited( const QString& str )
{
    int pos;
    QString tmp = str;
    kDebug() << str << targetDependency->validator();
    addDependency->setEnabled( !str.isEmpty() 
                               && ( !targetDependency->validator() 
                               || targetDependency->validator()->validate( tmp, pos ) == QValidator::Acceptable ) );
}

void NativeAppConfigPage::activateDeps( int idx )
{
    dependencies->setEnabled( dependencyAction->itemData( idx ).toString() != "Nothing" );
    targetDependency->setEnabled( dependencyAction->itemData( idx ).toString() != "Nothing" );
}

void NativeAppConfigPage::checkActions( const QItemSelection& selected, const QItemSelection& unselected )
{
    Q_UNUSED( unselected );
    kDebug() << "checkActions";
    if( !selected.indexes().isEmpty() )
    {
        kDebug() << "have selection";
        Q_ASSERT( selected.indexes().count() == 1 );
        QModelIndex idx = selected.indexes().at( 0 );
        kDebug() << "index" << idx;
        moveDepUp->setEnabled( idx.row() > 0 );
        moveDepDown->setEnabled( idx.row() < dependencies->count() - 1 );
        removeDependency->setEnabled( true );
    } else 
    {
        removeDependency->setEnabled( false );
        moveDepUp->setEnabled( false );
        moveDepDown->setEnabled( false );
    }
}

void NativeAppConfigPage::moveDependencyDown()
{
    QList<QListWidgetItem*> list = dependencies->selectedItems();
    if( !list.isEmpty() )
    {
        Q_ASSERT( list.count() == 1 );
        QListWidgetItem* item = list.at( 0 );
        int row = dependencies->row( item );
        dependencies->takeItem( row );
        dependencies->insertItem( row+1, item );
        dependencies->selectionModel()->select( dependencies->model()->index( row+1, 0, QModelIndex() ), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::SelectCurrent );
    }
}

void NativeAppConfigPage::moveDependencyUp()
{
    
    QList<QListWidgetItem*> list = dependencies->selectedItems();
    if( !list.isEmpty() )
    {
        Q_ASSERT( list.count() == 1 );
        QListWidgetItem* item = list.at( 0 );
        int row = dependencies->row( item );
        dependencies->takeItem( row );
        dependencies->insertItem( row-1, item );
        dependencies->selectionModel()->select( dependencies->model()->index( row-1, 0, QModelIndex() ), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::SelectCurrent );
    }
}

void NativeAppConfigPage::addDep()
{
    QListWidgetItem* item = new QListWidgetItem( targetDependency->text(), dependencies );
    item->setData( Qt::UserRole, targetDependency->itemPath() );
    targetDependency->setText("");
    addDependency->setEnabled( false );
    dependencies->selectionModel()->select( dependencies->model()->index( dependencies->model()->rowCount() - 1, 0, QModelIndex() ), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::SelectCurrent );
}

void NativeAppConfigPage::removeDep()
{
    QList<QListWidgetItem*> list = dependencies->selectedItems();
    if( !list.isEmpty() )
    {
        Q_ASSERT( list.count() == 1 );
        int row = dependencies->row( list.at(0) );
        delete dependencies->takeItem( row );
        
        dependencies->selectionModel()->select( dependencies->model()->index( row - 1, 0, QModelIndex() ), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::SelectCurrent );
    }
}

void NativeAppConfigPage::saveToConfiguration( KConfigGroup cfg, KDevelop::IProject* project ) const
{
    Q_UNUSED( project );
    cfg.writeEntry( ExecutePlugin::isExecutableEntry, executableRadio->isChecked() );
    if( executableRadio-> isChecked() )
    {
        cfg.writeEntry( ExecutePlugin::executableEntry, executablePath->url() );
        cfg.deleteEntry( ExecutePlugin::projectTargetEntry );
    } else
    {
        cfg.writeEntry( ExecutePlugin::projectTargetEntry, projectTarget->itemPath() );
        cfg.deleteEntry( ExecutePlugin::executableEntry );
    }
    cfg.writeEntry( ExecutePlugin::argumentsEntry, arguments->text() );
    cfg.writeEntry( ExecutePlugin::workingDirEntry, workingDirectory->url() );
    cfg.writeEntry( ExecutePlugin::environmentGroupEntry, environment->currentProfile() );
    //TODO: Implement external terminal support
    //cfg.writeEntry( ExecutePlugin::useTerminalEntry, runInTerminal->isChecked() );
    cfg.writeEntry( ExecutePlugin::dependencyActionEntry, dependencyAction->itemData( dependencyAction->currentIndex() ).toString() );
    QVariantList deps;
    for( int i = 0; i < dependencies->count(); i++ )
    {
        deps << dependencies->item( i )->data( Qt::UserRole );
    }
    cfg.writeEntry( ExecutePlugin::dependencyEntry, KDevelop::qvariantToString( QVariant( deps ) ) );
}

QString NativeAppConfigPage::title() const 
{
    return i18n("Configure Native Application");
}

QList< KDevelop::LaunchConfigurationPageFactory* > NativeAppLauncher::configPages() const 
{
    return QList<KDevelop::LaunchConfigurationPageFactory*>();
}

QString NativeAppLauncher::description() const
{
    return "Executes Native Applications";
}

QString NativeAppLauncher::id() 
{
    return "nativeAppLauncher";
}

QString NativeAppLauncher::name() const 
{
    return i18n("Native Application");
}

NativeAppLauncher::NativeAppLauncher()
{
}

KJob* NativeAppLauncher::start(const QString& launchMode, KDevelop::ILaunchConfiguration* cfg)
{
    Q_ASSERT(cfg);
    if( !cfg )
    {
        return 0;
    }
    if( launchMode == "execute" )
    {
        IExecutePlugin* iface = KDevelop::ICore::self()->pluginController()->pluginForExtension("org.kdevelop.IExecutePlugin")->extension<IExecutePlugin>();
        Q_ASSERT(iface);
        KJob* depjob = iface->dependecyJob( cfg );
        QList<KJob*> l;
        if( depjob )
        {
            l << depjob;
        }
        l << new NativeAppJob( KDevelop::ICore::self()->runController(), cfg );
        return new KDevelop::ExecuteCompositeJob( KDevelop::ICore::self()->runController(), l );
        
    }
    kWarning() << "Unknown launch mode " << launchMode << "for config:" << cfg->name();
    return 0;
}

QStringList NativeAppLauncher::supportedModes() const
{
    return QStringList() << "execute";
}

KDevelop::LaunchConfigurationPage* NativeAppPageFactory::createWidget(QWidget* parent)
{
    return new NativeAppConfigPage( parent );
}

NativeAppPageFactory::NativeAppPageFactory()
{
}

NativeAppConfigType::NativeAppConfigType() 
{
    factoryList.append( new NativeAppPageFactory() );
}

QString NativeAppConfigType::name() const
{
    return i18n("Native Application");
}


QList<KDevelop::LaunchConfigurationPageFactory*> NativeAppConfigType::configPages() const 
{
    return factoryList;
}

QString NativeAppConfigType::id() const 
{
    return ExecutePlugin::_nativeAppConfigTypeId;
}

KIcon NativeAppConfigType::icon() const
{
    return KIcon("system-run");
}

bool NativeAppConfigType::canLaunch ( KDevelop::ProjectBaseItem* item ) const
{
    if( item->target() && item->target()->executable() ) {
        return canLaunch( item->target()->executable()->builtUrl() );
    }
    return false;
}

bool NativeAppConfigType::canLaunch ( const KUrl& file ) const
{
    return ( file.isLocalFile() && QFileInfo( file.toLocalFile() ).isExecutable() );
}

void NativeAppConfigType::configureLaunchFromItem ( KConfigGroup cfg, KDevelop::ProjectBaseItem* item ) const
{
    cfg.writeEntry( ExecutePlugin::isExecutableEntry, false );
    KDevelop::ProjectModel* model = KDevelop::ICore::self()->projectController()->projectModel();
    cfg.writeEntry( ExecutePlugin::projectTargetEntry, model->pathFromIndex( model->indexFromItem( item ) ) );
    cfg.writeEntry( ExecutePlugin::workingDirEntry, item->executable()->builtUrl().upUrl() );
    cfg.sync();
}

void NativeAppConfigType::configureLaunchFromCmdLineArguments ( KConfigGroup cfg, const QStringList& args ) const
{
    cfg.writeEntry( ExecutePlugin::isExecutableEntry, true );
    cfg.writeEntry( ExecutePlugin::executableEntry, args.first() );
    QStringList a(args);
    a.removeFirst();
    cfg.writeEntry( ExecutePlugin::argumentsEntry, a);
    cfg.sync();
}


#include "nativeappconfig.moc"
