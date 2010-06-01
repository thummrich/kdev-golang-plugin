/*
 * GDB Debugger Support
 *
 * Copyright 1999-2001 John Birch <jbb@kdevelop.org>
 * Copyright 2001 by Bernd Gehrmann <bernd@kdevelop.org>
 * Copyright 2006 Vladimir Prus <ghost@cs.msu.su>
 * Copyright 2007 Hamish Rodda <rodda@kde.org>
 * Copyright 2009 Niko Sams <niko.sams@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "debugsession.h"

#include <typeinfo>

#include <QtCore/QFileInfo>
#include <QtGui/QApplication>

#include <KMessageBox>
#include <KLocalizedString>
#include <KToolBar>
#include <KParts/MainWindow>
#include <KSharedConfig>
#include <KStandardDirs>
#include <KShell>

#include <interfaces/idocument.h>
#include <interfaces/icore.h>
#include <interfaces/iuicontroller.h>
#include <interfaces/idocumentcontroller.h>
#include <util/processlinemaker.h>
#include <util/environmentgrouplist.h>
#include <execute/iexecuteplugin.h>
#include <interfaces/ilaunchconfiguration.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/idebugcontroller.h>
#include <debugger/breakpoint/breakpointmodel.h>

#include "breakpointcontroller.h"
#include "variablecontroller.h"
#include "gdb.h"
#include "gdbcommandqueue.h"
#include "stty.h"
#include "gdbframestackmodel.h"

using namespace KDevelop;

namespace GDBDebugger {

DebugSession::DebugSession()
    : KDevelop::IDebugSession(),
      m_sessionState(NotStartedState),
      justRestarted_(false),
      m_config(KGlobal::config(), "GDB Debugger"),
      commandQueue_(new CommandQueue),
      tty_(0),
      state_(s_dbgNotStarted|s_appNotStarted),
      programHasExited_(false),
      state_reload_needed(false),
      stateReloadInProgress_(false)
{
    configure();

    // FIXME: this poking at parent's member variable is bad.
    // Introduce functions to set them?
    m_breakpointController = new BreakpointController(this);
    m_variableController = new VariableController(this);

    m_procLineMaker = new KDevelop::ProcessLineMaker(this);

    connect( m_procLineMaker, SIGNAL(receivedStdoutLines(const QStringList&)),
             this, SIGNAL(applicationStandardOutputLines(const QStringList&)));
    connect( m_procLineMaker, SIGNAL(receivedStderrLines(const QStringList&)),
             this, SIGNAL(applicationStandardErrorLines(const QStringList&)) );
    setupController();
}

// Deleting the session involves shutting down gdb nicely.
// When were attached to a process, we must first detach so that the process
// can continue running as it was before being attached. gdb is quite slow to
// detach from a process, so we must process events within here to get a "clean"
// shutdown.
DebugSession::~DebugSession()
{
    kDebug();

    if (!stateIsOn(s_dbgNotStarted)) {
        stopDebugger();
        // This currently isn't working, so comment out until it can be resolved - at the moment it just causes a delay on stopping kdevelop
        //m_process->waitForFinished();
    }

    delete commandQueue_;
}

void DebugSession::emitShowStepInSource(const QString& file, int line, const QString& address)
{
    kDebug() << file << line << address;
    emit gdbShowStepInSource(file, line, address);
    if (!file.isEmpty()) {
        // Debugger counts lines from 1
        emit showStepInSource(KUrl::fromPath(file), line-1);
    } else {
        emit clearExecutionPoint();
    }
}

KDevelop::IDebugSession::DebuggerState DebugSession::state() const {
    return m_sessionState;
}

#define ENUM_NAME(o,e,v) (o::staticMetaObject.enumerator(o::staticMetaObject.indexOfEnumerator(#e)).valueToKey((v)))
void DebugSession::setSessionState(DebuggerState state)
{
    kDebug() << "STATE CHANGED" << this << state << ENUM_NAME(IDebugSession, DebuggerState, state);
    if (state != m_sessionState) {
        m_sessionState = state;
        emit stateChanged(state);
    }
}

KDevelop::IFrameStackModel* DebugSession::createFrameStackModel()
{
    return new GdbFrameStackModel(this);
}

void DebugSession::setupController()
{
    // variableTree -> gdbBreakpointWidget
//     connect( variableTree,          SIGNAL(toggleWatchpoint(const QString &)),
//              gdbBreakpointWidget,   SLOT(slotToggleWatchpoint(const QString &)));

    // controller -> procLineMaker
    connect( this,            SIGNAL(ttyStdout(const QByteArray&)),
             m_procLineMaker,         SLOT(slotReceivedStdout(const QByteArray&)));
    connect( this,            SIGNAL(ttyStderr(const QByteArray&)),
             m_procLineMaker,         SLOT(slotReceivedStderr(const QByteArray&)));

//     connect(statusBarIndicator, SIGNAL(doubleClicked()),
//             controller, SLOT(explainDebuggerStatus()));

    // TODO: reimplement / re-enable
    //connect(this, SIGNAL(addWatchVariable(const QString&)), controller->variables(), SLOT(slotAddWatchVariable(const QString&)));
    //connect(this, SIGNAL(evaluateExpression(const QString&)), controller->variables(), SLOT(slotEvaluateExpression(const QString&)));
}

void DebugSession::_gdbStateChanged(DBGStateFlags oldState, DBGStateFlags newState)
{
    QString message;

    DBGStateFlags changedState = oldState ^ newState;

    if (changedState & s_dbgNotStarted) {
        if (newState & s_dbgNotStarted) {
            message = i18n("Debugger stopped");

        } else {
            setSessionState(StartingState);
        }

        //core()->running(this, false);
        // TODO enable/disable tool views as applicable
    }

    // As soon as debugger clears 's_appNotStarted' flag, we
    // set 'justRestarted' variable.
    // The other approach would be to set justRestarted in slotRun, slotCore
    // and slotAttach.
    // Note that setting this var in startDebugger is not OK, because the
    // initial state of debugger is exactly the same as state after pause,
    // so we'll always show varaibles view.
    if (changedState & s_appNotStarted) {
        if (newState & s_appNotStarted) {
            setSessionState(StoppedState);
            justRestarted_ = false;

        } else {
            justRestarted_ = true;
        }
    }

    if (changedState & s_explicitBreakInto)
        if (!(newState & s_explicitBreakInto))
            message = i18n("Application interrupted");

    if (changedState & s_programExited) {
        if (newState & s_programExited) {
            message = i18n("Process exited");
            setSessionState(StoppedState);
        }
    }

    if (changedState & s_appRunning) {
        if (newState & s_appRunning) {
            message = i18n("Application is running");
            setSessionState(ActiveState);
        }
        else
        {
            if (!(newState & s_appNotStarted)) {
                message = i18n("Application is paused");
                setSessionState(PausedState);

                // On the first stop, show the variables view.
                // We do it on first stop, and not at debugger start, because
                // a program might just successfully run till completion. If we show
                // the var views on start and hide on stop, this will look like flicker.
                // On the other hand, if application is paused, it's very
                // likely that the user wants to see variables.
                if (justRestarted_)
                {
                    justRestarted_ = false;
                    //mainWindow()->setViewAvailable(variableWidget, true);
                    emit raiseVariableViews();
                }
            }
        }
    }

    // And now? :-)
    kDebug(9012) << "Debugger state: " << newState << ": ";
    kDebug(9012) << "   " << message;

    if (!message.isEmpty())
        emit showMessage(message, 3000);

    if (!(oldState & s_dbgNotStarted) && (newState & s_dbgNotStarted))
    {
        emit finished();
        setSessionState(EndedState); //this will delete the DebugSession, so do it last
    }

    emit gdbStateChanged(oldState, newState);
}

void DebugSession::examineCoreFile(const KUrl& debugee, const KUrl& coreFile)
{
    setStateOff(s_programExited|s_appNotStarted);
    setStateOn(s_core);

    if (stateIsOn(s_dbgNotStarted))
      startDebugger(0);

    // TODO support non-local URLs
    queueCmd(new GDBCommand(GDBMI::FileExecAndSymbols, debugee.toLocalFile()));
    queueCmd(new GDBCommand(GDBMI::NonMI, "core " + coreFile.toLocalFile()));

    raiseEvent(connected_to_program);
    raiseEvent(program_state_changed);
}

void DebugSession::attachToProcess(int pid)
{
    kDebug() << pid;

    setStateOff(s_appNotStarted|s_programExited);
    setStateOn(s_attached);

    //set current state to running, after attaching we will get *stopped response
    setStateOn(s_appRunning);

    if (stateIsOn(s_dbgNotStarted))
      startDebugger(0);

    // Currently, we always start debugger with a name of binary,
    // we might be connecting to a different binary completely,
    // so cancel all symbol tables gdb has.
    // We can't omit application name from gdb invocation
    // because for libtool binaries, we have no way to guess
    // real binary name.
    queueCmd(new GDBCommand(GDBMI::FileExecAndSymbols));

    queueCmd(new GDBCommand(GDBMI::TargetAttach, pid, this, &DebugSession::handleTargetAttach, true));

    raiseEvent(connected_to_program);

    emit raiseFramestackViews();
}

void DebugSession::run()
{
    if (stateIsOn(s_appNotStarted|s_dbgNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecContinue));
}

void DebugSession::stepOut()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecFinish));
}

void DebugSession::restartDebugger()
{
    // We implement restart as kill + slotRun, as opposed as plain "run"
    // command because kill + slotRun allows any special logic in slotRun
    // to apply for restart.
    //
    // That includes:
    // - checking for out-of-date project
    // - special setup for remote debugging.
    //
    // Had we used plain 'run' command, restart for remote debugging simply
    // would not work.
    slotKill();
    run();
}

void DebugSession::stopDebugger()
{
    commandQueue_->clear();

    kDebug(9012) << "DebugSession::slotStopDebugger() called";
    if (stateIsOn(s_shuttingDown) || !m_gdb)
        return;

    setStateOn(s_shuttingDown);
    kDebug(9012) << "DebugSession::slotStopDebugger() executing";

    // Get gdb's attention if it's busy. We need gdb to be at the
    // command line so we can stop it.
    if (!m_gdb->isReady())
    {
        kDebug(9012) << "gdb busy on shutdown - interruping";
        m_gdb->interrupt();
    }

    // If the app is attached then we release it here. This doesn't stop
    // the app running.
    if (stateIsOn(s_attached))
    {
        queueCmd(new GDBCommand(GDBMI::TargetDetach));
        emit gdbUserCommandStdout("(gdb) detach\n");
    }

    // Now try to stop gdb running.
    queueCmd(new GDBCommand(GDBMI::GdbExit));
    emit gdbUserCommandStdout("(gdb) quit");

    // We cannot wait forever, kill gdb after 5 seconds if it's not yet quit
    QTimer::singleShot(5000, this, SLOT(slotKillGdb()));

    emit reset();
}

// Pausing an app removes any pending run commands so that the app doesn't
// start again. If we want to be silent then we remove any pending info
// commands as well.
void DebugSession::interruptDebugger()
{
    Q_ASSERT(m_gdb);

    setStateOn(s_explicitBreakInto);

    m_gdb->interrupt();
}

void DebugSession::runToCursor()
{
    if (KDevelop::IDocument* doc = KDevelop::ICore::self()->documentController()->activeDocument()) {
        KTextEditor::Cursor cursor = doc->cursorPosition();
        if (cursor.isValid())
            runUntil(doc->url().path(), cursor.line() + 1);
    }
}

void DebugSession::jumpToCursor()
{
    if (KDevelop::IDocument* doc = KDevelop::ICore::self()->documentController()->activeDocument()) {
        KTextEditor::Cursor cursor = doc->cursorPosition();
        if (cursor.isValid())
            jumpTo(doc->url().path(), cursor.line() + 1);
    }
}

void DebugSession::stepOver()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecNext));
}

void DebugSession::stepOverInstruction()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecNextInstruction));
}

void DebugSession::stepInto()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecStep));
}

void DebugSession::stepIntoInstruction()
{
    if (stateIsOn(s_appNotStarted|s_shuttingDown))
        return;

    queueCmd(new GDBCommand(GDBMI::ExecStepInstruction));
}

void DebugSession::slotDebuggerAbnormalExit()
{
    emit raiseOutputViews();

    KMessageBox::information(
        KDevelop::ICore::self()->uiController()->activeMainWindow(),
        i18n("<b>GDB exited abnormally</b>"
             "<p>This is likely a bug in GDB. "
             "Examine the gdb output window and then stop the debugger"),
        i18n("GDB exited abnormally"));

    // Note: we don't stop the debugger here, becuse that will hide gdb
    // window and prevent the user from finding the exact reason of the
    // problem.
}

bool DebugSession::restartAvaliable() const
{
    if (stateIsOn(s_attached) || stateIsOn(s_core)) {
        return false;
    } else {
        return true;
    }
}
void DebugSession::configure()
{
//     KConfigGroup config(KGlobal::config(), "GDB Debugger");
//
//     // A a configure.gdb script will prevent these from uncontrolled growth...
//     config_configGdbScript_       = config.readEntry("Remote GDB Configure Script", "");
//     config_runShellScript_        = config.readEntry("Remote GDB Shell Script", "");
//     config_runGdbScript_          = config.readEntry("Remote GDB Run Script", "");
//
//     // PORTING TODO: where is this in the ui?
//     config_forceBPSet_            = config.readEntry("Allow Forced Breakpoint Set", true);
//
//     config_dbgTerminal_           = config.readEntry("Separate Terminal For Application IO", false);
//
//     bool old_displayStatic        = config_displayStaticMembers_;
//     config_displayStaticMembers_  = config.readEntry("Display Static Members",false);
//
//     bool old_asmDemangle  = config_asmDemangle_;
//     config_asmDemangle_   = config.readEntry("Display Demangle Names",true);
//
//     bool old_breakOnLoadingLibrary_ = config_breakOnLoadingLibrary_;
//     config_breakOnLoadingLibrary_ = config.readEntry("Try Setting Breakpoints On Loading Libraries",true);
//
//     // FIXME: should move this into debugger part or variable widget.
//     int old_outputRadix  = config_outputRadix_;
// #if 0
//     config_outputRadix_   = DomUtil::readIntEntry("Output Radix", 10);
//     varTree_->setRadix(config_outputRadix_);
// #endif
//
//
//     if (( old_displayStatic             != config_displayStaticMembers_   ||
//             old_asmDemangle             != config_asmDemangle_            ||
//             old_breakOnLoadingLibrary_  != config_breakOnLoadingLibrary_  ||
//             old_outputRadix             != config_outputRadix_)           &&
//             m_gdb)
//     {
//         bool restart = false;
//         if (stateIsOn(s_dbgBusy))
//         {
//             slotPauseApp();
//             restart = true;
//         }
//
//         if (old_displayStatic != config_displayStaticMembers_)
//         {
//             if (config_displayStaticMembers_)
//                 queueCmd(new GDBCommand(GDBMI::GdbSet, "print static-members on"));
//             else
//                 queueCmd(new GDBCommand(GDBMI::GdbSet, "print static-members off"));
//         }
//         if (old_asmDemangle != config_asmDemangle_)
//         {
//             if (config_asmDemangle_)
//                 queueCmd(new GDBCommand(GDBMI::GdbSet, "print asm-demangle on"));
//             else
//                 queueCmd(new GDBCommand(GDBMI::GdbSet, "print asm-demangle off"));
//         }
//
//         // Disabled for MI port.
//         if (old_outputRadix != config_outputRadix_)
//         {
//             queueCmd(new GDBCommand(GDBMI::GdbSet, QString().sprintf("output-radix %d",
//                                 config_outputRadix_)));
//
//             // FIXME: should do this in variable widget anyway.
//             // After changing output radix, need to refresh variables view.
//             raiseEvent(program_state_changed);
//
//         }
//
//         if (config_configGdbScript_.isValid())
//           queueCmd(new GDBCommand(GDBMI::NonMI, "source " + config_configGdbScript_.toLocalFile()));
//
//
//         if (restart)
//             queueCmd(new GDBCommand(GDBMI::ExecContinue));
//     }
}

// **************************************************************************

void DebugSession::addCommand(GDBCommand* cmd)
{
    queueCmd(cmd);
}

void DebugSession::addCommand(GDBMI::CommandType type, const QString& str)
{
    queueCmd(new GDBCommand(type, str));
}

void DebugSession::addCommandToFront(GDBCommand* cmd)
{
    queueCmd(cmd, QueueAtFront);
}

void DebugSession::addCommandBeforeRun(GDBCommand* cmd)
{
    queueCmd(cmd, QueueWhileInterrupted);
}

// Fairly obvious that we'll add whatever command you give me to a queue
// If you tell me to, I'll put it at the head of the queue so it'll run ASAP
// Not quite so obvious though is that if we are going to run again. then any
// information requests become redundent and must be removed.
// We also try and run whatever command happens to be at the head of
// the queue.
void DebugSession::queueCmd(GDBCommand *cmd, QueuePosition queue_where)
{
    if (stateIsOn(s_dbgNotStarted))
    {
        KMessageBox::information(
            qApp->activeWindow(),
            i18n("<b>Gdb command sent when debugger is not running</b><br>"
            "The command was:<br> %1", cmd->initialString()),
            i18n("Internal error"));
        return;
    }

    if (stateReloadInProgress_)
        cmd->setStateReloading(true);

    bool varCommandWithContext= (cmd->type() >= GDBMI::VarAssign
                                 && cmd->type() <= GDBMI::VarUpdate
                                 && cmd->type() != GDBMI::VarDelete);

    bool stackCommandWithContext = (cmd->type() >= GDBMI::StackInfoDepth
                                    && cmd->type() <= GDBMI::StackListLocals);

    if (varCommandWithContext || stackCommandWithContext)
    {
        // Most var commands should be executed in the context
        // of the selected thread and frame.
        if (cmd->thread() == -1)
            cmd->setThread(frameStackModel()->currentThread());

        if (cmd->frame() == -1)
            cmd->setFrame(frameStackModel()->currentFrame());
    }

    commandQueue_->enqueue(cmd, queue_where);

    kDebug(9012) << "QUEUE: " << cmd->initialString()
                  << (stateReloadInProgress_ ? "(state reloading)" : "");

    setStateOn(s_dbgBusy);
    raiseEvent(debugger_busy);

    executeCmd();
}

bool DebugSession::executeCmd()
{
    Q_ASSERT(m_gdb);

    if (!m_gdb->isReady())
        return false;

    GDBCommand* currentCmd = commandQueue_->nextCommand();
    if (!currentCmd)
        return false;

    QString commandText = currentCmd->cmdToSend();
    bool bad_command = false;
    QString message;

    int length = commandText.length();
    // No i18n for message since it's mainly for debugging.
    if (length == 0)
    {
        // The command might decide it's no longer necessary to send
        // it.
        if (SentinelCommand* sc = dynamic_cast<SentinelCommand*>(currentCmd))
        {
            kDebug(9012) << "SEND: sentinel command, not sending";
            sc->invokeHandler();
        }
        else
        {
            kDebug(9012) << "SEND: command " << currentCmd->initialString()
                          << "changed its mind, not sending";
        }

        delete currentCmd;
        return executeCmd();
    }
    else
    {
        if (commandText[length-1] != '\n')
        {
            bad_command = true;
            message = "Debugger command does not end with newline";
        }
    }
    if (bad_command)
    {
        KMessageBox::information(qApp->activeWindow(),
                                 i18n("<b>Invalid debugger command</b><br>%1", message),
                                 i18n("Invalid debugger command"));
        return executeCmd();
    }

    m_gdb->execute(currentCmd);
    return true;
}

// **************************************************************************

void DebugSession::destroyCmds()
{
    commandQueue_->clear();
}


void DebugSession::slotProgramStopped(const GDBMI::ResultRecord& r)
{
    /* By default, reload all state on program stop.  */
    state_reload_needed = true;
    setStateOff(s_appRunning);

    QString reason;
    if (r.hasField("reason")) reason = r["reason"].literal();

    if (reason == "exited-normally" || reason == "exited")
    {
        programNoApp(i18n("Exited normally"));
        programHasExited_ = true;
        state_reload_needed = false;
        return;
    }

    if (reason == "exited-signalled")
    {
        programNoApp(i18n("Exited on signal %1", r["signal-name"].literal()));
        // FIXME: figure out why this variable is needed.
        programHasExited_ = true;
        state_reload_needed = false;
        return;
    }

    if (reason == "watchpoint-scope")
    {
        QString number = r["wpnum"].literal();

        // FIXME: shuld remove this watchpoint
        // But first, we should consider if removing all
        // watchpoinst on program exit is the right thing to
        // do.

        queueCmd(new GDBCommand(GDBMI::ExecContinue));

        state_reload_needed = false;
        return;
    }

    if (reason == "signal-received")
    {
        QString name = r["signal-name"].literal();
        QString user_name = r["signal-meaning"].literal();

        if (name == "SIGSEGV") {
            setStateOn(s_appNotStarted|s_programExited);
        }

        // SIGINT is a "break into running program".
        // We do this when the user set/mod/clears a breakpoint but the
        // application is running.
        // And the user does this to stop the program also.
        bool suppress_reporting = false;
        if (name == "SIGINT" && stateIsOn(s_explicitBreakInto))
        {
            suppress_reporting = true;
            // TODO: check that we do something reasonable on
            // implicit break into program (for setting breakpoints,
            // or whatever).

            setStateOff(s_explicitBreakInto);
            // Will show the source line in the code
            // handling non-special stop kinds, below.
        }

        if (!suppress_reporting)
        {
            // Whenever we have a signal raised then tell the user, but don't
            // end the program as we want to allow the user to look at why the
            // program has a signal that's caused the prog to stop.
            // Continuing from SIG FPE/SEGV will cause a "Cannot ..." and
            // that'll end the program.
            programFinished(i18n("Program received signal %1 (%2)", name, user_name));
        }
    }

    if (!reason.contains("exited"))
    {
        // FIXME: we should immediately update the current thread and
        // frame in the framestackmodel, so that any user actions
        // are in that thread. However, the way current framestack model
        // is implemented, we can't change thread id until we refresh
        // the entire list of threads -- otherwise we might set a thread
        // id that is not already in the list, and it will be upset.
        
        if (r.hasField("frame")) {
            const GDBMI::Value& frame = r["frame"];
            if (frame.hasField("fullname")
                && frame.hasField("line")
                && frame.hasField("addr")) {
                emitShowStepInSource(frame["fullname"].literal(),
                     frame["line"].literal().toInt(),
                     frame["addr"].literal());

                raiseEvent(program_state_changed);
                state_reload_needed = false;
            }
        }
    }
}


