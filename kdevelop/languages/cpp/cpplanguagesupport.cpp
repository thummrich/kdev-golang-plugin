/*
* KDevelop C++ Language Support
*
* Copyright 2005 Matt Rogers <mattr@kde.org>
* Copyright 2006 Adam Treat <treat@kde.org>
* Copyright 2007-2008 David Nolden<david.nolden.kdevelop@art-master.de>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU Library General Public License as
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
#include "cpplanguagesupport.h"
#include <config.h>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QApplication>
#include <QAction>
#include <QTimer>
#include <QReadWriteLock>
#include <kactioncollection.h>
#include <kaction.h>
#include <QExtensionFactory>
#include <QtDesigner/QExtensionFactory>

#include <kdebug.h>
#include <kcomponentdata.h>
#include <kstandarddirs.h>
#include <kpluginfactory.h>
#include <kaboutdata.h>
#include <kpluginloader.h>
#include <kio/netaccess.h>
#include <ktexteditor/document.h>
#include <ktexteditor/view.h>
#include <ktexteditor/smartinterface.h>
#include <language/codecompletion/codecompletion.h>

#include <interfaces/icore.h>
#include <language/interfaces/iproblem.h>
#include <interfaces/iproject.h>
#include <interfaces/idocument.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/ilanguage.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/iprojectcontroller.h>
#include <project/interfaces/ibuildsystemmanager.h>
#include <language/interfaces/iquickopen.h>
#include <interfaces/iplugincontroller.h>
#include <project/projectmodel.h>
#include <language/backgroundparser/backgroundparser.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/stringhelpers.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/smartconverter.h>
#include <language/duchain/functiondefinition.h>
#include <language/codegen/coderepresentation.h>
#include <interfaces/contextmenuextension.h>

#include "preprocessjob.h"
#include "rpp/preprocessor.h"
#include "ast.h"
#include "parsesession.h"
#include "cpphighlighting.h"
#include "cppparsejob.h"
#include "codecompletion/model.h"
#include "cppeditorintegrator.h"
#include "usebuilder.h"
#include "environmentmanager.h"
#include "cppduchain/navigation/navigationwidget.h"
#include "cppduchain/cppduchain.h"
#include "codegen/codeassistant.h"
#include "codegen/cppnewclass.h"
//#include "codegen/makeimplementationprivate.h"

#include "includepathresolver.h"
#include "setuphelpers.h"
#include "quickopen.h"
#include "cppdebughelper.h"
#include "codegen/simplerefactoring.h"
#include "includepathcomputer.h"
#include "codecompletion/missingincludemodel.h"
//#include <valgrind/callgrind.h>


// #define CALLGRIND_TRACE_UI_LOCKUP

#define DEBUG_UI_LOCKUP
#define LOCKUP_INTERVAL 300

#ifdef CALLGRIND_TRACE_UI_LOCKUP
#define DEBUG_UI_LOCKUP
#define LOCKUP_INTERVAL 5
#endif
#include "cpputils.h"

KTextEditor::Cursor normalizeCursor(KTextEditor::Cursor c) {
  c.setColumn(0);
  return c;
}


using namespace KDevelop;

CppLanguageSupport* CppLanguageSupport::m_self = 0;

KDevelop::ContextMenuExtension CppLanguageSupport::contextMenuExtension(KDevelop::Context* context)
{
    ContextMenuExtension cm;
    SimpleRefactoring::self().doContextMenu(cm, context);
    return cm;
}

///Tries to find a definition for the declaration at given cursor-position and document-url. DUChain must be locked.
Declaration* definitionForCursorDeclaration(const KDevelop::SimpleCursor& cursor, const KUrl& url) {
  QList<TopDUContext*> topContexts = DUChain::self()->chainsForDocument( url );
  foreach(TopDUContext* ctx, topContexts) {
    Declaration* decl = DUChainUtils::declarationInLine(cursor, ctx);
    if(decl && FunctionDefinition::definition(decl))
      return FunctionDefinition::definition(decl);
  }
  return 0;
}

// For unit-tests that compile cpplanguagesupport.cpp into their executable
// don't create the factories as that means 2 instances of the factory
#ifndef BUILD_TESTS
K_PLUGIN_FACTORY(KDevCppSupportFactory, registerPlugin<CppLanguageSupport>(); )
K_EXPORT_PLUGIN(KDevCppSupportFactory(KAboutData("kdevcppsupport","kdevcpp", ki18n("C++ Support"), "0.1", ki18n("Support for C++ Language"), KAboutData::License_GPL)))
#else
class KDevCppSupportFactory : public KPluginFactory
{
public:
    static KComponentData componentData() { return KComponentData(); };
};
#endif

CppLanguageSupport::CppLanguageSupport( QObject* parent, const QVariantList& /*args*/ )
    : KDevelop::IPlugin( KDevCppSupportFactory::componentData(), parent ),
      KDevelop::ILanguageSupport(),
      m_standardMacros(0)
{
    m_self = this;

    KDEV_USE_EXTENSION_INTERFACE( KDevelop::ILanguageSupport )
    setXMLFile( "kdevcppsupport.rc" );

    m_highlights = new CppHighlighting( this );
    m_cc = new KDevelop::CodeCompletion( this, new Cpp::CodeCompletionModel(0), name() );
    m_missingIncludeCompletion = new KDevelop::CodeCompletion( this, new Cpp::MissingIncludeCompletionModel(0), name() );

    Cpp::EnvironmentManager::init();
    Cpp::EnvironmentManager::self()->setSimplifiedMatching(true);
    Cpp::EnvironmentManager::self()->setMatchingLevel(Cpp::EnvironmentManager::Disabled);
//     Cpp::EnvironmentManager::self()->setMatchingLevel(Cpp::EnvironmentManager::Naive);
//     Cpp::EnvironmentManager::self()->setMatchingLevel(Cpp::EnvironmentManager::Full);

    m_includeResolver = new CppTools::IncludePathResolver;

    m_quickOpenDataProvider = new IncludeFileDataProvider();

    IQuickOpen* quickOpen = core()->pluginController()->extensionForPlugin<IQuickOpen>("org.kdevelop.IQuickOpen");

    if( quickOpen )
        quickOpen->registerProvider( IncludeFileDataProvider::scopes(), QStringList(i18n("Files")), m_quickOpenDataProvider );
    else
        kWarning() << "Quickopen not found";

#ifdef DEBUG_UI_LOCKUP
    m_blockTester = new UIBlockTester(LOCKUP_INTERVAL);
#endif

    m_assistant = new Cpp::StaticCodeAssistant;
}

