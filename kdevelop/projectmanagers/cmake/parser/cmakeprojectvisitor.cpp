/* KDevelop CMake Support
 *
 * Copyright 2007-2008 Aleix Pol <aleixpol@gmail.com>
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

#include "cmakeprojectvisitor.h"
#include "cmakeast.h"
#include "cmakecondition.h"
#include "astfactory.h"

#include <language/editor/simplerange.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/parsingenvironment.h>
#include <language/duchain/declaration.h>
#include <language/duchain/types/functiontype.h>
#include <language/duchain/types/delayedtype.h>

#include <KProcess>
#include <KLocale>
#include <KDebug>
#include <QHash>
#include <QQueue>
#include <QFile>
#include <QDir>
#include <QtCore/qglobal.h>
#include <QByteArray>
#include <QFileInfo>
#include <QScriptEngine>
#include <QScriptValue>
#include <language/editor/editorintegrator.h>
#include <language/duchain/smartconverter.h>

using namespace KDevelop;

void debugMsgs(const QString& message) { kDebug(9032) << "message:" << message; }

CMakeProjectVisitor::message_callback CMakeProjectVisitor::s_msgcallback=debugMsgs;

CMakeProjectVisitor::CMakeProjectVisitor(const QString& root, ReferencedTopDUContext parent)
    : m_root(root), m_vars(0), m_macros(0), m_topctx(0), m_parentCtx(parent)
{
}

QStringList CMakeProjectVisitor::envVarDirectories(const QString &varName)
{
    QString env=QString::fromLatin1(qgetenv(varName.toLatin1()));
//     kDebug(9042) << ".......resolving env:" << varName << "=" << QProcess::systemEnvironment() << env;
    if(!env.isEmpty())
    {
        QChar separator;
#ifdef Q_OS_WIN
        separator = ';';
#else
        separator = ':';
#endif
        kDebug(9042) << "resolving env:" << varName << "=" << env;
        return env.split(separator);
    }
    else
    {
        kDebug(9032) << "warning:" << varName << " not found";
        return QStringList();
    }
}

QList< CMakeProjectVisitor::IntPair > CMakeProjectVisitor::parseArgument(const QString &exp)
{
    QString name;
    QStack<int> opened;
    QList< IntPair > pos;
    bool gotDollar=false;
    for(int i=exp.indexOf('$'); i<exp.size(); i++)
    {
        switch(exp[i].unicode())
        {
            case '$':
                gotDollar=true;
                break;
            case '{':
                if(gotDollar)
                {
                    opened.push(i);
                }
                gotDollar=false;
                break;
            case '}':
                if(!opened.isEmpty())
                    pos.append(IntPair(opened.pop(), i, opened.count()));
                break;
        }
    }

    for(int i=pos.count()-1; i>=0 && !opened.isEmpty(); i--)
    {
        if(pos[i].first==opened.top())
            opened.pop();
        pos[i].level -= opened.size();
    }
    return pos;
}

QStringList CMakeProjectVisitor::theValue(const QString& exp, const IntPair& thecase) const
{
    int dollar=exp.lastIndexOf('$', thecase.first);
    QString type=exp.mid(dollar+1, thecase.first-dollar-1);
    QString var=exp.mid(thecase.first+1, thecase.second-thecase.first-1);
    QStringList vars;
//     kDebug() << "lalalallalala" << exp << thecase.print();
    if(type.isEmpty())
    {
        if(m_vars->contains(var))
            vars = m_vars->value(var);
        else if(m_cache->contains(var))
            vars = m_cache->value(var).value.split(';');
    }
    else if(type=="ENV")
    {
        vars=envVarDirectories(var);
    }
    else
        kDebug() << "error: I do not understand the key: " << type;

//     kDebug() << "solving: " << var << vars << exp;
    return vars;
}

QString replaceOne(const QString& var, const QString& id, const QString& value, int dollar)
{
//     kDebug() << "ooo" << var << value << id << var[dollar+id.size()-1] << (dollar+id.size());
//     kDebug() << "kkkk" << var.mid(0, dollar) << value << var.mid(dollar+id.size(), var.size()-(dollar+id.size()));
    return var.mid(0, dollar)+value+var.mid(dollar+id.size(), var.size()-(dollar+id.size()));
}

QStringList CMakeProjectVisitor::value(const QString& exp, const QList<IntPair>& poss, int& desired) const
{
    QString var=exp;
    QList<IntPair> invars;
    invars += poss[desired];
    //kDebug() << ">>>>>" << exp << desired << poss.count();
    for(; desired+1<poss.size() && poss[desired].level>1; desired++)
    {
        invars+=poss[desired+1];
        //kDebug() << "poss@"<< desired+1 << "="<< poss[desired+1].print();
    }

    //kDebug() << ";;;;;" << invars.count();
    if(invars.count()>1)
    {
        QList<IntPair>::const_iterator itConstEnd=invars.constEnd();
        QList<IntPair>::iterator itEnd=invars.end();
        QList<IntPair>::iterator itBegin=invars.begin();
        for(QList<IntPair>::const_iterator it=invars.constBegin(); (it+1)!=itConstEnd; ++it)
        {
            const IntPair& subvar=*it;
            int dollar=var.lastIndexOf('$', subvar.first);
            QString id=var.mid(dollar, subvar.second-dollar+1), value=theValue(var, subvar).join(QChar(';'));

            int diff=value.size()-id.size();
            for(QList<IntPair>::iterator it=itBegin; it!=itEnd; ++it)
            {
                if(it->first > subvar.first) it->first += diff;
                if(it->second> subvar.second) it->second+= diff;
            }

            var=replaceOne(var, id, value, dollar);
        }
    }
    return theValue(var, invars.last());
}

QStringList CMakeProjectVisitor::resolveVariable(const CMakeFunctionArgument &exp)
{
    QStringList ret;
    ret += QString();
    QList< IntPair > var = parseArgument(exp.value);

    int i=0;
    IntPair last(-1,-1, 0);

    for(QList<IntPair>::const_iterator it=var.constBegin(); it!=var.constEnd(); ++it, ++i)
    {
        while(it!=var.constEnd() && it->level>1)
            ++it;

        const IntPair& p=*it;
//         kDebug () << "reeeeeet" << ret << exp.value << p.print();
        int dollar=exp.value.lastIndexOf('$', p.first);
        QString pre=exp.value.mid(last.second+1, dollar-last.second-1);

        QStringList vars = value(exp.value, var, i);
//         kDebug() << "aaaaaaaaaA" << pre << vars;

        if(!vars.isEmpty())
        {
            pre+=vars.takeFirst();
        }
        ret.last()+=pre;
        ret += vars;
        last=p;
        
//         kDebug() << "yaaaaaaa" << ret;
//         i++;
    }
    ret.last().append(exp.value.mid(last.second+1, exp.value.count()-last.second));

    if(exp.quoted)
    {
        ret=QStringList(ret.join(QChar(';')));
    }
    return ret;
}

int CMakeProjectVisitor::notImplemented(const QString &name) const
{
    kDebug(9042) << "not implemented!" << name;
    return 1;
}

bool CMakeProjectVisitor::hasMacro(const QString& name) const
{
    Q_ASSERT(m_macros);
    return m_macros->contains(name);
}

int CMakeProjectVisitor::visit(const CMakeAst *ast)
{
    kDebug(9042) << "error! function not implemented" << ast->content()[ast->line()].name;
    foreach(const CMakeFunctionArgument& arg, ast->outputArguments())
    {
        //NOTE: this is a workaround, but fixes some issues.
        kDebug(9042) << "reseting: " << arg.value;
        m_vars->insert(arg.value, QStringList());
    }
    return 1;
}

int CMakeProjectVisitor::visit( const AddTestAst * test)
{
    Q_UNUSED(test);
    return 1;
}

int CMakeProjectVisitor::visit(const ProjectAst *project)
{
    m_projectName = project->projectName();
    if(!m_vars->contains("CMAKE_PROJECT_NAME"))
        m_vars->insert("CMAKE_PROJECT_NAME", QStringList(project->projectName()));
    
    m_vars->insert("PROJECT_NAME", QStringList(project->projectName()));
    m_vars->insert("PROJECT_SOURCE_DIR", QStringList(m_root));
    m_vars->insert("PROJECT_BINARY_DIR", m_vars->value("CMAKE_CURRENT_BINARY_DIR"));
    m_vars->insert(QString("%1_SOURCE_DIR").arg(m_projectName), QStringList(m_root));
    m_vars->insert(QString("%1_BINARY_DIR").arg(m_projectName), m_vars->value("CMAKE_CURRENT_BINARY_DIR"));
    return 1;
}

int CMakeProjectVisitor::visit( const SetTargetPropsAst * targetProps)
{
    kDebug(9042) << "setting target props for " << targetProps->targets() << targetProps->properties();
    foreach(const QString& tname, targetProps->targets())
    {
        foreach(const SetTargetPropsAst::PropPair& t, targetProps->properties())
        {
            m_props[TargetProperty][tname][t.first] = t.second.split(';');
        }
    }
    return 1;
}

int CMakeProjectVisitor::visit( const SetDirectoryPropsAst * dirProps)
{   
    QString dir=m_vars->value("CMAKE_CURRENT_SOURCE_DIR").join(QString());
    kDebug(9042) << "setting directory props for " << dirProps->properties() << dir;
    foreach(const SetDirectoryPropsAst::PropPair& t, dirProps->properties())
    {
        m_props[DirectoryProperty][dir][t.first] = t.second.split(';');
    }
    return 1;
}

int CMakeProjectVisitor::visit( const GetTargetPropAst * prop)
{
    kDebug(9042) << "getting target " << prop->target() << " prop " << prop->property() << prop->variableName();
    if(m_props[TargetProperty].contains(prop->target()) && !m_props[TargetProperty][prop->target()].contains(prop->property())) {
        if(prop->property().startsWith("LOCATION_") && m_props[TargetProperty][prop->target()].contains("IMPORTED_"+prop->property()))
            m_props[TargetProperty][prop->target()][prop->property()]=m_props[TargetProperty][prop->target()]["IMPORTED_"+prop->property()];
            
//         kDebug(9032) << "unexistent property" << prop->property() << "on" << prop->target();
    }
//     kDebug(9042) << "current properties" << m_props[TargetProperty][prop->target()].keys();
    
    m_vars->insert(prop->variableName(), m_props[TargetProperty][prop->target()][prop->property()]);
//     kDebug(9042) << "goooooot" << m_vars->value(prop->variableName());
    return 1;
}

int CMakeProjectVisitor::visit(const AddSubdirectoryAst *subd)
{
    kDebug(9042) << "adding subdirectory" << subd->sourceDir();

    VisitorState p=stackTop();

    Subdirectory d;
    d.name=subd->sourceDir();
    d.build_dir=subd->binaryDir().isEmpty() ? d.name : subd->binaryDir();
    d.desc=p.code->at(p.line);
    
    m_subdirectories += d;
    return 1;
}

int CMakeProjectVisitor::visit(const SubdirsAst *sdirs)
{
    kDebug(9042) << "adding subdirectories" << sdirs->directories() << sdirs->exluceFromAll();
    VisitorState p=stackTop();
    CMakeFunctionDesc desc=p.code->at(p.line);
    
    foreach(const QString& dir, sdirs->directories() + sdirs->exluceFromAll()) {
        Subdirectory d;
        d.name=dir;
        d.build_dir=dir;
        d.desc=desc;
        
        m_subdirectories += d;
    }
    return 1;
}

void CMakeProjectVisitor::printBacktrace(const QStack<VisitorState> &backtrace)
{
    int i=0;
    kDebug() << "backtrace" << backtrace.count();
    foreach(const VisitorState& v, backtrace)
    {
        if(v.code->count()>v.line)
          kDebug(9042) << i << ": ";//           << v.code->at(v.line).name;
        else
          kDebug(9042) << i << ": ------------------------";
        i++;
    }
}

CMakeProjectVisitor::VisitorState CMakeProjectVisitor::stackTop() const
{
    VisitorState p;
    QString filename=m_backtrace.front().code->at(m_backtrace.front().line).filePath;
    QStack<VisitorState>::const_iterator it=m_backtrace.constBegin();

    for(; it!=m_backtrace.constEnd(); ++it)
    {
        if(filename!=it->code->at(it->line).filePath)
            break;

        p=*it;
    }
    return p;
}

void CMakeProjectVisitor::defineTarget(const QString& id, const QStringList& sources, Target::Type t)
{
    kDebug(9042) << "Defining target" << id;
    if (m_targetForId.contains(id))
        kDebug(9032) << "warning! there already was a target called" << id;

    VisitorState p=stackTop();

    Declaration *d=0;
    if(!p.code->at(p.line).arguments.isEmpty()) {
        DUChainWriteLocker lock(DUChain::lock());
        d= new Declaration(p.code->at(p.line).arguments.first().range(), p.context);
        d->setIdentifier( Identifier(id) );
    }
    
    Target target;
    target.name=id;
    target.declaration=d;
    target.files=sources;
    target.type=t;
    target.desc=p.code->at(p.line);
    
    m_targetForId[id]=target;

    QString exe=id;
    switch(t) {
        case Target::Executable:
            exe += m_vars->value("CMAKE_EXECUTABLE_SUFFIX").join(QString());
            break;
        case Target::Library:
            exe = QString("%1%2%3").arg(m_vars->value("CMAKE_LIBRARY_PREFIX").join(QString())).arg(id).arg(m_vars->value("CMAKE_LIBRARY_SUFFIX").join(QString()));
            break;
        case Target::Custom:
            break;
    }
    
    m_props[TargetProperty][id]["LOCATION"]=QStringList(m_vars->value("CMAKE_CURRENT_BINARY_DIR").join(QString())+'/'+exe);
}

int CMakeProjectVisitor::visit(const AddExecutableAst *exec)
{
    if(!exec->isImported())
        defineTarget(exec->executable(), exec->sourceLists(), Target::Executable);
    else
        kDebug(9042) << "imported executable" << exec->executable();
    kDebug(9042) << "exec:" << exec->executable() << "->" << m_targetForId.contains(exec->executable())
        << "imported" << exec->isImported();
    return 1;
}

int CMakeProjectVisitor::visit(const AddLibraryAst *lib)
{
    if(!lib->isImported())
        defineTarget(lib->libraryName(), lib->sourceLists(), Target::Library);
    kDebug(9042) << "lib:" << lib->libraryName();
    return 1;
}

int CMakeProjectVisitor::visit(const SetAst *set)
{
    //TODO: Must deal with ENV{something} case
    QStringList values;
    if(set->storeInCache() && m_cache->contains(set->variableName()))
        values = m_cache->value(set->variableName()).value.split(';');
    else
        values = set->values();
    kDebug(9042) << "setting variable:" << set->variableName()/* << "to" << values*/;
    m_vars->insert(set->variableName(), values);
    return 1;
}

