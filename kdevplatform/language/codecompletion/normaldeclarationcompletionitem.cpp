/*
 * KDevelop Generic Code Completion Support
 *
 * Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>
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

#include "normaldeclarationcompletionitem.h"
#include "codecompletionmodel.h"
#include "../duchain/duchainlock.h"
#include "../duchain/duchain.h"
#include "../duchain/classfunctiondeclaration.h"
#include "../duchain/types/functiontype.h"
#include "../duchain/types/enumeratortype.h"
#include "../duchain/duchainutils.h"

#include <KTextEditor/Document>


namespace KDevelop {

const int normalBestMatchesCount = 5;

///@todo Implement a proper duchain based shortening-scheme, and use it throughout the completion
//If this is true, the return-values of argument-hints will be just written as "..." if they are too long
const bool shortenArgumentHintReturnValues = true;
const int maximumArgumentHintReturnValueLength = 30;
const int desiredTypeLength = 20;

NormalDeclarationCompletionItem::NormalDeclarationCompletionItem(KDevelop::DeclarationPointer decl, KSharedPtr<CodeCompletionContext> context, int inheritanceDepth)
  : m_completionContext(context), m_declaration(decl), m_inheritanceDepth(inheritanceDepth) {
}

KDevelop::DeclarationPointer NormalDeclarationCompletionItem::declaration() const {
  return m_declaration;
}

KSharedPtr< KDevelop::CodeCompletionContext > NormalDeclarationCompletionItem::completionContext() const {
  return m_completionContext;
}

int NormalDeclarationCompletionItem::inheritanceDepth() const
{
  return m_inheritanceDepth;
}

int NormalDeclarationCompletionItem::argumentHintDepth() const
{
  if( m_completionContext )
      return m_completionContext->depth();
    else
      return 0;
}

QString NormalDeclarationCompletionItem::declarationName() const
{
  QString ret = m_declaration->identifier().toString();
  if (ret.isEmpty())
    return "<unknown>";
  else
    return ret;
}

void NormalDeclarationCompletionItem::execute(KTextEditor::Document* document, const KTextEditor::Range& word) {

  if( m_completionContext && m_completionContext->depth() != 0 )
    return; //Do not replace any text when it is an argument-hint

  QString newText;

  {
    KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
    if(m_declaration) {
      newText = declarationName();
    } else {
      kDebug() << "Declaration disappeared";
      return;
    }
  }

  document->replaceText(word, newText);
  
  executed(document, word);
}

QWidget* NormalDeclarationCompletionItem::createExpandingWidget(const KDevelop::CodeCompletionModel* model) const
{
  Q_UNUSED(model);
  return 0;
}

bool NormalDeclarationCompletionItem::createsExpandingWidget() const
{
  return false;
}

QString NormalDeclarationCompletionItem::shortenedTypeString(KDevelop::DeclarationPointer decl, int desiredTypeLength) const
{
  Q_UNUSED(desiredTypeLength);
  return decl->abstractType()->toString();
}

void NormalDeclarationCompletionItem::executed(KTextEditor::Document* document, const KTextEditor::Range& word)
{
  Q_UNUSED(document);
  Q_UNUSED(word);
}

QVariant NormalDeclarationCompletionItem::data(const QModelIndex& index, int role, const KDevelop::CodeCompletionModel* model) const
{
  DUChainReadLocker lock(DUChain::lock(), 500);
  if(!lock.locked()) {
    kDebug(9007) << "Failed to lock the du-chain in time";
    return QVariant();
  }

  switch (role) {
    case Qt::DisplayRole:
      if (index.column() == CodeCompletionModel::Name) {
        return declarationName();
      } else if(index.column() == CodeCompletionModel::Postfix) {
          if (FunctionType::Ptr functionType = m_declaration->type<FunctionType>()) {
            // Retrieve const/volatile string
            return functionType->AbstractType::toString();
          }
      } else if(index.column() == CodeCompletionModel::Prefix) {
          if (m_declaration->abstractType()) {
            if(EnumeratorType::Ptr enumerator = m_declaration->type<EnumeratorType>()) {
              if(m_declaration->context()->owner() && m_declaration->context()->owner()->abstractType()) {
                if(!m_declaration->context()->owner()->identifier().isEmpty())
                  return shortenedTypeString(DeclarationPointer(m_declaration->context()->owner()), desiredTypeLength);
                else
                  return "enum";
              }
            }
            if (FunctionType::Ptr functionType = m_declaration->type<FunctionType>()) {
              ClassFunctionDeclaration* funDecl = dynamic_cast<ClassFunctionDeclaration*>(m_declaration.data());

              if (functionType->returnType()) {
                QString ret = shortenedTypeString(m_declaration, desiredTypeLength);
                if(shortenArgumentHintReturnValues && argumentHintDepth() && ret.length() > maximumArgumentHintReturnValueLength)
                  return QString("...");
                else
                  return ret;
              }else if(argumentHintDepth()) {
                return QString();//Don't show useless prefixes in the argument-hints
              }else if(funDecl && funDecl->isConstructor() )
                return "<constructor>";
              else if(funDecl && funDecl->isDestructor() )
                return "<destructor>";
              else
                return "<incomplete type>";

            } else {
              return shortenedTypeString(m_declaration, desiredTypeLength);
            }
          } else {
            return "<incomplete type>";
          }
        }
      break;
    case CodeCompletionModel::BestMatchesCount:
      return QVariant(normalBestMatchesCount);
    break;
    case CodeCompletionModel::IsExpandable:
      return QVariant(createsExpandingWidget());
    case CodeCompletionModel::ExpandingWidget: {
      QWidget* nav = createExpandingWidget(model);
      Q_ASSERT(nav);
      model->addNavigationWidget(this, nav);

      QVariant v;
      v.setValue<QWidget*>(nav);
      return v;
    }
    case CodeCompletionModel::ScopeIndex:
      return static_cast<int>(reinterpret_cast<long>(m_declaration->context()));

    case CodeCompletionModel::CompletionRole:
      return (int)completionProperties();
    case Qt::DecorationRole:
     {
      if( index.column() == CodeCompletionModel::Icon ) {
        CodeCompletionModel::CompletionProperties p = completionProperties();
        lock.unlock();
        return DUChainUtils::iconForProperties(p);
      }
      break;
    }

  }
  return QVariant();
}

}

