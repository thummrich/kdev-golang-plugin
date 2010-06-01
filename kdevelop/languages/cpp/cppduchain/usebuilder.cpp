/* This file is part of KDevelop
    Copyright 2006 Hamish Rodda <rodda@kde.org>

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

#include "usebuilder.h"

#include <ktexteditor/smartrange.h>
#include <ktexteditor/smartinterface.h>

#include "cppeditorintegrator.h"
#include "name_compiler.h"
#include <language/duchain/declaration.h>
#include <language/duchain/use.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <rpp/pp-location.h>

#include "expressionvisitor.h"
#include <parsesession.h>

using namespace KTextEditor;
using namespace KDevelop;

UseBuilder::UseBuilder (ParseSession* session)
  : UseBuilderBase(), m_finishContext(true), m_localUsesBuilt(false)
{
  setEditor(new CppEditorIntegrator(session), true);
}

UseBuilder::UseBuilder (CppEditorIntegrator* editor)
  : UseBuilderBase(), m_finishContext(true)
{
  setEditor(editor, false);
}

UseBuilder::~UseBuilder()
{
}

void UseBuilder::buildUses(AST *node)
{
  TopDUContextPointer topContext;
  {
    DUChainReadLocker lock(DUChain::lock());
    topContext = TopDUContextPointer(node->ducontext->topContext());
  }
  //We will have some caching in TopDUContext until this objects lifetime is over
  TopDUContext::Cache cache(topContext);
  Cpp::TypeConversionCacheEnabler enableConversionCache;

  UseBuilderBase::buildUses(node);
}

void UseBuilder::visitExpressionOrDeclarationStatement(ExpressionOrDeclarationStatementAST * exp) {
  if( exp->expressionChosen )
    visitExpression(exp->expression);
  else
    visit(exp->declaration);
}

void UseBuilder::visitCondition(ConditionAST *node)
{
  visit(node->type_specifier);
  visit(node->declarator);
  visit(node->expression);
}

void UseBuilder::visitExpressionStatement(ExpressionStatementAST * exp) {
  visitExpression(exp);
}

void UseBuilder::visitBinaryExpression(BinaryExpressionAST * exp) {
  visitExpression(exp);
}
void UseBuilder::visitCastExpression(CastExpressionAST * exp) {
  visitExpression(exp);
}
void UseBuilder::visitConditionalExpression(ConditionalExpressionAST * exp) {
  visitExpression(exp);
}
void UseBuilder::visitCppCastExpression(CppCastExpressionAST * exp) {
  visitExpression(exp);
}
void UseBuilder::visitNewExpression(NewExpressionAST * exp) {
  visitExpression(exp);
}
void UseBuilder::visitPostfixExpression(PostfixExpressionAST * exp) {
  visitExpression(exp);
}
void UseBuilder::visitSizeofExpression(SizeofExpressionAST * exp) {
  visitExpression(exp);
}
void UseBuilder::visitSubscriptExpression(SubscriptExpressionAST * exp) {
  visitExpression(exp);
}
void UseBuilder::visitUnaryExpression(UnaryExpressionAST * exp) {
  visitExpression(exp);
}

void UseBuilder::visitPrimaryExpression (PrimaryExpressionAST* exp)
{
  visitExpression(exp);
/*  UseBuilderBase::visitPrimaryExpression(node);

  if (node->name)
    newUse(node->name);*/
}

class UseExpressionVisitor : public Cpp::ExpressionVisitor {
  public:
  UseExpressionVisitor(ParseSession* session, UseBuilder* useBuilder, bool dumpProblems = false) : Cpp::ExpressionVisitor(session), m_builder(useBuilder), m_lastEndToken(0), m_dumpProblems(dumpProblems) {
    reportRealProblems(true);
  }
  ~UseExpressionVisitor() {
    typedef KSharedPtr<KDevelop::Problem> P;
    foreach(const P& problem, realProblems())
      m_builder->addProblem(problem);
  }
  private:

    virtual void usingDeclaration(AST* node,
                                  size_t start_token, size_t end_token,
                                  const KDevelop::DeclarationPointer& decl ) {
      SimpleRange range = m_builder->editor()->findRange(start_token, end_token);
      m_builder->newUse(node, range, decl.data());
    }

    virtual void problem(AST* node, const QString& str) {
      if(m_dumpProblems)
        Cpp::ExpressionVisitor::problem(node, str);
/*      else
        kDebug(9007) << "problem";*/
    }

    UseBuilder* m_builder;
    std::size_t m_lastEndToken; //Assume everything is processed from left to right
    bool m_dumpProblems;
};

