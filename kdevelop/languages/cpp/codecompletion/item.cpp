/*
 * KDevelop C++ Code Completion Support
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

#include "item.h"
#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <ktexteditor/range.h>
#include <ktexteditor/view.h>
#include <ktexteditor/document.h>
#include <cpptypes.h>
#include <QModelIndex>
#include "helpers.h"
#include "model.h"
#include <language/duchain/declaration.h>
#include <language/duchain/classfunctiondeclaration.h>
#include <language/duchain/namespacealiasdeclaration.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/classdeclaration.h>
#include "../cppduchain/qtfunctiondeclaration.h"
#include <language/duchain/use.h>
#include <typeutils.h>
#include <cppduchain.h>
#include <templatedeclaration.h>
#include <language/codecompletion/codecompletionhelper.h>
#include "context.h"
#include <ktexteditor/codecompletioninterface.h>

using namespace KDevelop;

namespace Cpp {



void NormalDeclarationCompletionItem::execute(KTextEditor::Document* document, const KTextEditor::Range& word) {
  //We have to use word directly, because it may be a smart-range that is updated during insertions and such

  if( completionContext() && completionContext()->depth() != 0 )
    return; //Do not replace any text when it is an argument-hint

  if(m_isQtSignalSlotCompletion) {
    bool addSignalSlot = true;
    {
      //Check whether we need to add SLOT( or SIGNAL(
      QString prefixText = document->text(KTextEditor::Range(word.start().line(), 0, word.start().line(), word.start().column()));
      kDebug() << "prefix" << prefixText;
      QRegExp signalSlotRegExp("(Q_)?(SIGNAL|SLOT)\\s*\\(");
      int signalSlotAt = signalSlotRegExp.lastIndexIn(prefixText);
      kDebug() << "signalSlotRegExp found at" << signalSlotAt;
      if(signalSlotAt != -1 && prefixText.mid(signalSlotAt + signalSlotRegExp.matchedLength()).trimmed().isEmpty())
        addSignalSlot = false;
    }

    KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
    QString functionSignature;
    Cpp::QtFunctionDeclaration* classFun = dynamic_cast<Cpp::QtFunctionDeclaration*>(m_declaration.data());
    if(classFun && classFun->type<FunctionType>() && (classFun->isSignal() || classFun->isSlot())) {
      ///@todo Replace previous signal/slot specifications
      functionSignature = classFun->identifier().toString();
      functionSignature += '(' + classFun->normalizedSignature().str() + ')';
      if(addSignalSlot) {
        if(classFun->isSignal())
          functionSignature = "SIGNAL(" + functionSignature + ')';
        else
          functionSignature = "SLOT(" + functionSignature + ')';
      }else{
        //Only add a closing )
        functionSignature += ')';
      }
    }
    lock.unlock();

    int extendRange = 0;
    {
      //Check whether we need to remove existing stuff
      //note: it might be that the identifier (e.g. Q_SIGNAL) is already included in
      //the word, so be pretty forgiving in the regexp
      const QString line = document->text(KTextEditor::Range(word.end().line(), word.end().column(),
                                                       word.end().line(), document->lineLength(word.end().line())));
      QRegExp existingRegExp("^\\s*((Q_)?(SIGNAL|SLOT)\\s*)?\\([^\\)]*\\s*\\)\\s*\\)");
      int from = line.indexOf(existingRegExp);
      if (from != -1) {
          extendRange = existingRegExp.matchedLength();
      }
    }

    document->replaceText(KTextEditor::Range(word.start().line(), word.start().column(),
                                             word.end().line(), word.end().column() + extendRange),
                          functionSignature);
    return;
  }
  
  QString newText;

  if(!useAlternativeText) {
    KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
    if(m_declaration) {
      newText = m_declaration->identifier().toString();
    } else {
      kDebug() << "Declaration disappeared";
      return;
    }
  }else{
    newText = alternativeText;
  }

  document->replaceText(word, newText);
  
  bool jumpForbidden = false;
  
  KDevelop::DUChainReadLocker lock(KDevelop::DUChain::lock());
  Cpp::TemplateDeclaration* templateDecl = dynamic_cast<Cpp::TemplateDeclaration*>(m_declaration.data());
  if(templateDecl) {
    DUContext* context = templateDecl->templateContext(m_declaration->topContext());
    if(context && context->localDeclarations().count() && context->localDeclarations()[0]->type<CppTemplateParameterType>()) {
      jumpForbidden = true;
      lock.unlock();
      document->insertText( word.end(), "<>" );
      document->activeView()->setCursorPosition( word.end() - KTextEditor::Cursor(0, 1) );
      lock.lock();
    }
  }
  
  if(m_declaration.data()->kind() == Declaration::Namespace) {
    lock.unlock();
    document->insertText(word.end(), "::");
    lock.lock();
  }
    
  if( !useAlternativeText && m_declaration && (dynamic_cast<AbstractFunctionDeclaration*>(m_declaration.data()) || completionContext()->isConstructorInitialization()) ) {
    //Do some intelligent stuff for functions with the parens:
    lock.unlock();
    insertFunctionParenText(document, word, m_declaration, jumpForbidden);
  }
}

const bool indentByDepth = false;

//The name to be viewed in the name column
inline QString nameForDeclaration(Declaration* dec) {
  QString ret = dec->identifier().toString();

  if (ret.isEmpty())
    return "<unknown>";
  else
    return ret;
}

KTextEditor::CodeCompletionModel::CompletionProperties NormalDeclarationCompletionItem::completionProperties() const {
  Declaration* dec = m_declaration.data();
  if(!dec)
    return (KTextEditor::CodeCompletionModel::CompletionProperties)0;

  CodeCompletionModel::CompletionProperties p = DUChainUtils::completionProperties(dec);

  AbstractType::Ptr type(dec->abstractType());
  if (type) {
    if (type->modifiers() & AbstractType::ConstModifier)
      p |= CodeCompletionModel::Const;
    if (type->modifiers() & AbstractType::VolatileModifier) {
      ;//TODO
    }

    switch (dec->abstractType()->whichType()) {
      case AbstractType::TypeIntegral:
        if (dec->type<EnumerationType>()) {
          // Remove variable bit set in DUChainUtils
          p &= ~CodeCompletionModel::Variable;
          p |= CodeCompletionModel::Enum;
        }
        if (dec->type<EnumeratorType>()) {
          //Get the properties from the parent, because that may contain information like "private"
          if(dec->context()->owner()) {
            p = DUChainUtils::completionProperties(dec->context()->owner());
          }
          // Remove variable bit set in DUChainUtils
          p &= 0xffffffff - CodeCompletionModel::Variable;
          p |= CodeCompletionModel::Enum;
        }
        break;
      case AbstractType::TypeStructure:
        if (CppClassType::Ptr classType =  dec->type<CppClassType>())
          p |= CodeCompletionModel::Class;
        break;
      default:
        break;
    }
  }

  if(useAlternativeText) {
    //Remove other scope flags, and add the local scope flag
    ///@todo Create an own "Hint" scope within KTextEditor::CodeCompletionModel, and use that
    p &= ~CodeCompletionModel::GlobalScope;
    p &= ~CodeCompletionModel::NamespaceScope;
    p |= CodeCompletionModel::LocalScope;
  }
  return p;
}

bool declarationNeedsTemplateParameters(const Declaration* decl) {
  const Cpp::TemplateDeclaration* asTemplate = dynamic_cast<const Cpp::TemplateDeclaration*>(decl);
  if(asTemplate) {
    DUContext* templateContext = asTemplate->templateContext(decl->topContext());
    if(templateContext) {
      foreach(Declaration* decl, templateContext->localDeclarations()) {
        if(decl->type<CppTemplateParameterType>())
          return true;
      }
    }
  }
  return false;
}


bool NormalDeclarationCompletionItem::completingTemplateParameters() const
{
  return m_isTemplateCompletion || declarationNeedsTemplateParameters(m_declaration.data());
}

QString NormalDeclarationCompletionItem::shortenedTypeString(KDevelop::DeclarationPointer decl, int desiredTypeLength) const
{
  if(m_cachedTypeStringDecl == decl && m_cachedTypeStringLength == static_cast<uint>(desiredTypeLength))
    return m_cachedTypeString;
  
  QString ret;
  
  if(completionContext() && completionContext()->duContext())
    ret = Cpp::shortenedTypeString(decl.data(), completionContext()->duContext(), desiredTypeLength);
  else
    ret = KDevelop::NormalDeclarationCompletionItem::shortenedTypeString(decl, desiredTypeLength);
  
  m_cachedTypeString = ret;
  m_cachedTypeStringDecl = decl;
  m_cachedTypeStringLength = desiredTypeLength;
  
  return ret;
}

KDevelop::QualifiedIdentifier NormalDeclarationCompletionItem::stripPrefix() const {
  if(completionContext() && completionContext()->duContext()) {
    const TopDUContext* top = completionContext()->duContext()->topContext();
    
    if(completionContext()->memberAccessContainer().allDeclarations.size())
      if( Declaration * const decl = completionContext()->memberAccessContainer().allDeclarations[0].getDeclaration(top) ) {
        AbstractType::Ptr t = decl->abstractType();
        IdentifiedType* idType = dynamic_cast<IdentifiedType*>(t.unsafeData());
        if(idType)
          return idType->qualifiedIdentifier();
      }
    
    return completionContext()->duContext()->scopeIdentifier(true);
  }
  
  return QualifiedIdentifier();
}

QList<KDevelop::IndexedType> NormalDeclarationCompletionItem::typeForArgumentMatching() const {
  QList<KDevelop::IndexedType> ret;
  if( m_declaration && completionContext() && completionContext()->memberAccessOperation() == Cpp::CodeCompletionContext::FunctionCallAccess )
  {
    if(listOffset < completionContext()->functions().count()) {
      Cpp::CodeCompletionContext::Function f( completionContext()->functions()[listOffset] );

      if( f.function.isValid() && f.function.isViable() && f.function.declaration() && f.function.declaration()->type<FunctionType>() && f.function.declaration()->type<FunctionType>()->indexedArgumentsSize() > (uint) f.matchedArguments ) {
        ret << f.function.declaration()->type<FunctionType>()->indexedArguments()[f.matchedArguments];
      }
    }
    
    if(ret.isEmpty() && m_declaration->kind() == Declaration::Instance && !m_declaration->isFunctionDeclaration()) {
      ret << m_declaration->indexedType();
    }
  }
  return ret;
}

QList<IndexedType> currentMatchContext;

void setStaticMatchContext(QList< KDevelop::IndexedType > types) {
  currentMatchContext = types;
}

QVariant NormalDeclarationCompletionItem::data(const QModelIndex& index, int role, const KDevelop::CodeCompletionModel* model) const {

  DUChainReadLocker lock(DUChain::lock(), 500);
  if(!lock.locked()) {
    kDebug(9007) << "Failed to lock the du-chain in time";
    return QVariant();
  }
  
  if(!completionContext()) {
    kDebug(9007) << "Missing completion-context";
    return QVariant();
  }

  //Stuff that does not require a declaration:
  switch (role) {
    case CodeCompletionModel::SetMatchContext:
      currentMatchContext = typeForArgumentMatching();
      return QVariant(1);
  };

  if(!m_declaration) {
    if(role == Qt::DisplayRole && index.column() == CodeCompletionModel::Name)
      return alternativeText;
    return QVariant();
  }else if(useAlternativeText) {
    if(role == Qt::DisplayRole) {
      if(index.column() == CodeCompletionModel::Name)
        return alternativeText;
      else
        return QVariant();
    }
  }

  Declaration* dec = const_cast<Declaration*>( m_declaration.data() );

  switch (role) {
    case CodeCompletionModel::MatchQuality:
    {
      if(m_fixedMatchQuality != -1)
        return QVariant(m_fixedMatchQuality);
      
      if( currentMatchContext.size()) {
        
        int bestQuality = 0;
        foreach(const IndexedType& type, currentMatchContext) {
          Cpp::TypeConversion conv(model->currentTopContext().data());
  
          AbstractType::Ptr ownType = effectiveType(dec);
          
          bool fromLValue = (bool)ownType.cast<ReferenceType>() || (!dynamic_cast<AbstractFunctionDeclaration*>(dec) && dec->kind() == Declaration::Instance);
          
          ///@todo fill the lvalue-ness correctly
          int q = ( conv.implicitConversion( completionContext()->applyPointerConversionForMatching(ownType->indexed(), fromLValue), type, fromLValue )  * 10 ) / Cpp::MaximumConversionResult;
          if(q > bestQuality)
            bestQuality = q;
        }
        
        return QVariant(bestQuality);
      }
    }
    return QVariant();
    case CodeCompletionModel::ItemSelected:
       return QVariant(Cpp::NavigationWidget::shortDescription(dec));
    case Qt::DisplayRole:
      switch (index.column()) {
        case CodeCompletionModel::Prefix:
        {
          if(m_isQtSignalSlotCompletion) {
            Cpp::QtFunctionDeclaration* funDecl = dynamic_cast<Cpp::QtFunctionDeclaration*>(dec);
            if(funDecl) {
              if(funDecl->isSignal())
                return QVariant("SIGNAL");
              if(funDecl->isSlot())
                return QVariant("SLOT");
            }
          }
          int depth = m_inheritanceDepth;
          if( depth >= 1000 )
            depth-=1000;
          QString indentation;
          if(indentByDepth)
            indentation = QString(depth, ' ');

          if(m_declaration->kind() == Declaration::Namespace)
            return indentation + "namespace";
          
          if( NamespaceAliasDeclaration* alias = dynamic_cast<NamespaceAliasDeclaration*>(dec) ) {
            if( alias->identifier().isEmpty() ) {
              return indentation + "using namespace";/* " + alias->importIdentifier().toString();*/
            } else {
              return indentation + "namespace";/* " + alias->identifier().toString() + " = " + alias->importIdentifier().toString();*/
            }
          }

          if( dec->isTypeAlias() )
            indentation += "typedef ";

          if( dec->kind() == Declaration::Type && !dec->type<FunctionType>() && !dec->isTypeAlias() ) {
              if (CppClassType::Ptr classType =  dec->type<CppClassType>()){
                ClassDeclaration* classDecl  = dynamic_cast<ClassDeclaration*>(dec);
                if(classDecl) {
                  switch (classDecl->classType()) {
                    case ClassDeclarationData::Class:
                      return indentation + "class";
                      break;
                    case ClassDeclarationData::Struct:
                      return indentation + "struct";
                      break;
                    case ClassDeclarationData::Union:
                      return indentation + "union";
                      break;
                    default:
                      ;
                  }
                }else if(dec->isForwardDeclaration()) {
                  return indentation + "class"; ///@todo Would be useful to have the class/struct/union info also for forward-declarations
                }
              }
              if(dec->type<EnumerationType>()) {
                return "enum";
              }
            return QVariant();
          }
          break;
        }
        case CodeCompletionModel::Scope: {
          //The scopes are not needed
          return QVariant();
/*          QualifiedIdentifier id = dec->qualifiedIdentifier();
          if (id.isEmpty())
            return QVariant();
          id.pop();
          if (id.isEmpty())
            return QVariant();
          return id.toString() + "::";*/
        }

        case CodeCompletionModel::Arguments:
        {
          QString ret;
          
          if(completingTemplateParameters())
            createTemplateArgumentList(*this, ret, 0);
          
          if (dec->type<FunctionType>()) {
            needCachedArgumentList();
            
            return m_cachedArgumentList->text;
          }
          
          return ret;
        }
        break;
      }
      break;
    case CodeCompletionModel::HighlightingMethod:
    if( index.column() == CodeCompletionModel::Arguments ) {
//       if( completionContext()->memberAccessOperation() == Cpp::CodeCompletionContext::FunctionCallAccess ) {
        return QVariant(CodeCompletionModel::CustomHighlighting);
/*      }else{
        return QVariant();
      }
      break;*/
    } /*else if(index.column() == CodeCompletionModel::Name) {
      return QVariant(CodeCompletionModel::CustomHighlighting);
    }*/

    break;

    case CodeCompletionModel::CustomHighlight:
    if( index.column() == CodeCompletionModel::Arguments /*&& completionContext()->memberAccessOperation() == Cpp::CodeCompletionContext::FunctionCallAccess*/ ) {
      needCachedArgumentList();
      
      return QVariant(m_cachedArgumentList->highlighting);
    }
