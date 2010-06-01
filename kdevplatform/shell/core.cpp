/***************************************************************************
 *   Copyright 2007 Alexander Dymo <adymo@kdevelop.org>             *
 *   Copyright 2007 Kris Wong <kris.p.wong@gmail.com>               *
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
#include "core.h"
#include "core_p.h"

#include <QtGui/QApplication>
#include <QtCore/QPointer>
#include <QtCore/QTimer>

#include <kdebug.h>
#include <kglobal.h>
#include <klocale.h>
#include <ksplashscreen.h>

#include <sublime/area.h>
#include <sublime/tooldocument.h>

#include <language/editor/editorintegrator.h>
#include <language/backgroundparser/backgroundparser.h>

#include "shellextension.h"

#include "mainwindow.h"
#include "sessioncontroller.h"
#include "uicontroller.h"
#include "plugincontroller.h"
#include "projectcontroller.h"
#include "partcontroller.h"
#include "languagecontroller.h"
#include "documentcontroller.h"
#include "runcontroller.h"
#include "session.h"
#include "documentationcontroller.h"
#include "sourceformattercontroller.h"
#include "progressmanager.h"
#include "selectioncontroller.h"
#include "debugcontroller.h"
#include "kdevplatformversion.h"
#include "workingsetcontroller.h"
#include <KMessageBox>

namespace KDevelop {

Core *Core::m_self = 0;
KAboutData aboutData()
{
    KAboutData aboutData( "kdevplatform", "kdevplatform", 
                          ki18n("KDevelop Platform"), KDEVPLATFORM_VERSION_STR, 
                          ki18n("Development Platform for IDE-like Applications"), 
                          KAboutData::License_LGPL_V2, ki18n( "Copyright 2004-2009, The KDevelop developers" ), 
                          KLocalizedString(), "http://www.kdevelop.org" );
    aboutData.addAuthor( ki18n("Andreas Pakulat"), ki18n( "Maintainer, Architecture, VCS Support, Project Management Support, QMake Projectmanager" ), "apaku@gmx.de" );
    aboutData.addAuthor( ki18n("Alexander Dymo"), ki18n( "Architecture, Sublime UI, Ruby support" ), "adymo@kdevelop.org" );
    aboutData.addAuthor( ki18n("David Nolden"), ki18n( "Definition-Use Chain, C++ Support" ), "david.nolden.kdevelop@art-master.de" );
    aboutData.addAuthor( ki18n("Aleix Pol Gonzalez"), ki18n( "CMake Support, Run Support, Kross Support" ), "aleixpol@kde.org" );
    aboutData.addAuthor( ki18n("Vladimir Prus"), ki18n( "GDB integration" ), "ghost@cs.msu.su" );
    aboutData.addAuthor( ki18n("Hamish Rodda"), ki18n( "Text editor integration, definition-use chain" ), "rodda@kde.org" );
    
    aboutData.addCredit( ki18n("Matt Rogers"), KLocalizedString(), "mattr@kde.org");
    aboutData.addCredit( ki18n("Cédric Pasteur"), ki18n("astyle and ident support"), "cedric.pasteur@free.fr" );
    aboutData.addCredit( ki18n("Evgeniy Ivanov"), ki18n("Distributed VCS, Git, Mercurial"), "powerfox@kde.ru" );
    //Veritas is outside in playground currently.
    //aboutData.addCredit( ki18n("Manuel Breugelmanns"), ki18n( "Veritas, QTest integraton"), "mbr.nxi@gmail.com" );
    aboutData.addCredit( ki18n("Robert Gruber") , ki18n( "SnippetPart, debugger and usability patches" ), "rgruber@users.sourceforge.net" );
    aboutData.addCredit( ki18n("Dukju Ahn"), ki18n( "Subversion plugin, Custom Make Manager, Overall improvements" ), "dukjuahn@gmail.com" );
    return aboutData;
}

CorePrivate::CorePrivate(Core *core):
    m_componentData( aboutData() ), m_core(core), m_cleanedUp(false), m_shuttingDown(false)
{
}

bool CorePrivate::initialize(Core::Setup mode, const QString& session )
{
    m_mode=mode;
    if( !sessionController )
    {
        sessionController = new SessionController(m_core);
    }
    if( !workingSetController && !(mode & Core::NoUi) )
    {
        workingSetController = new WorkingSetController(m_core);
    }
    kDebug() << "Creating ui controller";
    if( !uiController )
    {
        uiController = new UiController(m_core);
    }
    kDebug() << "Creating plugin controller";

    if( !pluginController )
    {
        pluginController = new PluginController(m_core);
    }
    if( !partController && !(mode & Core::NoUi))
    {
        partController = new PartController(m_core, uiController->defaultMainWindow());
    }

    if( !projectController )
    {
        projectController = new ProjectController(m_core);
    }

    if( !languageController )
    {
        languageController = new LanguageController(m_core);
    }

    if( !documentController )
    {
        documentController = new DocumentController(m_core);
    }

    if( !runController )
    {
        runController = new RunController(m_core);
    }

    if( !sourceFormatterController )
    {
        sourceFormatterController = new SourceFormatterController(m_core);
    }

    if ( !progressController) 
    {
        progressController = new ProgressManager();
    }

    if( !selectionController )
    {
        selectionController = new SelectionController(m_core);
    }
    
    if( !documentationController && !(mode & Core::NoUi) )
    {
        documentationController = new DocumentationController(m_core);
    }

    if( !debugController )
    {
        debugController = new DebugController(m_core);
    }

    kDebug() << "initializing ui controller";
    sessionController->initialize( session );
    // Initialize the item repository as first thing after loading the session,
    // TODO: Is this early enough, or should we put the loading of the session into
    // the controller construct
    globalItemRepositoryRegistry();

    if(!sessionController->lockSession())
    {
        QString errmsg = i18n("This session (%1) is already active in another running instance",
                              sessionController->activeSession() ? "null" : sessionController->activeSession()->id().toString() );
        if( mode & Core::NoUi ) {
            QTextStream qerr(stderr);
            qerr << endl << errmsg << endl;
        } else {
            KMessageBox::error(0, errmsg);
        }
        return false;
    }
    
    if(!(mode & Core::NoUi)) uiController->initialize();
    languageController->initialize();
    projectController->initialize();
    documentController->initialize();

    /* This is somewhat messy.  We want to load the areas before
        loading the plugins, so that when each plugin is loaded we
        know if an area wants some of the tool view from that plugin.
        OTOH, loading of areas creates documents, and some documents
        might require that a plugin is already loaded.
        Probably, the best approach would be to plugins to just add
        tool views to a list of available tool view, and then grab
        those tool views when loading an area.  */

    kDebug() << "loading session plugins";
    pluginController->initialize();

    if(!(mode & Core::NoUi))
    {
        workingSetController->initialize();
        /* Need to do this after everything else is loaded.  It's too
            hard to restore position of views, and toolbars, and whatever
            that are not created yet.  */
        uiController->loadAllAreas(KGlobal::config());
        uiController->defaultMainWindow()->show();
    }
    runController->initialize();
    sourceFormatterController->initialize();
    selectionController->initialize();
    documentationController->initialize();
    debugController->initialize();
    
    return true;
}
CorePrivate::~CorePrivate()
{
    delete selectionController;
    delete projectController;
    delete languageController;
    delete pluginController;
    delete uiController;
    delete partController;
    delete documentController;
    delete runController;
    delete sessionController;
    delete sourceFormatterController;
    delete documentationController;
    delete debugController;
    delete workingSetController;
}


