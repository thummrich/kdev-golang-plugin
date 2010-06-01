/* This file is part of KDevelop
 *
 * Copyright 1999-2001 John Birch <jbb@kdevelop.org>
 * Copyright 2001 by Bernd Gehrmann <bernd@kdevelop.org>
 * Copyright 2006 Vladimir Prus <ghost@cs.msu.su>
 * Copyright 2007 Hamish Rodda <rodda@kde.org>
 * Copyright 2009 Niko Sams <niko.sams@gmail.com>
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

#include "debugcontroller.h"

#include <QtCore/QMetaEnum>

#include <KDE/KLocale>
#include <KDE/KDebug>
#include <KDE/KActionCollection>
#include <KDE/KAction>
#include <KDE/KParts/Part>
#include <KDE/KParts/PartManager>
#include <KDE/KTextEditor/Document>
#include <KDE/KTextEditor/MarkInterface>

#include "../interfaces/idocument.h"
#include "../interfaces/icore.h"
#include "../interfaces/idocumentcontroller.h"
#include "../interfaces/ipartcontroller.h"
#include "../interfaces/contextmenuextension.h"
#include "../interfaces/context.h"
#include "../language/interfaces/editorcontext.h"
#include "../sublime/view.h"
#include "../sublime/mainwindow.h"
#include "../sublime/area.h"
#include "core.h"
#include "uicontroller.h"
#include "../debugger/breakpoint/breakpointmodel.h"
#include "../debugger/breakpoint/breakpointwidget.h"
#include "../debugger/variable/variablewidget.h"
#include "../debugger/framestack/framestackmodel.h"
#include "../debugger/framestack/framestackwidget.h"


namespace KDevelop {

template<class T>
class DebuggerToolFactory : public KDevelop::IToolViewFactory
{
public:
  DebuggerToolFactory(DebugController* controller, const QString &id, Qt::DockWidgetArea defaultArea)
  : m_controller(controller), m_id(id), m_defaultArea(defaultArea) {}

  virtual QWidget* create(QWidget *parent = 0)
  {
    return new T(m_controller, parent);
  }

  virtual QString id() const
  {
    return m_id;
  }

  virtual Qt::DockWidgetArea defaultPosition()
  {
    return m_defaultArea;
  }

  virtual void viewCreated(Sublime::View* view)
  {
      if (view->widget()->metaObject()->indexOfSignal("requestRaise()") != -1)
          QObject::connect(view->widget(), SIGNAL(requestRaise()), view, SLOT(requestRaise()));
  }

  /* At present, some debugger widgets (e.g. breakpoint) contain actions so that shortcuts
     work, but they don't need any toolbar.  So, suppress toolbar action.  */
  virtual QList<QAction*> toolBarActions( QWidget* viewWidget ) const
  {
      Q_UNUSED(viewWidget);
      return QList<QAction*>();
  }

private:
  DebugController* m_controller;
  QString m_id;
  Qt::DockWidgetArea m_defaultArea;
};

DebugController::DebugController(QObject *parent)
    : IDebugController(parent), KXMLGUIClient(),
      m_breakpointModel(new BreakpointModel(this)),
      m_variableCollection(new VariableCollection(this))
{
    setComponentData(KComponentData("kdevdebugger"));
    setXMLFile("kdevdebuggershellui.rc");

    if((Core::self()->setupFlags() & Core::NoUi)) return;
    setupActions();

    ICore::self()->uiController()->addToolView(
        i18n("Frame Stack"),
        new DebuggerToolFactory<FramestackWidget>(
            this, "org.kdevelop.debugger.StackView",
            Qt::BottomDockWidgetArea));

    ICore::self()->uiController()->addToolView(
        i18n("Breakpoints"),
        new DebuggerToolFactory<BreakpointWidget>(
            this, "org.kdevelop.debugger.BreakpointsView",
            Qt::BottomDockWidgetArea));

    ICore::self()->uiController()->addToolView(
        i18n("Variables"),
        new DebuggerToolFactory<VariableWidget>(
            this, "org.kdevelop.debugger.VariablesView",
            Qt::LeftDockWidgetArea));

    foreach(KParts::Part* p, KDevelop::ICore::self()->partController()->parts())
        partAdded(p);
    connect(KDevelop::ICore::self()->partController(),
            SIGNAL(partAdded(KParts::Part*)),
            this,
            SLOT(partAdded(KParts::Part*)));

}

void DebugController::initialize()
{
    stateChanged("ended");
}


void DebugController::cleanup()
{
    if (m_currentSession) m_currentSession->stopDebugger();
}

DebugController::~DebugController()
{
}

BreakpointModel* DebugController::breakpointModel()
{
    return m_breakpointModel;
}