void CppLanguageSupport::createActionsForMainWindow (Sublime::MainWindow* window, QString& _xmlFile, KActionCollection& actions)
{
    _xmlFile = xmlFile();

    KAction* switchDefinitionDeclaration = actions.addAction("switch_definition_declaration");
    switchDefinitionDeclaration->setText( i18n("&Switch Definition/Declaration") );
    switchDefinitionDeclaration->setShortcut( Qt::CTRL | Qt::SHIFT | Qt::Key_C );
    connect(switchDefinitionDeclaration, SIGNAL(triggered(bool)), this, SLOT(switchDefinitionDeclaration()));

    KAction* newClassAction = actions.addAction("code_new_class");
    newClassAction->setText( i18n("Create &New Class") );
    connect(newClassAction, SIGNAL(triggered(bool)), this, SLOT(newClassWizard()));
    
//    KAction* pimplAction = actions->addAction("code_private_implementation");
//    pimplAction->setText( i18n("Make Class Implementation Private") );
//    pimplAction->setShortcut(Qt::ALT | Qt::META | Qt::Key_P);
//    connect(pimplAction, SIGNAL(triggered(bool)), &SimpleRefactoring::self(), SLOT(executePrivateImplementationAction()));

    KAction* renameDeclarationAction = actions.addAction("code_rename_declaration");
    renameDeclarationAction->setText( i18n("Rename Declaration") );
    renameDeclarationAction->setIcon(KIcon("edit-rename"));
    renameDeclarationAction->setShortcut( Qt::CTRL | Qt::ALT | Qt::Key_R);
    connect(renameDeclarationAction, SIGNAL(triggered(bool)), &SimpleRefactoring::self(), SLOT(executeRenameAction()));

    KAction* moveIntoSourceAction = actions.addAction("code_move_definition");
    moveIntoSourceAction->setText( i18n("Move into Source") );
    moveIntoSourceAction->setShortcut( Qt::CTRL | Qt::ALT | Qt::Key_S);
    connect(moveIntoSourceAction, SIGNAL(triggered(bool)), &SimpleRefactoring::self(), SLOT(executeMoveIntoSourceAction()));
}