void DebugSession::reloadProgramState()
{
    raiseEvent(program_state_changed);
    state_reload_needed = false;
}


// **************************************************************************

// There is no app anymore. This can be caused by program exiting
// an invalid program specified or ...
// gdb is still running though, but only the run command (may) make sense
// all other commands are disabled.
void DebugSession::programNoApp(const QString& msg)
{
    kDebug() << msg;

    setState(s_appNotStarted|s_programExited|(state_&s_shuttingDown));

    destroyCmds();

    // The application has existed, but it's possible that
    // some of application output is still in the pipe. We use
    // different pipes to communicate with gdb and to get application
    // output, so "exited" message from gdb might have arrived before
    // last application output. Get this last bit.

    // Note: this method can be called when we open an invalid
    // core file. In that case, tty_ won't be set.
    if (tty_)
        tty_->readRemaining();

    // Tty is no longer usable, delete it. Without this, QSocketNotifier
    // will continiously bomd STTY with signals, so we need to either disable
    // QSocketNotifier, or delete STTY. The latter is simpler, since we can't
    // reuse it for future debug sessions anyway.

    delete tty_;
    tty_ = 0;

    m_gdb->kill();
    m_gdb->deleteLater();

    setStateOn(s_dbgNotStarted);

    raiseEvent(program_exited);

    raiseEvent(debugger_exited);

    emit showMessage(msg, 0);

    programFinished(msg);
}