int CMakeProjectVisitor::visit(const IncludeDirectoriesAst * dirs)
{
    kDebug(9042) << "adding include directories" << dirs->includedDirectories();
    IncludeDirectoriesAst::IncludeType t = dirs->includeType();

    QStringList toInclude = dirs->includedDirectories();

    if(t==IncludeDirectoriesAst::Default)
    {
        if(m_vars->contains("CMAKE_INCLUDE_DIRECTORIES_BEFORE") && m_vars->value("CMAKE_INCLUDE_DIRECTORIES_BEFORE")[0]=="ON")
            t = IncludeDirectoriesAst::Before;
        else
            t = IncludeDirectoriesAst::After;
    }

    if(t==IncludeDirectoriesAst::After)
        m_includeDirectories += toInclude;
    else
        m_includeDirectories = toInclude + m_includeDirectories;
    kDebug(9042) << "done." << m_includeDirectories;
    return 1;
}

QString CMakeProjectVisitor::findFile(const QString &file, const QStringList &folders,
        const QStringList& suffixes, bool location)
{
    if( file.isEmpty() || QFileInfo(file).isAbsolute() )
         return file;

    QStringList suffixFolders, useSuffixes(suffixes);
    useSuffixes.prepend(QString());
    foreach(const QString& apath, folders)
    {
        foreach(const QString& suffix, useSuffixes)
        {
            suffixFolders.append(apath+'/'+suffix);
        }
    }

    KUrl path;
    foreach(const QString& mpath, suffixFolders)
    {
        if(mpath.isEmpty())
            continue;

        KUrl afile(mpath);
        afile.addPath(file);
        kDebug(9042) << "Trying:" << mpath << '.' << file;
        if(QFile::exists(afile.toLocalFile()))
        {
            if(location)
                path=mpath;
            else
                path=afile;
            break;
        }
    }
    //kDebug(9042) << "find file" << file << "into:" << folders << "found at:" << path;
    return path.toLocalFile(KUrl::RemoveTrailingSlash);
}

int CMakeProjectVisitor::visit(const IncludeAst *inc)
{
    Q_ASSERT(m_vars->contains("CMAKE_CURRENT_SOURCE_DIR"));
    const QStringList modulePath = m_vars->value("CMAKE_MODULE_PATH") + m_modulePath + m_vars->value("CMAKE_CURRENT_SOURCE_DIR");
    kDebug(9042) << "Include:" << inc->includeFile() << "@" << modulePath << " into ";

    QString possib=inc->includeFile();
    QString path;
    if(!KUrl(possib).isRelative() && QFile::exists(possib))
        path=possib;
    else
    {
        if(!possib.contains('.'))
            possib += ".cmake";
        path=findFile(possib, modulePath);
    }

    if(!path.isEmpty())
    {
        m_vars->insertMulti("CMAKE_CURRENT_LIST_FILE", QStringList(path));
        CMakeFileContent include = CMakeListsParser::readCMakeFile(path);
        if ( !include.isEmpty() )
        {
            kDebug(9042) << "including:" << path;
            walk(include, 0, true);
        }
        else
        {
            //FIXME: Put here the error.
            kDebug(9042) << "Include. Parsing error.";
        }
        m_vars->take("CMAKE_CURRENT_LIST_FILE");
    }
    else
    {
        if(!inc->optional())
        {
            kDebug(9032) << "error!! Could not find" << inc->includeFile() << "=" << possib << "into" << modulePath;
        }
    }

    if(!inc->resultVariable().isEmpty())
    {
        QString result="NOTFOUND";
        if(!path.isEmpty())
            result=path;
        m_vars->insert(inc->resultVariable(), QStringList(result));
    }
    kDebug(9042) << "include of" << inc->includeFile() << "done.";
    return 1;
}

