/*
   Copyright 2009 Niko Sams <niko.sams@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "gdbtest.h"

#include <QtTest/QTest>
#include <QSignalSpy>
#include <QDebug>
#include <QApplication>
#include <QFileInfo>
#include <QDir>

#include <KGlobal>
#include <KSharedConfig>
#include <KDebug>
#include <KProcess>
#include <qtest_kde.h>

#include <tests/testcore.h>
#include <shell/shellextension.h>
#include <debugger/breakpoint/breakpointmodel.h>
#include <interfaces/idebugcontroller.h>
#include <debugger/breakpoint/breakpoint.h>
#include <debugger/interfaces/ibreakpointcontroller.h>
#include <interfaces/ilaunchconfiguration.h>
#include <debugger/variable/variablecollection.h>
#include <debugger/interfaces/ivariablecontroller.h>
#include <debugger/framestack/framestackmodel.h>
#include <tests/autotestshell.h>

#include "gdbcommand.h"
#include "debugsession.h"
#include "gdbframestackmodel.h"

using KDevelop::AutoTestShell;

namespace GDBDebugger {


void GdbTest::init()
{
    AutoTestShell::init();
    m_core = new KDevelop::TestCore();
    m_core->initialize(KDevelop::Core::NoUi);

    //remove all breakpoints - so we can set our own in the test
    KConfigGroup breakpoints = KGlobal::config()->group("breakpoints");
    breakpoints.writeEntry("number", 0);
    breakpoints.sync();
    
    KDevelop::BreakpointModel* m = KDevelop::ICore::self()->debugController()->breakpointModel();
    m->removeRows(0, m->rowCount());

    KDevelop::VariableCollection *vc = KDevelop::ICore::self()->debugController()->variableCollection();
    for (int i=0; i < vc->watches()->childCount(); ++i) {
        delete vc->watches()->child(i);
    }
    vc->watches()->clear();
}

void GdbTest::cleanup()
{
    m_core->cleanup();
    delete m_core;
}

class TestLaunchConfiguration : public KDevelop::ILaunchConfiguration
{
public:
    TestLaunchConfiguration(KUrl executable = KUrl(QDir::currentPath()+"/unittests/debugee") ) {
        c = new KConfig();
        cfg = c->group("launch");
        cfg.writeEntry("isExecutable", true);
        cfg.writeEntry("Executable", executable);
    }
    ~TestLaunchConfiguration() {
        delete c;
    }
    virtual const KConfigGroup config() const { return cfg; }
    virtual QString name() const { return QString("Test-Launch"); }
    virtual KDevelop::IProject* project() const { return 0; }
    virtual KDevelop::LaunchConfigurationType* type() const { return 0; }
private:
    KConfigGroup cfg;
    KConfig *c;
};

class TestFrameStackModel : public KDevelop::GdbFrameStackModel
{
public:
    
    TestFrameStackModel(DebugSession* session) 
    : GdbFrameStackModel(session), fetchFramesCalled(0) {}
    
    int fetchFramesCalled;
    virtual void fetchFrames(int threadNumber, int from, int to)
    {
        fetchFramesCalled++;
        GdbFrameStackModel::fetchFrames(threadNumber, from, to);
    }
};

class TestDebugSession : public DebugSession
{
    Q_OBJECT
public:
    TestDebugSession() : DebugSession(), m_line(0)
    {
        qRegisterMetaType<KUrl>("KUrl");
        connect(this, SIGNAL(showStepInSource(KUrl, int)), SLOT(slotShowStepInSource(KUrl, int)));
        
        KDevelop::ICore::self()->debugController()->addSession(this);
    }
    
    KDevelop::IFrameStackModel* createFrameStackModel()
    {
        return new TestFrameStackModel(this);
    }
        
    KUrl url() { return m_url; }
    int line() { return m_line; }
    TestFrameStackModel *frameStackModel() const 
    { return static_cast<TestFrameStackModel*>(DebugSession::frameStackModel()); }

private slots:
    void slotShowStepInSource(const KUrl &url, int line)
    {
        m_url = url;
        m_line = line;
    }
private:
    KUrl m_url;
    int m_line;

};


#define WAIT_FOR_STATE(session, state) \
    waitForState((session), (state), __FILE__, __LINE__)

#define COMPARE_DATA(index, expected) \
    compareData((index), (expected), __FILE__, __LINE__)
void compareData(QModelIndex index, QString expected, const char *file, int line)
{
    QString s = index.model()->data(index, Qt::DisplayRole).toString();
    if (s != expected) {
        kFatal() << QString("'%0' didn't match expected '%1' in %2:%3").arg(s).arg(expected).arg(file).arg(line);
    }
}

static const QString debugeeFileName = QFileInfo(__FILE__).dir().path()+"/debugee.cpp";

KDevelop::BreakpointModel* breakpoints()
{
    return KDevelop::ICore::self()->debugController()->breakpointModel();
}

void GdbTest::testStdOut()
{
    TestDebugSession *session = new TestDebugSession;

    QSignalSpy outputSpy(session, SIGNAL(applicationStandardOutputLines(QStringList)));

    TestLaunchConfiguration cfg;
    session->startProgram(&cfg);
    WAIT_FOR_STATE(session, KDevelop::IDebugSession::EndedState);

    {
        QCOMPARE(outputSpy.count(), 1);
        QList<QVariant> arguments = outputSpy.takeFirst();
        QCOMPARE(arguments.count(), 1);
        QCOMPARE(arguments.first().toStringList(), QStringList() << "Hello, world!" << "Hello");
    }
}

void GdbTest::testBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;

    TestLaunchConfiguration cfg;

    KDevelop::Breakpoint * b = breakpoints()->addCodeBreakpoint(debugeeFileName, 28);
    QCOMPARE(session->breakpointController()->breakpointState(b), KDevelop::Breakpoint::NotStartedState);

    session->startProgram(&cfg);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->breakpointController()->breakpointState(b), KDevelop::Breakpoint::CleanState);
    session->stepInto();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    session->stepInto();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testDisableBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;

    TestLaunchConfiguration cfg;

    KDevelop::Breakpoint *b;

    //add disabled breakpoint before startProgram
    b = breakpoints()->addCodeBreakpoint(debugeeFileName, 29);
    b->setData(KDevelop::Breakpoint::EnableColumn, false);

    b = breakpoints()->addCodeBreakpoint(debugeeFileName, 21);
    session->startProgram(&cfg);
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    //disable existing breakpoint
    b->setData(KDevelop::Breakpoint::EnableColumn, false);

    //add another disabled breakpoint
    b = breakpoints()->addCodeBreakpoint(debugeeFileName, 31);
    QTest::qWait(300);
    b->setData(KDevelop::Breakpoint::EnableColumn, false);

    QTest::qWait(300);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);

}

void GdbTest::testChangeLocationBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;

    TestLaunchConfiguration cfg;

    KDevelop::Breakpoint *b = breakpoints()->addCodeBreakpoint(debugeeFileName, 27);

    session->startProgram(&cfg);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 27);

    QTest::qWait(100);
    b->setLine(28);
    QTest::qWait(100);
    session->run();

    QTest::qWait(100);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 28);
    QTest::qWait(500);
    breakpoints()->setData(breakpoints()->index(0, KDevelop::Breakpoint::LocationColumn), debugeeFileName+":30");
    QCOMPARE(b->line(), 29);
    QTest::qWait(100);
    QCOMPARE(b->line(), 29);
    session->run();
    QTest::qWait(100);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 29);
    session->run();

    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testDeleteBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;

    TestLaunchConfiguration cfg;

    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 1); //one for the "insert here" entry
    //add breakpoint before startProgram
    KDevelop::Breakpoint *b = breakpoints()->addCodeBreakpoint(debugeeFileName, 21);
    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 2);
    breakpoints()->removeRow(0);
    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 1);

    b = breakpoints()->addCodeBreakpoint(debugeeFileName, 22);

    session->startProgram(&cfg);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    breakpoints()->removeRow(0);
    QTest::qWait(100);
    session->run();

    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testPendingBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 28);

    KDevelop::Breakpoint * b = breakpoints()->addCodeBreakpoint(QFileInfo(__FILE__).dir().path()+"/gdbtest.cpp", 10);
    QCOMPARE(session->breakpointController()->breakpointState(b), KDevelop::Breakpoint::NotStartedState);

    session->startProgram(&cfg);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->breakpointController()->breakpointState(b), KDevelop::Breakpoint::PendingState);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testUpdateBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    KDevelop::Breakpoint * b = breakpoints()->addCodeBreakpoint(debugeeFileName, 28);
    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 2);

    session->startProgram(&cfg);

    //insert custom command as user might do it using GDB console
    session->addCommand(new UserCommand(GDBMI::NonMI, "break "+debugeeFileName+":28"));

    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(100);
    session->stepInto();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(KDevelop::ICore::self()->debugController()->breakpointModel()->rowCount(), 3);
    b = breakpoints()->breakpoint(1);
    QCOMPARE(b->url(), KUrl(debugeeFileName));
    QCOMPARE(b->line(), 27);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testIgnoreHitsBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    KDevelop::Breakpoint * b = breakpoints()->addCodeBreakpoint(debugeeFileName, 21);
    b->setIgnoreHits(1);

    b = breakpoints()->addCodeBreakpoint(debugeeFileName, 22);

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(100);
    b->setIgnoreHits(1);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testConditionBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    KDevelop::Breakpoint * b = breakpoints()->addCodeBreakpoint(debugeeFileName, 39);
    b->setCondition("x[0] == 'H'");

    b = breakpoints()->addCodeBreakpoint(debugeeFileName, 23);
    b->setCondition("i==2");

    b = breakpoints()->addCodeBreakpoint(debugeeFileName, 24);

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 24);
    b->setCondition("i == 0");
    QTest::qWait(100);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 39);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testBreakOnWriteBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    breakpoints()->addWatchpoint("foo::i");

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testBreakOnWriteWithConditionBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    KDevelop::Breakpoint *b = breakpoints()->addWatchpoint("foo::i");
    b->setCondition("foo::i==2");

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testBreakOnReadBreakpoint()
{
    /*
    test disabled because of gdb bug: http://sourceware.org/bugzilla/show_bug.cgi?id=10136

    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    KDevelop::Breakpoint *b = breakpoints()->addReadWatchpoint("foo::i");

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
    */
}