void DebugSession::programFinished(const QString& msg)
{
    QString m = QString("*** %0 ***").arg(msg);
    emit applicationStandardErrorLines(QStringList(m));

    /* Also show message in gdb window, so that users who
       prefer to look at gdb window know what's up.  */
    emit gdbUserCommandStdout(m);
}


void DebugSession::parseStreamRecord(const GDBMI::StreamRecord& s)
{
    if (s.reason == '~')
    {
        QString line = s.message;
        if (line.startsWith("Program terminated")) {
            //when examining core file
            setStateOff(s_appRunning);
            setStateOn(s_appNotStarted|s_programExited);
        } else if (line.startsWith("The program no longer exists")
            || line.startsWith("Program exited"))
        {
            programNoApp(line);
        }
    }
}

bool DebugSession::startDebugger(KDevelop::ILaunchConfiguration* cfg)
{
    kDebug(9012) << "Starting debugger controller";

    if(m_gdb) {
        kWarning() << "m_gdb object still existed";
        delete m_gdb;
        m_gdb = 0;
    }

    m_gdb = new GDB;

    // FIXME: here, we should wait until GDB is up and waiting for input.
    // Then, clear s_dbgNotStarted
    // It's better to do this right away so that the state bit is always
    // correct.

    /** FIXME: connect ttyStdout. It takes QByteArray, so
        I'm not sure what to do.  */
#if 0
    connect(m_gdb, SIGNAL(applicationOutput(const QString&)),
            this, SIGNAL(ttyStdout(const QString &)));
#endif
    connect(m_gdb, SIGNAL(userCommandOutput(const QString&)), this,
            SIGNAL(gdbUserCommandStdout(const QString&)));
    connect(m_gdb, SIGNAL(internalCommandOutput(const QString&)), this,
            SIGNAL(gdbInternalCommandStdout(const QString&)));

    connect(m_gdb, SIGNAL(ready()), this, SLOT(gdbReady()));
    connect(m_gdb, SIGNAL(gdbExited()), this, SLOT(gdbExited()));
    connect(m_gdb, SIGNAL(programStopped(const GDBMI::ResultRecord&)),
            this, SLOT(slotProgramStopped(const GDBMI::ResultRecord&)));
    connect(m_gdb, SIGNAL(programStopped(const GDBMI::ResultRecord&)),
            this, SIGNAL(programStopped(const GDBMI::ResultRecord&)));
    connect(m_gdb, SIGNAL(programRunning()),
            this, SLOT(programRunning()));

    connect(m_gdb, SIGNAL(streamRecord(const GDBMI::StreamRecord&)),
            this, SLOT(parseStreamRecord(const GDBMI::StreamRecord&)));

    // Start gdb. Do this after connecting all signals so that initial
    // GDB output, and important events like "GDB died" are reported.


    if (cfg)
    {
        KConfigGroup config = cfg->config();
        m_gdb->start(config);
    }
    else
    {
        // FIXME: this is hack, I am not sure there's any way presently
        // to edit this via GUI.
        KConfigGroup config(KGlobal::config(), "GDB Debugger");
        m_gdb->start(config);
    }

    setStateOff(s_dbgNotStarted);

    // Initialise gdb. At this stage gdb is sitting wondering what to do,
    // and to whom. Organise a few things, then set up the tty for the application,
    // and the application itself
    //queueCmd(new GDBCommand(GDBMI::EnableTimings, "yes"));

    queueCmd(new CliCommand(GDBMI::GdbShow, "version", this, &DebugSession::handleVersion));

    // This makes gdb pump a variable out on one line.
    queueCmd(new GDBCommand(GDBMI::GdbSet, "width 0"));
    queueCmd(new GDBCommand(GDBMI::GdbSet, "height 0"));

    queueCmd(new GDBCommand(GDBMI::SignalHandle, "SIG32 pass nostop noprint"));
    queueCmd(new GDBCommand(GDBMI::SignalHandle, "SIG41 pass nostop noprint"));
    queueCmd(new GDBCommand(GDBMI::SignalHandle, "SIG42 pass nostop noprint"));
    queueCmd(new GDBCommand(GDBMI::SignalHandle, "SIG43 pass nostop noprint"));
    
    queueCmd(new GDBCommand(GDBMI::EnablePrettyPrinting));

    QString fileName = KStandardDirs::locate("data", "kdevgdb/printers/gdbinit");
    if (!fileName.isEmpty()) {
        queueCmd(new GDBCommand(GDBMI::NonMI, "source " + fileName));
    }

    return true;
}

