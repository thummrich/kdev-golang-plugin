/* This file is part of KDevelop
    Copyright 2006-2008 Hamish Rodda <rodda@kde.org>

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

#ifndef ABSTRACTUSEBUILDER_H
#define ABSTRACTUSEBUILDER_H

#include "../declaration.h"
#include "../use.h"
#include "../topducontext.h"
#include "../duchain.h"
#include "../duchainlock.h"

#include <language/editor/editorintegrator.h>
#include <ktexteditor/smartinterface.h>

namespace KDevelop {

/**
 * \short Abstract definition-use chain use builder class
 *
 * The AbstractUseBuilder is a convenience class template for creating customized
 * definition-use chain use builders from an AST.  It simplifies:
 * - use of your editor integrator
 * - creating or modifying existing \ref Use "Uses"
 *
 * \author Hamish Rodda \<rodda@kde.org\>
 */
template<typename T, typename NameT, typename LanguageSpecificUseBuilderBase>
class AbstractUseBuilder: public LanguageSpecificUseBuilderBase
{
public:
  /// Constructor.
  AbstractUseBuilder()
    : m_finishContext(true)
  {
  }

  /**
   * Iterate an existing duchain, and add, remove or modify uses as determined
   * from the ast.
   *
   * \param node AST node to start visiting.
   */
  void buildUses(T *node)
  {
    TopDUContext* top = dynamic_cast<TopDUContext*>(contextFromNode(node));

    if (top) {
      DUChainWriteLocker lock(DUChain::lock());
      top->clearUsedDeclarationIndices();
      if(top->features() & TopDUContext::AllDeclarationsContextsAndUses)
        LanguageSpecificUseBuilderBase::setRecompiling(true);
    }

    LanguageSpecificUseBuilderBase::supportBuild(node);
  }

protected:
  /**
   * Register a new use at the AST node \a name.
   *
   * \param node AST node which both represents a use and the identifier for the declaration which is being used.
   */
  
  struct ContextUseTracker {
    QSet<KTextEditor::SmartRange*> reuseRanges;
    QList<QPair<KDevelop::Use, KTextEditor::SmartRange*> > createUses;
  };
  
  void newUse(NameT* name)
  {
    QualifiedIdentifier id = identifierForNode(name);

    SimpleRange newRange = editorFindRange(name, name);

    DUChainWriteLocker lock(DUChain::lock()); ///@todo Don't call findDeclarations during write-lock, it can lead to UI lockups
    QList<Declaration*> declarations = LanguageSpecificUseBuilderBase::currentContext()->findDeclarations(id, newRange.start);
    foreach (Declaration* declaration, declarations)
      if (!declaration->isForwardDeclaration()) {
        declarations.clear();
        declarations.append(declaration);
        break;
      }
    // If we don't break, there's no non-forward declaration

    lock.unlock();
    newUse( name, newRange, !declarations.isEmpty() ? declarations.first() : 0 );
  }

  ///@todo Work this over! We must not pass around "Declaration*" values if the duchain is not locked.

  /**
   * Register a new use for a \a declaration with a \a node.
   *
   * \param node Node which encompasses the use.
   * \param decl Declaration which is being used. May be null when a declaration cannot be found for the use.
   */
  void newUse(T* node, KDevelop::Declaration* declaration)
  {
    newUse(node, editorFindRange(node, node), declaration);
  }

