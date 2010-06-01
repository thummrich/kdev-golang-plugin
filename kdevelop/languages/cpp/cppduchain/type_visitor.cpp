/* This file is part of KDevelop
    Copyright 2002-2005 Roberto Raggi <roberto@kdevelop.org>
    Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>

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

#include "type_visitor.h"
#include "name_visitor.h"
#include "lexer.h"
#include "symbol.h"
#include "tokens.h"
#include "parsesession.h"
#include "cppduchain.h"
#include "expressionvisitor.h"
#include "typebuilder.h"
#include <language/duchain/duchainlock.h>

using namespace Cpp;

#include <QtCore/QString>

#define LOCKDUCHAIN     DUChainReadLocker lock(DUChain::lock())


TypeASTVisitor::TypeASTVisitor(ParseSession* session, Cpp::ExpressionVisitor* visitor, const KDevelop::DUContext* context, const KDevelop::TopDUContext* source, const KDevelop::DUContext* localVisibilityContext, bool debug)
    : m_session(session), m_visitor(visitor), m_context(context), m_localContext(localVisibilityContext),
      m_source(source), m_flags(KDevelop::DUContext::NoSearchFlags), m_debug(debug)
{
  m_position = m_context->range().end;
  m_stopSearch = false;
}

void TypeASTVisitor::run(TypeIdAST *node)
{
  run(node->type_specifier);

  if(node->declarator && m_type) {
    if( m_type ) {
      LOCKDUCHAIN;
      
      if( node->declarator && node->declarator->ptr_ops ) {
        //Apply pointer operators
        const ListNode<PtrOperatorAST*> *it = node->declarator->ptr_ops->toFront(), *end = it;

        do
          {
            PtrOperatorAST* ptrOp = it->element;
            if (ptrOp && ptrOp->op) { ///@todo check ordering, eventually walk the chain in reversed order
              IndexedString op = m_session->token_stream->token(ptrOp->op).symbol();
              static IndexedString ref("&");
              static IndexedString ptr("*");
              if (!op.isEmpty()) {
                if (op == ref) {
                  ReferenceType::Ptr pointer(new ReferenceType());
                  pointer->setModifiers(TypeBuilder::parseConstVolatile(m_session, ptrOp->cv));
                  pointer->setBaseType(m_type);
                  m_type = pointer.cast<AbstractType>();
                } else if (op == ptr) {
                  PointerType::Ptr pointer(new PointerType());
                  pointer->setModifiers(TypeBuilder::parseConstVolatile(m_session, ptrOp->cv));
                  pointer->setBaseType(m_type);
                  m_type = pointer.cast<AbstractType>();
                }
              }
            }
            it = it->next;
          }
        while (it != end);
      }
    }
  }
}

void TypeASTVisitor::run(TypeSpecifierAST *node)
{
  m_typeId.clear();
  _M_cv.clear();

  visit(node);

  
  if (node && node->cv && m_type) {
      LOCKDUCHAIN;
      m_type->setModifiers((AbstractType::CommonModifiers)(m_type->modifiers() | TypeBuilder::parseConstVolatile(m_session, node->cv)));
  }
  
}

void TypeASTVisitor::visitClassSpecifier(ClassSpecifierAST *node)
{
  if(m_stopSearch)
    return;
  visit(node->name);
}

void TypeASTVisitor::visitEnumSpecifier(EnumSpecifierAST *node)
{
  if(m_stopSearch)
    return;
  visit(node->name);
}

void TypeASTVisitor::visitElaboratedTypeSpecifier(ElaboratedTypeSpecifierAST *node)
{
  if(m_stopSearch)
    return;
  visit(node->name);
}

TypePtr< KDevelop::AbstractType > TypeASTVisitor::type() const {
  if(m_stopSearch)
    return TypePtr< KDevelop::AbstractType >();
  return m_type;
}

QList<KDevelop::DeclarationPointer> TypeASTVisitor::declarations() const
{
  if(m_stopSearch)
    return QList<KDevelop::DeclarationPointer>();
  
  return m_declarations;
}

void TypeASTVisitor::visitSimpleTypeSpecifier(SimpleTypeSpecifierAST *node)
{
  if(m_stopSearch)
    return;
  
  Cpp::FindDeclaration find( m_context, m_source, m_flags, m_context->range().end );
  find.openQualifiedIdentifier(false);
  
  // Don't forget the modifiers!
  uint modifiers = AbstractType::NoModifiers;
  if (node->cv) {
    const ListNode<uint> *it = node->cv->toFront();
    const ListNode<uint> *end = it;
    do {
      int kind = m_session->token_stream->kind(it->element);
      switch (kind) {
        case Token_const:
          modifiers |= AbstractType::ConstModifier;
          break;
        case Token_volatile:
          modifiers |= AbstractType::VolatileModifier;
          break;
        default:
          //kDebug() << "Unknown modifier token" << kind;
          break;
      }
      it = it->next;
    } while (it != end);
  }
  
  if (node->integrals)
    {
      uint type = IntegralType::TypeNone;

      const ListNode<uint> *it2 = node->integrals->toFront();
      const ListNode<uint> *end = it2;
      do {
        int kind = m_session->token_stream->kind(it2->element);
        switch (kind) {
          case Token_char:
            type = IntegralType::TypeChar;
            break;
          case Token_wchar_t:
            type = IntegralType::TypeWchar_t;
            break;
          case Token_bool:
            type = IntegralType::TypeBoolean;
            break;
          case Token_short:
            modifiers |= AbstractType::ShortModifier;
            break;
          case Token_int:
            type = IntegralType::TypeInt;
            break;
          case Token_long:
            if (modifiers & AbstractType::LongModifier)
              modifiers |= AbstractType::LongLongModifier;
            else
              modifiers |= AbstractType::LongModifier;
            break;
          case Token_signed:
            modifiers |= AbstractType::SignedModifier;
            break;
          case Token_unsigned:
            modifiers |= AbstractType::UnsignedModifier;
            break;
          case Token_float:
            type = IntegralType::TypeFloat;
            break;
          case Token_double:
            type = IntegralType::TypeDouble;
            break;
          case Token_void:
            type = IntegralType::TypeVoid;
            break;
        }

        it2 = it2->next;
      } while (it2 != end);

      if(type == IntegralType::TypeNone)
        type = IntegralType::TypeInt; //Happens, example: "unsigned short"

      KDevelop::IntegralType::Ptr integral ( new KDevelop::IntegralType(type) );
      integral->setModifiers(modifiers);
      
      m_type = integral.cast<AbstractType>();
      
      m_typeId = QualifiedIdentifier(integral->toString());
    }
  else if (node->type_of)
    {
      if (node->expression)
      {
        ExpressionParser parser;
        node->expression->ducontext = const_cast<DUContext*>(m_context);
        ExpressionEvaluationResult result = parser.evaluateType(node->expression, m_session);
        m_type = result.type.abstractType();
        m_typeId = QualifiedIdentifier(result.toString());
      }
    }

  {
    LOCKDUCHAIN;
    find.closeQualifiedIdentifier();
    m_declarations = find.lastDeclarations();
    if(!m_declarations.isEmpty() && m_declarations[0])
      m_type = m_declarations[0]->abstractType();
  }
  
  visit(node->name);
}

void TypeASTVisitor::visitName(NameAST *node)
{
  if(m_stopSearch)
    return;
  
  NameASTVisitor name_cc(m_session, m_visitor, m_context, m_source, m_localContext, m_position, m_flags, m_debug);
  name_cc.run(node);
  if(name_cc.stoppedSearch()) {
    m_stopSearch = true;
    return;
  }
  
  LOCKDUCHAIN;
  
  m_typeId = name_cc.identifier();
  m_declarations = name_cc.declarations();
  if(!m_declarations.isEmpty() && m_declarations[0])
    m_type = m_declarations[0]->abstractType();
}

QStringList TypeASTVisitor::cvString() const
{
  if(m_stopSearch)
    return QStringList();
  
  QStringList lst;

  foreach (int q, cv())
    {
      if (q == Token_const)
        lst.append(QLatin1String("const"));
      else if (q == Token_volatile)
        lst.append(QLatin1String("volatile"));
    }

  return lst;
}

bool TypeASTVisitor::isConstant() const
{
  if(m_stopSearch)
    return false;
  
    return _M_cv.contains(Token_const);
}

bool TypeASTVisitor::isVolatile() const
{
  if(m_stopSearch)
    return false;
  
    return _M_cv.contains(Token_volatile);
}

QualifiedIdentifier TypeASTVisitor::identifier() const
{
  if(m_stopSearch)
    return QualifiedIdentifier();
  
  return m_typeId;
}

