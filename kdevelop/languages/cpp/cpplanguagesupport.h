/*
 * KDevelop C++ Language Support
 *
 * Copyright 2005 Matt Rogers <mattr@kde.org>
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

#ifndef KDEVCPPLANGUAGESUPPORT_H
#define KDEVCPPLANGUAGESUPPORT_H

#include <interfaces/iplugin.h>
#include <language/interfaces/ilanguagesupport.h>
#include "environmentmanager.h"
#include <QThread>

class CppHighlighting;
class CPPParseJob;
class CppCodeCompletion;
class AST;
class TranslationUnitAST;
class IncludeFileDataProvider;

namespace KDevelop { class ICodeHighlighting; class IProject; class IDocument; class SimpleRange; class CodeCompletion; template<class T> class DUChainPointer; typedef DUChainPointer<TopDUContext> TopDUContextPointer; }
namespace Cpp { class EnvironmentManager; class StaticCodeAssistant; }
namespace CppTools { class IncludePathResolver; }

///A class that helps detecting what exactly makes the UI block. To use it, just place a breakpoint on UIBlockTester::lockup() and inspect the execution-position of the main thread
class UIBlockTester : public QObject {
        Q_OBJECT
    class UIBlockTesterThread : public QThread {
    public:
      UIBlockTesterThread( UIBlockTester& parent );
      void run();
      void stop();
    private:
      UIBlockTester& m_parent;
      bool m_stop;
    };
  friend class UIBlockTesterThread;
public:

  ///@param milliseconds when the ui locks for .. milliseconds, lockup() is called
  UIBlockTester( uint milliseconds );
  ~UIBlockTester();

private slots:
  void timer();

protected:
   virtual void lockup();

 private:
     UIBlockTesterThread m_thread;
     QDateTime m_lastTime;
     QMutex m_timeMutex;
     QTimer * m_timer;
     uint m_msecs;
};

class CppLanguageSupport : public KDevelop::IPlugin, public KDevelop::ILanguageSupport
{
Q_OBJECT
Q_INTERFACES( KDevelop::ILanguageSupport )
public:
    explicit CppLanguageSupport( QObject* parent, const QVariantList& args = QVariantList() );
    virtual ~CppLanguageSupport();

    QString name() const;

    const KDevelop::ICodeHighlighting *codeHighlighting() const;
    KDevelop::ILanguage *language();
    KDevelop::ContextMenuExtension contextMenuExtension(KDevelop::Context* context);
    KDevelop::ParseJob *createParseJob( const KUrl &url );
    //KDevelop::AstRepresentationPtr  generateAst(const KDevelop::TopDUContext & topContext);
    
    static CppLanguageSupport* self();

    virtual void createActionsForMainWindow(Sublime::MainWindow* window, QString& xmlFile, KActionCollection& actions);
/**
 * There may be multiple differnt parsed versions of a document available in the du-chain.
 * This function helps choosing the right one, by creating a standard parsing-environment,
 * and searching for a TopDUContext that fits in. If this fails, a random version is chosen.
 *
 * If simplified environment-matching is enabled, and a proxy-context is found, it returns
 * that proxy-contexts target-context, so the returned context may be used for completion etc.
 * without additional checking.
 *
 * @todo Move this somewhere more general
 *
 * @warning The du-chain must be locked before calling this.
* */
  virtual KDevelop::TopDUContext *standardContext(const KUrl& url, bool proxyContext = false);
  
public slots:
    void findIncludePathsForJob(CPPParseJob* job);

    ///UI:
    void switchDefinitionDeclaration();

    void newClassWizard();

private:

    //Returns the identifier and its range under the cursor as first return-value, and the tail behind it as the second
    //If the given line is an include directive, the complete line is returned starting at the directive
    QPair<QPair<QString, KDevelop::SimpleRange>, QString> cursorIdentifier(const KUrl& url, const KDevelop::SimpleCursor& position) const;

    QPair<KDevelop::TopDUContextPointer, KDevelop::SimpleRange> importedContextForPosition(const KUrl& url, const KDevelop::SimpleCursor& position);

    QPair<KDevelop::SimpleRange, const rpp::pp_macro*> usedMacroForPosition(const KUrl& url, const KDevelop::SimpleCursor& position);

    virtual KDevelop::SimpleRange specialLanguageObjectRange(const KUrl& url, const KDevelop::SimpleCursor& position);

    virtual QPair<KUrl, KDevelop::SimpleCursor> specialLanguageObjectJumpCursor(const KUrl& url, const KDevelop::SimpleCursor& position);

    virtual QWidget* specialLanguageObjectNavigationWidget(const KUrl& url, const KDevelop::SimpleCursor& position);

    static CppLanguageSupport* m_self;

    CppHighlighting *m_highlights;
    KDevelop::CodeCompletion *m_cc, *m_missingIncludeCompletion;
    Cpp::ReferenceCountedMacroSet *m_standardMacros;
    
    CppTools::IncludePathResolver *m_includeResolver;
    IncludeFileDataProvider* m_quickOpenDataProvider;
    UIBlockTester* m_blockTester;
    
    Cpp::StaticCodeAssistant * m_assistant;
};

#endif

