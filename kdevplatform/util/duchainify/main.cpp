/***************************************************************************
 *   Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>                         *
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

#include <shell/core.h>
#include <shell/shellextension.h>
#include <language/backgroundparser/parsejob.h>
#include <interfaces/ilanguage.h>
#include <interfaces/iplugincontroller.h>
#include <interfaces/ilanguagecontroller.h>

#include <KDE/KApplication>
#include <KDE/KCmdLineArgs>
#include <KDE/KAboutData>
#include <KDE/KDebug>

#include <QtCore/QStringList>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <stdio.h>
#include <tests/autotestshell.h>
#include <QDirIterator>
#include <language/backgroundparser/backgroundparser.h>
#include "main.h"
#include <language/duchain/duchain.h>

bool verbose=false, warnings=false;

using namespace KDevelop;

void messageOutput(QtMsgType type, const char *msg)
{
    
    switch (type) {
        case QtDebugMsg:
            if(verbose)
                std::cerr << msg << std::endl;
            break;
        case QtWarningMsg:
            if(warnings)
                std::cerr << msg << std::endl;
            break;
        case QtCriticalMsg:
            std::cerr << msg << std::endl;
            break;
        case QtFatalMsg:
            std::cerr << msg << std::endl;
            abort();
    }
}


Manager::Manager(KCmdLineArgs* args) : m_total(0), m_args(args), m_allFilesAdded(0)
{
}

void Manager::init()
{
    KUrl::List includes;

    if(m_args->count() == 0) {
        std::cerr << "Need directory to duchainify" << std::endl;
        QCoreApplication::exit(1);
    }

    verbose=m_args->isSet("verbose");
    warnings=m_args->isSet("warnings");

    uint features = TopDUContext::VisibleDeclarationsAndContexts;
    if(m_args->isSet("f"))
    {
        QString featuresStr = m_args->getOption("f");
        if(featuresStr == "visible-declarations")
        {
            features = TopDUContext::VisibleDeclarationsAndContexts;
        }
        else if(featuresStr == "all-declarations")
        {
            features = TopDUContext::AllDeclarationsAndContexts;
        }
        else if(featuresStr == "all-declarations-and-uses")
        {
            features = TopDUContext::AllDeclarationsContextsAndUses;
        }
        else if(featuresStr == "all-declarations-and-uses-and-AST")
        {
            features = TopDUContext::AllDeclarationsContextsAndUses | TopDUContext::AST;
        }
        else if(featuresStr == "empty")
        {
            features = TopDUContext::Empty;
        }
        else if(featuresStr == "simplified-visible-declarations")
        {
            features = TopDUContext::SimplifiedVisibleDeclarationsAndContexts;
        }
        else{
            std::cerr << "Wrong feature-string given\n";
            QCoreApplication::exit(2);
        }
    }
    if(m_args->isSet("force-update"))
        features |= TopDUContext::ForceUpdate;
    
    if(m_args->isSet("threads"))
    {
        bool ok = false;
        int count = m_args->getOption("threads").toInt(&ok);
        ICore::self()->languageController()->backgroundParser()->setThreadCount(count);
        if(!ok) {
            std::cerr << "bad thread count\n";
            QCoreApplication::exit(3);
        }
    }

    for(int i=0; i<m_args->count(); i++)
    {
        addToBackgroundParser(m_args->arg(i), (TopDUContext::Features)features);
    }

    m_allFilesAdded = 1;

    if ( m_total ) {
        std::cout << "Added " << m_total << " files to the background parser" << std::endl;
    } else {
        std::cout << "no files added to the background parser" << std::endl;
        QCoreApplication::exit(0);
    }
}

void Manager::updateReady(IndexedString url, ReferencedTopDUContext topContext)
{
    kDebug() << "finished" << url.toUrl().toLocalFile() << "success: " << (bool)topContext;
    
    m_waiting.remove(url.toUrl());
    
    std::cout << "processed " << (m_total - m_waiting.size()) << " out of " << m_total << std::endl;
    
    if(m_waiting.isEmpty() && m_allFilesAdded)
    {
        std::cout << "ready" << std::endl;
        QApplication::quit();
    }
}


void Manager::addToBackgroundParser(QString path, TopDUContext::Features features)
{
    QFileInfo info(path);
    
    if(info.isFile())
    {
        kDebug() << "adding file" << path;
        KUrl pathUrl(path);
        
        m_waiting << pathUrl;
        ++m_total;
        
        KDevelop::DUChain::self()->updateContextForUrl(KDevelop::IndexedString(pathUrl), features, this);
        
    }else if(info.isDir())
    {
        QDirIterator contents(path);
        while(contents.hasNext()) {
            QString newPath = contents.next();
            if(!newPath.endsWith("."))
                addToBackgroundParser(newPath, features);
        }
    }
}

QSet< KUrl > Manager::waiting()
{
    return m_waiting;
}

using namespace KDevelop;
int main(int argc, char** argv)
{
    qInstallMsgHandler(messageOutput);
    KAboutData aboutData( "duchainify", 0, ki18n( "duchainify" ),
                          "1", ki18n("Duchain builder application"), KAboutData::License_GPL,
                          ki18n( "(c) 2009 David Nolden" ), KLocalizedString(), "http://www.kdevelop.org" );
    KCmdLineArgs::init( argc, argv, &aboutData, KCmdLineArgs::CmdLineArgNone );
    KCmdLineOptions options;
    options.add("+dir", ki18n("directory"));
    
    options.add("warnings", ki18n("Show warnings"));
    options.add("verbose", ki18n("Show warnings and debug output"));
    options.add("force-update", ki18n("Enforce an update of the top-contexts corresponding to the given files"));
    options.add("threads <count>", ki18n("Number of threads to use"));
    options.add("f <features>", ki18n("Features to build. Options: empty, simplified-visible-declarations, visible-declarations (default), all-declarations, all-declarations-and-uses, all-declarations-and-uses-and-AST"));
    KCmdLineArgs::addCmdLineOptions( options );

    KCmdLineArgs *args = KCmdLineArgs::parsedArgs();

    KApplication app(false);

    AutoTestShell::init();
    Core::initialize(0, KDevelop::Core::NoUi);
    Manager manager(args);

    QTimer::singleShot(0, &manager, SLOT(init()));
    return app.exec();
}

#include "main.moc"
