/***************************************************************************
 *   Copyright 2008 Manuel Breugelmans <mbr.nxi@gmail.com>                 *
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

#ifndef KDEVELOP_SHELL_PROJECTCONTROLLERTEST_INCLUDED
#define KDEVELOP_SHELL_PROJECTCONTROLLERTEST_INCLUDED

#include <QtCore/QObject>
#include <KUrl>
#include <QDir>

class QSignalSpy;
namespace KDevelop
{
class Core;
class IProject;
class ProjectController;
}

class FakeFileManager;

class ProjectControllerTest : public QObject
{
Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();

    void openProject();
    void closeProject();
    void openCloseOpen();
    void openMultiple();
    void reopen();
    void reopenWhileLoading();

    void emptyProject();
    void singleFile();
    void singleDirectory();
    void fileInSubdirectory();

private:
    KUrl writeProjectConfig(const QString& name);

    QSignalSpy* createOpenedSpy();
    QSignalSpy* createClosedSpy();
    QSignalSpy* createClosingSpy();

    void assertProjectOpened(const QString& name, KDevelop::IProject*& proj);
    void assertSpyCaughtProject(QSignalSpy* spy, KDevelop::IProject* proj);
    void assertProjectClosed(KDevelop::IProject* proj);
    void assertEmptyProjectModel();

    FakeFileManager* createFileManager();

private:
    KUrl m_projFileUrl;
    KDevelop::Core* m_core;
    KDevelop::ProjectController* m_projCtrl;
    QString m_projName;
    QList<KUrl> m_tmpConfigs;
    QDir m_scratchDir;
    KUrl m_projFolder;

    QList<FakeFileManager*> m_fileManagerGarbage;
};

#endif // KDEVELOP_SHELL_PROJECTCONTROLLERTEST_INCLUDED