bool DebugSession::startProgram(KDevelop::ILaunchConfiguration* cfg)
{
    if (stateIsOn( s_appNotStarted ) )
    {
        emit showMessage(i18n("Running program"), 1000);
    }

    if (stateIsOn(s_dbgNotStarted))
        if (!startDebugger(cfg))
            return false;

    if (stateIsOn(s_shuttingDown)) {
        kDebug() << "Tried to run when debugger shutting down";
        return false;
    }



    KConfigGroup grp = cfg->config();
    KDevelop::EnvironmentGroupList l(KGlobal::config());

    if (grp.readEntry("Break on Start", false)) {
        BreakpointModel* m = KDevelop::ICore::self()->debugController()->breakpointModel();
        bool found = false;
        foreach (KDevelop::Breakpoint *b, m->breakpoints()) {
            if (b->location() == "main") {
                found = true;
                break;
            }
        }
        if (!found) {
            KDevelop::ICore::self()->debugController()->breakpointModel()->addCodeBreakpoint("main");
        }
    }


    // Configuration values
    bool    config_breakOnLoadingLibrary_ = grp.readEntry( GDBDebugger::breakOnLibLoadEntry, false );
    bool    config_forceBPSet_ = grp.readEntry( GDBDebugger::allowForcedBPEntry, true );
    bool    config_displayStaticMembers_ = grp.readEntry( GDBDebugger::staticMembersEntry, false );
    bool    config_asmDemangle_ = grp.readEntry( GDBDebugger::demangleNamesEntry, true );
    //bool    config_dbgTerminal_ = grp.readEntry( GDBDebugger::separateTerminalEntry, false );
    bool    config_dbgTerminal_ = false;
    KUrl config_dbgShell_ = grp.readEntry( GDBDebugger::debuggerShellEntry, KUrl() );
    KUrl config_configGdbScript_ = grp.readEntry( GDBDebugger::remoteGdbConfigEntry, KUrl() );
    KUrl config_runShellScript_ = grp.readEntry( GDBDebugger::remoteGdbShellEntry, KUrl() );
    KUrl config_runGdbScript_ = grp.readEntry( GDBDebugger::remoteGdbRunEntry, KUrl() );
    int config_outputRadix_ = 10;

    // Need to set up a new TTY for each run...
    if (tty_)
        delete tty_;

    tty_ = new STTY(config_dbgTerminal_);//, Settings::terminalEmulatorName( *KGlobal::config() ));
    if (!config_dbgTerminal_)
    {
        connect( tty_, SIGNAL(OutOutput(const QByteArray&)), SIGNAL(ttyStdout(const QByteArray&)) );
        connect( tty_, SIGNAL(ErrOutput(const QByteArray&)), SIGNAL(ttyStderr(const QByteArray&)) );
    }

    QString tty(tty_->getSlave());
    if (tty.isEmpty())
    {
        KMessageBox::information(qApp->activeWindow(), i18n("GDB cannot use the tty* or pty* devices.\n"
                                    "Check the settings on /dev/tty* and /dev/pty*\n"
                                    "As root you may need to \"chmod ug+rw\" tty* and pty* devices "
                                    "and/or add the user to the tty group using "
                                    "\"usermod -G tty username\"."), i18n("Warning"));

        delete tty_;
        tty_ = 0;
        return false;
    }

    queueCmd(new GDBCommand(GDBMI::InferiorTtySet, tty));


    IExecutePlugin* iface = KDevelop::ICore::self()->pluginController()->pluginForExtension("org.kdevelop.IExecutePlugin")->extension<IExecutePlugin>();
    Q_ASSERT(iface);

    // Only dummy err here, actual erros have been checked already in the job and we don't get here if there were any
    QString err;
    QString executable = iface->executable(cfg, err).toLocalFile();
    QString envgrp = iface->environmentGroup(cfg);

    QStringList arguments = iface->arguments(cfg, err);
    // Change the "Working directory" to the correct one
    QString dir = iface->workingDirectory(cfg).toLocalFile();
    if (dir.isEmpty() || !KUrl(dir).isValid())
    {
        dir = QFileInfo(executable).absolutePath();
    }
    
    queueCmd(new GDBCommand(GDBMI::EnvironmentCd, dir));

    // Set the run arguments
    if (!arguments.isEmpty())
        queueCmd(
            new GDBCommand(GDBMI::ExecArguments, KShell::joinArgs( arguments )));

    foreach (const QString& envvar, l.createEnvironment(envgrp, QStringList()))
        queueCmd(new GDBCommand(GDBMI::GdbSet, "environment " + envvar));

    // Needed so that breakpoint widget has a chance to insert breakpoints.
    // FIXME: a bit hacky, as we're really not ready for new commands.
    setStateOn(s_dbgBusy);
    raiseEvent(debugger_ready);


    if (config_displayStaticMembers_)
        queueCmd(new GDBCommand(GDBMI::GdbSet, "print static-members on"));
    else
        queueCmd(new GDBCommand(GDBMI::GdbSet, "print static-members off"));
    if (config_asmDemangle_)
        queueCmd(new GDBCommand(GDBMI::GdbSet, "print asm-demangle on"));
    else
        queueCmd(new GDBCommand(GDBMI::GdbSet, "print asm-demangle off"));

    if (config_configGdbScript_.isValid())
        queueCmd(new GDBCommand(GDBMI::NonMI, "source " + config_configGdbScript_.toLocalFile()));


    if (!config_runShellScript_.isEmpty()) {
        // Special for remote debug...
        QByteArray tty(tty_->getSlave().toLatin1());
        QByteArray options = QByteArray(">") + tty + QByteArray("  2>&1 <") + tty;

        QProcess *proc = new QProcess;
        QStringList arguments;
        arguments << "-c" << config_runShellScript_.toLocalFile() +
            ' ' + executable + QString::fromAscii( options );

        proc->start("sh", arguments);
        //PORTING TODO QProcess::DontCare);
    }

    if (!config_runGdbScript_.isEmpty()) {// gdb script at run is requested

        // Race notice: wait for the remote gdbserver/executable
        // - but that might be an issue for this script to handle...

        // Future: the shell script should be able to pass info (like pid)
        // to the gdb script...

        kDebug(9012) << "Running gdb script " << config_runGdbScript_.toLocalFile();
        queueCmd(new GDBCommand(GDBMI::NonMI, "source " + config_runGdbScript_.toLocalFile()));

        // Note: script could contain "run" or "continue"
    }
    else
    {
        queueCmd(new GDBCommand(GDBMI::FileExecAndSymbols, executable, this, &DebugSession::handleFileExecAndSymbols, true));
        raiseEvent(connected_to_program);
        queueCmd(new GDBCommand(GDBMI::ExecRun));
    }

    setStateOff(s_appNotStarted|s_programExited);

    return true;
}