void CppLanguageSupport::switchDefinitionDeclaration()
{
  kDebug(9007) << "switching definition/declaration";

  KUrl docUrl;
  SimpleCursor cursor;
  
  ///Step 1: Find the current top-level context of type DUContext::Other(the highest code-context).
  ///-- If it belongs to a function-declaration or definition, it can be retrieved through owner(), and we are in a definition.
  ///-- If no such context could be found, search for a declaration on the same line as the cursor, and switch to the according definition
  
  {
    KDevelop::IDocument* doc = core()->documentController()->activeDocument();
    if(!doc || !doc->textDocument() || !doc->textDocument()->activeView()) {
      kDebug(9007) << "No active document";
      return;
    }
    
    docUrl = doc->textDocument()->url();
    cursor = SimpleCursor(doc->textDocument()->activeView()->cursorPosition()); 
  }
  
  KUrl switchCandidate = CppUtils::sourceOrHeaderCandidate(docUrl);
  
  if(switchCandidate.isValid())
  {
    
    DUChainReadLocker lock;

    //If the file has not been parsed yet, update it
    TopDUContext* ctx = standardContext(docUrl);
    //At least 'VisibleDeclarationsAndContexts' is required so we can do a switch
    if(!ctx || (ctx->parsingEnvironmentFile() && !ctx->parsingEnvironmentFile()->featuresSatisfied(TopDUContext::VisibleDeclarationsAndContexts)))
    {
      lock.unlock();
      kDebug(9007) << "Parsing switch-candidate before switching" << switchCandidate;
      DUChain::self()->waitForUpdate(IndexedString(switchCandidate), TopDUContext::VisibleDeclarationsAndContexts);
    }
  }
  
  kDebug(9007) << "Document:" << docUrl;

  DUChainReadLocker lock(DUChain::lock());

  TopDUContext* standardCtx = standardContext(docUrl);
  if(standardCtx) {
    Declaration* definition = 0;

    DUContext* ctx = standardCtx->findContext(cursor);
    if(!ctx)
      ctx = standardCtx;

    if(ctx)
      kDebug() << "found context" << ctx->scopeIdentifier();
    else
      kDebug() << "found no context";

    while(ctx && ctx->parentContext() && ctx->parentContext()->type() == DUContext::Other)
      ctx = ctx->parentContext();

    if(ctx && ctx->owner() && ctx->type() == DUContext::Other && ctx->owner()->isDefinition()) {
      definition = ctx->owner();
      kDebug() << "found definition while traversing:" << definition->toString();
    }

    if(!definition && ctx) {
      definition = DUChainUtils::declarationInLine(cursor, ctx);
      if(definition)
        kDebug() << "found definition using declarationInLine:" << definition->toString();
      else
        kDebug() << "not found definition using declarationInLine";
    }

    FunctionDefinition* def = dynamic_cast<FunctionDefinition*>(definition);
    if(def && def->declaration()) {
      Declaration* declaration = def->declaration();
      KTextEditor::Range targetRange = declaration->range().textRange();
      KUrl url(declaration->url().str());
      kDebug() << "found definition that has declaration: " << definition->toString() << "range" << targetRange << "url" << url;
      lock.unlock();

      KDevelop::IDocument* document = core()->documentController()->documentForUrl(url);
      
      if(!document || 
          (document && document->textDocument() && document->textDocument()->activeView() && !targetRange.contains(document->textDocument()->activeView()->cursorPosition()))) {
        KTextEditor::Cursor pos(normalizeCursor(targetRange.start()));
        core()->documentController()->openDocument(url, KTextEditor::Range(pos, pos));
      }else if(document)
        core()->documentController()->openDocument(url);
      return;
    }else{
      kDebug(9007) << "Definition has no assigned declaration";
    }

    kDebug(9007) << "Could not get definition/declaration from context";
  }else{
    kDebug(9007) << "Got no context for the current document";
  }

  Declaration* def = definitionForCursorDeclaration(cursor, docUrl);

  if(def) {
    KUrl url(def->url().str());
    KTextEditor::Range targetRange = def->range().textRange();

    if(def->internalContext()) {
      targetRange.end() = def->internalContext()->range().end.textCursor();
    }else{
      kDebug(9007) << "Declaration does not have internal context";
    }
    lock.unlock();

    KDevelop::IDocument* document = core()->documentController()->documentForUrl(url);
    
    if(!document || 
        (document && document->textDocument() && (!document->textDocument()->activeView() || !targetRange.contains(document->textDocument()->activeView()->cursorPosition())))) {
      KTextEditor::Cursor pos(normalizeCursor(targetRange.start()));
      core()->documentController()->openDocument(url, KTextEditor::Range(pos, pos));
    }else if(document) {
      //The cursor is already in the target range, only open the document
      core()->documentController()->openDocument(url);
    }
    return;
  }else{
    kWarning(9007) << "Found no definition assigned to cursor position";
  }

  lock.unlock();
  ///- If no definition/declaration could be found to switch to, just switch the document using normal header/source heuristic by file-extension

  if(switchCandidate.isValid()) {
    core()->documentController()->openDocument(switchCandidate);
  }else{
    kDebug(9007) << "Found no source/header candidate to switch";
  }
}