int CMakeProjectVisitor::visit(const FindPackageAst *pack)
{
    if(!haveToFind(pack->name()))
        return 1;
    const QStringList modulePath = m_vars->value("CMAKE_MODULE_PATH") + m_modulePath;
    kDebug(9042) << "Find:" << pack->name() << "package." << m_modulePath << "No module: " << pack->noModule();

    QStringList possibleModuleNames;
    if(!pack->noModule()) //TODO Also implied by a whole slew of additional options.
    {
        // Look for a Find{package}.cmake
        QString possib=pack->name();
        if(!possib.endsWith(".cmake"))
            possib += ".cmake";
        possib.prepend("Find");
        possibleModuleNames += possib;
    }

    QString var="CMAKE_INSTALL_PREFIX";
    QString instPath;
    if(m_vars->contains(var))
        instPath = m_vars->value(var).join(QString());
    else if(m_cache->contains(var))
        instPath = m_cache->value(var).value;

    kDebug(9042) << "config mode" << m_vars->value(var).join(QString()) << m_cache->value(var).value << instPath;

    #if defined(Q_OS_WIN)
    modulePath.prepend(instPath);
    modulePath.prepend(instPath+"/cmake");
    #endif
    QString name=pack->name();
    QStringList postfix=QStringList() << QString() << "/cmake" << "/CMake";
    QStringList configPath;
    foreach(const QString& post, postfix)
    {
        configPath.prepend(instPath+"/share/"+name.toLower()+post);
        configPath.prepend(instPath+"/lib/"+name.toLower()+post);
    }

    QString varName=pack->name()+"_DIR";
    if(m_cache->contains(varName))
        configPath.prepend(m_cache->value(varName).value);

    QStringList possibleConfigNames;
    possibleConfigNames+=QString("%1Config.cmake").arg(pack->name());
    possibleConfigNames+=QString("%1-config.cmake").arg(pack->name().toLower());

    QString path;
    foreach(const QString& possib, possibleModuleNames)
    {
        path=findFile(possib, modulePath);
        if(!path.isEmpty()) {
            break;
        }
    }
    if (path.isEmpty()) {
        foreach(const QString& possib, possibleConfigNames) {
            path = findFile(possib, configPath);
            if (!path.isEmpty()) {
                m_vars->insert(pack->name()+"_DIR", QStringList(KUrl(path).directory()));
                break;
            }
        }
    }

    if(!path.isEmpty())
    {
        m_vars->insertMulti("CMAKE_CURRENT_LIST_FILE", QStringList(path));
        if(pack->isRequired())
            m_vars->insert(pack->name()+"_FIND_REQUIRED", QStringList("TRUE"));
        if(pack->isQuiet())
            m_vars->insert(pack->name()+"_FIND_QUIET", QStringList("TRUE"));
        CMakeFileContent package=CMakeListsParser::readCMakeFile( path );
        if ( !package.isEmpty() )
        {
            path=KUrl(path).pathOrUrl();
            kDebug(9042) << "================== Found" << path << "===============";
            walk(package, 0, true);
        }
        else
        {
            kDebug(9032) << "error: find_package. Parsing error." << path;
        }

        if(pack->noModule())
        {
            m_vars->insert(QString("%1_CONFIG").arg(pack->name()), QStringList(path));
        }
        m_vars->take("CMAKE_CURRENT_LIST_FILE");
    }
    else
    {
        if(pack->isRequired()) {
            //FIXME: Put here the error.
            kDebug(9032) << "error: Could not find" << pack->name() << "into" << modulePath;
        }
        m_vars->insert(QString("%1_DIR").arg(pack->name()), QStringList(QString("%1_DIR-NOTFOUND").arg(pack->name())));
    }
    kDebug(9042) << "Exit. Found:" << pack->name() << m_vars->value(pack->name()+"_FOUND");

    return 1;
}

KDevelop::ReferencedTopDUContext CMakeProjectVisitor::createContext(const KUrl& path, ReferencedTopDUContext aux,
                                                                    int endl ,int endc, bool isClean)
{
    DUChainWriteLocker lock(DUChain::lock());
    KDevelop::ReferencedTopDUContext topctx=DUChain::self()->chainForDocument(path);
    
    if(topctx)
    {
        EditorIntegrator editor;
        editor.setCurrentUrl(topctx->url());
        
        SmartConverter converter(&editor);
        converter.deconvertDUChain(topctx);
        
        if(isClean) {
            topctx->deleteLocalDeclarations();
            topctx->deleteChildContextsRecursively();
            topctx->deleteUses();
        }
    }
    else
    {
        IndexedString idxpath(path);
        topctx=new TopDUContext(idxpath, SimpleRange(0,0, endl, endc),
                                new ParsingEnvironmentFile(idxpath));
        DUChain::self()->addDocumentChain(topctx);

        Q_ASSERT(DUChain::self()->chainForDocument(path));
    }
    
    //Clean the re-used top-context. This is problematic since it may affect independent projects, but it's better then letting things accumulate.
    ///@todo This is problematic when the same file is used from within multiple CMakeLists.txts,
    ///      for example a standard import like FindKDE4.cmake, because it creates a cross-dependency
    ///      between the topducontext's of independent projects, like for example kdebase and kdevplatform
    ///@todo Solve that by creating unique versions of all used top-context on a per-project basis using ParsignEnvironmentFile for disambiguation.
    
    foreach(DUContext* importer, topctx->importers())
        importer->removeImportedParentContext(topctx);
    topctx->clearImportedParentContexts();
    
    topctx->addImportedParentContext(aux);

    /// @todo should we check for NULL or assert?
    if (aux)
      aux->addImportedParentContext(topctx);
    
    return topctx;
}

bool CMakeProjectVisitor::haveToFind(const QString &varName)
{
    if(m_vars->contains(varName+"_FOUND"))
        return false;
    else if(m_vars->contains(varName+"-NOTFOUND"))
        m_vars->remove(varName+"-NOTFOUND");
    return true;
}

int CMakeProjectVisitor::visit(const FindProgramAst *fprog)
{
    if(!haveToFind(fprog->variableName()))
        return 1;
    if(m_cache->contains(fprog->variableName()))
    {
        kDebug(9042) << "FindProgram: cache" << fprog->variableName() << m_cache->value(fprog->variableName()).value;
        m_vars->insert(fprog->variableName(), m_cache->value(fprog->variableName()).value.split(';'));
        return 1;
    }

    QStringList modulePath = fprog->path();
#ifdef Q_OS_WIN
    if(!fprog->noSystemEnvironmentPath() && !fprog->noDefaultPath())
        modulePath += envVarDirectories("Path");
    kDebug() << "added Path env for program finding" << envVarDirectories("Path");
#else
    if(!fprog->noSystemEnvironmentPath() && !fprog->noDefaultPath())
        modulePath += envVarDirectories("PATH");
#endif
    kDebug(9042) << "Find:" << fprog->variableName() << fprog->filenames() << "program into" << modulePath<<":"<< fprog->path();
    QString path;
    foreach(const QString& filename, fprog->filenames())
    {
        path=findExecutable(filename, modulePath, fprog->pathSuffixes());
        if(!path.isEmpty())
            break;
    }

    if(!path.isEmpty())
        m_vars->insert(fprog->variableName(), QStringList(path));
    else
        m_vars->insert(fprog->variableName()+"-NOTFOUND", QStringList());

    kDebug(9042) << "FindProgram:" << fprog->variableName() << "=" << m_vars->value(fprog->variableName()) << modulePath;
    return 1;
}

QString CMakeProjectVisitor::findExecutable(const QString& file,
                const QStringList& directories, const QStringList& pathSuffixes) const
{
    QString path;
    QStringList suffixes=m_vars->value("CMAKE_EXECUTABLE_SUFFIX");
    suffixes.prepend(QString());
    kDebug() << "finding executable, using suffixes" << suffixes;

    foreach(const QString& suffix, suffixes)
    {
        path=findFile(file+suffix, directories, pathSuffixes);
        if(!path.isEmpty())
            break;
    }
    return path;
}

int CMakeProjectVisitor::visit(const FindPathAst *fpath)
{
    if(!haveToFind(fpath->variableName()))
        return 1;
    if(m_cache->contains(fpath->variableName()))
    {
        kDebug() << "FindPath: cache" << fpath->variableName();
        m_vars->insert(fpath->variableName(), m_cache->value(fpath->variableName()).value.split(';'));
        return 1;
    }

    bool error=false;
    QStringList locationOptions = fpath->path()+fpath->hints();
    QStringList path, files=fpath->filenames();
    QStringList suffixes=fpath->pathSuffixes();

    if(!fpath->noDefaultPath())
    {
        QStringList pp=m_vars->value("CMAKE_PREFIX_PATH");
        foreach(const QString& path, pp) {
            locationOptions += path+"/include";
        }
        locationOptions += pp;
        locationOptions += m_vars->value("CMAKE_INCLUDE_PATH");
        locationOptions += m_vars->value("CMAKE_FRAMEWORK_PATH");
        
        pp=m_vars->value("CMAKE_SYSTEM_PREFIX_PATH");
        foreach(const QString& path, pp) {
            locationOptions += path+"/include";
        }
        locationOptions += m_vars->value("CMAKE_SYSTEM_INCLUDE_PATH");
        locationOptions += m_vars->value("CMAKE_SYSTEM_FRAMEWORK_PATH");
    }

    kDebug(9042) << "Find:" << /*locationOptions << "@" <<*/ fpath->variableName() << /*"=" << files <<*/ " path.";
    foreach(const QString& p, files)
    {
        QString p1=findFile(p, locationOptions, suffixes, true);
        if(p1.isEmpty())
        {
            kDebug(9042) << p << "not found";
            error=true;
        }
        else
        {
            path += p1;
        }
    }

    if(!path.isEmpty())
    {
        m_vars->insert(fpath->variableName(), QStringList(path));
    }
    else
    {
        kDebug(9042) << "Path not found";
    }
    kDebug(9042) << "Find path: " << fpath->variableName() << m_vars->value(fpath->variableName());
//     m_vars->insert(fpath->variableName()+"-NOTFOUND", QStringList());
    return 1;
}

