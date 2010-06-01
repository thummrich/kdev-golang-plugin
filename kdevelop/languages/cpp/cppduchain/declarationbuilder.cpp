/* This file is part of KDevelop
    Copyright 2006-2007 Hamish Rodda <rodda@kde.org>
    Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>

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

#include "declarationbuilder.h"

#include "debugbuilders.h"

#include <QByteArray>
#include <typeinfo>

#include "templatedeclaration.h"

#include <ktexteditor/smartrange.h>
#include <ktexteditor/smartinterface.h>

#include "parser/type_compiler.h"
#include "parser/commentformatter.h"

#include <language/duchain/forwarddeclaration.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/repositories/itemrepository.h>
#include <language/duchain/types/identifiedtype.h>
#include <language/duchain/namespacealiasdeclaration.h>
#include <language/duchain/aliasdeclaration.h>
#include <util/pushvalue.h>

#include "qtfunctiondeclaration.h"
#include "qpropertydeclaration.h"
#include "cppeditorintegrator.h"
#include "name_compiler.h"
#include <language/duchain/classfunctiondeclaration.h>
#include <language/duchain/functiondeclaration.h>
#include <language/duchain/functiondefinition.h>
#include "templateparameterdeclaration.h"
#include "type_compiler.h"
#include "tokens.h"
#include "parsesession.h"
#include "cpptypes.h"
#include "cppduchain.h"
#include "cpptypes.h"
#include <language/duchain/classdeclaration.h>

#include "cppdebughelper.h"
#include "name_visitor.h"
#include "usebuilder.h"

using namespace KTextEditor;
using namespace KDevelop;
using namespace Cpp;

ClassDeclarationData::ClassType classTypeFromTokenKind(int kind)
{
  switch(kind)
  {
  case Token_struct:
    return ClassDeclarationData::Struct;
  case Token_union:
    return ClassDeclarationData::Union;
  default:
    return ClassDeclarationData::Class;
  }
}

bool DeclarationBuilder::changeWasSignificant() const
{
  ///@todo Also set m_changeWasSignificant if publically visible declarations were removed(needs interaction with abstractcontextbuilder)
  return m_changeWasSignificant;
}

DeclarationBuilder::DeclarationBuilder (ParseSession* session)
  : DeclarationBuilderBase(), m_changeWasSignificant(false), m_ignoreDeclarators(false), m_declarationHasInitializer(false), m_collectQtFunctionSignature(false)
{
  setEditor(new CppEditorIntegrator(session), true);
}

DeclarationBuilder::DeclarationBuilder (CppEditorIntegrator* editor)
  : DeclarationBuilderBase(), m_changeWasSignificant(false), m_ignoreDeclarators(false), m_declarationHasInitializer(false), m_collectQtFunctionSignature(false)
{
  setEditor(editor, false);
}

ReferencedTopDUContext DeclarationBuilder::buildDeclarations(Cpp::EnvironmentFilePointer file, AST *node, IncludeFileList* includes, const ReferencedTopDUContext& updateContext, bool removeOldImports)
{
  ReferencedTopDUContext top = buildContexts(file, node, includes, updateContext, removeOldImports);

  Q_ASSERT(m_accessPolicyStack.isEmpty());
  Q_ASSERT(m_functionDefinedStack.isEmpty());

  return top;
}

// DUContext* DeclarationBuilder::buildSubDeclarations(const HashedString& url, AST *node, KDevelop::DUContext* parent) {
//   DUContext* top = buildSubContexts(url, node, parent);
//
//   Q_ASSERT(m_accessPolicyStack.isEmpty());
//   Q_ASSERT(m_functionDefinedStack.isEmpty());
//
//   return top;
// }

void DeclarationBuilder::visitTemplateParameter(TemplateParameterAST * ast) {
  
  //Backup and zero the parameter declaration, because we will handle it here directly, and don't want a normal one to be created
  
  m_ignoreDeclarators = true;
  DeclarationBuilderBase::visitTemplateParameter(ast);
  m_ignoreDeclarators = false;
  
  if( ast->type_parameter || ast->parameter_declaration ) {
    ///@todo deal with all the other stuff the AST may contain
    TemplateParameterDeclaration* decl;
    if(ast->type_parameter)
      decl = openDeclaration<TemplateParameterDeclaration>(ast->type_parameter->name, ast, Identifier(), false, !ast->type_parameter->name);
    else
      decl = openDeclaration<TemplateParameterDeclaration>(ast->parameter_declaration->declarator ? ast->parameter_declaration->declarator->id : 0, ast, Identifier(), false, !ast->parameter_declaration->declarator);

    DUChainWriteLocker lock(DUChain::lock());
    AbstractType::Ptr type = lastType();
    if( type.cast<CppTemplateParameterType>() ) {
      type.cast<CppTemplateParameterType>()->setDeclaration(decl);
    } else {
      kDebug(9007) << "bad last type";
    }
    decl->setAbstractType(type);

    if( ast->type_parameter && ast->type_parameter->type_id ) {
      //Extract default type-parameter
      QualifiedIdentifier defaultParam;

      QString str;
      ///Only record the strings, because these expressions may depend on template-parameters and thus must be evaluated later
      str += stringFromSessionTokens( editor()->parseSession(), ast->type_parameter->type_id->start_token, ast->type_parameter->type_id->end_token );

      defaultParam = QualifiedIdentifier(str);

      decl->setDefaultParameter(defaultParam);
    }

    if( ast->parameter_declaration ) {
      if( ast->parameter_declaration->expression )
        decl->setDefaultParameter( QualifiedIdentifier( stringFromSessionTokens( editor()->parseSession(), ast->parameter_declaration->expression->start_token, ast->parameter_declaration->expression->end_token ) ) );
    }
    closeDeclaration(ast->parameter_declaration);
  }
}

void DeclarationBuilder::parseComments(const ListNode<uint> *comments)
{
  setComment(CommentFormatter::formatComment(comments, editor()->parseSession()));
}


void DeclarationBuilder::visitFunctionDeclaration(FunctionDefinitionAST* node)
{

  parseComments(node->comments);
  parseStorageSpecifiers(node->storage_specifiers);
  parseFunctionSpecifiers(node->function_specifiers);
  
  //Used to map to the top level function node once the Declaration is built
  if(m_mapAst)
    m_mappedNodes.push(node);
  
  m_functionDefinedStack.push(node->start_token);

  DeclarationBuilderBase::visitFunctionDeclaration(node);

  m_functionDefinedStack.pop();
  
  if(m_mapAst)
    m_mappedNodes.pop();

  popSpecifiers();
}

//Visitor that clears the ducontext from all AST nodes
struct ClearDUContextVisitor : public DefaultVisitor {

  virtual void visit(AST* node) {
    if(node)
      node->ducontext = 0;
    DefaultVisitor::visit(node);
  }
};

void DeclarationBuilder::visitInitDeclarator(InitDeclaratorAST *node)
{
  PushValue<bool> setHasInitialize(m_declarationHasInitializer, (bool)node->initializer);

  if(currentContext()->type() == DUContext::Other) {
    //Cannot declare a a function within a code-context
    node->declarator->parameter_is_initializer = true;
  }else if(!m_inFunctionDefinition && node->declarator && node->declarator->parameter_declaration_clause && node->declarator->id) {
    //Decide whether the parameter-declaration clause is valid
    DUChainWriteLocker lock(DUChain::lock());
    SimpleCursor pos = editor()->findPosition(node->start_token, KDevelop::EditorIntegrator::FrontEdge);
    
    QualifiedIdentifier id;
    identifierForNode(node->declarator->id, id);    
    DUContext* previous = currentContext();

    DUContext* previousLast = lastContext();
    QVector<KDevelop::DUContext::Import> importedParentContexts = m_importedParentContexts;
    
    openPrefixContext(node, id, pos); //We create a temporary prefix-context to search from within the right scope
    
    DUContext* tempContext = currentContext();
    if (currentContext()->type() != DUContext::Class)
      node->declarator->parameter_is_initializer = !checkParameterDeclarationClause(node->declarator->parameter_declaration_clause);
    closePrefixContext(id);
    
    
    
    if(tempContext != previous) {
      
      //We remove all of its traces from the AST using ClearDUContextVisitor.
      ClearDUContextVisitor clear;
      clear.visit(node);

      ///@todo We don't delete the tempContext, as that may cause crashes. Problem: This leaves garbage in the duchain
      ///@todo Solve the redundancy issue once and for all, properly, using a SimpleDeclarationOrFunctionDeclarationAST or similar.
      
      //Since we don't delete the temporary context, at least collapse its range.
      tempContext->setRange(SimpleRange(tempContext->range().start, tempContext->range().end));
      
      setLastContext(previousLast);
      m_importedParentContexts = importedParentContexts;
    }
    Q_ASSERT(currentContext() == previous);
  }
  
  DeclarationBuilderBase::visitInitDeclarator(node);
}

void DeclarationBuilder::visitQPropertyDeclaration(QPropertyDeclarationAST* node)
{
  QPropertyDeclaration *decl = openDeclaration<QPropertyDeclaration>(node->name, node->name);
  decl->setIsStored(node->stored);
  decl->setIsUser(node->user);
  decl->setIsConstant(node->constant);
  decl->setIsFinal(node->final);

  DeclarationBuilderBase::visitQPropertyDeclaration(node);
  AbstractType::Ptr type = lastType();
  closeDeclaration(true);

  if(type) {
    DUChainWriteLocker lock(DUChain::lock());
    decl->setAbstractType(type);
    decl->setAccessPolicy(KDevelop::Declaration::Public);
  }

  m_pendingPropertyDeclarations.insert(currentContext(), qMakePair(decl, node));
}

KDevelop::IndexedDeclaration DeclarationBuilder::resolveMethodName(NameAST *node)
{
  QualifiedIdentifier id;
  identifierForNode(node, id);

  DUChainReadLocker lock(DUChain::lock());
  if(currentDeclaration() && currentDeclaration()->internalContext()) {
    const QList<Declaration*> declarations = currentDeclaration()->internalContext()->findDeclarations(id, SimpleCursor::invalid(), AbstractType::Ptr(), 0, DUContext::OnlyFunctions);
    if(!declarations.isEmpty())
      return KDevelop::IndexedDeclaration(declarations.first());
  }

  return KDevelop::IndexedDeclaration();
}

void DeclarationBuilder::resolvePendingPropertyDeclarations(const QList<PropertyResolvePair> &pairs)
{
  foreach(const PropertyResolvePair &pair, pairs) {
    if(pair.second->getter){
      const KDevelop::IndexedDeclaration declaration = resolveMethodName(pair.second->getter);
      if(declaration.isValid())
        pair.first->setReadMethod(declaration);
    }
    if(pair.second->setter){
      const KDevelop::IndexedDeclaration declaration = resolveMethodName(pair.second->setter);
      if(declaration.isValid())
        pair.first->setWriteMethod(declaration);
    }
    if(pair.second->resetter){
      const KDevelop::IndexedDeclaration declaration = resolveMethodName(pair.second->resetter);
      if(declaration.isValid())
        pair.first->setResetMethod(declaration);
    }
    if(pair.second->notifier){
      const KDevelop::IndexedDeclaration declaration = resolveMethodName(pair.second->notifier);
      if(declaration.isValid())
        pair.first->setNotifyMethod(declaration);
    }
    if(pair.second->designableMethod){
      const KDevelop::IndexedDeclaration declaration = resolveMethodName(pair.second->designableMethod);
      if(declaration.isValid())
        pair.first->setDesignableMethod(declaration);
    }
    if(pair.second->scriptableMethod){
      const KDevelop::IndexedDeclaration declaration = resolveMethodName(pair.second->scriptableMethod);
      if(declaration.isValid())
        pair.first->setScriptableMethod(declaration);
    }
  }
}

void DeclarationBuilder::visitSimpleDeclaration(SimpleDeclarationAST* node)
{
  parseComments(node->comments);
  parseStorageSpecifiers(node->storage_specifiers);
  parseFunctionSpecifiers(node->function_specifiers);

  if(m_mapAst)
    m_mappedNodes.push(node);
  
  m_functionDefinedStack.push(0);

  DeclarationBuilderBase::visitSimpleDeclaration(node);

  m_functionDefinedStack.pop();
  
  if(m_mapAst)
    m_mappedNodes.pop();

  popSpecifiers();
}

void DeclarationBuilder::visitDeclarator (DeclaratorAST* node)
{
  if(m_ignoreDeclarators) {
    DeclarationBuilderBase::visitDeclarator(node);
    return;
  }
  //need to make backup because we may temporarily change it
  ParameterDeclarationClauseAST* parameter_declaration_clause_backup = node->parameter_declaration_clause;

  m_collectQtFunctionSignature = !m_accessPolicyStack.isEmpty() && ((m_accessPolicyStack.top() & FunctionIsSlot) || (m_accessPolicyStack.top() & FunctionIsSignal));
  m_qtFunctionSignature = QByteArray();
  
  if (node->parameter_declaration_clause && !node->parameter_is_initializer) {

    if(m_collectQtFunctionSignature) //We need to do this just to collect the signature
      checkParameterDeclarationClause(node->parameter_declaration_clause);
    
    Declaration* decl = openFunctionDeclaration(node->id, node);
    
    ///Create mappings iff the AST feature is specified
    if(m_mapAst && !m_mappedNodes.empty())
      editor()->parseSession()->mapAstDuChain(m_mappedNodes.top(), KDevelop::DeclarationPointer(decl));

    if( !m_functionDefinedStack.isEmpty() ) {
        DUChainWriteLocker lock(DUChain::lock());
        decl->setDeclarationIsDefinition( (bool)m_functionDefinedStack.top() );
    }
    
    applyFunctionSpecifiers();
  } else {
    openDefinition(node->id, node, node->id == 0);
    node->parameter_declaration_clause = 0;
  }

  m_collectQtFunctionSignature = false;

  applyStorageSpecifiers();

  DeclarationBuilderBase::visitDeclarator(node);

  if (node->parameter_declaration_clause) {
    if (!m_functionDefinedStack.isEmpty() && m_functionDefinedStack.top() && node->id) {

      DUChainWriteLocker lock(DUChain::lock());
      //We have to search for the fully qualified identifier, so we always get the correct class
      QualifiedIdentifier id = currentContext()->scopeIdentifier(false);
      QualifiedIdentifier id2;
      identifierForNode(node->id, id2);
      id += id2;
      
      id.setExplicitlyGlobal(true);

      if (id.count() > 1 ||
           (m_inFunctionDefinition && (currentContext()->type() == DUContext::Namespace || currentContext()->type() == DUContext::Global))) {
        SimpleCursor pos = currentDeclaration()->range().start;//editor()->findPosition(m_functionDefinedStack.top(), KDevelop::EditorIntegrator::FrontEdge);
        // TODO: potentially excessive locking

        QList<Declaration*> declarations = currentContext()->findDeclarations(id, pos, AbstractType::Ptr(), 0, DUContext::OnlyFunctions);

        FunctionType::Ptr currentFunction = lastType().cast<FunctionType>();
        int functionArgumentCount = 0;
        if(currentFunction)
          functionArgumentCount = currentFunction->arguments().count();

        for( int cycle = 0; cycle < 3; cycle++ ) {
          bool found = false;
          ///We do 2 cycles: In the first cycle, we want an exact match. In the second, we accept approximate matches.
          foreach (Declaration* dec, declarations) {
            if (dec->isForwardDeclaration())
              continue;
            if(dec == currentDeclaration() || dec->isDefinition())
              continue;
            //Compare signatures of function-declarations:
            if(dec->abstractType()->indexed() == lastType()->indexed())
            {
              //The declaration-type matches this definition, good.
            }else{
              if(cycle == 0) {
                //First cycle, only accept exact matches
                continue;
              }else if(cycle == 1){
                //Second cycle, match by argument-count
                FunctionType::Ptr matchFunction = dec->type<FunctionType>();
                if(currentFunction && matchFunction && currentFunction->arguments().count() == functionArgumentCount ) {
                  //We have a match
                }else{
                  continue;
                }
              }else if(cycle == 2){
                //Accept any match, so just continue
              }
              if(FunctionDefinition::definition(dec) && wasEncountered(FunctionDefinition::definition(dec)))
                continue; //Do not steal declarations
            }

            if(FunctionDefinition* funDef = dynamic_cast<FunctionDefinition*>(currentDeclaration())) {
              funDef->setDeclaration(dec);
            }

            found = true;
            break;
          }
          if(found)
            break;
        }
      }
    }
  }

  closeDeclaration();

  node->parameter_declaration_clause = parameter_declaration_clause_backup;
}

ForwardDeclaration * DeclarationBuilder::openForwardDeclaration(NameAST * name, AST * range)
{
  return openDeclaration<ForwardDeclaration>(name, range);
}

template<class Type>
Type hasTemplateContext( const QList<Type>& contexts ) {
  foreach( const Type& context, contexts )
    if( context && context->type() == KDevelop::DUContext::Template )
      return context;
  return Type(0);
}

DUContext::Import hasTemplateContext( const QVector<DUContext::Import>& contexts, TopDUContext* top ) {
  foreach( const DUContext::Import& context, contexts )
    if( context.context(top) && context.context(top)->type() == KDevelop::DUContext::Template )
      return context;

  return DUContext::Import();
}

//Check whether the given context is a template-context by checking whether it imports a template-parameter context
KDevelop::DUContext* isTemplateContext( KDevelop::DUContext* context ) {
  return hasTemplateContext( context->importedParentContexts(), context->topContext() ).context(context->topContext());
}

template<class T>
T* DeclarationBuilder::openDeclaration(NameAST* name, AST* rangeNode, const Identifier& customName, bool collapseRangeAtStart, bool collapseRangeAtEnd)
{
  DUChainWriteLocker lock(DUChain::lock());

  KDevelop::DUContext* templateCtx = hasTemplateContext(m_importedParentContexts, topContext()).context(topContext());

  ///We always need to create a template declaration when we're within a template, so the declaration can be accessed
  ///by specialize(..) and its indirect DeclarationId
  if( templateCtx || m_templateDeclarationDepth ) {
    Cpp::SpecialTemplateDeclaration<T>* ret = openDeclarationReal<Cpp::SpecialTemplateDeclaration<T> >( name, rangeNode, customName, collapseRangeAtStart, collapseRangeAtEnd );
    ret->setTemplateParameterContext(templateCtx);
    return ret;
  } else{
    return openDeclarationReal<T>( name, rangeNode, customName, collapseRangeAtStart, collapseRangeAtEnd );
  }
}

template<class T>
T* DeclarationBuilder::openDeclarationReal(NameAST* name, AST* rangeNode, const Identifier& customName, bool collapseRangeAtStart, bool collapseRangeAtEnd, const SimpleRange* customRange)
{
  SimpleRange newRange;
  if(name) {
    uint start = name->unqualified_name->start_token;
    uint end = name->unqualified_name->end_token;

    //We must exclude the tilde. Else we may get totally messed up ranges when the name of a destructor is renamed in a macro
    if(name->unqualified_name->tilde)
      start = name->unqualified_name->tilde+1;

    newRange = editor()->findRange(start, end);
  }else if(rangeNode) {
    newRange = editor()->findRange(rangeNode);
  }else if(customRange) {
    newRange = *customRange;
  }

  if(collapseRangeAtStart)
    newRange.end = newRange.start;
  else if(collapseRangeAtEnd)
    newRange.start = newRange.end;

  Identifier localId = customName;

  if (name) {
    //If this is an operator thing, build the type first. Since it's part of the name, the type-builder doesn't catch it normally
    if(name->unqualified_name && name->unqualified_name->operator_id)
      visit(name->unqualified_name->operator_id);
    
    QualifiedIdentifier id;
    identifierForNode(name, id);

    if(localId.isEmpty())
      localId = id.last();
  }

  T* declaration = 0;

  if (recompiling()) {
    // Seek a matching declaration

    // Translate cursor to take into account any changes the user may have made since the text was retrieved
    LockedSmartInterface iface = editor()->smart();
    SimpleRange translated = editor()->translate(iface, newRange);

#ifdef DEBUG_UPDATE_MATCHING
    kDebug() << "checking" << localId.toString() << "range" << translated.textRange();
#endif

    ///@todo maybe order the declarations within ducontext and change here back to walking the indices, because that's easier to debug and faster
    QList<Declaration*> decls = currentContext()->findLocalDeclarations(localId, SimpleCursor::invalid(), 0, AbstractType::Ptr(), DUContext::NoFiltering);
    foreach( Declaration* dec, decls ) {

      if( wasEncountered(dec) )
        continue;

#ifdef DEBUG_UPDATE_MATCHING
      if( !(typeid(*dec) == typeid(T)) )
        kDebug() << "typeid mismatch:" << typeid(*dec).name() << typeid(T).name();

      if (!(dec->range() == translated))
        kDebug() << "range mismatch" << dec->range().textRange() << translated.textRange();

      if(!(localId == dec->identifier()))
        kDebug() << "id mismatch" << dec->identifier().toString() << localId.toString();
#endif

        //This works because dec->textRange() is taken from a smart-range. This means that now both ranges are translated to the current document-revision.
      if (dec->range() == translated &&
          (localId == dec->identifier() || (localId.isUnique() && dec->identifier().isUnique())) &&
          typeid(T) == typeid(*dec)
         )
      {
        // Match
        TemplateDeclaration* templateDecl = dynamic_cast<TemplateDeclaration*>(dec);
        if(templateDecl)
          templateDecl->deleteAllInstantiations(); //Delete all instantiations so we have a fresh start
        
        declaration = dynamic_cast<T*>(dec);
        break;
      }
    }

    if(!declaration) {
      ///Second run of the above, this time ignoring the ranges.
      foreach( Declaration* dec, decls ) {
        if( wasEncountered(dec) )
          continue;
        
        if ((localId == dec->identifier() || (localId.isUnique() && dec->identifier().isUnique())) &&
            typeid(*dec) == typeid(T)
          )
        {
          // Match
          declaration = dynamic_cast<T*>(dec);
          declaration->setRange(translated);
          break;
        }
      }
    }
  }
#ifdef DEBUG_UPDATE_MATCHING
  if(declaration)
    kDebug() << "found match for" << localId.toString();
  else
    kDebug() << "nothing found for" << localId.toString();
#endif

  if (!declaration) {
    if(currentContext()->inSymbolTable())
      m_changeWasSignificant = true; //We are adding a declaration that comes into the symbol table, so mark the change significant
/*    if( recompiling() )
      kDebug(9007) << "creating new declaration while recompiling: " << localId << "(" << newRange.textRange() << ")";*/
    LockedSmartInterface iface = editor()->smart();

    SmartRange* prior = editor()->currentRange(iface);

    ///We don't want to move the parent range around if the context is collapsed, so we find a parent range that can hold this range.
    KDevVarLengthArray<SmartRange*, 5> backup;
    while(editor()->currentRange(iface) && !editor()->currentRange(iface)->contains(newRange.textRange())) {
      backup.append(editor()->currentRange(iface));
      editor()->exitCurrentRange(iface);
    }

    if(prior && !editor()->currentRange(iface)) {
      editor()->setCurrentRange(iface, backup.back());
      backup.pop_back();
    }

    SmartRange* range = editor()->currentRange(iface) ? editor()->createRange(iface, newRange.textRange()) : 0;

    editor()->exitCurrentRange(iface);

    for(int a = backup.size()-1; a >= 0; --a)
      editor()->setCurrentRange(iface, backup[a]);
  
    Q_ASSERT(editor()->currentRange(iface) == prior);

    declaration = new T(newRange, currentContext());
    declaration->setSmartRange(range);
    declaration->setIdentifier(localId);
  }

  //Clear some settings
  AbstractFunctionDeclaration* funDecl = dynamic_cast<AbstractFunctionDeclaration*>(declaration);
  if(funDecl)
    funDecl->clearDefaultParameters();

  declaration->setDeclarationIsDefinition(false); //May be set later

  declaration->setIsTypeAlias(m_inTypedef);

  if( localId.templateIdentifiersCount() ) {
    TemplateDeclaration* templateDecl = dynamic_cast<TemplateDeclaration*>(declaration);
    if( declaration && templateDecl ) {
      ///This is a template-specialization. Find the class it is specialized from.
      localId.clearTemplateIdentifiers();

      ///@todo Make sure the searched class is in the same namespace
      QList<Declaration*> decls = currentContext()->findDeclarations(QualifiedIdentifier(localId), editor()->findPosition(name->start_token, KDevelop::EditorIntegrator::FrontEdge) );

      if( !decls.isEmpty() )
      {
        foreach( Declaration* decl, decls )
          if( TemplateDeclaration* baseTemplateDecl = dynamic_cast<TemplateDeclaration*>(decl) ) {
            templateDecl->setSpecializedFrom(baseTemplateDecl);
            break;
          }

        if( !templateDecl->specializedFrom().isValid() )
          kDebug(9007) << "Could not find valid specialization-base" << localId.toString() << "for" << declaration->toString();
      }
    } else {
      kDebug(9007) << "Specialization of non-template class" << declaration->toString();
    }

  }

  declaration->setComment(comment());
  clearComment();

  setEncountered(declaration);

  openDeclarationInternal(declaration);

  return declaration;
}