//     if( index.column() == CodeCompletionModel::Name ) {
//       //Bold
//       QTextCharFormat boldFormat;
//       boldFormat.setFontWeight((QFont::Normal + QFont::DemiBold)/2);
// 
//       QList<QVariant> ret;
//       ret << 0;
//       ret << nameForDeclaration(dec).length();
//       ret << QVariant(boldFormat);
// 
//       return QVariant(ret);
//     }
    break;
    case Qt::DecorationRole:
     {
      CodeCompletionModel::CompletionProperties p = completionProperties();

      //If it's a signal, remove t he protected flag when computing the decoration. Signals are always protected, and this will give a nicer icon.
      if(p & CodeCompletionModel::Signal)
        p = CodeCompletionModel::Signal;
      //If it's a slot, remove all flags except the slot flag, because that will give a nicer icon. Access-rights are checked anyway.
      if(p & CodeCompletionModel::Slot)
        p = CodeCompletionModel::Slot;
      
      
      if( index.column() == CodeCompletionModel::Icon ) {
        lock.unlock();
        return DUChainUtils::iconForProperties(p);
      }
      break;
    }
  }
  lock.unlock();

  return KDevelop::NormalDeclarationCompletionItem::data(index, role, model);
}

void NormalDeclarationCompletionItem::needCachedArgumentList() const
{
  if(!m_cachedArgumentList)
  {
    m_cachedArgumentList = KSharedPtr<CachedArgumentList>(new CachedArgumentList);
    
    if(!m_declaration)
      return;
    
    if(m_isTemplateCompletion || declarationNeedsTemplateParameters(m_declaration.data()))
      createTemplateArgumentList(*this, m_cachedArgumentList->text, &m_cachedArgumentList->highlighting);


    if (m_declaration->type<FunctionType>())
      createArgumentList(*this, m_cachedArgumentList->text, &m_cachedArgumentList->highlighting);
  }
}