int CMakeProjectVisitor::visit(const FindLibraryAst *flib)
{
    if(!haveToFind(flib->variableName()))
        return 1;
    if(m_cache->contains(flib->variableName()))
    {
        kDebug(9042) << "FindLibrary: cache" << flib->variableName();
        m_vars->insert(flib->variableName(), m_cache->value(flib->variableName()).value.split(';'));
        return 1;
    }

    bool error=false;
    QStringList locationOptions = flib->path()+flib->hints();
    QStringList files=flib->filenames();
    QString path;

    if(!flib->noDefaultPath())
    {

        QStringList opt=m_vars->value("CMAKE_PREFIX_PATH");
        foreach(const QString& s, opt)
            locationOptions.append(s+"/lib");

        locationOptions += m_vars->value("CMAKE_LIBRARY_PATH");
        locationOptions += m_vars->value("CMAKE_FRAMEWORK_PATH");
        
        locationOptions += m_vars->value("CMAKE_SYSTEM_LIBRARY_PATH");
        
        opt=m_vars->value("CMAKE_SYSTEM_PREFIX_PATH");
        foreach(const QString& s, opt)
            locationOptions.append(s+"/lib");
    }
    
    foreach(const QString& p, files)
    {
        foreach(const QString& prefix, m_vars->value("CMAKE_FIND_LIBRARY_PREFIXES"))
        {
            foreach(const QString& suffix, m_vars->value("CMAKE_FIND_LIBRARY_SUFFIXES"))
            {
                QString p1=findFile(prefix+p+suffix, locationOptions, flib->pathSuffixes());
                if(p1.isEmpty())
                {
                    kDebug(9042) << p << "not found";
                    error=true;
                }
                else
                {
                    path = p1;
                    break;
                }
            }
            if(!path.isEmpty())
                break;
        }
        if(!path.isEmpty())
            break;
    }

    if(!path.isEmpty())
    {
        m_vars->insert(flib->variableName(), QStringList(path));
    }
    else
        kDebug(9032) << "error. Library" << flib->filenames() << "not found";
//     m_vars->insert(fpath->variableName()+"-NOTFOUND", QStringList());
    kDebug(9042) << "Find Library:" << flib->filenames() << m_vars->value(flib->variableName());
    return 1;
}

int CMakeProjectVisitor::visit(const FindFileAst *ffile)
{
    if(!haveToFind(ffile->variableName()))
        return 1;
    if(m_cache->contains(ffile->variableName()))
    {
        kDebug(9042) << "FindFile: cache" << ffile->variableName();
        m_vars->insert(ffile->variableName(), m_cache->value(ffile->variableName()).value.split(';'));
        return 1;
    }

    bool error=false;
    QStringList locationOptions = ffile->path()+ffile->hints();
    if(!ffile->noDefaultPath())
    {
        QStringList pp=m_vars->value("CMAKE_PREFIX_PATH");
        foreach(const QString& path, pp) {
            locationOptions += path+"/include";
        }
        locationOptions += pp;
        locationOptions += m_vars->value("CMAKE_INCLUDE_PATH");
        locationOptions += m_vars->value("CMAKE_FRAMEWORK_PATH");
        
        pp=m_vars->value("CMAKE_SYSTEM_PREFIX_PATH");
        foreach(const QString& path, pp) {
            locationOptions += path+"/include";
        }
        locationOptions += m_vars->value("CMAKE_SYSTEM_INCLUDE_PATH");
        locationOptions += m_vars->value("CMAKE_SYSTEM_FRAMEWORK_PATH");
    }
    QStringList path, files=ffile->filenames();

    kDebug(9042) << "Find File:" << ffile->filenames();
    foreach(const QString& p, files)
    {
        QString p1=findFile(p, locationOptions, ffile->pathSuffixes());
        if(p1.isEmpty())
        {
            kDebug(9042) << p << "not found";
            error=true;
        }
        else
        {
            path += p1;
        }
    }

    if(!path.isEmpty())
    {
        m_vars->insert(ffile->variableName(), QStringList(path));
    }
    else
        kDebug(9032) << "error. File" << ffile->filenames() << "not found";
//     m_vars->insert(fpath->variableName()+"-NOTFOUND", QStringList());
    return 1;
}


int CMakeProjectVisitor::visit(const TryCompileAst *tca)
{
    kDebug(9042) << "try_compile" << tca->resultName() << tca->binDir() << tca->source()
            << "cmakeflags" << tca->cmakeFlags() << "outputvar" << tca->outputName();
    if(m_projectName.isEmpty())
    {
        kDebug(9042) << "file compile" << tca->compileDefinitions() << tca->copyFile();
    }
    else
    {
        kDebug(9042) << "project compile" << tca->projectName() << tca->targetName();
    }
    
    QString value;
    if(m_cache->contains(tca->resultName()))
        value=m_cache->value(tca->resultName()).value;
    else
        value="TRUE";
    
    m_vars->insert(tca->resultName(), QStringList(value));
    return 1;
}


int CMakeProjectVisitor::visit(const TargetLinkLibrariesAst *)
{
    kDebug(9042) << "target_link_libraries";
    return 1;
}

void CMakeProjectVisitor::macroDeclaration(const CMakeFunctionDesc& def, const CMakeFunctionDesc& end, const QStringList& args)
{
    if(def.arguments.isEmpty() || end.arguments.isEmpty())
        return;
    QString id=def.arguments.first().value.toLower();
    
    DUChainWriteLocker lock(DUChain::lock());
    QList<Declaration*> decls=m_topctx->findDeclarations(Identifier(id));
    SimpleRange sr=def.arguments.first().range();
    SimpleRange endsr=end.arguments.first().range();
    int idx;
    
    if(!decls.isEmpty())
    {
        idx=m_topctx->indexForUsedDeclaration(decls.first());
        m_topctx->createUse(idx, sr, 0);
    }
    else
    {
        Declaration *d = new Declaration(sr, m_topctx);
        d->setIdentifier( Identifier(id) );

        FunctionType* func=new FunctionType();
        foreach(const QString& arg, args)
        {
            DelayedType *delayed=new DelayedType;
            delayed->setIdentifier( IndexedTypeIdentifier(arg) );
            func->addArgument(AbstractType::Ptr(delayed));
        }
        d->setAbstractType( AbstractType::Ptr(func) );
        idx=m_topctx->indexForUsedDeclaration(d);
    }
    m_topctx->createUse(idx, endsr, 0);
}

int CMakeProjectVisitor::visit(const MacroAst *macro)
{
    kDebug(9042) << "Adding macro:" << macro->macroName();
    Macro m;
    m.name = macro->macroName();
    m.knownArgs=macro->knownArgs();
    m.isFunction=false;
    
    return declareFunction(m, macro->content(), macro->line(), "endmacro");
}

int CMakeProjectVisitor::visit(const FunctionAst *func)
{
    kDebug(9042) << "Adding function:" << func->name();
    Macro m;
    m.name = func->name();
    m.knownArgs=func->knownArgs();
    m.isFunction=true;
    
    return declareFunction(m, func->content(), func->line(), "endfunction");
}

int CMakeProjectVisitor::declareFunction(Macro m, const CMakeFileContent& content,
                                         int initial, const QString& end)
{
    CMakeFileContent::const_iterator it=content.constBegin()+initial;
    CMakeFileContent::const_iterator itEnd=content.constEnd();

    int lines=0;
    for(; it!=itEnd; ++it)
    {
        if(it->name.toLower()==end)
            break;
        m.code += *it;
        ++lines;
    }
    ++lines; //We do not want to return to endmacro

    if(it!=itEnd)
    {
        m_macros->insert(m.name, m);

        macroDeclaration(content[initial], content[initial+lines-1], m.knownArgs);
    }
    return lines;
}