VariableCollection* DebugController::variableCollection()
{
    return m_variableCollection;
}

void DebugController::partAdded(KParts::Part* part)
{
    if (KTextEditor::Document* doc = dynamic_cast<KTextEditor::Document*>(part)) {
        KTextEditor::MarkInterface *iface = dynamic_cast<KTextEditor::MarkInterface*>(doc);
        if( !iface )
            return;
        
        iface->setMarkPixmap(KTextEditor::MarkInterface::Execution, *executionPointPixmap());
    }
}

IDebugSession* DebugController::currentSession()
{
    return m_currentSession;
}

void DebugController::setupActions()
{
    KActionCollection* ac = actionCollection();

    KAction* action = m_continueDebugger = new KAction(KIcon("media-playback-start"), i18n("&Continue"), this);
    action->setToolTip( i18n("Continues the application execution") );
    action->setWhatsThis( i18n("<b>Continue application execution</b><p>"
        "Continues the execution of your application in the "
        "debugger. This only takes effect when the application "
        "has been halted by the debugger (i.e. a breakpoint has "
        "been activated or the interrupt was pressed).</p>") );
    ac->addAction("debug_continue", action);
    connect(action, SIGNAL(triggered(bool)), this, SLOT(run()));

    #if 0
    m_restartDebugger = action = new KAction(KIcon("media-seek-backward"), i18n("&Restart"), this);
    action->setToolTip( i18n("Restart program") );
    action->setWhatsThis( i18n("<b>Restarts application</b><p>"
                               "Restarts applications from the beginning.</p>"
                              ) );
    action->setEnabled(false);
    connect(action, SIGNAL(triggered(bool)), this, SLOT(restartDebugger()));
    ac->addAction("debug_restart", action);
    #endif

    m_interruptDebugger = action = new KAction(KIcon("media-playback-pause"), i18n("Interrupt"), this);
    action->setToolTip( i18n("Interrupt application") );
    action->setWhatsThis(i18n("<b>Interrupt application</b><p>Interrupts the debugged process or current debugger command.</p>"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(interruptDebugger()));
    ac->addAction("debug_pause", action);

    m_runToCursor = action = new KAction(KIcon("debug-run-cursor"), i18n("Run to &Cursor"), this);
    action->setToolTip( i18n("Run to cursor") );
    action->setWhatsThis(i18n("<b>Run to cursor</b><p>Continues execution until the cursor position is reached.</p>"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(runToCursor()));
    ac->addAction("debug_runtocursor", action);


    m_jumpToCursor = action = new KAction(KIcon("debug-execute-to-cursor"), i18n("Set E&xecution Position to Cursor"), this);
    action->setToolTip( i18n("Jump to cursor") );
    action->setWhatsThis(i18n("<b>Set Execution Position </b><p>Set the execution pointer to the current cursor position.</p>"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(jumpToCursor()));
    ac->addAction("debug_jumptocursor", action);

    m_stepOver = action = new KAction(KIcon("debug-step-over"), i18n("Step &Over"), this);
    action->setShortcut(Qt::Key_F10);
    action->setToolTip( i18n("Step over the next line") );
    action->setWhatsThis( i18n("<b>Step over</b><p>"
                               "Executes one line of source in the current source file. "
                               "If the source line is a call to a function the whole "
                               "function is executed and the app will stop at the line "
                               "following the function call.</p>") );
    connect(action, SIGNAL(triggered(bool)), this, SLOT(stepOver()));
    ac->addAction("debug_stepover", action);


    m_stepOverInstruction = action = new KAction(KIcon("debug-step-instruction"), i18n("Step over Ins&truction"), this);
    action->setToolTip( i18n("Step over instruction") );
    action->setWhatsThis(i18n("<b>Step over instruction</b><p>Steps over the next assembly instruction.</p>"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(stepIntoInstruction()));
    ac->addAction("debug_stepoverinst", action);


    m_stepInto = action = new KAction(KIcon("debug-step-into"), i18n("Step &Into"), this);
    action->setShortcut(Qt::Key_F11);
    action->setToolTip( i18n("Step into the next statement") );
    action->setWhatsThis( i18n("<b>Step into</b><p>"
                               "Executes exactly one line of source. If the source line "
                               "is a call to a function then execution will stop after "
                               "the function has been entered.</p>") );
    connect(action, SIGNAL(triggered(bool)), this, SLOT(stepInto()));
    ac->addAction("debug_stepinto", action);


    m_stepIntoInstruction = action = new KAction(KIcon("debug-step-into-instruction"), i18n("Step into I&nstruction"), this);
    action->setToolTip( i18n("Step into instruction") );
    action->setWhatsThis(i18n("<b>Step into instruction</b><p>Steps into the next assembly instruction.</p>"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(stepOverInstruction()));
    ac->addAction("debug_stepintoinst", action);

    m_stepOut = action = new KAction(KIcon("debug-step-out"), i18n("Step O&ut"), this);
    action->setShortcut(Qt::Key_F12);
    action->setToolTip( i18n("Steps out of the current function") );
    action->setWhatsThis( i18n("<b>Step out</b><p>"
                               "Executes the application until the currently executing "
                               "function is completed. The debugger will then display "
                               "the line after the original call to that function. If "
                               "program execution is in the outermost frame (i.e. in "
                               "main()) then this operation has no effect.</p>") );
    connect(action, SIGNAL(triggered(bool)), this, SLOT(stepOut()));
    ac->addAction("debug_stepout", action);

    m_toggleBreakpoint = action = new KAction(i18n("Toggle Breakpoint"), this);
    action->setToolTip(i18n("Toggle breakpoint"));
    action->setWhatsThis(i18n("<b>Toggle breakpoint</b><p>Toggles the breakpoint at the current line in editor.</p>"));
    connect(action, SIGNAL(triggered(bool)), this, SLOT(toggleBreakpoint()));
    ac->addAction("debug_toggle_breakpoint", action);
}

void DebugController::addSession(IDebugSession* session)
{
    kDebug() << session;
    Q_ASSERT(session->variableController());
    Q_ASSERT(session->breakpointController());
    Q_ASSERT(session->frameStackModel());

    //TODO support multiple sessions
    if (m_currentSession) {
        m_currentSession->stopDebugger();
    }
    m_currentSession = session;
        
    connect(session, SIGNAL(stateChanged(KDevelop::IDebugSession::DebuggerState)), SLOT(debuggerStateChanged(KDevelop::IDebugSession::DebuggerState)));
    connect(session, SIGNAL(showStepInSource(KUrl,int)), SLOT(showStepInSource(KUrl,int)));
    connect(session, SIGNAL(clearExecutionPoint()), SLOT(clearExecutionPoint()));
    connect(session, SIGNAL(raiseFramestackViews()), SIGNAL(raiseFramestackViews()));
    
    updateDebuggerState(session->state(), session);
        
    emit currentSessionChanged(session);
    
    if((Core::self()->setupFlags() & Core::NoUi)) return;


    Sublime::MainWindow* mainWindow = Core::self()->uiControllerInternal()->activeSublimeWindow();
    if (mainWindow->area()->objectName() != "debug") {
        QString workingSet = mainWindow->area()->workingSet();
        ICore::self()->uiController()->switchToArea("debug", IUiController::ThisWindow);
        mainWindow->area()->setWorkingSet(workingSet);
    }
}

void DebugController::clearExecutionPoint()
{
    kDebug();
    foreach (KDevelop::IDocument* document, KDevelop::ICore::self()->documentController()->openDocuments()) {
        KTextEditor::MarkInterface *iface = dynamic_cast<KTextEditor::MarkInterface*>(document->textDocument());
        if (!iface)
            continue;

        QHashIterator<int, KTextEditor::Mark*> it = iface->marks();
        while (it.hasNext())
        {
            KTextEditor::Mark* mark = it.next().value();
            if( mark->type & KTextEditor::MarkInterface::Execution )
                iface->removeMark( mark->line, KTextEditor::MarkInterface::Execution );
        }
    }
}

void DebugController::showStepInSource(const KUrl &url, int lineNum)
{
    if((Core::self()->setupFlags() & Core::NoUi)) return;

    clearExecutionPoint();
    kDebug() << url << lineNum;

    Q_ASSERT(dynamic_cast<IDebugSession*>(sender()));
    QPair<KUrl,int> openUrl = static_cast<IDebugSession*>(sender())->convertToLocalUrl(qMakePair<KUrl,int>( url, lineNum ));
    KDevelop::IDocument* document = KDevelop::ICore::self()
        ->documentController()
        ->openDocument(openUrl.first, KTextEditor::Cursor(openUrl.second, 0));

    if( !document )
        return;

    KTextEditor::MarkInterface *iface = dynamic_cast<KTextEditor::MarkInterface*>(document->textDocument());
    if( !iface )
        return;

    document->textDocument()->blockSignals(true);
    iface->addMark( lineNum, KTextEditor::MarkInterface::Execution );
    document->textDocument()->blockSignals(false);
}


void DebugController::debuggerStateChanged(KDevelop::IDebugSession::DebuggerState state)
{
    Q_ASSERT(dynamic_cast<IDebugSession*>(sender()));
    IDebugSession* session = static_cast<IDebugSession*>(sender());
    kDebug() << session << state << "current" << m_currentSession;
    if (session == m_currentSession) {
        updateDebuggerState(state, session);
    }

    if (state == IDebugSession::EndedState) {
        if (session == m_currentSession) {
            m_currentSession = 0;
            emit currentSessionChanged(0);
            Sublime::MainWindow* mainWindow = Core::self()->uiControllerInternal()->activeSublimeWindow();
            if (mainWindow && mainWindow->area()->objectName() != "code") {
                QString workingSet = mainWindow->area()->workingSet();
                ICore::self()->uiController()->switchToArea("code", IUiController::ThisWindow);
                mainWindow->area()->setWorkingSet(workingSet);
            }
            ICore::self()->uiController()->findToolView(i18n("Debug"), 0, IUiController::Raise);
        }
        session->deleteLater();
    }
}

void DebugController::updateDebuggerState(IDebugSession::DebuggerState state, IDebugSession *session)
{
    Q_UNUSED(session);
    if((Core::self()->setupFlags() & Core::NoUi)) return;

    kDebug() << state;
    switch (state) {
        case IDebugSession::StoppedState:
        case IDebugSession::NotStartedState:
        case IDebugSession::StoppingState:
            kDebug() << "new state: stopped";
            stateChanged("stopped");
            clearExecutionPoint();
            //m_restartDebugger->setEnabled(session->restartAvaliable());
            break;
        case IDebugSession::StartingState:
        case IDebugSession::PausedState:
            kDebug() << "new state: paused";
            stateChanged("paused");
            //m_restartDebugger->setEnabled(session->restartAvaliable());
            break;
        case IDebugSession::ActiveState:
            kDebug() << "new state: active";
            stateChanged("active");
            //m_restartDebugger->setEnabled(false);
            clearExecutionPoint();
            break;
        case IDebugSession::EndedState:
            kDebug() << "new state: ended";
            stateChanged("ended");
            clearExecutionPoint();
            //m_restartDebugger->setEnabled(false);
            break;
    }
    if (state == IDebugSession::PausedState) {
        ICore::self()->uiController()->activeMainWindow()->activateWindow();
    }
}

ContextMenuExtension DebugController::contextMenuExtension( Context* context )
{
    ContextMenuExtension menuExt;

    if( context->type() != Context::EditorContext )
        return menuExt;

    KDevelop::EditorContext *econtext = dynamic_cast<KDevelop::EditorContext*>(context);
    if (!econtext)
        return menuExt;

    if (m_currentSession && m_currentSession->isRunning()) {
        menuExt.addAction( KDevelop::ContextMenuExtension::DebugGroup, m_runToCursor);
    }

    if (econtext->url().isLocalFile()) {
        menuExt.addAction( KDevelop::ContextMenuExtension::DebugGroup, m_toggleBreakpoint);
    }
    return menuExt;
}

#if 0
void DebugController::restartDebugger() {
    if (m_currentSession) {
        m_currentSession->restartDebugger();
    }
}
#endif

void DebugController::stopDebugger() {
    if (m_currentSession) {
        m_currentSession->stopDebugger();
    }
}
void DebugController::interruptDebugger() {
    if (m_currentSession) {
        m_currentSession->interruptDebugger();
    }
}

void DebugController::run() {
    if (m_currentSession) {
        m_currentSession->run();
    }
}

void DebugController::runToCursor() {
    if (m_currentSession) {
        m_currentSession->runToCursor();
    }
}
void DebugController::jumpToCursor() {
    if (m_currentSession) {
        m_currentSession->jumpToCursor();
    }
}
void DebugController::stepOver() {
    if (m_currentSession) {
        m_currentSession->stepOver();
    }
}
void DebugController::stepIntoInstruction() {
    if (m_currentSession) {
        m_currentSession->stepIntoInstruction();
    }
}
void DebugController::stepInto() {
    if (m_currentSession) {
        m_currentSession->stepInto();
    }
}
void DebugController::stepOverInstruction() {
    if (m_currentSession) {
        m_currentSession->stepOverInstruction();
    }
}
void DebugController::stepOut() {
    if (m_currentSession) {
        m_currentSession->stepOut();
    }
}

void DebugController::toggleBreakpoint()
{
    if (KDevelop::IDocument* document = KDevelop::ICore::self()->documentController()->activeDocument()) {
        KTextEditor::Cursor cursor = document->cursorPosition();
        if (!cursor.isValid()) return;
        breakpointModel()->toggleBreakpoint(document->url(), cursor);
    }
}

const QPixmap* DebugController::executionPointPixmap()
{
  static QPixmap pixmap=KIcon("go-next").pixmap(QSize(22,22), QIcon::Normal, QIcon::Off);
  return &pixmap;
}

}
