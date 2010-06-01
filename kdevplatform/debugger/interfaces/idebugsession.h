/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2009 Niko Sams <niko.sams@gmail.com>                        *
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

#ifndef KDEV_IDEBUGSESSION_H
#define KDEV_IDEBUGSESSION_H

#include <QtCore/QObject>
#include <QtCore/QAbstractItemModel>
#include "../debuggerexport.h"

class KUrl;

namespace KDevelop {

class IVariableController;
class IBreakpointController;
class IFrameStackModel;
class Breakpoint;
class StackModel;

class KDEVPLATFORMDEBUGGER_EXPORT IDebugSession : public QObject
{
    Q_OBJECT
    Q_ENUMS(DebuggerState)
public:
    IDebugSession();
    virtual ~IDebugSession();

    enum DebuggerState {
        NotStartedState,
        StartingState,
        ActiveState,
        PausedState,
        StoppingState,
        StoppedState,
        EndedState
    };

    enum event_t { program_state_changed = 1, 
                   program_exited, 
                   debugger_exited,
                   // Emitted when the thread or frame that is selected in UI
                   // changes.
                   thread_or_frame_changed, 
                   debugger_busy, 
                   debugger_ready,
                   // Raised when debugger believe that program start running.
                   // Can be used to hide current line indicator.
                   // Don't count on this being raise in all cases where
                   // program is running.
                   program_running,
                   // Raise when the debugger is in touch with the program,
                   // and should have access to its debug symbols. The program
                   // is not necessary running yet, or might already exited,
                   // or be otherwise dead.
                   connected_to_program
    };

public:
    /**
     * Current state of the debug session
     */
    virtual DebuggerState state() const = 0;

    /**
     * Should return if restart is currently available
     */
    virtual bool restartAvaliable() const = 0;

    /**
     * Returns if the debugee is currently running. This includes paused.
     */
    bool isRunning() const;
    
    /**
     * Returns the local Url for a source file used in the current debug session.
     *
     * The default implementation just returns the url and is sufficient for
     * local debuggers. Remote debuggers can implement a path mapping mechanism.
     */
    virtual QPair<KUrl, int> convertToLocalUrl(const QPair<KUrl, int> &remoteUrl) const;

    /**
     * Returns the remote Url for a source file used in the current debug session.
     *
     * The default implementation just returns the url and is sufficient for
     * local debuggers. Remote debuggers can implement a path mapping mechanism.
     */
    virtual QPair<KUrl, int> convertToRemoteUrl(const QPair<KUrl, int> &localUrl) const;

    IBreakpointController *breakpointController() const;
    IVariableController *variableController() const;    
    IFrameStackModel *frameStackModel() const; 

public Q_SLOTS:
    virtual void restartDebugger() = 0;
    virtual void stopDebugger() = 0;
    virtual void interruptDebugger() = 0;
    virtual void run() = 0;
    virtual void runToCursor() = 0;
    virtual void jumpToCursor() = 0;
    virtual void stepOver() = 0;
    virtual void stepIntoInstruction() = 0;
    virtual void stepInto() = 0;
    virtual void stepOverInstruction() = 0;
    virtual void stepOut() = 0;

Q_SIGNALS:
    void stateChanged(KDevelop::IDebugSession::DebuggerState state);
    void showStepInSource(const KUrl& file, int line);
    void clearExecutionPoint();
    void finished();

    void raiseFramestackViews();

    /** This signal is emitted whenever the given event in a program
        happens. See DESIGN.txt for expected handled of each event.

        NOTE: this signal should never be emitted directly. Instead,
        use raiseEvent.
    */
    void event(IDebugSession::event_t e);

public:
    using QObject::event; // prevent hiding of base method.
       
protected:

    /** Raises the specified event. Should be used instead of
        emitting 'event' directly, since this method can perform
        additional book-keeping for events.
        FIXME: it might make sense to automatically route
        events to all debugger components, as opposed to requiring
        that they connect to any signal.
    */
    virtual void raiseEvent(event_t e);
    friend class FrameStackModel;
    
    virtual IFrameStackModel* createFrameStackModel() = 0;

    IBreakpointController *m_breakpointController;
    IVariableController *m_variableController;    
    mutable IFrameStackModel *m_frameStackModel;
};

}

#endif