int CMakeProjectVisitor::visit(const MacroCallAst *call)
{
    if(m_macros->contains(call->name()))
    {
        const Macro code=m_macros->value(call->name());
        kDebug(9042) << "Running macro:" << call->name() << "params:" << call->arguments() << "=" << code.knownArgs << "for" << code.code.count() << "lines";
        
        if(code.knownArgs.count() > call->arguments().count())
        {
            kDebug(9032) << "error: more parameters needed when calling" << call->name();
        }
        else
        {
            {
                DUChainWriteLocker lock(DUChain::lock());
                QList<Declaration*> decls=m_topctx->findDeclarations(Identifier(call->name().toLower()));

                if(!decls.isEmpty())
                {
                    int idx=m_topctx->indexForUsedDeclaration(decls.first());
                    m_topctx->createUse(idx, call->content()[call->line()].nameRange(), 0);
                }
            }

            //Giving value to parameters
            QStringList::const_iterator mit = code.knownArgs.constBegin();
            QStringList::const_iterator cit = call->arguments().constBegin();
            QStringList argn;
            bool haveArgn=false;
            int i=0;
            while(cit != call->arguments().constEnd())
            {
                m_vars->insertMulti(QString("ARGV%1").arg(i), QStringList(*cit));
                if(mit!=code.knownArgs.constEnd())
                {
                    kDebug(9042) << "param:" << *mit << "=" << *cit;
                    m_vars->insertMulti(*mit, QStringList(*cit));
                    mit++;
                }
                else
                {
                    haveArgn=true;
                    argn += *cit;
                }
                cit++;
                i++;
            }
            m_vars->insertMulti("ARGN", argn);
            m_vars->insertMulti("ARGV", call->arguments());
            m_vars->insertMulti("ARGC", QStringList(QString::number(call->arguments().count())));
            kDebug(9042) << "argn=" << m_vars->value("ARGN");

            //Executing
            int len = walk(code.code, 1);
            kDebug(9042) << "visited!" << call->name()  <<
                m_vars->value("ARGV") << "_" << m_vars->value("ARGN") << "..." << len;

            //Restoring
            i=1;
            foreach(const QString& name, code.knownArgs)
            {
                m_vars->take(QString("ARGV%1").arg(i));
                m_vars->take(name);
                i++;
            }

            m_vars->take("ARGV");
            m_vars->take("ARGC");
            m_vars->take("ARGN");

        }
    }
    else
    {
        kDebug(9032) << "error: Did not find the macro:" << call->name() << call->content()[call->line()].writeBack();
    }
    
    return 1;
}

void usesForArguments(const QStringList& names, const QList<int>& args, const ReferencedTopDUContext& topctx, const CMakeFunctionDesc& func)
{
    //TODO: Should not return here
    if(args.size()!=names.size())
        return;

    //We define the uses for the used variable without ${}
    foreach(int use, args)
    {
        DUChainWriteLocker lock(DUChain::lock());
        QString var=names[use];
        QList<Declaration*> decls=topctx->findDeclarations(Identifier(var));

        if(!decls.isEmpty() && func.arguments.count() > use)
        {
            CMakeFunctionArgument arg=func.arguments[use];
            int idx=topctx->indexForUsedDeclaration(decls.first());
            topctx->createUse(idx, SimpleRange(arg.line-1, arg.column-1, arg.line-1, arg.column-1+var.size()), 0);
        }
    }
}

int CMakeProjectVisitor::visit(const IfAst *ifast)  //Highly crappy code
{
    int lines=ifast->line();
    if( ifast->condition().isEmpty() )
    {
        const CMakeFunctionDesc d = ifast->content().at( ifast->line() );
        kWarning() << "Parser couldn't parse condition of an IF in file:" << ifast->condition() << d.filePath << d.line;
    }

    int inside=0;
//     kDebug(9042) << "if() was false, looking for an else/elseif @" << lines;
    CMakeFileContent::const_iterator it=ifast->content().constBegin()+lines;
    CMakeFileContent::const_iterator itEnd=ifast->content().constEnd();

    bool visited=false;
    QList<int> ini;
    for(; it!=itEnd; ++it, lines++)
    {
        QString funcName=it->name.toLower();
//         kDebug(9032) << "looking @" << lines << it->writeBack() << ">>" << inside << visited;
        if(funcName=="if")
        {
            inside++;
        }
        else if(funcName=="endif")
        {
            inside--; 
            if(inside<=0) {
//                 Q_ASSERT(!ini.isEmpty());
                if(!it->arguments.isEmpty())
                    usesForArguments(ifast->condition(), ini, m_topctx, *it);
                break;
            }
//                 kDebug(9042) << "found an endif at:" << lines << "but" << inside;
        }

        if(inside==1)
        {
            bool result = false;

            if(funcName=="if" || funcName=="elseif")
            {
                CMakeCondition cond(this);
                IfAst myIf;
                QStringList condition;

                if(funcName=="if")
                {
                    condition=ifast->condition();
                }
                else
                {
                    if(!myIf.parseFunctionInfo(resolveVariables(*it)))
                        kDebug(9042) << "uncorrect condition correct" << it->writeBack();
                    condition=myIf.condition();
                }
                result=cond.condition(condition);
                if(funcName=="if")
                    ini=cond.variableArguments();

                usesForArguments(condition, cond.variableArguments(), m_topctx, *it);
                kDebug(9042) << ">> " << funcName << condition << result;
            }
            else if(funcName=="else")
            {
                kDebug(9042) << ">> else";
                result=true;
                usesForArguments(ifast->condition(), ini, m_topctx, *it);
            }

            if(!visited && result)
            {
                kDebug(9042) << "About to visit " << funcName << "?" << result;

                int oldpos=lines;
                lines = walk(ifast->content(), lines+1)-1;

                it+=lines-oldpos;

                visited=true;
//                 kDebug(9042) << "Visited. now in" << it->name;
            }
        }
    }

    if(it==itEnd)
    {
        kDebug() << "error. found an unfinished endif";
        return ifast->content().size()-ifast->line();
    }
    else
    {
//     kDebug() << "finish" << "<>" << ifast->condition() << '|' << lines-ifast->line() << " to " << lines << '<' << ifast->content().size();
//     kDebug(9042) << "endif==" << ifast->content()[lines].writeBack();
        return lines-ifast->line()+1;
    }
}

int CMakeProjectVisitor::visit(const ExecProgramAst *exec)
{
    QString execName = exec->executableName();
    QStringList argsTemp = exec->arguments();
    QStringList args;

    foreach(const QString& arg, argsTemp)
    {
        args += arg.split(' ');
    }
    kDebug(9042) << "Executing:" << execName << "::" << args << "in" << exec->workingDirectory();

    KProcess p;
    if(!exec->workingDirectory().isEmpty())
        p.setWorkingDirectory(exec->workingDirectory());
    p.setOutputChannelMode(KProcess::MergedChannels);
    p.setProgram(execName, args);
    p.start();

    if(!p.waitForFinished())
    {
        kDebug(9032) << "error: failed to execute:" << execName << "error:" << p.error() << p.exitCode();
    }

    if(!exec->returnValue().isEmpty())
    {
        kDebug(9042) << "execution returned: " << exec->returnValue() << " = " << p.exitCode();
        m_vars->insert(exec->returnValue(), QStringList(QString::number(p.exitCode())));
    }

    if(!exec->outputVariable().isEmpty())
    {
        QByteArray b = p.readAllStandardOutput();
        QString t;
        t.prepend(b.trimmed());
        m_vars->insert(exec->outputVariable(), QStringList(t.trimmed()));
        kDebug(9042) << "executed" << execName << "<" << t;
    }
    return 1;
}

int CMakeProjectVisitor::visit(const ExecuteProcessAst *exec)
{
    kDebug(9042) << "executing... " << exec->commands();
    QList<KProcess*> procs;
    foreach(const QStringList& _args, exec->commands())
    {
        QStringList args(_args);
        KProcess *p=new KProcess(), *prev=0;
        if(!procs.isEmpty())
        {
            prev=procs.last();
        }
        p->setWorkingDirectory(exec->workingDirectory());
        p->setOutputChannelMode(KProcess::MergedChannels);
        QString execName=args.takeFirst();
        p->setProgram(execName, args);
        p->start();
        procs.append(p);
        kDebug(9042) << "Executing:" << execName << "::" << args /*<< "into" << *m_vars*/;

        if(prev)
        {
            prev->setStandardOutputProcess(p);
        }
    }

    foreach(KProcess* p, procs)
    {
        if(!p->waitForFinished())
        {
            kDebug(9042) << "error: failed to execute:" << p;
        }
    }
    
    if(!procs.isEmpty() && !exec->resultVariable().isEmpty())
    {
        kDebug(9042) << "execution returned: " << exec->resultVariable() << " = " << procs.last()->exitCode();
        m_vars->insert(exec->resultVariable(), QStringList(QString::number(procs.last()->exitCode())));
    }

    //FIXME: remove condition when filtering bad output
    if(!procs.isEmpty() && !exec->outputVariable().isEmpty())
    {
        QByteArray b = procs.last()->readAllStandardOutput();
        QString t;
        t.prepend(b.trimmed());
        m_vars->insert(exec->outputVariable(), QStringList(t.trimmed().replace("\\", "\\\\")));
        kDebug(9042) << "executed " << exec->outputVariable() << "=" << t;
    }
    qDeleteAll(procs);
    return 1;
}