ClassDeclaration* DeclarationBuilder::openClassDefinition(NameAST* name, AST* range, bool collapseRange, ClassDeclarationData::ClassType classType) {
  Identifier id;

  if(!name) {
    //Unnamed class/struct, use a unique id
    static QAtomicInt& uniqueClassNumber( KDevelop::globalItemRepositoryRegistry().getCustomCounter("Unnamed Class Ids", 1) );
    id = Identifier::unique( uniqueClassNumber.fetchAndAddRelaxed(1) );
  }

  ClassDeclaration* ret = openDeclaration<ClassDeclaration>(name, range, id, collapseRange);
  DUChainWriteLocker lock(DUChain::lock());
  ret->setDeclarationIsDefinition(true);
  ret->clearBaseClasses();
  
  if(m_accessPolicyStack.isEmpty())
    ret->setAccessPolicy(KDevelop::Declaration::Public);
  else
    ret->setAccessPolicy(currentAccessPolicy());
  
  ret->setClassType(classType);
  return ret;
}

Declaration* DeclarationBuilder::openDefinition(NameAST* name, AST* rangeNode, bool collapseRange)
{
  Declaration* ret = openNormalDeclaration(name, rangeNode, KDevelop::Identifier(), collapseRange);
  
  ///Create mappings iff the AST feature is specified
  if(m_mapAst && !m_mappedNodes.empty())
    editor()->parseSession()->mapAstDuChain(m_mappedNodes.top(), KDevelop::DeclarationPointer(ret));

  DUChainWriteLocker lock(DUChain::lock());
  ret->setDeclarationIsDefinition(true);
  return ret;
}