CppLanguageSupport::~CppLanguageSupport()
{
    ILanguage* lang = language();
    if (lang) {
        lang->parseLock()->lockForWrite();
        m_self = 0; //By locking the parse-mutexes, we make sure that parse- and preprocess-jobs get a chance to finish in a good state
        lang->parseLock()->unlock();
    }

    delete m_quickOpenDataProvider;

    // Remove any documents waiting to be parsed from the background paser.
    core()->languageController()->backgroundParser()->clear(this);


    delete m_includeResolver;
#ifdef DEBUG_UI_LOCKUP
    delete m_blockTester;
#endif
    delete m_assistant;
}

CppLanguageSupport* CppLanguageSupport::self() {
    return m_self;
}

KDevelop::ParseJob *CppLanguageSupport::createParseJob( const KUrl &url )
{
    return new CPPParseJob( url );
}

const KDevelop::ICodeHighlighting *CppLanguageSupport::codeHighlighting() const
{
    return m_highlights;
}

void CppLanguageSupport::findIncludePathsForJob(CPPParseJob* job)
{
  IncludePathComputer* comp = new IncludePathComputer(KUrl(job->document().str()), job->preprocessorProblemsPointer());
  comp->computeForeground();
  job->gotIncludePaths(comp);
}

QString CppLanguageSupport::name() const
{
    return "C++";
}

KDevelop::ILanguage *CppLanguageSupport::language()
{
    return core()->languageController()->language(name());
}