int CMakeProjectVisitor::visit(const FileAst *file)
{
    Q_ASSERT(m_vars->contains("CMAKE_CURRENT_SOURCE_DIR"));
    switch(file->type()) //TODO
    {
        case FileAst::Write:
            kDebug(9042) << "(ni) File write: " << file->path() << file->message();
            break;
        case FileAst::Append:
            kDebug(9042) << "(ni) File append: " << file->path() << file->message();
            break;
        case FileAst::Read:
        {
            KUrl filename=file->path();
            QFileInfo ifile(filename.toLocalFile());
            kDebug(9042) << "FileAst: reading " << file->path() << ifile.isFile();
            if(!ifile.isFile())
                return 1;
            QFile f(filename.toLocalFile());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
                return 1;
            QString output;
            while (!f.atEnd()) {
                QByteArray line = f.readLine();
                output += line;
            }
            m_vars->insert(file->variable(), QStringList(output));
            kDebug(9042) << "FileAst: read ";
        }
            break;
        case FileAst::Glob: {
            QStringList matches;
            QString relative=file->path();
            foreach(const QString& glob, file->globbingExpressions())
            {
                QStringList globs;
                QString current;
                if(KUrl::isRelativeUrl(glob)) {
                    KUrl urlGlob(glob);
                    current=urlGlob.upUrl().path();
                    
                    globs.append(urlGlob.fileName());
                } else if(!relative.isEmpty()) {
                    current=relative;
                    globs.append(glob);
                } else {
                    current=m_vars->value("CMAKE_CURRENT_SOURCE_DIR").first();
                    globs.append(glob);
                }
                
                QDir d(current);
                matches+=d.entryList(globs, QDir::NoDotAndDotDot | QDir::AllEntries);
            }
            m_vars->insert(file->variable(), matches);
            kDebug(9042) << "file glob" << file->path() << file->globbingExpressions() << matches;
        } break;
        case FileAst::GlobRecurse: {
            QString current;
            if(file->path().isEmpty())
                current=m_vars->value("CMAKE_CURRENT_SOURCE_DIR").first();
            else
                current=file->path();
            QQueue<QString> candidates;
            candidates.enqueue(current);
            QStringList directories;
            while(!candidates.isEmpty())
            {
                QString dir=candidates.dequeue();
                directories.append(dir);
                QDir direc(dir);
                candidates += direc.entryList(QDir::NoDotAndDotDot | QDir::Dirs);
            }

            QDir d(current);
            QStringList matches=d.entryList(file->globbingExpressions(), QDir::NoDotAndDotDot | QDir::AllEntries);
            m_vars->insert(file->variable(), matches);
            kDebug(9042) << "file glob_recurse" << file->path() << file->globbingExpressions() << matches;
        }   break;
        case FileAst::Remove:
        case FileAst::RemoveRecurse:
            kDebug(9042) << "warning. file-remove or remove_recurse. KDevelop won't remove anything.";
            break;
        case FileAst::MakeDirectory:
            kDebug(9042) << "warning. file-make_directory. KDevelop won't create anything.";
            break;
        case FileAst::RelativePath:
            m_vars->insert(file->variable(), QStringList(KUrl::relativePath(file->directory(), file->path())));
            kDebug(9042) << "file relative_path" << file->directory() << file->path();
            break;
        case FileAst::ToCmakePath:
#ifdef Q_OS_WIN
            m_vars->insert(file->variable(), file->path().replace("\\", "/").split(';'));
#else
            m_vars->insert(file->variable(), file->path().split(':'));
#endif
            kDebug(9042) << "file TO_CMAKE_PATH variable:" << file->variable() << "="
                    << m_vars->value(file->variable()) << "path:" << file->path();
            break;
        case FileAst::ToNativePath:
            m_vars->insert(file->variable(), QStringList(file->path().replace('/', QDir::separator())));
            kDebug(9042) << "file TO_NATIVE_PATH variable:" << file->variable() << "="
                    << m_vars->value(file->variable()) << "path:" << file->path();
            break;
        default:
            kDebug(9032) << "error: not implemented. file:" << file->type() <<
                "variable:" << file->variable() << "file:" << file->path() << file->content()[file->line()].arguments[0].value;
            break;
    }
    return 1;
}

int CMakeProjectVisitor::visit(const MessageAst *msg)
{
    s_msgcallback(msg->message().join(QString()));
    return 1;
}

int CMakeProjectVisitor::visit(const MathAst *math)
{
    QScriptEngine eng;
    QScriptValue result = eng.evaluate(math->expression());

    if (result.isError())
    {
        kDebug(9032) << "error: found an error while calculating" << math->expression();
    }
    kDebug(9042) << "math. " << math->expression() << "=" << result.toString();
    m_vars->insert(math->outputVariable(), QStringList(result.toString()));
    return 1;
}

int CMakeProjectVisitor::visit(const GetFilenameComponentAst *filecomp)
{
    QString dir;
    Q_ASSERT(m_vars->contains("CMAKE_CURRENT_SOURCE_DIR"));

    dir=m_vars->value("CMAKE_CURRENT_SOURCE_DIR").first();
    QFileInfo fi(dir, filecomp->fileName());

    QString val;
    switch(filecomp->type())
    {
        case GetFilenameComponentAst::Path:
            val=fi.canonicalPath();
            break;
        case GetFilenameComponentAst::Absolute:
            val=fi.absoluteFilePath();
            break;
        case GetFilenameComponentAst::Name:
            val=fi.fileName();
            break;
        case GetFilenameComponentAst::Ext:
            val=fi.suffix();
            break;
        case GetFilenameComponentAst::NameWe:
            val=fi.fileName().left(fi.fileName().length()-fi.suffix().length()-1);
            break;
        case GetFilenameComponentAst::Program:
            kDebug(9042) << "error: filenamecopmonent PROGRAM not implemented"; //TODO: <<
            break;
    }
    m_vars->insert(filecomp->variableName(), QStringList(val));
    kDebug(9042) << "filename component" << filecomp->variableName() << "= "
            << filecomp->fileName() << "=" << val << endl;
    return 1;
}

int CMakeProjectVisitor::visit(const GetSourceFilePropAst* prop)
{
    kDebug(9042) << "not supported yet :::" << prop->variableName();
    m_vars->insert(prop->variableName(), QStringList());
    return 1;
}

int CMakeProjectVisitor::visit(const OptionAst *opt)
{
    kDebug(9042) << "option" << opt->variableName() << "-" << opt->description();
    if(!m_vars->contains(opt->variableName()))
    {
        m_vars->insert(opt->variableName(), QStringList(opt->defaultValue()));
    }
    return 1;
}

int CMakeProjectVisitor::visit(const ListAst *list)
{
    QString output = list->output();
    
    QStringList theList = m_vars->value(list->list());
    switch(list->type())
    {
        case ListAst::Length:
            m_vars->insert(output, QStringList(QString::number(theList.count())));
            kDebug(9042) << "List length" << m_vars->value(output);
            break;
        case ListAst::Get: {
            bool contains = m_vars->contains(list->list());
            QStringList indices;
            if(contains) {
                foreach(int idx, list->index())
                {
                    if(idx>=theList.count())
                        kDebug(9032) << "error! trying to GET an element that doesn't exist!" << idx;
                    else if(idx>=0)
                        indices += theList[idx];
                    else
                        indices += theList[theList.size()+idx];
                }
            } else
                indices += "NOTFOUND";
            m_vars->insert(output, indices);
            kDebug(9042) << "List: Get" << list->list() << theList << list->output() << list->index();
        }   break;
        case ListAst::Append:
            theList += list->elements();
            m_vars->insert(list->list(), theList);
            break;
        case ListAst::Find: {
            int idx=theList.indexOf(list->elements().first());
            
            m_vars->insert(list->output(), QStringList(QString::number(idx)));
            kDebug(9042) << "List: Find" << theList << list->output() << list->elements() << idx;
        }   break;
        case ListAst::Insert: {
            int p=list->index().first();
            foreach(const QString& elem, list->elements())
            {
                theList.insert(p >=0 ? p : (theList.size()+p), elem);
                p += p>=0? 1 : 0;
            }
            m_vars->insert(list->list(), theList);
        }   break;
        case ListAst::RemoveItem:
            kDebug(9042) << "list remove item: " << theList << list->elements();
            foreach(const QString& elem, list->elements())
            {
                theList.removeAll(elem);
            }

            m_vars->insert(list->list(), theList);
            break;
        case ListAst::RemoveAt: {
            QList<int> indices=list->index();
            qSort(indices);
            QList<int>::const_iterator it=indices.constEnd();
            kDebug(9042) << "list remove: " << theList << indices;
            do
            {
                --it;
                theList.removeAt(*it >= 0 ? *it : theList.size()+*it);
            } while(it!=indices.constBegin());
            m_vars->insert(list->list(), theList);
        }   break;
        case ListAst::Sort:
            qSort(theList);
            m_vars->insert(list->list(), theList);
            break;
        case ListAst::Reverse: {
            QStringList reversed;
            foreach(const QString& elem, theList)
                reversed.prepend(elem);
            m_vars->insert(list->list(), reversed);
            }
            break;
        case ListAst::RemoveDuplicates: {
            QStringList noduplicates;
            foreach(const QString& elem, theList) {
                if(!noduplicates.contains(elem))
                    noduplicates.append(elem);
            }
            m_vars->insert(list->list(), noduplicates);
        }   break;
    }
    kDebug(9042) << "List!!" << list->output() << '='<< m_vars->value(list->output()) << " -> " << m_vars->value(list->list());
    return 1;
}