QWidget* NormalDeclarationCompletionItem::createExpandingWidget(const KDevelop::CodeCompletionModel* model) const
{
  return new Cpp::NavigationWidget(m_declaration, model->currentTopContext());
}

bool NormalDeclarationCompletionItem::createsExpandingWidget() const
{
  return true;
}

KSharedPtr<CodeCompletionContext> NormalDeclarationCompletionItem::completionContext() const {
  return KSharedPtr<CodeCompletionContext>::staticCast(m_completionContext);
}

void IncludeFileCompletionItem::execute(KTextEditor::Document* document, const KTextEditor::Range& _word) {

  KTextEditor::Range word(_word);

  QString newText = includeItem.isDirectory ? includeItem.name + '/' : includeItem.name;

  if(!includeItem.isDirectory) {
    //Add suffix and newline
    QString lineText = document->line( word.end().line() ).trimmed();
    if(lineText.startsWith("#include")) {
      lineText = lineText.mid(8).trimmed();
      if(lineText.startsWith('"'))
        newText += '\"';
      else if(lineText.startsWith('<'))
       newText += '>';
    }

    word.end().setColumn( document->lineLength(word.end().line()) );
  }

  document->replaceText(word, newText);
}

void TypeConversionCompletionItem::setPrefix(QString s) {
  m_prefix = s;
}