// **************************************************************************
//                                SLOTS
//                                *****
// For most of these slots data can only be sent to gdb when it
// isn't busy and it is running.

// **************************************************************************

void DebugSession::slotKillGdb()
{
    if (!stateIsOn(s_programExited) && stateIsOn(s_shuttingDown))
    {
        kDebug(9012) << "gdb not shutdown - killing";
        m_gdb->kill();

        setState(s_dbgNotStarted | s_appNotStarted);

        raiseEvent(debugger_exited);
    }
}

// **************************************************************************

void DebugSession::slotKill()
{
    if (stateIsOn(s_dbgNotStarted|s_shuttingDown))
        return;

    if (stateIsOn(s_dbgBusy))
    {
        interruptDebugger();
    }

    // The -exec-abort is not implemented in gdb
    // queueCmd(new GDBCommand(GDBMI::ExecAbort));
    queueCmd(new GDBCommand(GDBMI::NonMI, "kill"));

    setStateOn(s_appNotStarted);
}

// **************************************************************************

void DebugSession::runUntil(const KUrl& url, int line)
{
    if (stateIsOn(s_dbgNotStarted|s_shuttingDown))
        return;

    if (!url.isValid())
        queueCmd(new GDBCommand(GDBMI::ExecUntil, line));
    else
        queueCmd(new GDBCommand(GDBMI::ExecUntil,
                QString("%1:%2").arg(url.toLocalFile()).arg(line)));
}