void GdbTest::testBreakOnReadBreakpoint2()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 27);

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 27);

    breakpoints()->addReadWatchpoint("foo::i");

    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 22);

    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testBreakOnAccessBreakpoint()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 27);

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 27);

    breakpoints()->addAccessWatchpoint("foo::i");

    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 22);

    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);


    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 22);

    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);

    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testInsertBreakpointWhileRunning()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg(KUrl(QDir::currentPath()+"/unittests/debugeeslow"));
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugeeslow.cpp";

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::ActiveState);
    QTest::qWait(2000);
    kDebug() << "adding breakpoint";
    KDevelop::Breakpoint *b = breakpoints()->addCodeBreakpoint(fileName, 23);
    QTest::qWait(100);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);
    b->setDeleted();
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testInsertBreakpointWhileRunningMultiple()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg(KUrl(QDir::currentPath()+"/unittests/debugeeslow"));
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugeeslow.cpp";

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::ActiveState);
    QTest::qWait(2000);
    kDebug() << "adding breakpoint";
    KDevelop::Breakpoint *b1 = breakpoints()->addCodeBreakpoint(fileName, 22);
    KDevelop::Breakpoint *b2 = breakpoints()->addCodeBreakpoint(fileName, 23);
    QTest::qWait(100);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 22);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);
    b1->setDeleted();
    b2->setDeleted();
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testInsertBreakpointFunctionName()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint("main");

    session->startProgram(&cfg);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 27);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testShowStepInSource()
{
    TestDebugSession *session = new TestDebugSession;

    qRegisterMetaType<KUrl>("KUrl");
    QSignalSpy showStepInSourceSpy(session, SIGNAL(showStepInSource(KUrl, int)));

    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 29);
    session->startProgram(&cfg);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    session->stepInto();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    session->stepInto();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);

    {
        QCOMPARE(showStepInSourceSpy.count(), 3);
        QList<QVariant> arguments = showStepInSourceSpy.takeFirst();
        QCOMPARE(arguments.first().value<KUrl>(), KUrl::fromPath(debugeeFileName));
        QCOMPARE(arguments.at(1).toInt(), 29);

        arguments = showStepInSourceSpy.takeFirst();
        QCOMPARE(arguments.first().value<KUrl>(), KUrl::fromPath(debugeeFileName));
        QCOMPARE(arguments.at(1).toInt(), 22);

        arguments = showStepInSourceSpy.takeFirst();
        QCOMPARE(arguments.first().value<KUrl>(), KUrl::fromPath(debugeeFileName));
        QCOMPARE(arguments.at(1).toInt(), 23);
    }
}