int CMakeProjectVisitor::visit(const ForeachAst *fea)
{
    kDebug(9042) << "foreach>" << fea->loopVar() << "=" << fea->arguments() << "range=" << fea->range();
    int end = 1;
    if(fea->range())
    {
        for( int i = fea->ranges().start; i < fea->ranges().stop; i += fea->ranges().step )
        {
            m_vars->insertMulti(fea->loopVar(), QStringList(QString::number(i)));
            end=walk(fea->content(), fea->line()+1);
            m_vars->take(fea->loopVar());
        }
    }
    else
    {
        //Looping in a list of values
        QStringList args=fea->arguments();
        if(args.count()==1 && args.first().isEmpty()) { //if the args are empty
            int lines=fea->line()+1, depth=1;
            CMakeFileContent::const_iterator it=fea->content().constBegin()+lines;
            CMakeFileContent::const_iterator itEnd=fea->content().constEnd();
            for(; depth>0 && it!=itEnd; ++it, lines++)
            {
                if(it->name.toLower()=="foreach")
                {
                    depth++;
                }
                else if(it->name.toLower()=="endforeach")
                {
                    depth--;
                }
            }
            end=lines-1;
        }
        else
        {
            foreach(const QString& s, args)
            {
                m_vars->insert(fea->loopVar(), QStringList(s));
                kDebug(9042) << "looping" << fea->loopVar() << "=" << m_vars->value(fea->loopVar());
                end=walk(fea->content(), fea->line()+1);
            }
        }
    }
    kDebug(9042) << "EndForeach" << fea->loopVar();
    return end-fea->line()+1;
}

int CMakeProjectVisitor::visit(const StringAst *sast)
{
    kDebug(9042) << "String to" /*<< sast->input()*/ << sast->outputVariable();
    switch(sast->type())
    {
        case StringAst::Regex:
        {
            QStringList res;
            QRegExp rx(sast->regex());
            rx.setPatternSyntax(QRegExp::RegExp2);
            QString totalInput = sast->input().join(QString());
            switch(sast->cmdType())
            {
                case StringAst::Match:
                {
                    int match=rx.indexIn(totalInput);
                    if(match>=0) {
                        res = QStringList(totalInput.mid(match, rx.matchedLength()));
                        break;
                    }
                    break;
                }
                case StringAst::MatchAll:
                {
                    int pos = 0;
                    while( (pos = rx.indexIn( totalInput, pos ) ) != -1 )
                    {
                        res << rx.cap();
                        pos += rx.matchedLength();
                    }
                    break;
                }
                case StringAst::RegexReplace:
                {
                    foreach(const QString& in, sast->input())
                    {
                        // QString() is required to get rid of the const
                        res.append(QString(in).replace(rx, sast->replace()));
                    }
                }
                    break;
                default:
                    kDebug(9032) << "ERROR String: Not a regex. " << sast->cmdType();
                    break;
            }
            kDebug() << "regex" << sast->outputVariable() << "=" << sast->regex() << res;
            m_vars->insert(sast->outputVariable(), res);
        }
            break;
        case StringAst::Replace: {
            QStringList out;
            foreach(const QString& _in, sast->input())
            {
                QString in(_in);
                QString aux=in.replace(sast->regex(), sast->replace());
                out += aux.split(';'); //FIXME: HUGE ugly hack
            }
            kDebug(9042) << "string REPLACE" << sast->input() << "=>" << out;
            m_vars->insert(sast->outputVariable(), out);
        }   break;
        case StringAst::Compare:
        {
            QString res;
            switch(sast->cmdType()){
                case StringAst::Equal:
                case StringAst::NotEqual:
                    if(sast->input()[0]==sast->input()[1] && sast->cmdType()==StringAst::Equal)
                        res = "TRUE";
                    else
                        res = "FALSE";
                    break;
                case StringAst::Less:
                case StringAst::Greater:
                    if(sast->input()[0]<sast->input()[1] && sast->cmdType()==StringAst::Less)
                        res = "TRUE";
                    else
                        res = "FALSE";
                    break;
                default:
                    kDebug(9042) << "String: Not a compare. " << sast->cmdType();
            }
            m_vars->insert(sast->outputVariable(), QStringList(res));
        }
            break;
        case StringAst::Ascii: {
            QString res;
            foreach(const QString& ascii, sast->input())
            {
                bool ok;
                res += QChar(ascii.toInt(&ok, 10));
            }

            m_vars->insert(sast->outputVariable(), QStringList(res));
        }   break;
        case StringAst::Configure:
            //This is not up to the cmake support
            kDebug(9032) << "warning! String configure is not supported!" << sast->content()[sast->line()].writeBack();
            break;
        case StringAst::ToUpper:
            m_vars->insert(sast->outputVariable(), QStringList(sast->input()[0].toUpper()));
            break;
        case StringAst::ToLower:
            m_vars->insert(sast->outputVariable(), QStringList(sast->input()[0].toLower()));
            break;
        case StringAst::Length:
            m_vars->insert(sast->outputVariable(), QStringList(QString::number(sast->input()[0].count())));
            break;
        case StringAst::Substring:
        {
            QString res=sast->input()[0];
            res=res.mid(sast->begin(), sast->length());
            m_vars->insert(sast->outputVariable(), QStringList(res));
        }   break;
        case StringAst::Strip:
            m_vars->insert(sast->outputVariable(), QStringList(CMakeFunctionArgument::unescapeValue( sast->string() )));
            break;
        case StringAst::Random: {
            QString alphabet=sast->string(), result;
            for(int i=0; i<sast->length(); i++)
            {
                int randv=qrand() % alphabet.size();
                result += alphabet[randv];
            }
            m_vars->insert(sast->outputVariable(), QStringList(result));
        }   break;
    }
    kDebug(9042) << "String " << m_vars->value(sast->outputVariable());
    return 1;
}


int CMakeProjectVisitor::visit(const GetCMakePropertyAst *past)
{
    QStringList output;
    switch(past->type())
    {
        case GetCMakePropertyAst::Variables:
            kDebug(9042) << "get cmake prop: variables:" << m_vars->size();
            output = m_vars->keys();
            break;
        case GetCMakePropertyAst::CacheVariables:
            output = m_cache->keys();
            break;
        case GetCMakePropertyAst::Commands:      //FIXME: We do not have commands yet
            output = QStringList();
            break;
        case GetCMakePropertyAst::Macros:
            output = m_macros->keys();
            break;
    }
    m_vars->insert(past->variableName(), output);
    return 1;
}

int CMakeProjectVisitor::visit(const CustomCommandAst *ccast)
{
    kDebug(9042) << "CustomCommand" << ccast->outputs();
    if(ccast->isForTarget())
    {
        //TODO: implement me
    }
    else
    {
        foreach(const QString& out, ccast->outputs())
        {
            m_generatedFiles[out] = QStringList(ccast->mainDependency())/*+ccast->otherDependencies()*/;
            kDebug(9042) << "Have to generate:" << out << "with" << m_generatedFiles[out];
        }
    }
    return 1;
}

int CMakeProjectVisitor::visit(const CustomTargetAst *ctar)
{
    kDebug(9042) << "custom_target " << ctar->target() << ctar->dependencies() << ", " << ctar->commandArgs();
    kDebug(9042) << ctar->content()[ctar->line()].writeBack();

    defineTarget(ctar->target(), ctar->dependencies(), Target::Custom);
    return 1;
}

QPair<QString, QString> definition(const QString& param)
{
    QPair<QString, QString> ret;
    if(!param.startsWith("-D"))
        return ret;
    int eq=param.indexOf('=', 2);
    ret.first=param.mid(2, eq-2);
    if(eq>0)
        ret.second=param.mid(eq+1);
    return ret;
}

int CMakeProjectVisitor::visit(const AddDefinitionsAst *addDef)
{
//     kDebug(9042) << "Adding defs: " << addDef->definitions();
    foreach(const QString& def, addDef->definitions())
    {
        if(def.isEmpty())
            continue;
        QPair<QString, QString> definePair=definition(def);
        if(definePair.first.isEmpty())
            kDebug(9042) << "error: definition not matched" << def;

        m_defs[definePair.first]=definePair.second;
        kDebug(9042) << "added definition" << definePair.first << "=" << definePair.second << " from " << def;
    }
    return 1;
}

int CMakeProjectVisitor::visit(const RemoveDefinitionsAst *remDef)
{
    foreach(const QString& def, remDef->definitions())
    {
        if(def.isEmpty())
            continue;
        QPair<QString, QString> definePair=definition(def);
        if(definePair.first.isEmpty())
            kDebug(9042) << "error: definition not matched" << def;

        m_defs.remove(definePair.first);
        kDebug(9042) << "removed definition" << definePair.first << " from " << def;
    }
    return 1;
}

int CMakeProjectVisitor::visit(const MarkAsAdvancedAst *maa)
{
    kDebug(9042) << "Mark As Advanced" << maa->advancedVars();
    return 1;
}

/// @todo Add support for platform-specific argument splitting
/// (UNIX_COMMAND and WINDOWS_COMMAND) introduced in CMake 2.8.
int CMakeProjectVisitor::visit( const SeparateArgumentsAst * separgs )
{
    QString varName=separgs->variableName();
    QStringList res;
    foreach(const QString& value, m_vars->value(varName))
    {
        res += value.split(' ');
    }
    m_vars->insert(separgs->variableName(), res);
    return 1;
}

int CMakeProjectVisitor::visit(const SetPropertyAst* setp)
{
    kDebug() << "setprops" << setp->type() << setp->name() << setp->values();
    if(setp->type()==GlobalProperty)
        m_props[GlobalProperty][QString()][setp->name()]=setp->values();
    else
    {
        CategoryType& cm=m_props[setp->type()];
        foreach(const QString &it, setp->args())
            cm[it].insert(setp->name(), setp->values());
    }
    return 1;
}

