/*
   Copyright 2008 David Nolden <david.nolden.kdevelop@art-master.de>

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

#include "missingincludeitem.h"
#include <language/duchain/namespacealiasdeclaration.h>
#include <language/duchain/persistentsymboltable.h>
#include <language/duchain/types/abstracttype.h>
#include <language/duchain/types/delayedtype.h>
#include <language/duchain/types/identifiedtype.h>
#include "../cppduchain/navigation/navigationwidget.h"
#include "../cppduchain/typeutils.h"
#include "../cppduchain/templateparameterdeclaration.h"
#include "../cpplanguagesupport.h"
#include "../cpputils.h"
#include "model.h"
#include <klocale.h>
#include <ktexteditor/document.h>
#include <language/duchain/classdeclaration.h>
#include <language/duchain/duchainutils.h>
#include <declarationbuilder.h>
#include <language/duchain/stringhelpers.h>
#include "helpers.h"
#include "missingincludemodel.h"
#include <language/duchain/aliasdeclaration.h>
#include "sourcemanipulation.h"

//Whether relative urls like "../bla" should be allowed. Even if this is false, they will be preferred over global urls.
bool allowDotDot = true;
const uint maxDeclarationCount = 30;

using namespace KTextEditor;
using namespace KDevelop;

namespace Cpp {

///Makes sure the line is not in a comment, moving it behind if needed. Just does very simple matching, should be ok for header copyright-notices and such.
int moveBehindComment(KTextEditor::Document* document, int line, int maxLine) {

  DUChainReadLocker lock(DUChain::lock());
  TopDUContext* top = DUChainUtils::standardContextForUrl(document->url());
  if(!top)
    return line;
  Cpp::SourceCodeInsertion insertion(top);

  int firstValid = insertion.firstValidCodeLineBefore(maxLine);
  if(firstValid > line && firstValid <= maxLine)
    return firstValid;

  return line;
}

///Decide whether the file is allowed to be included directly. If yes, this should return false.
bool isBlacklistedInclude(KUrl url) {
  QString fileName = url.fileName();
  if(isSource(fileName))
    return true;

  url = url.upUrl();
  //Do not allow including directly from the bits directory. Instead use one of the forwarding headers in other directories, when possible.
  if(url.fileName() == "bits" && url.toLocalFile().contains("/include/c++/")) {
    return true;
  }
  
  return false;
}

QualifiedIdentifier removeTemplateParameters(QualifiedIdentifier baseIdentifier) {
  QualifiedIdentifier  identifier;
  for(int a = 0; a < baseIdentifier.count(); ++a) {
    Identifier part = baseIdentifier.at(a);
    part.clearTemplateIdentifiers();
    identifier.push(part);
  }
  return identifier;
}

QList<KDevelop::CompletionTreeItemPointer> itemsForFile(QString displayTextPrefix, QString file, KUrl::List includePaths, KUrl currentPath, IndexedDeclaration decl, uint argumentHintDepth, QSet<QString>& directives) {
  QList<KDevelop::CompletionTreeItemPointer> ret;
  //We have found a potential declaration. Now find the shortest include path.
  QString shortestDirective;
  bool isRelativeToCurrentDir = false;
  
  if(isSource(file))
    return ret;
  
  foreach(const KUrl& includePath, includePaths) {
    QString relative = KUrl::relativePath( QFileInfo(includePath.toLocalFile()).canonicalFilePath(), QFileInfo(file).canonicalFilePath() );
    if(relative.startsWith("./"))
      relative = relative.mid(2);
    
    if(shortestDirective.isEmpty() || (relative.length() < shortestDirective.length() && (allowDotDot || !relative.startsWith(".."))) || (shortestDirective.startsWith("..") && !relative.startsWith(".."))) {
      shortestDirective = relative;
      
      isRelativeToCurrentDir = includePath.equals( currentPath );
    }
  }
  if(!shortestDirective.isEmpty()) {
    if(isRelativeToCurrentDir)
      shortestDirective = "\"" + shortestDirective + "\"";
    else
      shortestDirective = "<" + shortestDirective + ">";
    
    if(!directives.contains(shortestDirective))
      ret << KDevelop::CompletionTreeItemPointer(new MissingIncludeCompletionItem(shortestDirective, displayTextPrefix, decl, (int)argumentHintDepth));
    
    directives.insert(shortestDirective);
  }
  return ret;
}

struct DirectiveShorterThan {
  bool operator()(const KDevelop::CompletionTreeItemPointer& lhs, const KDevelop::CompletionTreeItemPointer& rhs) {
    const MissingIncludeCompletionItem* l = dynamic_cast<const MissingIncludeCompletionItem*>(lhs.data());
    const MissingIncludeCompletionItem* r = dynamic_cast<const MissingIncludeCompletionItem*>(rhs.data());
    if(l && r)
      return l->m_addedInclude.length() < r->m_addedInclude.length();
    return false;
  }
};

QStringList candidateIncludeFiles(Declaration* decl) {
  QStringList ret;

  bool inBlacklistDir = isBlacklistedInclude(decl->url().toUrl());
  
  foreach(KDevelop::ParsingEnvironmentFilePointer ptr, decl->topContext()->parsingEnvironmentFile()->importers()) {
    if(ptr->imports().count() == 1 || inBlacklistDir) {
      if(isBlacklistedInclude(ptr->url().toUrl()))
        continue;
      //This file is a forwader, add it to the list

      //Forwarders must be completely empty
      if(ptr->topContext()->localDeclarations().count())
        continue;
      
      QString file(ptr->url().toUrl().toLocalFile());
      ret << file;
    }
  }
  
  if(!inBlacklistDir)
    ret << decl->url().toUrl().toLocalFile();
  
  return ret;
}

KSharedPtr<MissingIncludeCompletionItem> includeDirectiveFromUrl(KUrl fromUrl, KDevelop::IndexedDeclaration decl) {
  KSharedPtr<MissingIncludeCompletionItem> item;
  if(decl.data()) {
    QSet<QString> temp;
    QStringList candidateFiles = candidateIncludeFiles(decl.data());

    QList<KDevelop::CompletionTreeItemPointer> items;
    foreach(const QString& file, candidateFiles)
      items += itemsForFile(QString(), file, CppUtils::findIncludePaths(fromUrl, 0), fromUrl, decl, 0, temp);

    qSort<QList<KDevelop::CompletionTreeItemPointer>::iterator, DirectiveShorterThan>(items.begin(), items.end(), DirectiveShorterThan());
    if(!items.isEmpty()) {
      item = KSharedPtr<MissingIncludeCompletionItem>(dynamic_cast<MissingIncludeCompletionItem*>(items.begin()->data()));
    }
  }
  return item;
}

QList<KDevelop::CompletionTreeItemPointer> missingIncludeCompletionItems(QString expression, QString displayTextPrefix, Cpp::ExpressionEvaluationResult expressionResult, KDevelop::DUContext* context, int argumentHintDepth, bool needInstance) {
  
  AbstractType::Ptr type = TypeUtils::targetType(expressionResult.type.abstractType(), context->topContext());
  
  //Collect all visible "using namespace" imports
  QList<Declaration*> imports = context->findDeclarations( globalImportIdentifier() );
  QSet<QualifiedIdentifier> prefixes;
  prefixes.insert(QualifiedIdentifier());
  foreach(Declaration* importDecl, imports) {
    NamespaceAliasDeclaration* aliasDecl = dynamic_cast<NamespaceAliasDeclaration*>(importDecl);
    if(aliasDecl) {
      prefixes.insert(aliasDecl->importIdentifier());
    }else{
      kDebug() << "Import is not based on NamespaceAliasDeclaration";
    }
  }
  
  QualifiedIdentifier namespaceScope = context->scopeIdentifier(false);
  for(int a = 1; a <= namespaceScope.count(); ++a)
    prefixes << namespaceScope.left(a); //Also search within enclosing namespaces
  
  QList<KDevelop::CompletionTreeItemPointer> ret;
  QList<KDevelop::CompletionTreeItemPointer> blacklistRet;

  QualifiedIdentifier identifier;
  if(type) {
    DelayedType::Ptr delayed = type.cast<DelayedType>();
    if(delayed)
      //Remove all template parameters, because the symbol-table doesn't know about those
      identifier = removeTemplateParameters(delayed->identifier().identifier().identifier());
    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(type.unsafeData());
    if(idType) {
      identifier = removeTemplateParameters(idType->qualifiedIdentifier());
    }
  }else{
    //expression probably contains a part that needs to be resolved
    
    if(expression.contains(".") || expression.contains("->")) {
      ///@todo Check if parts of the expression are unresolved, like in "unresolvedClass.callFunction"
      kDebug() << "doing nothing with expression" << expression;
    }else{
      kDebug() << "looking up" << expression << "as qualified identifier";
      identifier = removeTemplateParameters(QualifiedIdentifier(expression));
      QList<Declaration*> visibleDecls = context->findDeclarations(identifier);
      foreach(Declaration* decl, visibleDecls) {
        if(!decl->isForwardDeclaration())
          return ret; //Do not search for fitting declarations if a valid one is visible right now from here
      }
    }
  }
  
  if(identifier.isEmpty()) {
    return ret;
  }
  
  KUrl currentUrl(context->topContext()->url().str());
  KUrl currentPath(context->topContext()->url().str());
  currentPath.setFileName(QString());
  
  Cpp::EnvironmentFilePointer env(dynamic_cast<Cpp::EnvironmentFile*>(context->topContext()->parsingEnvironmentFile().data()));
  if(!env)
    return ret;
  
  
  KUrl::List includePaths;
  
  foreach(const IndexedString& path, env->includePaths())
    includePaths << path.toUrl();
  
  includePaths.prepend(currentPath);
  
  QSet<QString> directives;
  
  QSet<DeclarationId> haveForwardDeclarationItems;
  
  ///Search the persistent symbol table
  foreach(QualifiedIdentifier prefix, prefixes) {
    prefix.setExplicitlyGlobal(false);
    const IndexedDeclaration* declarations;
    uint declarationCount;
    QualifiedIdentifier id = prefix + identifier;

    PersistentSymbolTable::self().declarations( id, declarationCount, declarations );

    if(declarationCount >  maxDeclarationCount)
      declarationCount = maxDeclarationCount;
    
    for(uint a = 0; a < declarationCount; ++a) {
      KDevelop::ParsingEnvironmentFilePointer env = DUChain::self()->environmentFileForDocument(declarations[a].indexedTopContext());
      if(!env || !dynamic_cast<Cpp::EnvironmentFile*>(env.data()))
        continue;
      
      Declaration* decl = declarations[a].declaration();
      
      if(!decl)
        continue;
      if(dynamic_cast<KDevelop::AliasDeclaration*>(decl))
        continue;
      
      if(!isSource(context->url().str())) {
        if(decl && (decl->context()->type() == DUContext::Namespace || decl->context()->type() == DUContext::Global) && !needInstance && (decl->type<CppClassType>() || decl->type<KDevelop::EnumerationType>()) ) {
          if(!haveForwardDeclarationItems.contains(decl->id()))
            ret += KDevelop::CompletionTreeItemPointer( new ForwardDeclarationItem(DeclarationPointer(decl)) );
          haveForwardDeclarationItems.insert(decl->id());
        }
      }
      
      if(decl && !decl->isForwardDeclaration()) {
        if(context->topContext()->imports(decl->topContext(), SimpleCursor::invalid()))
          continue;
        
        QString file(decl->url().toUrl().toLocalFile());
        
        bool inBlacklistDir = isBlacklistedInclude(decl->url().toUrl());
        
        foreach(KDevelop::ParsingEnvironmentFilePointer ptr, decl->topContext()->parsingEnvironmentFile()->importers()) {
          if(ptr->imports().count() == 1 || inBlacklistDir) {
            if(isBlacklistedInclude(ptr->url().toUrl()))
              continue;
            //This file is a forwader, add it to the list

            //Forwarders must be completely empty
            if(ptr->topContext()->localDeclarations().count())
              continue;
            
            QString file(ptr->url().toUrl().toLocalFile());
            ret += itemsForFile(displayTextPrefix, file, includePaths, currentPath, decl, argumentHintDepth, directives);
          }
        }
        
        if(inBlacklistDir)
          blacklistRet += itemsForFile(displayTextPrefix, file, includePaths, currentPath, decl, argumentHintDepth, directives);
        else
          ret += itemsForFile(displayTextPrefix, file, includePaths, currentPath, decl, argumentHintDepth, directives);
      }
    }
  }

  if(ret.isEmpty())
    ret += blacklistRet;
  
  {
    //If there is non-relative include directives, remove the relative ones
    QList<KDevelop::CompletionTreeItemPointer> relativeIncludes;
    QList<KDevelop::CompletionTreeItemPointer> nonRelativeIncludes;
    
    for(QList<KDevelop::CompletionTreeItemPointer>::iterator it = ret.begin(); it != ret.end(); ) {
      MissingIncludeCompletionItem* currentItem = dynamic_cast<MissingIncludeCompletionItem*>(it->data());
      if(currentItem) {
        if(currentItem->m_addedInclude.contains("\"../") || currentItem->m_addedInclude.contains("<../"))
          relativeIncludes << *it;
        else
          nonRelativeIncludes << *it;
      }
      ++it;
    }
    
    if(!nonRelativeIncludes.isEmpty()) {
      foreach(KDevelop::CompletionTreeItemPointer relative, relativeIncludes)
        ret.removeAll(relative);
    }
  }

  qSort<QList<KDevelop::CompletionTreeItemPointer>::iterator, DirectiveShorterThan>(ret.begin(), ret.end(), DirectiveShorterThan());
  
  return ret;
}

#define RETURN_CACHED_ICON(name) {static QIcon icon(KIcon(name).pixmap(QSize(16, 16))); return icon;}

QVariant MissingIncludeCompletionItem::data(const QModelIndex& index, int role, const KDevelop::CodeCompletionModel* model) const {
  DUChainReadLocker lock(DUChain::lock(), 500);
  if(!lock.locked()) {
    kDebug(9007) << "Failed to lock the du-chain in time";
    return QVariant();
  }
  if(role == Qt::DecorationRole)
    if(index.column() == KTextEditor::CodeCompletionModel::Icon)
      RETURN_CACHED_ICON("CTparents")
  
  switch (role) {
    case KTextEditor::CodeCompletionModel::IsExpandable:
      return QVariant(true);
    case KTextEditor::CodeCompletionModel::ExpandingWidget: {
      if(!m_decl.data())
        return QVariant();
      
      Cpp::NavigationWidget* nav = new Cpp::NavigationWidget(DeclarationPointer(m_decl.data()), TopDUContextPointer(m_decl.data()->topContext()));
      model->addNavigationWidget(this, nav);

       QVariant v;
       v.setValue<QWidget*>((QWidget*)nav);
       return v;
    }
    case Qt::DisplayRole:
      switch (index.column()) {
        case KTextEditor::CodeCompletionModel::Prefix:
            return i18n("Add include directive");
        case KTextEditor::CodeCompletionModel::Name: {
          QString suffix = ", #include " + m_addedInclude;
          if(!m_decl.data())
            return m_displayTextPrefix + suffix;
          else if(m_decl.data()->kind() == Declaration::Namespace)
            return m_displayTextPrefix + " namespace " + m_decl.data()->identifier().toString()  + suffix;
          else
            return m_displayTextPrefix + m_decl.data()->toString() + suffix;
        }
      }
      break;
    case KTextEditor::CodeCompletionModel::ItemSelected:
    {
      if(!m_decl.data())
        return QVariant();
      return QVariant( Cpp::NavigationWidget::shortDescription(m_decl.data()) );
    }
  }

  return QVariant();
}

QString MissingIncludeCompletionItem::lineToInsert() const {
  return "#include " + m_addedInclude;
}

void MissingIncludeCompletionItem::execute(KTextEditor::Document* document, const KTextEditor::Range& word) {

  QString insertLine = lineToInsert();
  int lastLineWithInclude = 0;
  int checkLines = word.start().line() -1;
  for(int a = 0; a < checkLines; ++a) {
    QString lineText = document->line(a);
    if(lineText.trimmed().startsWith("#include")) {
      QString ending = lineText.trimmed();
      if(!ending.isEmpty())
        ending = ending.left( ending.length()-1 ).trimmed(); //Remove the last > or "
      
      if(!ending.endsWith(".moc"))
        lastLineWithInclude = a;
    }
  }

  document->insertLine(moveBehindComment(document, lastLineWithInclude+1, word.start().line()), insertLine);
  MissingIncludeCompletionModel::startCompletionAfterParsing(IndexedString(document->url()));
}

int MissingIncludeCompletionItem::inheritanceDepth() const {
  return 0;
}

ForwardDeclarationItem::ForwardDeclarationItem(KDevelop::DeclarationPointer decl) : NormalDeclarationCompletionItem(decl) {
}

QVariant ForwardDeclarationItem::data(const QModelIndex& index, int role, const KDevelop::CodeCompletionModel* model) const {
  if(role == Qt::DisplayRole && index.column() == KTextEditor::CodeCompletionModel::Prefix)
    return i18n("Add Forward-Declaration");

  if(role == Qt::DecorationRole && index.column() == KTextEditor::CodeCompletionModel::Icon) {
    RETURN_CACHED_ICON("dialog-ok"); ///@todo Better icon for the create-forward declaration action
  }
  
  QVariant ret = NormalDeclarationCompletionItem::data(index, role, model);
  
  if(role == Qt::DisplayRole && index.column() == KTextEditor::CodeCompletionModel::Name) {
    //Add some text behind the item, so we get some more info in minimal completion mode
    DUChainReadLocker lock(DUChain::lock());
    if(m_declaration)
      return m_declaration->qualifiedIdentifier().toString() + ": " + i18n("forward-declare");
    return ret;
//     return m_displayTextPrefix + ": " + i18n("Add Forward-Declaration");
  }
  
  return ret;
}

void ForwardDeclarationItem::execute(KTextEditor::Document* document, const KTextEditor::Range& word) {
  DUChainReadLocker lock(DUChain::lock());
  if(m_declaration) {
    TopDUContext* top = DUChainUtils::standardContextForUrl(document->url());
    if(!top)
      return;
    Cpp::SourceCodeInsertion insertion(top);
    
    insertion.setInsertBefore(KDevelop::SimpleCursor(word.start()));
    
    insertion.insertForwardDeclaration(m_declaration.data());
    
    lock.unlock();
    
    insertion.changes().setReplacementPolicy(DocumentChangeSet::WarnOnFailedChange);
    
    if(!insertion.changes().applyAllChanges())
      return;
  }
}

}
