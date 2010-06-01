/* KDevelop CMake Support
 *
 * Copyright 2006 Matt Rogers <mattr@kde.org>
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

#include "cmakeloadprojecttest.h"

#include <language/duchain/duchainlock.h>
#include <language/duchain/duchain.h>
#include <language/duchain/topducontext.h>


#include "cmListFileLexer.h"
#include "cmakelistsparser.h"
#include "cmakeprojectvisitor.h"
#include "cmakeast.h"
#include <cmakeparserutils.h>

QTEST_MAIN( CMakeLoadProjectTest )

using namespace KDevelop;

CMakeLoadProjectTest::CMakeLoadProjectTest()
{
}

CMakeLoadProjectTest::~CMakeLoadProjectTest()
{
}

void CMakeLoadProjectTest::testTinyCMakeProject()
{
    CMakeProjectVisitor v = parseProject( QString(CMAKE_TESTS_PROJECTS_DIR)+"/tiny_project" );
    QCOMPARE(v.targets().count(), 1);
    QCOMPARE(v.targets().at( 0 ).name, QString("foo") );
    QCOMPARE(v.targets().at( 0 ).files, QStringList() << "foo.cpp" );
}

void CMakeLoadProjectTest::testSmallQt4Project()
{
    CMakeProjectVisitor v = parseProject(CMAKE_TESTS_PROJECTS_DIR "/qt4app");
    QCOMPARE(v.targets().count(), 1);
    QCOMPARE(v.projectName(), QString("qt4app"));
    QCOMPARE(v.targets().at( 0 ).name, QString("qt4app") );
    QCOMPARE(v.targets().at( 0 ).files, QStringList() << "qt4app.cpp" << "main.cpp" );
}


void CMakeLoadProjectTest::testSmallKDE4Project()
{
    CMakeProjectVisitor v = parseProject(CMAKE_TESTS_PROJECTS_DIR "/kde4app");
    QCOMPARE(v.targets().count(), 2);
    QCOMPARE(v.projectName(), QString("kde4app"));
    QCOMPARE(v.targets().at( 0 ).name, QString("kde4app") );
    QCOMPARE(v.targets().at( 0 ).files, QStringList() << "kde4app.cpp" << "main.cpp" << "kde4appview.cpp" 
                                                      << CMAKE_TESTS_PROJECTS_DIR "/kde4app/ui_kde4appview_base.h" 
                                                      << CMAKE_TESTS_PROJECTS_DIR "/kde4app/ui_prefs_base.h" 
                                                      << CMAKE_TESTS_PROJECTS_DIR "/kde4app/settings.cpp" 
                                                      << CMAKE_TESTS_PROJECTS_DIR "/kde4app/settings.h" );
}

CMakeProjectVisitor CMakeLoadProjectTest::parseProject( const QString& sourcedir )
{
    QString projectfile = sourcedir+"/CMakeLists.txt";
    CMakeFileContent code=CMakeListsParser::readCMakeFile(projectfile);

    QPair<VariableMap,QStringList> initials = CMakeParserUtils::initialVariables();
    MacroMap mm;
    VariableMap vm = initials.first;
    CacheValues cv;
    vm.insert("CMAKE_SOURCE_DIR", QStringList(sourcedir));
    
    KDevelop::ReferencedTopDUContext buildstrapContext=new TopDUContext(IndexedString("buildstrap"), SimpleRange(0,0, 0,0));
    DUChain::self()->addDocumentChain(buildstrapContext);
    ReferencedTopDUContext ref=buildstrapContext;
    QStringList modulesPath = vm["CMAKE_MODULE_PATH"];
    foreach(const QString& script, initials.second)
    {
        ref = CMakeParserUtils::includeScript(CMakeProjectVisitor::findFile(script, modulesPath, QStringList()), ref, &vm, &mm, sourcedir, &cv, modulesPath );
    }
    
    vm.insert("CMAKE_CURRENT_BINARY_DIR", QStringList(sourcedir));
    vm.insert("CMAKE_CURRENT_LIST_FILE", QStringList(projectfile));
    vm.insert("CMAKE_CURRENT_SOURCE_DIR", QStringList(sourcedir));

    CMakeProjectVisitor v(projectfile, ref);
    v.setVariableMap(&vm);
    v.setMacroMap(&mm);
    v.setCacheValues(&cv);
    v.setModulePath(modulesPath);
    v.walk(code, 0);

    ReferencedTopDUContext ctx=v.context();
    return v;
}

#include "cmakeloadprojecttest.moc"