  /**
   * Register a new use.
   *
   * \param newRange Text range which encompasses the use.
   * \param decl Declaration which is being used. May be null when a declaration cannot be found for the use.
   */
  void newUse(T* node, const SimpleRange& newRange, Declaration* declaration)
  {
    DUChainWriteLocker lock(DUChain::lock());

    bool encountered = false; ///@todo This can cause I/O, so it would be better if it was possible with only a read-lock
    int declarationIndex = LanguageSpecificUseBuilderBase::currentContext()->topContext()->indexForUsedDeclaration(declaration);
    int contextUpSteps = 0; //We've got to use the stack here, and not parentContext(), because the order may be different

    {
      //We've got to consider the translated range, and while we use it, the smart-mutex needs to be locked
      LockedSmartInterface iface = LanguageSpecificUseBuilderBase::editor()->smart();
      SimpleRange translated = LanguageSpecificUseBuilderBase::editor()->translate(iface, newRange);
      
//       if(iface)
//         kDebug() << "translated by" << (translated.start.textCursor() - newRange.start.textCursor()) << (translated.end.textCursor() - newRange.end.textCursor()) << "to revision" << iface->currentRevision();
      
      KTextEditor::Range textTranslated  = translated.textRange();

      /*
      * We need to find a context that this use fits into, which must not necessarily be the current one.
      * The reason are macros like SOME_MACRO(SomeClass), where SomeClass is expanded to be within a
      * sub-context that comes from the macro. That sub-context will have a very small range, and will most
      * probably not be the range of the actual "SomeClass" text, so the "SomeClass" use has to be moved
      * into the context that surrounds the SOME_MACRO invocation.
      * */
      DUContext* newContext = LanguageSpecificUseBuilderBase::currentContext();
      while (!newContext->range().contains(translated) && contextUpSteps < (LanguageSpecificUseBuilderBase::contextStack().size()-1)) {
        ++contextUpSteps;
        newContext = LanguageSpecificUseBuilderBase::contextStack()[LanguageSpecificUseBuilderBase::contextStack().size()-1-contextUpSteps];
      }
      
      KTextEditor::SmartRange* use = 0;

      if (contextUpSteps) {
        LanguageSpecificUseBuilderBase::editor()->setCurrentRange(iface, newContext->smartRange()); //We have to do this, because later we will call closeContext(), and that will close one smart-range
        m_finishContext = false;
        openContext(newContext);
        m_finishContext = true;
        currentUseTracker() = m_trackerStack.at(m_trackerStack.size()-contextUpSteps-2);

        Q_ASSERT(m_contexts[m_trackerStack.size()-contextUpSteps-2] == LanguageSpecificUseBuilderBase::currentContext());
      }

      if (LanguageSpecificUseBuilderBase::recompiling() && this->currentContext()->smartRange()) {

        //Find a smart-range that we can reuse
        KTextEditor::SmartRange* containerRange = this->currentContext()->smartRange();
        KTextEditor::SmartRange* child  = containerRange->mostSpecificRange(textTranslated);
        while(child && child->parentRange() != containerRange)
          child = child->parentRange();
        
        //Solution for multiple equal ranges or ranges ending at the same position
        while(child && child->end() == textTranslated.end() && (!currentUseTracker().reuseRanges.contains(child) || *child != textTranslated))
          child = containerRange->childAfter(child);
        
        if(child && *child == textTranslated && currentUseTracker().reuseRanges.contains(child)) {
          //We found a range to re-use
          currentUseTracker().reuseRanges.remove(child);
          use = child;
        }
      }
      if (!encountered) {
        if(!use) {
          use = LanguageSpecificUseBuilderBase::editor()->currentRange(iface) ? LanguageSpecificUseBuilderBase::editor()->createRange(iface, newRange.textRange()) : 0;
          LanguageSpecificUseBuilderBase::editor()->exitCurrentRange(iface);
        }
        
        if (LanguageSpecificUseBuilderBase::m_mapAst)
          LanguageSpecificUseBuilderBase::editor()->parseSession()->mapAstUse(
            node, qMakePair<DUContextPointer, SimpleRange>(DUContextPointer(newContext), newRange));

        currentUseTracker().createUses << qMakePair(KDevelop::Use(newRange, declarationIndex), use);
      }
    }

    if (contextUpSteps) {
      Q_ASSERT(m_contexts[m_trackerStack.size()-contextUpSteps-2] == LanguageSpecificUseBuilderBase::currentContext());
      m_trackerStack[m_trackerStack.size()-contextUpSteps-2] = currentUseTracker();
      m_finishContext = false;
      closeContext();
      m_finishContext = true;
    }
  }

  /**
   * Reimplementation of openContext, to track which uses should be assigned to which context.
   */
  virtual void openContext(KDevelop::DUContext* newContext)
  {
    LanguageSpecificUseBuilderBase::openContext(newContext);

    DUChainWriteLocker lock(DUChain::lock());
    LockedSmartInterface iface = LanguageSpecificUseBuilderBase::editor()->smart();
    
    ContextUseTracker newTracker;
    foreach(KTextEditor::SmartRange* range, newContext->useRanges())
      newTracker.reuseRanges.insert(range);
    
    m_trackerStack.push(newTracker);
    m_contexts.push(newContext);
  }

  /**
   * Reimplementation of closeContext, to track which uses should be assigned to which context.
   */
  virtual void closeContext()
  {
    if(m_finishContext) {
      DUChainWriteLocker lock(DUChain::lock());

      LockedSmartInterface iface = LanguageSpecificUseBuilderBase::editor()->smart();
      //Delete all ranges that were not re-used
      if(this->currentContext()->smartRange() && iface) {
        this->currentContext()->takeUseRanges();
        foreach(KTextEditor::SmartRange* range, currentUseTracker().reuseRanges) {
#ifdef DEBUG_UPDATE_MATCHING
          if(!range->isEmpty()) //we cannot find empty ranges, so don't give warnings for them
            kDebug() << "deleting not re-used range:" << *range;
#endif
          delete range;
        }
      }
      
      this->currentContext()->deleteUses();
      
      Q_ASSERT(this->currentContext()->usesCount() == 0);
      
      ContextUseTracker& tracker(currentUseTracker());
      for(int a = 0; a < tracker.createUses.size(); ++a) {
        KTextEditor::SmartRange* range = 0;
        
        if(this->currentContext()->smartRange() && iface) {
          range = tracker.createUses[a].second;
          Q_ASSERT(range);
        }
        
        Q_ASSERT(this->currentContext()->usesCount() == a);
        this->currentContext()->createUse(tracker.createUses[a].first.m_declarationIndex, tracker.createUses[a].first.m_range, range);
      }
      
    }

    LanguageSpecificUseBuilderBase::closeContext();

    m_trackerStack.pop();
    m_contexts.pop();
  }

private:
  inline ContextUseTracker& currentUseTracker() { return m_trackerStack.top(); }
  QStack<ContextUseTracker> m_trackerStack;
  QStack<KDevelop::DUContext*> m_contexts;

  //Whether not encountered uses should be deleted during closeContext()
  bool m_finishContext;
};

}

#endif // ABSTRACTUSEBUILDER_H