void GdbTest::testStack()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    TestFrameStackModel *stackModel = session->frameStackModel();
    
    breakpoints()->addCodeBreakpoint(debugeeFileName, 21);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    QModelIndex tIdx = stackModel->index(0,0);    
    QCOMPARE(stackModel->rowCount(QModelIndex()), 1);
    QCOMPARE(stackModel->columnCount(QModelIndex()), 3);
    COMPARE_DATA(tIdx, "#1 at foo");
    

    QCOMPARE(stackModel->rowCount(tIdx), 2);
    QCOMPARE(stackModel->columnCount(tIdx), 3);
    COMPARE_DATA(tIdx.child(0, 0), "0");
    COMPARE_DATA(tIdx.child(0, 1), "foo");
    COMPARE_DATA(tIdx.child(0, 2), debugeeFileName+":23");
    COMPARE_DATA(tIdx.child(1, 0), "1");
    COMPARE_DATA(tIdx.child(1, 1), "main");
    COMPARE_DATA(tIdx.child(1, 2), debugeeFileName+":29");


    session->stepOut();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    COMPARE_DATA(tIdx, "#1 at main");
    QCOMPARE(stackModel->rowCount(tIdx), 1);
    COMPARE_DATA(tIdx.child(0, 0), "0");
    COMPARE_DATA(tIdx.child(0, 1), "main");
    COMPARE_DATA(tIdx.child(0, 2), debugeeFileName+":30");

    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testStackFetchMore()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg(KUrl(QDir::currentPath()+"/unittests/debugeerecursion"));
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugeerecursion.cpp";
    
    TestFrameStackModel *stackModel = session->frameStackModel();

    breakpoints()->addCodeBreakpoint(fileName, 25);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->frameStackModel()->fetchFramesCalled, 1);

    QModelIndex tIdx = stackModel->index(0,0);
    QCOMPARE(stackModel->rowCount(QModelIndex()), 1);
    QCOMPARE(stackModel->columnCount(QModelIndex()), 3);
    COMPARE_DATA(tIdx, "#1 at foo");

    QCOMPARE(stackModel->rowCount(tIdx), 21);
    COMPARE_DATA(tIdx.child(0, 0), "0");
    COMPARE_DATA(tIdx.child(0, 1), "foo");
    COMPARE_DATA(tIdx.child(0, 2), fileName+":26");
    COMPARE_DATA(tIdx.child(1, 0), "1");
    COMPARE_DATA(tIdx.child(1, 1), "foo");
    COMPARE_DATA(tIdx.child(1, 2), fileName+":24");
    COMPARE_DATA(tIdx.child(2, 0), "2");
    COMPARE_DATA(tIdx.child(2, 1), "foo");
    COMPARE_DATA(tIdx.child(2, 2), fileName+":24");
    COMPARE_DATA(tIdx.child(19, 0), "19");
    COMPARE_DATA(tIdx.child(20, 0), "20");

    stackModel->fetchMoreFrames();
    QTest::qWait(200);
    QCOMPARE(stackModel->fetchFramesCalled, 2);
    QCOMPARE(stackModel->rowCount(tIdx), 41);
    COMPARE_DATA(tIdx.child(20, 0), "20");
    COMPARE_DATA(tIdx.child(21, 0), "21");
    COMPARE_DATA(tIdx.child(22, 0), "22");
    COMPARE_DATA(tIdx.child(39, 0), "39");
    COMPARE_DATA(tIdx.child(40, 0), "40");

    stackModel->fetchMoreFrames();
    QTest::qWait(200);
    QCOMPARE(stackModel->fetchFramesCalled, 3);
    QCOMPARE(stackModel->rowCount(tIdx), 61);
    COMPARE_DATA(tIdx.child(40, 0), "40");
    COMPARE_DATA(tIdx.child(41, 0), "41");
    COMPARE_DATA(tIdx.child(42, 0), "42");
    COMPARE_DATA(tIdx.child(60, 0), "60");

    stackModel->fetchMoreFrames();
    QTest::qWait(200);
    QCOMPARE(stackModel->fetchFramesCalled, 4);
    QCOMPARE(stackModel->rowCount(tIdx), 81);

    stackModel->fetchMoreFrames();
    QTest::qWait(200);
    QCOMPARE(stackModel->fetchFramesCalled, 5);
    QCOMPARE(stackModel->rowCount(tIdx), 101);
    COMPARE_DATA(tIdx.child(100, 0), "100");
    COMPARE_DATA(tIdx.child(100, 1), "main");
    COMPARE_DATA(tIdx.child(100, 2), fileName+":30");

    stackModel->fetchMoreFrames(); //nothing to fetch, we are at the end
    QTest::qWait(200);
    QCOMPARE(stackModel->fetchFramesCalled, 5);
    QCOMPARE(stackModel->rowCount(tIdx), 101);

    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testStackDeactivateAndActive()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;
    
    TestFrameStackModel *stackModel = session->frameStackModel();

    breakpoints()->addCodeBreakpoint(debugeeFileName, 21);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    QModelIndex tIdx = stackModel->index(0,0);

    session->stepOut();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(200);
    COMPARE_DATA(tIdx, "#1 at main");
    QCOMPARE(stackModel->rowCount(tIdx), 1);
    COMPARE_DATA(tIdx.child(0, 0), "0");
    COMPARE_DATA(tIdx.child(0, 1), "main");
    COMPARE_DATA(tIdx.child(0, 2), debugeeFileName+":30");

    session->run();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testStackSwitchThread()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg(KUrl(QDir::currentPath()+"/unittests/debugeethreads"));
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugeethreads.cpp";
    
    TestFrameStackModel *stackModel = session->frameStackModel();

    breakpoints()->addCodeBreakpoint(fileName, 38);
    QVERIFY(session->startProgram(&cfg));
    QTest::qWait(500);
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    QCOMPARE(stackModel->rowCount(), 4);

    QModelIndex tIdx = stackModel->index(0,0);
    COMPARE_DATA(tIdx, "#1 at main");
    QCOMPARE(stackModel->rowCount(tIdx), 1);
    COMPARE_DATA(tIdx.child(0, 0), "0");
    COMPARE_DATA(tIdx.child(0, 1), "main");
    COMPARE_DATA(tIdx.child(0, 2), fileName+":39");

    tIdx = stackModel->index(1,0);
    QVERIFY(stackModel->data(tIdx).toString().startsWith("#2 at pthread_cond_timedwait"));
    stackModel->setCurrentThread(2);
    QTest::qWait(200);
    int rows = stackModel->rowCount(tIdx);
    QVERIFY(rows > 3);

    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testAttach()
{
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugeeslow.cpp";

    KProcess debugeeProcess;
    debugeeProcess << "nice" << QDir::currentPath()+"/unittests/debugeeslow";
    debugeeProcess.start();
    Q_ASSERT(debugeeProcess.waitForStarted());
    QTest::qWait(100);

    TestDebugSession *session = new TestDebugSession;
    session->attachToProcess(debugeeProcess.pid());
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    breakpoints()->addCodeBreakpoint(fileName, 32);
    QTest::qWait(100);
    session->run();
    QTest::qWait(2000);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 32);

    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testCoreFile()
{
    QFile f("core");
    if (f.exists()) f.remove();

    KProcess debugeeProcess;
    debugeeProcess.setOutputChannelMode(KProcess::MergedChannels);
    debugeeProcess << "bash" << "-c" << "ulimit -c unlimited; ./unittests/debugeecrash";
    debugeeProcess.start();
    debugeeProcess.waitForFinished();
    kDebug() << debugeeProcess.readAll();
    QFile f2("core");
    if (!f2.exists()) {
        QFAIL("no core dump found");
    }

    TestDebugSession *session = new TestDebugSession;
    session->examineCoreFile(KUrl(QDir::currentPath()+"/unittests/debugeecrash"), KUrl(QDir::currentPath()+"/core"));
    
    TestFrameStackModel *stackModel = session->frameStackModel();
    
    WAIT_FOR_STATE(session, DebugSession::StoppedState);

    QModelIndex tIdx = stackModel->index(0,0);
    QCOMPARE(stackModel->rowCount(QModelIndex()), 1);
    QCOMPARE(stackModel->columnCount(QModelIndex()), 3);
    COMPARE_DATA(tIdx, "#1 at foo");

    session->stopDebugger();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}