Declaration* DeclarationBuilder::openNormalDeclaration(NameAST* name, AST* rangeNode, const Identifier& customName, bool collapseRange) {
  if(currentContext()->type() == DUContext::Class) {
    ClassMemberDeclaration* mem = openDeclaration<ClassMemberDeclaration>(name, rangeNode, customName, collapseRange);

    DUChainWriteLocker lock(DUChain::lock());
    mem->setAccessPolicy(currentAccessPolicy());
    return mem;
  } else if(currentContext()->type() == DUContext::Template) {
    return openDeclaration<TemplateParameterDeclaration>(name, rangeNode, customName, collapseRange);
  } else {
    return openDeclaration<Declaration>(name, rangeNode, customName, collapseRange);
  }
}

Declaration* DeclarationBuilder::openFunctionDeclaration(NameAST* name, AST* rangeNode) {

   QualifiedIdentifier id;
   identifierForNode(name, id);
   Identifier localId = id.last(); //This also copies the template arguments
   if(id.count() > 1) {
     //Merge the scope of the declaration, and put them tog. Add semicolons instead of the ::, so you can see it's not a qualified identifier.
     //Else the declarations could be confused with global functions.
     //This is done before the actual search, so there are no name-clashes while searching the class for a constructor.

     QString newId = id.last().identifier().str();
     for(int a = id.count()-2; a >= 0; --a)
       newId = id.at(a).identifier().str() + "::" + newId;

     localId.setIdentifier(newId);

     FunctionDefinition* ret = openDeclaration<FunctionDefinition>(name, rangeNode, localId);
     DUChainWriteLocker lock(DUChain::lock());
     ret->setDeclaration(0);
     return ret;
   }

  if(currentContext()->type() == DUContext::Class) {
    if(!m_collectQtFunctionSignature) {
      ClassFunctionDeclaration* fun = openDeclaration<ClassFunctionDeclaration>(name, rangeNode, localId);
      DUChainWriteLocker lock(DUChain::lock());
      fun->setAccessPolicy(currentAccessPolicy());
      fun->setIsAbstract(m_declarationHasInitializer);
      return fun;
    }else{
      QtFunctionDeclaration* fun = openDeclaration<QtFunctionDeclaration>(name, rangeNode, localId);
      DUChainWriteLocker lock(DUChain::lock());
      fun->setAccessPolicy(currentAccessPolicy());
      fun->setIsAbstract(m_declarationHasInitializer);
      fun->setIsSlot(m_accessPolicyStack.top() & FunctionIsSlot);
      fun->setIsSignal(m_accessPolicyStack.top() & FunctionIsSignal);
      QByteArray temp(QMetaObject::normalizedSignature("(" + m_qtFunctionSignature + ")"));
      IndexedString signature(temp.mid(1, temp.length()-2));
//       kDebug() << "normalized signature:" << signature.str() << "from:" << QString::fromUtf8(m_qtFunctionSignature);
      fun->setNormalizedSignature(signature);
      return fun;
    }
  } else if(m_inFunctionDefinition && (currentContext()->type() == DUContext::Namespace || currentContext()->type() == DUContext::Global)) {
    //May be a definition
     FunctionDefinition* ret = openDeclaration<FunctionDefinition>(name, rangeNode, localId);
     DUChainWriteLocker lock(DUChain::lock());
     ret->setDeclaration(0);
     return ret;
  }else{
    return openDeclaration<FunctionDeclaration>(name, rangeNode, localId);
  }
}