TopDUContext* CppLanguageSupport::standardContext(const KUrl& url, bool proxyContext)
{
  DUChainReadLocker lock(DUChain::lock());
  const ParsingEnvironment* env = PreprocessJob::standardEnvironment();
  KDevelop::TopDUContext* top;
  top = KDevelop::DUChain::self()->chainForDocument(url, env, Cpp::EnvironmentManager::self()->isSimplifiedMatching());

  if( !top ) {
    //kDebug(9007) << "Could not find perfectly matching version of " << url << " for completion";
    //Preferably pick a context that is not empty
    QList<TopDUContext*> candidates = DUChain::self()->chainsForDocument(url);
    foreach(TopDUContext* candidate, candidates)
      if(!candidate->localDeclarations().isEmpty() || !candidate->childContexts().isEmpty())
      top = candidate;
    if(!top && !candidates.isEmpty())
      top = candidates[0];
  }

  if(top && (top->parsingEnvironmentFile() && top->parsingEnvironmentFile()->isProxyContext()) && !proxyContext)
  {
    if(!top->importedParentContexts().isEmpty())
    {
      top = dynamic_cast<TopDUContext*>(top->importedParentContexts().first().context(0));

      if(!top)
      {
        kDebug(9007) << "WARNING: Proxy-context had invalid content-context";
      }

    } else {
      kDebug(9007) << "ERROR: Proxy-context has no content-context";
    }
  }

  return top;
}

QPair<QPair<QString, SimpleRange>, QString> CppLanguageSupport::cursorIdentifier(const KUrl& url, const SimpleCursor& position) const {
  KDevelop::IDocument* doc = core()->documentController()->documentForUrl(url);
  if(!doc || !doc->textDocument() || !doc->textDocument()->activeView())
    return qMakePair(qMakePair(QString(), SimpleRange::invalid()), QString());

  int lineNumber = position.line;
  int lineLength = doc->textDocument()->lineLength(lineNumber);

  QString line = doc->textDocument()->text(KTextEditor::Range(lineNumber, 0, lineNumber, lineLength));

  if(CppUtils::findEndOfInclude(line) != -1) { //If it is an include, return the complete line
    int start = 0;
    while(start < lineLength && line[start] == ' ')
      ++start;

    return qMakePair( qMakePair(line, SimpleRange(lineNumber, start, lineNumber, lineLength)), QString() );
  }

  // not an include, if at all a Makro, hence clear strings
  line = clearStrings(line);

  int start = position.column;
  int end = position.column;

  while(start > 0 && (line[start].isLetterOrNumber() || line[start] == '_') && (line[start-1].isLetterOrNumber() || line[start-1] == '_'))
    --start;

  while(end <  lineLength && (line[end].isLetterOrNumber() || line[end] == '_'))
    ++end;

  SimpleRange wordRange = SimpleRange(SimpleCursor(lineNumber, start), SimpleCursor(lineNumber, end));

  return qMakePair( qMakePair(line.mid(start, end-start), wordRange), line.mid(end) );
}

QPair<TopDUContextPointer, SimpleRange> CppLanguageSupport::importedContextForPosition(const KUrl& url, const SimpleCursor& position) {
  QPair<QPair<QString, SimpleRange>, QString> found = cursorIdentifier(url, position);
  if(!found.first.second.isValid())
    return qMakePair(TopDUContextPointer(), SimpleRange::invalid());

  QString word(found.first.first);
  SimpleRange wordRange(found.first.second);

  int pos = 0;
  for(; pos < word.size(); ++pos) {
    if(word[pos] == '"' || word[pos] == '<') {
      wordRange.start.column = ++pos;
      break;
    }
  }

  for(; pos < word.size(); ++pos) {
    if(word[pos] == '"' || word[pos] == '>') {
      wordRange.end.column = pos;
      break;
    }
  }

  if(wordRange.start > wordRange.end)
    wordRange.start = wordRange.end;

  //Since this is called by the editor while editing, use a fast timeout so the editor stays responsive
  DUChainReadLocker lock(DUChain::lock(), 100);
  if(!lock.locked()) {
    kDebug(9007) << "Failed to lock the du-chain in time";
    return qMakePair(TopDUContextPointer(), SimpleRange::invalid());
  }

  TopDUContext* ctx = standardContext(url);
  if(word.isEmpty() || !ctx || !ctx->parsingEnvironmentFile())
    return qMakePair(TopDUContextPointer(), SimpleRange::invalid());

  if((ctx->parsingEnvironmentFile() && ctx->parsingEnvironmentFile()->isProxyContext())) {
    kDebug() << "Strange: standard-context for" << ctx->url().str() << "is a proxy-context";
    return qMakePair(TopDUContextPointer(), SimpleRange::invalid());
  }

  Cpp::EnvironmentFilePointer p(dynamic_cast<Cpp::EnvironmentFile*>(ctx->parsingEnvironmentFile().data()));

  Q_ASSERT(p);

  if(CppUtils::findEndOfInclude(word) != -1) {
    //It's an #include, find out which file was included at the given line
    foreach(const DUContext::Import &imported, ctx->importedParentContexts()) {
      if(imported.context(0)) {
        if(ctx->importPosition(imported.context(0)).line == wordRange.start.line) {
          if(TopDUContext* importedTop = dynamic_cast<TopDUContext*>(imported.context(0)))
            return qMakePair(TopDUContextPointer(importedTop), wordRange);
        }
      }
    }
  }
  return qMakePair(TopDUContextPointer(), SimpleRange::invalid());
}