// **************************************************************************

void DebugSession::jumpTo(const KUrl& url, int line)
{
    if (stateIsOn(s_dbgNotStarted|s_shuttingDown))
        return;

    if (url.isValid()) {
        queueCmd(new GDBCommand(GDBMI::NonMI, QString("tbreak %1:%2").arg(url.toLocalFile()).arg(line)));
        queueCmd(new GDBCommand(GDBMI::NonMI, QString("jump %1:%2").arg(url.toLocalFile()).arg(line)));
    }
}

// **************************************************************************

// FIXME: connect to GDB's slot.
void DebugSession::defaultErrorHandler(const GDBMI::ResultRecord& result)
{
    QString msg = result["msg"].literal();

    if (msg.contains("No such process"))
    {
        setState(s_appNotStarted|s_programExited);
        raiseEvent(program_exited);
        return;
    }

    KMessageBox::information(
        qApp->activeWindow(),
        i18n("<b>Debugger error</b>"
             "<p>Debugger reported the following error:"
             "<p><tt>%1", result["msg"].literal()),
        i18n("Debugger error"));

    // Error most likely means that some change made in GUI
    // was not communicated to the gdb, so GUI is now not
    // in sync with gdb. Resync it.
    //
    // Another approach is to make each widget reload it content
    // on errors from commands that it sent, but that's too complex.
    // Errors are supposed to happen rarely, so full reload on error
    // is not a big deal. Well, maybe except for memory view, but
    // it's no auto-reloaded anyway.
    //
    // Also, don't reload state on errors appeared during state
    // reloading!
    if (!m_gdb->currentCommand()->stateReloading())
        raiseEvent(program_state_changed);
}