void DeclarationBuilder::classTypeOpened(AbstractType::Ptr type) {
  //We override this so we can get the class-declaration into a usable state(with filled type) earlier
    DUChainWriteLocker lock(DUChain::lock());

    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(type.unsafeData());

    if( idType && !idType->declarationId().isValid() ) //When the given type has no declaration yet, assume we are declaring it now
        idType->setDeclaration( currentDeclaration() );

    currentDeclaration()->setType(type);
}

void DeclarationBuilder::closeDeclaration(bool forceInstance)
{
  {
    DUChainWriteLocker lock(DUChain::lock());
      
    if (lastType()) {

      AbstractType::Ptr type = typeForCurrentDeclaration();
      IdentifiedType* idType = dynamic_cast<IdentifiedType*>(type.unsafeData());
      DelayedType::Ptr delayed = type.cast<DelayedType>();

      //When the given type has no declaration yet, assume we are declaring it now.
      //If the type is a delayed type, it is a searched type, and not a declared one, so don't set the declaration then.
      if( !forceInstance && idType && !idType->declarationId().isValid() && !delayed ) {
          idType->setDeclaration( currentDeclaration() );
          //Q_ASSERT(idType->declaration() == currentDeclaration());
      }

      if(currentDeclaration()->kind() != Declaration::NamespaceAlias && currentDeclaration()->kind() != Declaration::Alias) {
        //If the type is not identified, it is an instance-declaration too, because those types have no type-declarations.
        if( (((!idType) || (idType && idType->declarationId() != currentDeclaration()->id())) && !currentDeclaration()->isTypeAlias() && !currentDeclaration()->isForwardDeclaration() ) || dynamic_cast<AbstractFunctionDeclaration*>(currentDeclaration()) || forceInstance )
          currentDeclaration()->setKind(Declaration::Instance);
        else
          currentDeclaration()->setKind(Declaration::Type);
      }

      currentDeclaration()->setType(type);
    }else{
      currentDeclaration()->setAbstractType(AbstractType::Ptr());
      if(dynamic_cast<ClassDeclaration*>(currentDeclaration()))
        currentDeclaration()->setKind(Declaration::Type);
    }
    if(TemplateDeclaration* templateDecl = dynamic_cast<TemplateDeclaration*>(currentDeclaration())) {
      //The context etc. may have been filled with new items, and the declaration may have been searched unsuccessfully, or wrong instantiations created.
      TemplateDeclaration* deleteInstantiationsOf = 0;
      if(templateDecl->instantiatedFrom())
        deleteInstantiationsOf = templateDecl->instantiatedFrom();
      else if(templateDecl->specializedFrom().data())
        deleteInstantiationsOf = dynamic_cast<TemplateDeclaration*>(templateDecl->specializedFrom().data());
      else
        deleteInstantiationsOf = templateDecl;
      
      if(deleteInstantiationsOf) {
        CppDUContext<DUContext>* ctx = dynamic_cast<CppDUContext<DUContext>*>(dynamic_cast<Declaration*>(deleteInstantiationsOf)->internalContext());
        deleteInstantiationsOf->deleteAllInstantiations();
        if(ctx)
          ctx->deleteAllInstantiations();
      }
    }
  }

  if(lastContext() && (lastContext()->type() != DUContext::Other || currentDeclaration()->isFunctionDeclaration()))
    eventuallyAssignInternalContext();

  ifDebugCurrentFile( DUChainReadLocker lock(DUChain::lock()); kDebug() << "closing declaration" << currentDeclaration()->toString() << "type" << (currentDeclaration()->abstractType() ? currentDeclaration()->abstractType()->toString() : QString("notype")) << "last:" << (lastType() ? lastType()->toString() : QString("(notype)")); )

  DeclarationBuilderBase::closeDeclaration();
}