void UseBuilder::visitExpression(AST* node) {

  UseExpressionVisitor visitor( editor()->parseSession(), this );
  if( !node->ducontext )
    node->ducontext = currentContext();
  
  visitor.parse( node );
}

void UseBuilder::visitNamespaceAliasDefinition(NamespaceAliasDefinitionAST* node) {
  DefaultVisitor::visitNamespaceAliasDefinition(node);
  buildUsesForName(node->alias_name);
}

void UseBuilder::visitUsingDirective(UsingDirectiveAST* node) {
  DefaultVisitor::visitUsingDirective(node);
  buildUsesForName(node->name);
}

void UseBuilder::buildUsesForName(NameAST* name) {
  if(name) {
    UseExpressionVisitor visitor( editor()->parseSession(), this );
    if(name) {
      if( !name->ducontext )
        name->ducontext = currentContext();
      
      visitor.parse( name );
    }
  }
}

void UseBuilder::visitBaseSpecifier(BaseSpecifierAST* node) {
  buildUsesForName(node->name);
}

void UseBuilder::visitMemInitializer(MemInitializerAST * node)
{
  if( !node->ducontext )
    node->ducontext = currentContext();
  
  UseExpressionVisitor visitor( editor()->parseSession(), this );
    
  visitor.parse( node );
}

void UseBuilder::visitElaboratedTypeSpecifier(ElaboratedTypeSpecifierAST* node)
{
  UseBuilderBase::visitElaboratedTypeSpecifier(node);
  
  if(!node->isDeclaration) {
    UseExpressionVisitor visitor( editor()->parseSession(), this );
    if( !node->ducontext ) {
      if(lastContext() && lastContext()->type() == DUContext::Template && lastContext()->parentContext() == currentContext())
        node->ducontext = lastContext();//Use the template-context so we can build uses for the template-parameters of template functions
      else
        node->ducontext = currentContext();
    }
    
    visitor.parse( node );
  }
}

void UseBuilder::visitSimpleDeclaration(SimpleDeclarationAST* node)
{
  if(node->init_declarators)
  {
    //Overridden so we can build uses for constructors like "A a(3);"
    UseExpressionVisitor visitor( editor()->parseSession(), this );
    if( !node->ducontext ) {
      if(lastContext() && lastContext()->type() == DUContext::Template && lastContext()->parentContext() == currentContext())
        node->ducontext = lastContext();//Use the template-context so we can build uses for the template-parameters of template functions
      else
        node->ducontext = currentContext();
    }
    
    visitor.parse( node );
  }else{
    DefaultVisitor::visitSimpleDeclaration(node);    
  }
}

void UseBuilder::visitSimpleTypeSpecifier(SimpleTypeSpecifierAST* node)
{
  UseBuilderBase::visitSimpleTypeSpecifier(node);
  
  UseExpressionVisitor visitor( editor()->parseSession(), this );
  if( !node->ducontext ) {
    if(lastContext() && lastContext()->type() == DUContext::Template && lastContext()->parentContext() == currentContext())
      node->ducontext = lastContext();//Use the template-context so we can build uses for the template-parameters of template functions
    else
      node->ducontext = currentContext();
  }
  
  visitor.parse( node );
}

void UseBuilder::visitDeclarator(DeclaratorAST* node)
{
  if(node->id) {
    UseExpressionVisitor visitor( editor()->parseSession(), this );
    if( !node->id->ducontext )
      node->id->ducontext = currentContext();

    visitor.parseNamePrefix(node->id);
  }

  UseBuilderBase::visitDeclarator(node);
}

void UseBuilder::visitClassSpecifier(ClassSpecifierAST* node)
{
  if(node->name) {
    UseExpressionVisitor visitor( editor()->parseSession(), this );
    if( !node->name->ducontext )
      node->name->ducontext = currentContext();

    visitor.parseNamePrefix(node->name);
  }

  UseBuilderBase::visitClassSpecifier(node);
}

void UseBuilder::visitTypeId(TypeIdAST* node) {
  {
    UseExpressionVisitor visitor( editor()->parseSession(), this );
    if( !node->ducontext )
      node->ducontext = currentContext();

    visitor.parse( node );
  }
  
  UseBuilderBase::visitTypeId(node);
}

void UseBuilder::visitUsing(UsingAST *node)
{
  if(node->name) {
    UseExpressionVisitor visitor( editor()->parseSession(), this );
    if( !node->name->ducontext )
      node->name->ducontext = currentContext();

    visitor.parse( node->name );
  }
  UseBuilderBase::visitUsing(node);
}

void UseBuilder::addProblem(KSharedPtr< KDevelop::Problem > problem) {
  m_problems << problem;
}

QList< KSharedPtr< KDevelop::Problem > > UseBuilder::problems() const {
  return m_problems;
}