int CMakeProjectVisitor::visit(const GetPropertyAst* getp)
{
    kDebug() << "getprops";
    QStringList retv;
    QString catn;
    if(getp->type()!=GlobalProperty)
    {
        catn=getp->typeName();
    }
    retv=m_props[getp->type()][catn][getp->name()];
    m_vars->insert(getp->outputVariable(), retv);
    return 1;
}

int CMakeProjectVisitor::visit(const GetDirPropertyAst* getdp)
{
    kDebug() << "getprops";
    QStringList retv;
    QString dir=getdp->directory();
    if(dir.isEmpty()) {
        dir=m_vars->value("CMAKE_CURRENT_SOURCE_DIR").join(QString());
    } else if(KUrl::isRelativeUrl(dir)) {
        KUrl u(m_vars->value("CMAKE_CURRENT_SOURCE_DIR").join(QString()));
        u.addPath(dir);
        dir=u.path();
    }
    
    retv=m_props[DirectoryProperty][dir][getdp->propName()];
    m_vars->insert(getdp->outputVariable(), retv);
    
    return 1;
}

int CMakeProjectVisitor::visit( const WhileAst * whileast)
{
    CMakeCondition cond(this);
    bool result=cond.condition(whileast->condition());
    usesForArguments(whileast->condition(), cond.variableArguments(), m_topctx, whileast->content()[whileast->line()]);

    kDebug(9042) << "Visiting While" << whileast->condition() << "?" << result;
    if(result)
    {
        walk(whileast->content(), whileast->line()+1);
        walk(whileast->content(), whileast->line());
    }
    CMakeFileContent::const_iterator it=whileast->content().constBegin()+whileast->line()+1;
    CMakeFileContent::const_iterator itEnd=whileast->content().constEnd();
    int lines=0, inside=1;
    for(; inside>0 && it!=itEnd; ++it, lines++)
    {
        QString funcName=it->name.toLower();
        if(funcName=="while")
            inside++;
        else if(funcName=="endwhile")
            inside--;
    }

    if(it!=itEnd) {
        usesForArguments(whileast->condition(), cond.variableArguments(), m_topctx, *(it-1));
    }
    return lines;
}

CMakeFunctionDesc CMakeProjectVisitor::resolveVariables(const CMakeFunctionDesc & exp)
{
    CMakeFunctionDesc ret=exp;
    ret.arguments.clear();

    foreach(const CMakeFunctionArgument &arg, exp.arguments)
    {
        if(arg.value.contains('$'))
            ret.addArguments(resolveVariable(arg));
        else
            ret.arguments.append(arg);
    }

    return ret;
}

enum RecursivityType { No, Yes, End };

RecursivityType recursivity(const QString& functionName)
{
    if(functionName.toUpper()=="IF" || functionName.toUpper()=="WHILE" ||
       functionName.toUpper()=="FOREACH" || functionName.toUpper()=="MACRO")
        return Yes;
    else if(functionName.toUpper()=="ELSE" || functionName.toUpper()=="ELSEIF")
        return End;
    else if(functionName.toUpper().startsWith("END"))
        return End;
    return No;
}

int CMakeProjectVisitor::walk(const CMakeFileContent & fc, int line, bool isClean)
{
    ReferencedTopDUContext aux=m_topctx;
    KUrl url(fc[0].filePath);
    
    if(!m_topctx || m_topctx->url().toUrl()!=url)
    {
        kDebug(9042) << "Creating a context for" << url;
        m_topctx=createContext(url, aux ? aux : m_parentCtx, fc.last().endLine-1, fc.last().endColumn-1, isClean);
        if(!aux)
            aux=m_topctx;
    }
    VisitorState p;
    p.code = &fc;
    p.context = m_topctx;
    p.line = line;

    m_backtrace.push(p);

    CMakeFileContent::const_iterator it=fc.constBegin()+line, itEnd=fc.constEnd();
    for(; it!=itEnd; )
    {
        Q_ASSERT( line<fc.count() );
        Q_ASSERT( line>=0 );
//         kDebug(9042) << "@" << line;

        Q_ASSERT( *it == fc[line] );
//         kDebug(9042) << "At line" << line << "/" << fc.count();
        CMakeAst* element = AstFactory::self()->createAst(it->name);

        if(!element)
            element = new MacroCallAst;

        createUses(*it);
//         kDebug(9042) << "resolving:" << it->writeBack();
            
        CMakeFunctionDesc func = resolveVariables(*it); //FIXME not correct in while case
        bool correct = element->parseFunctionInfo(func);
//         kDebug(9042) << "resolved:" << func.writeBack() << correct;
        if(!correct)
        {
            kDebug(9042) << "error! found an error while processing" << func.writeBack() << "was" << it->writeBack() << endl
                << " at" << func.filePath << ":" << func.line << endl;
            //FIXME: Should avoid to run?
        }

        RecursivityType r = recursivity(func.name);
        if(r==End)
        {
//             kDebug(9042) << "Found an end." << func.writeBack();
            delete element;
            m_backtrace.pop();
            m_topctx=aux;
            return line;
        }
        if(element->isDeprecated()) {
            kDebug(9032) << "Warning: Using the function: " << func.name << " which is deprecated by cmake.";
            DUChainWriteLocker lock(DUChain::lock());
            KSharedPtr<Problem> p(new Problem);
            p->setDescription(i18n("%1 is a deprecated command and should not be used", func.name));
            p->setRange(it->nameRange());
            p->setFinalLocation(DocumentRange(url.prettyUrl(), KTextEditor::Range(fc.first().range().start.textCursor(), fc.last().range().end.textCursor())));
            m_topctx->addProblem(p);
        }
        element->setContent(fc, line);

        createDefinitions(element);

        m_vars->insert("CMAKE_CURRENT_LIST_LINE", QStringList(QString::number(it->line)));
        int lines=element->accept(this);
        line+=lines;
        m_backtrace.top().line = line;
        m_backtrace.top().context = m_topctx;
        delete element;
        
        if(line>fc.count()) {
            DUChainWriteLocker lock(DUChain::lock());
            KSharedPtr<Problem> p(new Problem);
            p->setDescription(i18n("Unfinished function. "));
            p->setRange(it->nameRange());
            p->setFinalLocation(DocumentRange(url.prettyUrl(), KTextEditor::Range(fc.first().range().start.textCursor(), fc.last().range().end.textCursor())));
            m_topctx->addProblem(p);
            
            break;
        }
        
        it+=lines;
    }
    m_backtrace.pop();
    m_topctx=aux;
    kDebug(9042) << "Walk stopped @" << line;
    return line;
}

void CMakeProjectVisitor::createDefinitions(const CMakeAst *ast)
{
    if(!m_topctx)
        return;
    DUChainWriteLocker lock(DUChain::lock());
    foreach(const CMakeFunctionArgument &arg, ast->outputArguments())
    {
        if(!arg.isCorrect())
            continue;
        QList<Declaration*> decls=m_topctx->findDeclarations(Identifier(arg.value));
        if(decls.isEmpty())
        {
            Declaration *d = new Declaration(arg.range(), m_topctx);
            d->setIdentifier( Identifier(arg.value) );
        }
        else
        {
            int idx=m_topctx->indexForUsedDeclaration(decls.first());
            m_topctx->createUse(idx, arg.range(), 0);
        }
    }
}

void CMakeProjectVisitor::createUses(const CMakeFunctionDesc& desc)
{
    if(!m_topctx)
        return;
    DUChainWriteLocker lock(DUChain::lock());
    foreach(const CMakeFunctionArgument &arg, desc.arguments)
    {
        if(!arg.isCorrect() || !arg.value.contains('$'))
            continue;

        QList<IntPair> var = parseArgument(arg.value);
        QList<IntPair>::const_iterator it, itEnd=var.constEnd();
        for(it=var.constBegin(); it!=itEnd; ++it)
        {
            QString var=arg.value.mid(it->first+1, it->second-it->first-1);
            QList<Declaration*> decls=m_topctx->findDeclarations(Identifier(var));
            
            if(!decls.isEmpty())
            {
                int idx=m_topctx->indexForUsedDeclaration(decls.first());
                m_topctx->createUse(idx, SimpleRange(arg.line-1, arg.column+it->first, arg.line-1, arg.column+it->second-1), 0);
            }
        }
    }
}

void CMakeProjectVisitor::setCacheValues( CacheValues* cache)
{
    m_cache=cache;
}

void CMakeProjectVisitor::setVariableMap(VariableMap * vars)
{
    m_vars=vars;
}

bool isGenerated(const QString& name)
{
    return name.indexOf("#[")>=0;
}

QStringList CMakeProjectVisitor::dependees(const QString& s) const
{
    QStringList ret;
    if(isGenerated(s))
    {
        foreach(const QString& f, m_generatedFiles[s])
            ret += dependees(f);
    }
    else
    {
        ret += s;
    }
    return ret;
}

QStringList CMakeProjectVisitor::resolveDependencies(const QStringList & files) const
{
    QStringList ret;
    foreach(const QString& s, files)
    {
        if(isGenerated(s))
        {
            kDebug(9042) << "Generated:" << s;
            QStringList gen = dependees(s);
            
            foreach(const QString& file, gen)
            {
                if(!ret.contains(file))
                    ret.append(file);
            }
        }
        else
        {
            ret.append(s);
        }
    }
    return ret;
}