void DeclarationBuilder::visitTypedef(TypedefAST *def)
{
  parseComments(def->comments);

  DeclarationBuilderBase::visitTypedef(def);
}

void DeclarationBuilder::visitEnumSpecifier(EnumSpecifierAST* node)
{
  Declaration * declaration = openDefinition(node->name, node, node->name == 0);
  
  ///Create mappings iff the AST feature is specified
  if(m_mapAst)
    editor()->parseSession()->mapAstDuChain(node, KDevelop::DeclarationPointer(declaration));

  DeclarationBuilderBase::visitEnumSpecifier(node);

  closeDeclaration();
}

///Replaces a CppTemplateParameterType with a DelayedType
struct TemplateTypeExchanger : public KDevelop::TypeExchanger {

  TemplateTypeExchanger(TopDUContext* top) : m_top(top) {
  }

  virtual AbstractType::Ptr exchange( const AbstractType::Ptr& type )
  {
    if(CppTemplateParameterType::Ptr templateParamType = type.cast<CppTemplateParameterType>()) {
      Declaration* decl = templateParamType->declaration(m_top);
      if(decl) {
        DelayedType::Ptr newType(new DelayedType());
        
        IndexedTypeIdentifier id(QualifiedIdentifier(decl->identifier()));
        
        if(type->modifiers() & AbstractType::ConstModifier)
            id.setIsConstant(true);
           
        newType->setIdentifier(id);
        newType->setKind(KDevelop::DelayedType::Delayed);
        
        return newType.cast<AbstractType>();
      }
    }
    return type;
  }
  private:
    TopDUContext* m_top;
};

Cpp::InstantiationInformation DeclarationBuilder::createSpecializationInformation(Cpp::InstantiationInformation base, UnqualifiedNameAST* name, KDevelop::DUContext* templateContext) {
    if(name->template_arguments || base.isValid()) 
    {
      //Append a scope part
      InstantiationInformation newCurrent;
      newCurrent.previousInstantiationInformation = base.indexed();
      if(!name->template_arguments)
        return newCurrent;
      //Process the template arguments if they exist
      const ListNode<TemplateArgumentAST*> * start = name->template_arguments->toFront();
      const ListNode<TemplateArgumentAST*> * current = start;
      do {
        NameASTVisitor visitor(editor()->parseSession(), 0, templateContext, currentContext()->topContext(), templateContext, templateContext->range().end/*, DUContext::NoUndefinedTemplateParams*/);
        ExpressionEvaluationResult res = visitor.processTemplateArgument(current->element);
        AbstractType::Ptr type = res.type.abstractType();
        
        TemplateTypeExchanger exchanger(currentContext()->topContext());
        
        if(type) {
          type = exchanger.exchange(type);
          type->exchangeTypes(&exchanger);
        }
        
        newCurrent.addTemplateParameter(type);

        current = current->next;
      }while(current != start);
      return newCurrent;
    }else{
      return base;
    }
}

Cpp::IndexedInstantiationInformation DeclarationBuilder::createSpecializationInformation(NameAST* name, DUContext* templateContext)
{
  InstantiationInformation currentInfo;
  if(name->qualified_names) {
    const ListNode<UnqualifiedNameAST*> * start = name->qualified_names->toFront();
    const ListNode<UnqualifiedNameAST*> * current = start;
    do {
      currentInfo = createSpecializationInformation(currentInfo, current->element, templateContext);
      current = current->next;
    }while(current != start);
  }
  if(name->unqualified_name)
    currentInfo = createSpecializationInformation(currentInfo, name->unqualified_name, templateContext);
  return currentInfo.indexed();
}

void DeclarationBuilder::visitEnumerator(EnumeratorAST* node)
{
  //Ugly hack: Since we want the identifier only to have the range of the id(not
  //the assigned expression), we change the range of the node temporarily
  uint oldEndToken = node->end_token;
  node->end_token = node->id + 1;

  Identifier id(editor()->parseSession()->token_stream->token(node->id).symbol());
  Declaration* decl = openNormalDeclaration(0, node, id);

  node->end_token = oldEndToken;

  DeclarationBuilderBase::visitEnumerator(node);

  EnumeratorType::Ptr enumeratorType = lastType().cast<EnumeratorType>();

  if(ClassMemberDeclaration* classMember = dynamic_cast<ClassMemberDeclaration*>(currentDeclaration())) {
    DUChainWriteLocker lock(DUChain::lock());
    classMember->setStatic(true);
  }

  closeDeclaration(true);

  if(enumeratorType) { ///@todo Move this into closeDeclaration in a logical way
    DUChainWriteLocker lock(DUChain::lock());
    enumeratorType->setDeclaration(decl);
    decl->setAbstractType(enumeratorType.cast<AbstractType>());
  }else if(!lastType().cast<DelayedType>()){ //If it's in a template, it may be DelayedType
    AbstractType::Ptr type = lastType();
    kWarning() << "not assigned enumerator type:" << typeid(*type.unsafeData()).name() << type->toString();
  }
}

void DeclarationBuilder::classContextOpened(ClassSpecifierAST* /*node*/, DUContext* context) {
  
  //We need to set this early, so we can do correct search while building
  DUChainWriteLocker lock(DUChain::lock());
  currentDeclaration()->setInternalContext(context);
}

void DeclarationBuilder::closeContext()
{
  if (!m_pendingPropertyDeclarations.isEmpty()) {
    if(m_pendingPropertyDeclarations.contains(currentContext()))
      resolvePendingPropertyDeclarations(m_pendingPropertyDeclarations.values(currentContext()));
  }

  DeclarationBuilderBase::closeContext();
}