KDevelop::VariableCollection *variableCollection()
{
    return KDevelop::ICore::self()->debugController()->variableCollection();
}

void GdbTest::testVariablesLocals()
{
    TestDebugSession *session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateLocals);

    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 22);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(1000);

    QCOMPARE(variableCollection()->rowCount(), 2);
    QModelIndex i = variableCollection()->index(1, 0);
    COMPARE_DATA(i, "Locals");
    QCOMPARE(variableCollection()->rowCount(i), 1);
    COMPARE_DATA(variableCollection()->index(0, 0, i), "i");
    COMPARE_DATA(variableCollection()->index(0, 1, i), "0");
    session->run();
    QTest::qWait(1000);
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    COMPARE_DATA(variableCollection()->index(0, 0, i), "i");
    COMPARE_DATA(variableCollection()->index(0, 1, i), "1");
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testVariablesLocalsStruct()
{
    TestDebugSession *session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateLocals);

    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 38);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(1000);

    QModelIndex i = variableCollection()->index(1, 0);
    QCOMPARE(variableCollection()->rowCount(i), 4);
    COMPARE_DATA(variableCollection()->index(1, 0, i), "ts");
    COMPARE_DATA(variableCollection()->index(1, 1, i), "{...}");
    QModelIndex ts = variableCollection()->index(1, 0, i);
    COMPARE_DATA(variableCollection()->index(0, 0, ts), "...");
    variableCollection()->expanded(ts);
    QTest::qWait(100);
    COMPARE_DATA(variableCollection()->index(0, 0, ts), "a");
    COMPARE_DATA(variableCollection()->index(0, 1, ts), "0");
    COMPARE_DATA(variableCollection()->index(1, 0, ts), "b");
    COMPARE_DATA(variableCollection()->index(1, 1, ts), "1");
    COMPARE_DATA(variableCollection()->index(2, 0, ts), "c");
    COMPARE_DATA(variableCollection()->index(2, 1, ts), "2");

    session->stepInto();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(1000);
    COMPARE_DATA(variableCollection()->index(1, 0, i), "ts");
    COMPARE_DATA(variableCollection()->index(1, 1, i), "{...}");
    COMPARE_DATA(variableCollection()->index(0, 1, ts), "1");

    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testVariablesWatches()
{
    TestDebugSession *session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateWatches);

    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 38);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    
    variableCollection()->watches()->add("ts");
    QTest::qWait(300);

    QModelIndex i = variableCollection()->index(0, 0);
    QCOMPARE(variableCollection()->rowCount(i), 1);
    COMPARE_DATA(variableCollection()->index(0, 0, i), "ts");
    COMPARE_DATA(variableCollection()->index(0, 1, i), "{...}");
    QModelIndex ts = variableCollection()->index(0, 0, i);
    COMPARE_DATA(variableCollection()->index(0, 0, ts), "...");
    variableCollection()->expanded(ts);
    QTest::qWait(100);
    COMPARE_DATA(variableCollection()->index(0, 0, ts), "a");
    COMPARE_DATA(variableCollection()->index(0, 1, ts), "0");
    COMPARE_DATA(variableCollection()->index(1, 0, ts), "b");
    COMPARE_DATA(variableCollection()->index(1, 1, ts), "1");
    COMPARE_DATA(variableCollection()->index(2, 0, ts), "c");
    COMPARE_DATA(variableCollection()->index(2, 1, ts), "2");

    session->stepInto();
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(100);
    COMPARE_DATA(variableCollection()->index(0, 0, i), "ts");
    COMPARE_DATA(variableCollection()->index(0, 1, i), "{...}");
    COMPARE_DATA(variableCollection()->index(0, 1, ts), "1");

    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testVariablesWatchesQuotes()
{
    TestDebugSession *session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateWatches);

    TestLaunchConfiguration cfg;

    const QString testString("test");
    const QString quotedTestString("\"" + testString + "\"");

    breakpoints()->addCodeBreakpoint(debugeeFileName, 38);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    variableCollection()->watches()->add(quotedTestString); //just a constant string
    QTest::qWait(300);

    QModelIndex i = variableCollection()->index(0, 0);
    QCOMPARE(variableCollection()->rowCount(i), 1);
    COMPARE_DATA(variableCollection()->index(0, 0, i), quotedTestString);
    COMPARE_DATA(variableCollection()->index(0, 1, i), "[" + QString::number(testString.length() + 1) + "]");

    QModelIndex testStr = variableCollection()->index(0, 0, i);
    COMPARE_DATA(variableCollection()->index(0, 0, testStr), "...");
    variableCollection()->expanded(testStr);
    QTest::qWait(100);
    int len = testString.length();
    for (int ind = 0; ind < len; ind++)
    {
        COMPARE_DATA(variableCollection()->index(ind, 0, testStr), QString::number(ind));
        QChar c = testString.at(ind);
        QString value = QString::number(c.toLatin1()) + " '" + c + "'";
        COMPARE_DATA(variableCollection()->index(ind, 1, testStr), value);
    }
    COMPARE_DATA(variableCollection()->index(len, 0, testStr), QString::number(len));
    COMPARE_DATA(variableCollection()->index(len, 1, testStr), "0 '\\000'");

    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testVariablesWatchesTwoSessions()
{
    TestDebugSession *session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateWatches);

    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 38);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    
    variableCollection()->watches()->add("ts");
    QTest::qWait(300);

    QModelIndex ts = variableCollection()->index(0, 0, variableCollection()->index(0, 0));
    variableCollection()->expanded(ts);
    QTest::qWait(100);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
    
    //check if variable is marked as out-of-scope
    QCOMPARE(variableCollection()->watches()->childCount(), 1);
    KDevelop::Variable* v = dynamic_cast<KDevelop::Variable*>(variableCollection()->watches()->child(0));
    QVERIFY(v);
    QVERIFY(!v->inScope());
    QCOMPARE(v->childCount(), 3);
    v = dynamic_cast<KDevelop::Variable*>(v->child(0));
    QVERIFY(!v->inScope());

    //start a second debug session
    session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateWatches);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(300);

    QCOMPARE(variableCollection()->watches()->childCount(), 1);
    ts = variableCollection()->index(0, 0, variableCollection()->index(0, 0));
    v = dynamic_cast<KDevelop::Variable*>(variableCollection()->watches()->child(0));
    QVERIFY(v);
    QVERIFY(v->inScope());
    QCOMPARE(v->childCount(), 3);

    v = dynamic_cast<KDevelop::Variable*>(v->child(0));
    QVERIFY(v->inScope());
    QCOMPARE(v->data(1, Qt::DisplayRole).toString(), QString::number(0));
    
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
    
    //check if variable is marked as out-of-scope
    v = dynamic_cast<KDevelop::Variable*>(variableCollection()->watches()->child(0));
    QVERIFY(!v->inScope());
    QVERIFY(!dynamic_cast<KDevelop::Variable*>(v->child(0))->inScope());
}