QPair<SimpleRange, const rpp::pp_macro*> CppLanguageSupport::usedMacroForPosition(const KUrl& url, const SimpleCursor& position) {
  //Extract the word under the cursor

  QPair<QPair<QString, SimpleRange>, QString> found = cursorIdentifier(url, position);
  if(!found.first.second.isValid())
    return qMakePair(SimpleRange::invalid(), (const rpp::pp_macro*)0);

  IndexedString word(found.first.first);
  SimpleRange wordRange(found.first.second);

  //Since this is called by the editor while editing, use a fast timeout so the editor stays responsive
  DUChainReadLocker lock(DUChain::lock(), 100);
  if(!lock.locked()) {
    kDebug(9007) << "Failed to lock the du-chain in time";
    return qMakePair(SimpleRange::invalid(), (const rpp::pp_macro*)0);
  }

  TopDUContext* ctx = standardContext(url, true);
  if(word.str().isEmpty() || !ctx || !ctx->parsingEnvironmentFile())
    return qMakePair(SimpleRange::invalid(), (const rpp::pp_macro*)0);

  Cpp::EnvironmentFilePointer p(dynamic_cast<Cpp::EnvironmentFile*>(ctx->parsingEnvironmentFile().data()));

  Q_ASSERT(p);

  if(!p->usedMacroNames().contains(word) && !p->definedMacroNames().contains(word))
    return qMakePair(SimpleRange::invalid(), (const rpp::pp_macro*)0);

  //We need to do a flat search through all macros here, which really hurts

  Cpp::ReferenceCountedMacroSet::Iterator it = p->usedMacros().iterator();

  while(it) {
    if(it.ref().name == word && !it.ref().isUndef())
      return qMakePair(wordRange, &it.ref());
    ++it;
  }

  it = p->definedMacros().iterator();
  while(it) {
    if(it.ref().name == word && !it.ref().isUndef())
      return qMakePair(wordRange, &it.ref());
    ++it;
  }

  return qMakePair(SimpleRange::invalid(), (const rpp::pp_macro*)0);
}

SimpleRange CppLanguageSupport::specialLanguageObjectRange(const KUrl& url, const SimpleCursor& position) {

  QPair<TopDUContextPointer, SimpleRange> import = importedContextForPosition(url, position);
  if(import.first)
    return import.second;

  return usedMacroForPosition(url, position).first;
}

QPair<KUrl, KDevelop::SimpleCursor> CppLanguageSupport::specialLanguageObjectJumpCursor(const KUrl& url, const SimpleCursor& position) {

  QPair<TopDUContextPointer, SimpleRange> import = importedContextForPosition(url, position);
    if(import.first) {
      DUChainReadLocker lock(DUChain::lock());
      if(import.first)
        return qMakePair(KUrl(import.first->url().str()), SimpleCursor(0,0));
    }

    QPair<SimpleRange, const rpp::pp_macro*> m = usedMacroForPosition(url, position);

    if(!m.first.isValid())
      return qMakePair(KUrl(), SimpleCursor::invalid());

    return qMakePair(KUrl(m.second->file.str()), SimpleCursor(m.second->sourceLine, 0));
}