void DeclarationBuilder::visitNamespace(NamespaceAST* ast) {

  {
    SimpleRange range;
    Identifier id;
    
    if(ast->namespace_name)
    {
      id = Identifier(editor()->tokensToStrings(ast->namespace_name, ast->namespace_name+1));
      range = editor()->findRange(ast->namespace_name, ast->namespace_name+1);
    }else
    {
      id = unnamedNamespaceIdentifier().identifier();
      range.start = editor()->findPosition(ast->linkage_body ? ast->linkage_body->start_token : ast->start_token, KDevelop::EditorIntegrator::FrontEdge);
      range.end = range.start;
    }

    DUChainWriteLocker lock(DUChain::lock());

    Declaration * declaration = openDeclarationReal<Declaration>(0, 0, id, false, false, &range);
    
    ///Create mappings iff the AST feature is specified
    if(m_mapAst)
      editor()->parseSession()->mapAstDuChain(ast, KDevelop::DeclarationPointer(declaration));
  }
  
  DeclarationBuilderBase::visitNamespace(ast);
  
  {
    DUChainWriteLocker lock(DUChain::lock());
    currentDeclaration()->setKind(KDevelop::Declaration::Namespace);
    clearLastType();
    closeDeclaration();
  }
}

void DeclarationBuilder::visitClassSpecifier(ClassSpecifierAST *node)
{
  PushValue<bool> setNotInTypedef(m_inTypedef, false);
  
  /**Open helper contexts around the class, so the qualified identifier matches.
   * Example: "class MyClass::RealClass{}"
   * Will create one helper-context named "MyClass" around RealClass
   * */

  SimpleCursor pos = editor()->findPosition(node->start_token, KDevelop::EditorIntegrator::FrontEdge);

  IndexedInstantiationInformation specializedWith;
  
  QualifiedIdentifier id;
  if( node->name ) {
    identifierForNode(node->name, id);
    openPrefixContext(node, id, pos);
    DUChainReadLocker lock(DUChain::lock());
    if(DUContext* templateContext = hasTemplateContext(m_importedParentContexts, topContext()).context(topContext())) {
      specializedWith = createSpecializationInformation(node->name, templateContext);
    }
  }

  int kind = editor()->parseSession()->token_stream->kind(node->class_key);
  
  ClassDeclaration * declaration = openClassDefinition(node->name, node, node->name == 0, classTypeFromTokenKind(kind));

  if (kind == Token_struct || kind == Token_union)
    m_accessPolicyStack.push(Declaration::Public);
  else
    m_accessPolicyStack.push(Declaration::Private);

  DeclarationBuilderBase::visitClassSpecifier(node);

  eventuallyAssignInternalContext();

  if( node->name ) {
    ///Copy template default-parameters from the forward-declaration to the real declaration if possible
    DUChainWriteLocker lock(DUChain::lock());

    QList<Declaration*> declarations = Cpp::findDeclarationsSameLevel(currentContext(), id.last(), pos);

    foreach( Declaration* decl, declarations ) {
      if( decl->abstractType()) {
        ForwardDeclaration* forward =  dynamic_cast<ForwardDeclaration*>(decl);
        if( forward ) {
          {
            KDevelop::DUContext* forwardTemplateContext = forward->internalContext();
            if( forwardTemplateContext && forwardTemplateContext->type() == DUContext::Template ) {

              KDevelop::DUContext* currentTemplateContext = getTemplateContext(currentDeclaration());
              if( (bool)forwardTemplateContext != (bool)currentTemplateContext ) {
                kDebug(9007) << "Template-contexts of forward- and real declaration do not match: " << currentTemplateContext << getTemplateContext(currentDeclaration()) << currentDeclaration()->internalContext() << forwardTemplateContext << currentDeclaration()->internalContext()->importedParentContexts().count();
              } else if( forwardTemplateContext && currentTemplateContext ) {
                if( forwardTemplateContext->localDeclarations().count() != currentTemplateContext->localDeclarations().count() ) {
                } else {

                  const QVector<Declaration*>& forwardList = forwardTemplateContext->localDeclarations();
                  const QVector<Declaration*>& realList = currentTemplateContext->localDeclarations();

                  QVector<Declaration*>::const_iterator forwardIt = forwardList.begin();
                  QVector<Declaration*>::const_iterator realIt = realList.begin();

                  for( ; forwardIt != forwardList.end(); ++forwardIt, ++realIt ) {
                    TemplateParameterDeclaration* forwardParamDecl = dynamic_cast<TemplateParameterDeclaration*>(*forwardIt);
                    TemplateParameterDeclaration* realParamDecl = dynamic_cast<TemplateParameterDeclaration*>(*realIt);
                    if( forwardParamDecl && realParamDecl ) {
                      if( !forwardParamDecl->defaultParameter().isEmpty() )
                        realParamDecl->setDefaultParameter(forwardParamDecl->defaultParameter());
                    }
                  }
                }
              }
            }
          }

          //Update instantiations in case of template forward-declarations
//           SpecialTemplateDeclaration<ForwardDeclaration>* templateForward = dynamic_cast<SpecialTemplateDeclaration<ForwardDeclaration>* > (decl);
//           SpecialTemplateDeclaration<Declaration>* currentTemplate = dynamic_cast<SpecialTemplateDeclaration<Declaration>* >  (currentDeclaration());
//
//           if( templateForward && currentTemplate )
//           {
//             //Change the types of all the forward-template instantiations
//             TemplateDeclaration::InstantiationsHash instantiations = templateForward->instantiations();
//
//             for( TemplateDeclaration::InstantiationsHash::iterator it = instantiations.begin(); it != instantiations.end(); ++it )
//             {
//               Declaration* realInstance = currentTemplate->instantiate(it.key().args, ImportTrace());
//               Declaration* forwardInstance = dynamic_cast<Declaration*>(*it);
//               //Now change the type of forwardInstance so it matches the type of realInstance
//               CppClassType::Ptr realClass = realInstance->type<CppClassType>();
//               CppClassType::Ptr forwardClass = forwardInstance->type<CppClassType>();
//
//               if( realClass && forwardClass ) {
//                 //Copy the class from real into the forward-declaration's instance
//                 copyCppClass(realClass.data(), forwardClass.data());
//               } else {
//                 kDebug(9007) << "Bad types involved in formward-declaration";
//               }
//             }
//           }//templateForward && currentTemplate
        }
      }
    }//foreach

  }//node-name

  TemplateDeclaration* tempDecl = dynamic_cast<TemplateDeclaration*>(currentDeclaration());
  
  if(tempDecl) {
    DUChainWriteLocker lock(DUChain::lock());
    tempDecl->setSpecializedWith(specializedWith);
  }
  closeDeclaration();
  
  ///Create mappings iff the AST feature is specified
  if(m_mapAst)
    editor()->parseSession()->mapAstDuChain(node, KDevelop::DeclarationPointer(declaration));
  
  if(node->name)
    closePrefixContext(id);

  m_accessPolicyStack.pop();
}

void DeclarationBuilder::visitBaseSpecifier(BaseSpecifierAST *node) {
  DeclarationBuilderBase::visitBaseSpecifier(node);

  BaseClassInstance instance;
  {
    DUChainWriteLocker lock(DUChain::lock());
    ClassDeclaration* currentClass = dynamic_cast<ClassDeclaration*>(currentDeclaration());
    if(currentClass) {

      instance.virtualInheritance = (bool)node->virt;

      //TypeUtils::unAliasedType(
      instance.baseClass = TypeUtils::unAliasedType(lastType())->indexed();
      if(currentClass->classType() == ClassDeclarationData::Struct)
        instance.access = KDevelop::Declaration::Public;
      else
        instance.access = KDevelop::Declaration::Private;

      if( node->access_specifier ) {
        int tk = editor()->parseSession()->token_stream->token(node->access_specifier).kind;

        switch( tk ) {
          case Token_private:
            instance.access = KDevelop::Declaration::Private;
            break;
          case Token_public:
            instance.access = KDevelop::Declaration::Public;
            break;
          case Token_protected:
            instance.access = KDevelop::Declaration::Protected;
            break;
        }
      }

      currentClass->addBaseClass(instance);
    }else{
      kWarning() << "base-specifier without class declaration";
    }
  }
  addBaseType(instance, node);
}