void GdbTest::testVariablesStopDebugger()
{
    TestDebugSession *session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateLocals);

    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 38);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    session->stopDebugger();
    QTest::qWait(300);
}


void GdbTest::testVariablesStartSecondSession()
{
    TestDebugSession *session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateLocals);

    TestLaunchConfiguration cfg;

    breakpoints()->addCodeBreakpoint(debugeeFileName, 38);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateLocals);

    breakpoints()->addCodeBreakpoint(debugeeFileName, 38);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);

    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testVariablesSwitchFrame()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateLocals);
    TestFrameStackModel *stackModel = session->frameStackModel();

    breakpoints()->addCodeBreakpoint(debugeeFileName, 24);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(500);

    QModelIndex i = variableCollection()->index(1, 0);
    COMPARE_DATA(i, "Locals");
    QCOMPARE(variableCollection()->rowCount(i), 1);
    COMPARE_DATA(variableCollection()->index(0, 0, i), "i");
    COMPARE_DATA(variableCollection()->index(0, 1, i), "1");

    stackModel->setCurrentFrame(1);
    QTest::qWait(200);

    i = variableCollection()->index(1, 0);
    QCOMPARE(variableCollection()->rowCount(i), 4);
    COMPARE_DATA(variableCollection()->index(2, 0, i), "argc");
    COMPARE_DATA(variableCollection()->index(2, 1, i), "1");
    COMPARE_DATA(variableCollection()->index(3, 0, i), "argv");

    breakpoints()->removeRow(0);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testVariablesQuicklySwitchFrame()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg;

    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateLocals);
    TestFrameStackModel *stackModel = session->frameStackModel();

    breakpoints()->addCodeBreakpoint(debugeeFileName, 24);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QTest::qWait(500);

    QModelIndex i = variableCollection()->index(1, 0);
    COMPARE_DATA(i, "Locals");
    QCOMPARE(variableCollection()->rowCount(i), 1);
    COMPARE_DATA(variableCollection()->index(0, 0, i), "i");
    COMPARE_DATA(variableCollection()->index(0, 1, i), "1");

    stackModel->setCurrentFrame(1);
    QTest::qWait(300);
    stackModel->setCurrentFrame(0);
    QTest::qWait(1);
    stackModel->setCurrentFrame(1);
    QTest::qWait(1);
    stackModel->setCurrentFrame(0);
    QTest::qWait(1);
    stackModel->setCurrentFrame(1);
    QTest::qWait(500);

    i = variableCollection()->index(1, 0);
    QCOMPARE(variableCollection()->rowCount(i), 4);
    QStringList locs;
    for (int j = 0; j < variableCollection()->rowCount(i); ++j) {
        locs << variableCollection()->index(j, 0, i).data().toString();
    }
    QVERIFY(locs.contains("argc"));
    QVERIFY(locs.contains("argv"));
    QVERIFY(locs.contains("x"));

    breakpoints()->removeRow(0);
    session->run();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}