QList<KDevelop::IndexedType> TypeConversionCompletionItem::typeForArgumentMatching() const {
  return QList<KDevelop::IndexedType>() << m_type;
}

QList<KDevelop::IndexedType> TypeConversionCompletionItem::type() const {
  return QList<KDevelop::IndexedType>() << m_type;
}

void TypeConversionCompletionItem::execute(KTextEditor::Document* document, const KTextEditor::Range& word) {
  if(argumentHintDepth() == 0)
    document->replaceText( word, m_text );
}

QVariant TypeConversionCompletionItem::data(const QModelIndex& index, int role, const KDevelop::CodeCompletionModel* model) const {

  switch (role) {
    case CodeCompletionModel::SetMatchContext:
      currentMatchContext = typeForArgumentMatching();
      return QVariant(1);
    case Qt::DisplayRole:
      switch (index.column()) {
        case CodeCompletionModel::Prefix:
          return QVariant(m_prefix);
        case CodeCompletionModel::Name: {
          return m_text;
        }
      }
      break;
    case CodeCompletionModel::ItemSelected:
      return QVariant();
    case CodeCompletionModel::MatchQuality:
    {
      DUChainReadLocker lock(DUChain::lock(), 500);
      if(!lock.locked()) {
        kDebug(9007) << "Failed to lock the du-chain in time";
        return QVariant();
      }
      
      if( currentMatchContext.size() ) {
        int bestQuality = 0;
        
        foreach(const IndexedType& type, currentMatchContext) {
          Cpp::TypeConversion conv(model->currentTopContext().data());

          ///@todo Think about lvalue-ness
          foreach(const IndexedType& ownType, typeForArgumentMatching()) {
            int quality = ( conv.implicitConversion( completionContext->applyPointerConversionForMatching(ownType, false), type, false )  * 10 ) / Cpp::MaximumConversionResult;
            if(quality > bestQuality)
              bestQuality = quality;
          }
        }
        
        return QVariant(bestQuality);
      }
    }
    return QVariant();
  }

  return QVariant();
}