QualifiedIdentifier DeclarationBuilder::resolveNamespaceIdentifier(const QualifiedIdentifier& identifier, const SimpleCursor& position)
{
  QList< Declaration* > decls = currentContext()->findDeclarations(identifier, position);
  
  QList<DUContext*> contexts;
  
  foreach(Declaration* decl, decls)
    if(decl->kind() == Declaration::Namespace && decl->internalContext())
      contexts << decl->internalContext();
  
  if( contexts.isEmpty() ) {
    //Failed to resolve namespace
    kDebug(9007) << "Failed to resolve namespace \"" << identifier << "\"";
    QualifiedIdentifier ret = identifier;
    ret.setExplicitlyGlobal(false);
    Q_ASSERT(ret.count());
    return ret;
  } else {
    QualifiedIdentifier ret = contexts.first()->scopeIdentifier(true);
    ret.setExplicitlyGlobal(false);
    if(ret.isEmpty())
        return ret;
    Q_ASSERT(ret.count());
    return ret;
  }
}

void DeclarationBuilder::visitUsing(UsingAST * node)
{
  DeclarationBuilderBase::visitUsing(node);

  QualifiedIdentifier id;
  identifierForNode(node->name, id);

  ///@todo only use the last name component as range
  AliasDeclaration* decl = openDeclaration<AliasDeclaration>(0, node->name ? (AST*)node->name : (AST*)node, id.last());
  {
    DUChainWriteLocker lock(DUChain::lock());

    SimpleCursor pos = editor()->findPosition(node->start_token, KDevelop::EditorIntegrator::FrontEdge);
    QList<Declaration*> declarations = currentContext()->findDeclarations(id, pos);
    if(!declarations.isEmpty()) {
      decl->setAliasedDeclaration(declarations[0]);
    }else{
      kDebug(9007) << "Aliased declaration not found:" << id.toString();
    }

    if(m_accessPolicyStack.isEmpty())
      decl->setAccessPolicy(KDevelop::Declaration::Public);
    else
      decl->setAccessPolicy(currentAccessPolicy());
  }

  closeDeclaration();
}

void DeclarationBuilder::visitUsingDirective(UsingDirectiveAST * node)
{
  DeclarationBuilderBase::visitUsingDirective(node);

  if( compilingContexts() ) {
    SimpleRange range = editor()->findRange(node->start_token);
    DUChainWriteLocker lock(DUChain::lock());
    NamespaceAliasDeclaration* decl = openDeclarationReal<NamespaceAliasDeclaration>(0, 0, globalImportIdentifier(), false, false, &range);
    {
      DUChainWriteLocker lock(DUChain::lock());
      QualifiedIdentifier id;
      identifierForNode(node->name, id);
      decl->setImportIdentifier( resolveNamespaceIdentifier(id, currentDeclaration()->range().start) );
    }
    closeDeclaration();
  }
}

void DeclarationBuilder::visitTypeId(TypeIdAST * typeId)
{
  //TypeIdAST contains a declarator, but that one does not declare anything
  PushValue<bool> disableDeclarators(m_ignoreDeclarators, true);
  
  DeclarationBuilderBase::visitTypeId(typeId);
}

void DeclarationBuilder::visitNamespaceAliasDefinition(NamespaceAliasDefinitionAST* node)
{
  DeclarationBuilderBase::visitNamespaceAliasDefinition(node);

  {
    DUChainReadLocker lock(DUChain::lock());
    if( currentContext()->type() != DUContext::Namespace && currentContext()->type() != DUContext::Global ) {
      ///@todo report problem
      kDebug(9007) << "Namespace-alias used in non-global scope";
    }
  }

  if( compilingContexts() ) {
    SimpleRange range = editor()->findRange(node->namespace_name);
    DUChainWriteLocker lock(DUChain::lock());
    NamespaceAliasDeclaration* decl = openDeclarationReal<NamespaceAliasDeclaration>(0, 0, Identifier(editor()->parseSession()->token_stream->token(node->namespace_name).symbol()), false, false, &range);
    {
      QualifiedIdentifier id;
      identifierForNode(node->alias_name, id);
      decl->setImportIdentifier( resolveNamespaceIdentifier(id, currentDeclaration()->range().start) );
    }
    closeDeclaration();
  }
}

void DeclarationBuilder::visitElaboratedTypeSpecifier(ElaboratedTypeSpecifierAST* node)
{
  PushValue<bool> setNotInTypedef(m_inTypedef, false);
  
  int kind = editor()->parseSession()->token_stream->kind(node->type);

  if( kind == Token_typename ) {
    //typename is completely handled by the type-builder
    DeclarationBuilderBase::visitElaboratedTypeSpecifier(node);
    return;
  }
  
  bool isFriendDeclaration = !m_storageSpecifiers.isEmpty() && (m_storageSpecifiers.top() & ClassMemberDeclaration::FriendSpecifier);

  bool openedDeclaration = false;

  if (node->name) {
    QualifiedIdentifier id;
    identifierForNode(node->name, id);

    bool forwardDeclarationGlobal = false;

    if(m_typeSpecifierWithoutInitDeclarators != node->start_token || isFriendDeclaration) {
      /**This is an elaborated type-specifier
       *
       * See iso c++ draft 3.3.4 for details.
       * Said shortly it means:
       * - Search for an existing declaration of the type. If it is found,
       *   it will be used, and we don't need to create a declaration.
       * - If it is not found, create a forward-declaration in the global/namespace scope.
       * - @todo While searching for the existing declarations, non-fitting overloaded names should be ignored.
       * */

      ///@todo think how this interacts with re-using duchains. In some cases a forward-declaration should still be created.
      QList<Declaration*> declarations;
      SimpleCursor pos = editor()->findPosition(node->start_token, KDevelop::EditorIntegrator::FrontEdge);

      {
        DUChainReadLocker lock(DUChain::lock());

        declarations = currentContext()->findDeclarations( id, pos);

        forwardDeclarationGlobal = true;
        
        //If a good declaration has been found, use its type. Else, create a new forward-declaration.
        foreach(Declaration* decl, declarations)
        {
          if((decl->topContext() != currentContext()->topContext() || wasEncountered(decl)) && decl->abstractType())
          {
            injectType(declarations.first()->abstractType());
            
            if( isFriendDeclaration ) {
              lock.unlock();
              createFriendDeclaration(node);
            }
            return;
          }
        }
      }
    }

    node->isDeclaration = true;

    // Create forward declaration
    switch (kind) {
      case Token_class:
      case Token_struct:
      case Token_union:
      case Token_enum:

        if(forwardDeclarationGlobal) {
          //Open the global context, so it is currentContext() and we can insert the forward-declaration there
          DUContext* globalCtx;
          {
            DUChainReadLocker lock(DUChain::lock());
            globalCtx = currentContext();
            while(globalCtx && globalCtx->type() != DUContext::Global && globalCtx->type() != DUContext::Namespace)
              globalCtx = globalCtx->parentContext();
            Q_ASSERT(globalCtx);
          }

          //Just temporarily insert the new context
          LockedSmartInterface iface = editor()->smart();
          injectContext( iface, globalCtx, currentContext()->smartRange() );
        }

        openForwardDeclaration(node->name, node);

        if(forwardDeclarationGlobal) {
          LockedSmartInterface iface = editor()->smart();
          closeInjectedContext(iface);
        }

        openedDeclaration = true;
        break;
    }
  }

  DeclarationBuilderBase::visitElaboratedTypeSpecifier(node);

  if (openedDeclaration) {
/*    DUChainWriteLocker lock(DUChain::lock());
    //Resolve forward-declarations that are declared after the real type was already declared
    Q_ASSERT(dynamic_cast<ForwardDeclaration*>(currentDeclaration()));
    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(lastType().data());
    if(idType && idType->declaration())
      static_cast<ForwardDeclaration*>(currentDeclaration())->setResolved(idType->declaration());*/
    closeDeclaration();
  }
  
  if( isFriendDeclaration )
    createFriendDeclaration(node);
}

void DeclarationBuilder::createFriendDeclaration(AST* range) {
  static IndexedIdentifier friendIdentifier(Identifier("friend"));
  openDeclaration<Declaration>(0, range, friendIdentifier.identifier(), true);
  closeDeclaration();
}