bool Core::initialize(KSplashScreen* splash, Setup mode, const QString& session )
{
    if (m_self)
        return true;

    m_self = new Core();
    bool ret = m_self->d->initialize(mode, session);
    if( splash ) {
        QTimer::singleShot( 200, splash, SLOT(deleteLater()) );
    }
    
    if(ret)
        emit m_self->initializationCompleted();
    
    return ret;
}

Core *KDevelop::Core::self()
{
    return m_self;
}

Core::Core(QObject *parent)
    : ICore(parent)
{
    d = new CorePrivate(this);
}

Core::Core(CorePrivate* dd, QObject* parent)
: ICore(parent), d(dd)
{
}

Core::~Core()
{
    kDebug() ;
    //Cleanup already called before mass destruction of GUI
    delete d;
}

Core::Setup Core::setupFlags() const
{
    return d->m_mode;
}

bool Core::shuttingDown() const
{
    return d->m_shuttingDown;
}

void Core::cleanup()
{
    d->m_shuttingDown = true;
    
    if (!d->m_cleanedUp) {
        d->debugController->cleanup();
        d->selectionController->cleanup();
        // Save the layout of the ui here, so run it first
        d->uiController->cleanup();

        if (d->workingSetController)
            d->workingSetController->cleanup();

        /* Must be called before projectController->cleanup(). */
        // Closes all documents (discards, as already saved if the user wished earlier)
        d->documentController->cleanup();
        d->runController->cleanup();

        d->projectController->cleanup();
        d->sourceFormatterController->cleanup();
        d->pluginController->cleanup();
        d->sessionController->cleanup();
        
        //Disable the functionality of the language controller
        d->languageController->cleanup();
    }

    d->m_cleanedUp = true;
}

KComponentData Core::componentData() const
{
    return d->m_componentData;
}

IUiController *Core::uiController()
{
    return d->uiController;
}

ISession* Core::activeSession()
{
    return sessionController()->activeSession();
}

SessionController *Core::sessionController()
{
    return d->sessionController;
}

UiController *Core::uiControllerInternal()
{
    return d->uiController;
}

IPluginController *Core::pluginController()
{
    return d->pluginController;
}

PluginController *Core::pluginControllerInternal()
{
    return d->pluginController;
}

IProjectController *Core::projectController()
{
    return d->projectController;
}

ProjectController *Core::projectControllerInternal()
{
    return d->projectController;
}

IPartController *Core::partController()
{
    return d->partController;
}

PartController *Core::partControllerInternal()
{
    return d->partController;
}

ILanguageController *Core::languageController()
{
    return d->languageController;
}

LanguageController *Core::languageControllerInternal()
{
    return d->languageController;
}

IDocumentController *Core::documentController()
{
    return d->documentController;
}

DocumentController *Core::documentControllerInternal()
{
    return d->documentController;
}

IRunController *Core::runController()
{
    return d->runController;
}

RunController *Core::runControllerInternal()
{
    return d->runController;
}

ISourceFormatterController* Core::sourceFormatterController()
{
    return d->sourceFormatterController;
}

SourceFormatterController* Core::sourceFormatterControllerInternal()
{
    return d->sourceFormatterController;
}


ProgressManager *Core::progressController()
{
    return d->progressController;
}

ISelectionController* Core::selectionController()
{
    return d->selectionController;
}

IDocumentationController* Core::documentationController()
{
    return d->documentationController;
}

DocumentationController* Core::documentationControllerInternal()
{
    return d->documentationController;
}

IDebugController* Core::debugController()
{
    return d->debugController;
}

DebugController* Core::debugControllerInternal()
{
    return d->debugController;
}

WorkingSetController* Core::workingSetControllerInternal()
{
    return d->workingSetController;
}

QString Core::version()
{
    return KDEVPLATFORM_VERSION_STR;
}

}