void DebugSession::gdbReady()
{
    stateReloadInProgress_ = false;

    if (!executeCmd())
    {
        /* We know that gdb is ready, so if executeCmd returns false
           it means there's nothing in command queue.  */

        if (state_reload_needed)
        {
            kDebug(9012) << "Finishing program stop\n";
            // Set to false right now, so that if 'actOnProgramPauseMI_part2'
            // sends some commands, we won't call it again when handling replies
            // from that commands.
            state_reload_needed = false;
            reloadProgramState();
        }

        kDebug(9012) << "No more commands\n";
        setStateOff(s_dbgBusy);
        raiseEvent(debugger_ready);
    }
}

void DebugSession::gdbExited()
{
    kDebug();
    /* Technically speaking, GDB is likely not to kill the application, and
       we should have some backup mechanism to make sure the application is
       killed by KDevelop.  But even if application stays around, we no longer
       can control it in any way, so mark it as exited.  */
    setStateOn(s_appNotStarted);
    setStateOn(s_dbgNotStarted);
    setStateOff(s_shuttingDown);
}

// FIXME: I don't fully remember what is the business with
// stateReloadInProgress_ and whether we can lift it to the
// generic level.
void DebugSession::raiseEvent(event_t e)
{
    if (e == program_exited || e == debugger_exited)
    {
        stateReloadInProgress_ = false;
    }

    if (e == program_state_changed)
    {
        stateReloadInProgress_ = true;
        kDebug(9012) << "State reload in progress\n";
    }

    IDebugSession::raiseEvent(e);

    if (e == program_state_changed)
    {
        stateReloadInProgress_ = false;
    }
}