void DeclarationBuilder::visitAccessSpecifier(AccessSpecifierAST* node)
{
  bool isSlot = false;
  bool isSignal = false;
  if (node->specs) {
    const ListNode<uint> *it = node->specs->toFront();
    const ListNode<uint> *end = it;
    do {
      int kind = editor()->parseSession()->token_stream->kind(it->element);
      switch (kind) {
        case Token___qt_slots__:
        case Token_k_dcop:
          isSlot = true;
          break;
        case Token_public:
          setAccessPolicy(Declaration::Public);
          break;
        case Token_k_dcop_signals:
        case Token___qt_signals__:
          isSignal = true;
        case Token_protected:
          setAccessPolicy(Declaration::Protected);
          break;
        case Token_private:
          setAccessPolicy(Declaration::Private);
          break;
      }

      it = it->next;
    } while (it != end);
  }
  
  if(isSignal)
    setAccessPolicy((KDevelop::Declaration::AccessPolicy)(currentAccessPolicy() | FunctionIsSignal));

  if(isSlot)
    setAccessPolicy((KDevelop::Declaration::AccessPolicy)(currentAccessPolicy() | FunctionIsSlot));
  

  DeclarationBuilderBase::visitAccessSpecifier(node);
}

void DeclarationBuilder::parseStorageSpecifiers(const ListNode<uint>* storage_specifiers)
{
  ClassMemberDeclaration::StorageSpecifiers specs = 0;

  if (storage_specifiers) {
    const ListNode<uint> *it = storage_specifiers->toFront();
    const ListNode<uint> *end = it;
    do {
      int kind = editor()->parseSession()->token_stream->kind(it->element);
      switch (kind) {
        case Token_friend:
          specs |= ClassMemberDeclaration::FriendSpecifier;
          break;
        case Token_auto:
          specs |= ClassMemberDeclaration::AutoSpecifier;
          break;
        case Token_register:
          specs |= ClassMemberDeclaration::RegisterSpecifier;
          break;
        case Token_static:
          specs |= ClassMemberDeclaration::StaticSpecifier;
          break;
        case Token_extern:
          specs |= ClassMemberDeclaration::ExternSpecifier;
          break;
        case Token_mutable:
          specs |= ClassMemberDeclaration::MutableSpecifier;
          break;
      }

      it = it->next;
    } while (it != end);
  }

  m_storageSpecifiers.push(specs);
}

void DeclarationBuilder::parseFunctionSpecifiers(const ListNode<uint>* function_specifiers)
{
  AbstractFunctionDeclaration::FunctionSpecifiers specs = 0;

  if (function_specifiers) {
    const ListNode<uint> *it = function_specifiers->toFront();
    const ListNode<uint> *end = it;
    do {
      int kind = editor()->parseSession()->token_stream->kind(it->element);
      switch (kind) {
        case Token_inline:
          specs |= AbstractFunctionDeclaration::InlineSpecifier;
          break;
        case Token_virtual:
          specs |= AbstractFunctionDeclaration::VirtualSpecifier;
          break;
        case Token_explicit:
          specs |= AbstractFunctionDeclaration::ExplicitSpecifier;
          break;
      }

      it = it->next;
    } while (it != end);
  }

  m_functionSpecifiers.push(specs);
}

void DeclarationBuilder::visitParameterDeclaration(ParameterDeclarationAST* node)
{
  if(m_mapAst)
    m_mappedNodes.push(node);
  
  DeclarationBuilderBase::visitParameterDeclaration(node);
  
  AbstractFunctionDeclaration* function = currentDeclaration<AbstractFunctionDeclaration>();

  if( function ) {
    
    if( node->expression ) {
      DUChainWriteLocker lock(DUChain::lock());
      //Fill default-parameters
      QString defaultParam = stringFromSessionTokens( editor()->parseSession(), node->expression->start_token, node->expression->end_token ).trimmed();

      function->addDefaultParameter(IndexedString(defaultParam));
    }
    if( !node->declarator ) {
      //If there is no declarator, still create a declaration
      openDefinition(0, node, true);
      closeDeclaration();
    }
  }
  
  if(m_mapAst)
    m_mappedNodes.pop();
}


void DeclarationBuilder::popSpecifiers()
{
  m_functionSpecifiers.pop();
  m_storageSpecifiers.pop();
}

void DeclarationBuilder::applyStorageSpecifiers()
{
  if (!m_storageSpecifiers.isEmpty() && m_storageSpecifiers.top() != 0)
    if (ClassMemberDeclaration* member = dynamic_cast<ClassMemberDeclaration*>(currentDeclaration())) {
      DUChainWriteLocker lock(DUChain::lock());

      member->setStorageSpecifiers(m_storageSpecifiers.top());
    }
}

void DeclarationBuilder::applyFunctionSpecifiers()
{
  DUChainWriteLocker lock(DUChain::lock());
  AbstractFunctionDeclaration* function = dynamic_cast<AbstractFunctionDeclaration*>(currentDeclaration());
  if(!function)
    return;
  
  if (!m_functionSpecifiers.isEmpty() && m_functionSpecifiers.top() != 0) {

    function->setFunctionSpecifiers(m_functionSpecifiers.top());
  }else{
    function->setFunctionSpecifiers((AbstractFunctionDeclaration::FunctionSpecifiers)0);
  }
  
  ///Eventually inherit the "virtual" flag from overridden functions
  ClassFunctionDeclaration* classFunDecl = dynamic_cast<ClassFunctionDeclaration*>(function);
  if(classFunDecl && !classFunDecl->isVirtual()) {
    QList<Declaration*> overridden;
    foreach(const DUContext::Import &import, currentContext()->importedParentContexts()) {
      DUContext* iContext = import.context(topContext());
      if(iContext) {
        overridden += iContext->findDeclarations(QualifiedIdentifier(classFunDecl->identifier()),
                                            SimpleCursor::invalid(), classFunDecl->abstractType(), classFunDecl->topContext(), DUContext::DontSearchInParent);
      }
    }
    if(!overridden.isEmpty()) {
      foreach(Declaration* decl, overridden) {
        if(AbstractFunctionDeclaration* fun = dynamic_cast<AbstractFunctionDeclaration*>(decl))
          if(fun->isVirtual())
            classFunDecl->setVirtual(true);
      }
    }
  }
}

bool DeclarationBuilder::checkParameterDeclarationClause(ParameterDeclarationClauseAST* clause)
{
    {
      DUChainReadLocker lock(DUChain::lock());
      if(currentContext()->type() == DUContext::Other) //Cannot declare a function in a code-context
        return false; ///@todo create warning/error
    }
    if(!clause || !clause->parameter_declarations)
      return true;
    AbstractType::Ptr oldLastType = lastType();

    const ListNode<ParameterDeclarationAST*> *start = clause->parameter_declarations->toFront();

    const ListNode<ParameterDeclarationAST*> *it = start;

    bool ret = false;

    do {
      ParameterDeclarationAST* ast = it->element;
      if(ast) {
        if(m_collectQtFunctionSignature) {
          uint endToken = ast->end_token;
          
          if(ast->type_specifier)
            endToken = ast->type_specifier->end_token;
          if(ast->declarator) {
            if(ast->declarator->id)
              endToken = ast->declarator->id->start_token;
            else
              endToken = ast->declarator->end_token;
          }
          
          if(!m_qtFunctionSignature.isEmpty())
            m_qtFunctionSignature += ", ";
          
          m_qtFunctionSignature += editor()->tokensToByteArray(ast->start_token, endToken);
          ret = true;
        }else{
        if(ast->expression || ast->declarator) {
          ret = true; //If one parameter has a default argument or a parameter name, it is surely a parameter
          break;
        }

        visit(ast->type_specifier);
        if( lastType() ) {
          //Break on the first valid thing found
          if( lastTypeWasInstance() ) {
            ret = false;
            break;
          }else if(lastType().cast<DelayedType>() && lastType().cast<DelayedType>()->kind() == DelayedType::Unresolved) {
            //When the searched item was not found, expect it to be a non-type
            ret = false;
          }else{
            ret = true;
            break;
          }
        }
        }
      }
      it = it->next;
    } while (it != start);

    setLastType(oldLastType);

    return ret;
}