QWidget* CppLanguageSupport::specialLanguageObjectNavigationWidget(const KUrl& url, const SimpleCursor& position) {

  QPair<TopDUContextPointer, SimpleRange> import = importedContextForPosition(url, position);
    if(import.first) {
      DUChainReadLocker lock(DUChain::lock());
      if(import.first) {
        //Prefer a standardContext, because the included one may have become empty due to
        if(import.first->localDeclarations().count() == 0 && import.first->childContexts().count() == 0) {

          KDevelop::TopDUContext* betterCtx = standardContext(KUrl(import.first->url().str()));

          if(betterCtx && (betterCtx->localDeclarations().count() != 0 || betterCtx->childContexts().count() != 0))
            return betterCtx->createNavigationWidget(0, 0, i18n("Emptied by preprocessor<br />"));
        }
        return import.first->createNavigationWidget();
      }
    }

    QPair<SimpleRange, const rpp::pp_macro*> m = usedMacroForPosition(url, position);
    if(!m.first.isValid())
      return 0;

    //Evaluate the preprocessed body
    QPair<QPair<QString, SimpleRange>, QString> found = cursorIdentifier(url, position);

    QString text = found.first.first;
    QString preprocessedBody;
    //Check whether tail contains arguments
    QString tail = found.second.trimmed(); ///@todo make this better.
    if(tail.startsWith("(")) {
      int i = findClose( tail, 0 );
      if(i != -1) {
        text += tail.left(i+1);
      }
    }

    {
      DUChainReadLocker lock(DUChain::lock());
      TopDUContext* ctx = standardContext(url, true);
      if(ctx) {
        Cpp::EnvironmentFile* p(dynamic_cast<Cpp::EnvironmentFile*>(ctx->parsingEnvironmentFile().data()));
        if(p) {
          kDebug() << "preprocessing" << text;
          preprocessedBody = Cpp::preprocess(text, p, position.line);
        }
      }
    }

    return new Cpp::NavigationWidget(*m.second, preprocessedBody);
}

void CppLanguageSupport::newClassWizard()
{
  //TODO: Should give some hint on where it should be added
  SimpleRefactoring::self().createNewClass(0);
}

UIBlockTester::UIBlockTesterThread::UIBlockTesterThread( UIBlockTester& parent ) : QThread(), m_parent( parent ), m_stop(false) {
}

 void UIBlockTester::UIBlockTesterThread::run() {
   while(!m_stop) {
           msleep( m_parent.m_msecs / 10 );
           m_parent.m_timeMutex.lock();
           QDateTime t = QDateTime::currentDateTime();
           uint msecs = m_parent.m_lastTime.time().msecsTo( t.time() );
           if( msecs > m_parent.m_msecs ) {
                   m_parent.lockup();
                   m_parent.m_lastTime = t;
           }
           m_parent.m_timeMutex.unlock();
  }
 }

 void UIBlockTester::UIBlockTesterThread::stop() {
         m_stop = true;
 }

 UIBlockTester::UIBlockTester( uint milliseconds ) : m_thread( *this ), m_msecs( milliseconds ) {
         m_timer = new QTimer( this );
         m_timer->start( milliseconds/10 );
         connect( m_timer, SIGNAL(timeout()), this, SLOT(timer()) );
         timer();
         m_thread.start();
 }
 UIBlockTester::~UIBlockTester() {
   m_thread.stop();
  m_thread.wait();
 }

 void UIBlockTester::timer() {
         m_timeMutex.lock();
         m_lastTime = QDateTime::currentDateTime();
         m_timeMutex.unlock();
#ifdef CALLGRIND_TRACE_UI_LOCKUP
         CALLGRIND_STOP_INSTRUMENTATION
#endif
 }

void UIBlockTester::lockup() {
        //std::cout << "UIBlockTester: lockup of the UI for " << m_msecs << endl; ///kdDebug(..) is not thread-safe..
#ifdef CALLGRIND_TRACE_UI_LOCKUP
    CALLGRIND_START_INSTRUMENTATION
#else
    kDebug() << "ui is blocking";
#endif
 }

#include "cpplanguagesupport.moc"