// **************************************************************************

void DebugSession::slotUserGDBCmd(const QString& cmd)
{
    queueCmd(new UserCommand(GDBMI::NonMI, cmd));

    // User command can theoreticall modify absolutely everything,
    // so need to force a reload.

    // We can do it right now, and don't wait for user command to finish
    // since commands used to reload all view will be executed after
    // user command anyway.
    if (!stateIsOn(s_appNotStarted) && !stateIsOn(s_programExited))
        raiseEvent(program_state_changed);
}

void DebugSession::explainDebuggerStatus()
{
    GDBCommand* currentCmd_ = m_gdb->currentCommand();
    QString information =
        i18np("1 command in queue\n", "%1 commands in queue\n", commandQueue_->count()) +
        i18ncp("Only the 0 and 1 cases need to be translated", "1 command being processed by gdb\n", "%1 commands being processed by gdb\n", (currentCmd_ ? 1 : 0)) +
        i18n("Debugger state: %1\n", state_);

    if (currentCmd_)
    {
        QString extra = i18n("Current command class: '%1'\n"
                             "Current command text: '%2'\n"
                             "Current command original text: '%3'\n",
                             typeid(*currentCmd_).name(),
                             currentCmd_->cmdToSend(),
                             currentCmd_->initialString());

        information += extra;
    }

    KMessageBox::information(qApp->activeWindow(), information,
                             i18n("Debugger status"));
}

bool DebugSession::stateIsOn(DBGStateFlags state) const
{
    return state_ & state;
}

void DebugSession::setStateOn(DBGStateFlags stateOn)
{
    DBGStateFlags oldState = state_;

    debugStateChange(state_, state_ | stateOn);
    state_ |= stateOn;

    _gdbStateChanged(oldState, state_);
}

void DebugSession::setStateOff(DBGStateFlags stateOff)
{
    DBGStateFlags oldState = state_;

    debugStateChange(state_, state_ & ~stateOff);
    state_ &= ~stateOff;

    _gdbStateChanged(oldState, state_);
}

void DebugSession::setState(DBGStateFlags newState)
{
    DBGStateFlags oldState = state_;

    debugStateChange(state_, newState);
    state_ = newState;

    _gdbStateChanged(oldState, state_);
}

void DebugSession::debugStateChange(DBGStateFlags oldState, DBGStateFlags newState)
{
    int delta = oldState ^ newState;
    if (delta)
    {
        QString out = "STATE: ";
        for(int i = 1; i < s_lastDbgState; i <<= 1)
        {
            if (delta & i)
            {
                if (i & newState)
                    out += '+';
                else
                    out += '-';

                bool found = false;
#define STATE_CHECK(name)\
    if (i == name) { out += #name; found = true; }
                STATE_CHECK(s_dbgNotStarted);
                STATE_CHECK(s_appNotStarted);
                STATE_CHECK(s_programExited);
                STATE_CHECK(s_attached);
                STATE_CHECK(s_core);
                STATE_CHECK(s_waitTimer);
                STATE_CHECK(s_shuttingDown);
                STATE_CHECK(s_explicitBreakInto);
                STATE_CHECK(s_dbgBusy);
                STATE_CHECK(s_appRunning);
#undef STATE_CHECK

                if (!found)
                    out += QString::number(i);
                out += ' ';

            }
        }
        kDebug(9012) << out;
    }
}

void DebugSession::programRunning()
{
    setStateOn(s_appRunning);
    raiseEvent(program_running);
}

void DebugSession::handleVersion(const QStringList& s)
{
    const int minVersion1 = 7;
    const int minVersion2 = 0;
    const int minVersion3 = 0;

    kDebug() << s.first();
    QRegExp rx("([0-9]+)\\.([0-9]+)(?:\\.([0-9]+))?");
    rx.indexIn(s.first());
    if (rx.cap(1).toInt() < minVersion1
        || (rx.cap(1).toInt() == minVersion1 && (rx.cap(2).toInt() < minVersion2
            || ( rx.cap(2).toInt() == minVersion2 && minVersion3 > 0 && rx.numCaptures() == 3
                && rx.cap(3).toInt() < minVersion3))))
    {
        if (qApp->type() == QApplication::Tty)  {
            //for unittest
            qFatal("You need gdb 7.0.0 or higher.");
        }
        KMessageBox::error(
            qApp->activeWindow(),
            i18n("<b>You need gdb 7.0.0 or higher.</b><br />"
            "You are using: %1", s.first()),
            i18n("gdb error"));
        stopDebugger();
    }
}


void DebugSession::handleFileExecAndSymbols(const GDBMI::ResultRecord& r)
{
    if (r.reason == "error") {
        KMessageBox::error(
            qApp->activeWindow(),
            i18n("<b>Could not start debugger:</b><br />")+
            r["msg"].literal(),
            i18n("Startup error"));
        stopDebugger();
    }
}

void DebugSession::handleTargetAttach(const GDBMI::ResultRecord& r)
{
    if (r.reason == "error") {
        KMessageBox::error(
            qApp->activeWindow(),
            i18n("<b>Could not attach debugger:</b><br />")+
            r["msg"].literal(),
            i18n("Startup error"));
        stopDebugger();
    }
}

}


#include "debugsession.moc"