int TypeConversionCompletionItem::argumentHintDepth() const {
  return m_argumentHintDepth;
}

TypeConversionCompletionItem::TypeConversionCompletionItem(QString text, KDevelop::IndexedType type, int argumentHintDepth, KSharedPtr<Cpp::CodeCompletionContext> _completionContext) : m_text(text), m_type(type), m_argumentHintDepth(argumentHintDepth), completionContext(_completionContext) {
}

MoreArgumentHintsCompletionItem::MoreArgumentHintsCompletionItem(KSharedPtr< KDevelop::CodeCompletionContext > context, QString text, uint oldNumber) : NormalDeclarationCompletionItem(DeclarationPointer(), context) {
  alternativeText = text;
  m_oldNumber = oldNumber;
}

namespace {
  const uint defaultMaxArgumentHints = 8;
  const uint maxArgumentHintsExtensionSteps = 20;
  uint currentMaxArgumentHints = defaultMaxArgumentHints;
};

uint MoreArgumentHintsCompletionItem::resetMaxArgumentHints()
{
  uint ret = currentMaxArgumentHints;
  currentMaxArgumentHints = defaultMaxArgumentHints;
  return ret;
}

void MoreArgumentHintsCompletionItem::execute(KTextEditor::Document* document, const KTextEditor::Range& word)
{
  currentMaxArgumentHints = m_oldNumber + maxArgumentHintsExtensionSteps;
  
  // Restart code-completion
  KTextEditor::CodeCompletionInterface* iface = dynamic_cast<KTextEditor::CodeCompletionInterface*>(document->activeView());
  Q_ASSERT(iface);
  iface->abortCompletion();
    ///@todo 1. This is a non-public interface, and 2. Completion should be started in "automatic invocation" mode
  QMetaObject::invokeMethod(document->activeView(), "userInvokedCompletion", Qt::QueuedConnection);
}

}