void GdbTest::testSegfaultDebugee()
{
    TestDebugSession *session = new TestDebugSession;
    session->variableController()->setAutoUpdate(KDevelop::IVariableController::UpdateLocals);
    TestLaunchConfiguration cfg(KUrl(QDir::currentPath()+"/unittests/debugeecrash"));
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugeecrash.cpp";

    breakpoints()->addCodeBreakpoint(fileName, 23);

    QVERIFY(session->startProgram(&cfg));

    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(session->line(), 23);
    session->run();

    WAIT_FOR_STATE(session, DebugSession::StoppedState);
    QCOMPARE(session->line(), 24);

    session->stopDebugger();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}

void GdbTest::testSwitchFrameGdbConsole()
{
    TestDebugSession *session = new TestDebugSession;

    TestLaunchConfiguration cfg;

    TestFrameStackModel *stackModel = session->frameStackModel();

    breakpoints()->addCodeBreakpoint(debugeeFileName, 24);
    QVERIFY(session->startProgram(&cfg));
    WAIT_FOR_STATE(session, DebugSession::PausedState);
    QCOMPARE(stackModel->currentFrame(), 0);
    stackModel->setCurrentFrame(1);
    QCOMPARE(stackModel->currentFrame(), 1);
    QTest::qWait(500);
    QCOMPARE(stackModel->currentFrame(), 1);

    session->slotUserGDBCmd("print x");
    QTest::qWait(500);
    //currentFrame must not reset to 0; Bug 222882
    QCOMPARE(stackModel->currentFrame(), 1);

}

//Bug 201771
void GdbTest::testInsertAndRemoveBreakpointWhileRunning()
{
    TestDebugSession *session = new TestDebugSession;
    TestLaunchConfiguration cfg(KUrl(QDir::currentPath()+"/unittests/debugeeslow"));
    QString fileName = QFileInfo(__FILE__).dir().path()+"/debugeeslow.cpp";

    session->startProgram(&cfg);

    WAIT_FOR_STATE(session, DebugSession::ActiveState);
    QTest::qWait(2000);
    kDebug() << "adding breakpoint";
    KDevelop::Breakpoint *b = breakpoints()->addCodeBreakpoint(fileName, 23);
    b->setDeleted();
    WAIT_FOR_STATE(session, DebugSession::EndedState);
}


void GdbTest::waitForState(GDBDebugger::DebugSession *session, DebugSession::DebuggerState state,
                            const char *file, int line)
{
    QPointer<GDBDebugger::DebugSession> s(session); //session can get deleted in DebugController
    kDebug() << "waiting for state" << state;
    QTime stopWatch;
    stopWatch.start();
    while (s->state() != state) {
        if (stopWatch.elapsed() > 5000) {
            kWarning() << "current state" << s->state() << "waiting for" << state;
            kFatal() << QString("Didn't reach state in %0:%1").arg(file).arg(line);
        }
        QTest::qWait(20);
        if (!s) {
            if (state == DebugSession::EndedState) break;
            kFatal() << QString("Didn't reach state; session ended in %0:%1").arg(file).arg(line);
        }
    }
    QTest::qWait(100);
}

}

QTEST_KDEMAIN(GDBDebugger::GdbTest, GUI)


#include "gdbtest.moc"
#include "moc_gdbtest.cpp"
