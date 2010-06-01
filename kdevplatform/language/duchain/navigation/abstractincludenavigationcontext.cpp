/*
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

#include "abstractincludenavigationcontext.h"

#include <QtGui/QTextDocument>

#include <klocale.h>

#include <language/duchain/duchain.h>
#include <language/duchain/parsingenvironment.h>
#include <language/duchain/declaration.h>

namespace KDevelop {

AbstractIncludeNavigationContext::AbstractIncludeNavigationContext(const IncludeItem& item,
                                                                   TopDUContextPointer topContext,
                                                                   const ParsingEnvironmentType& type)
    : AbstractNavigationContext(topContext), m_type(type), m_item(item)
{}

TopDUContext* pickContextWithData(QList<TopDUContext*> duchains, uint maxDepth,
                                   const ParsingEnvironmentType& type,
                                   bool forcePick = true) {
  TopDUContext* duchain = 0;

  foreach(TopDUContext* ctx, duchains) {
    if(!ctx->parsingEnvironmentFile() || ctx->parsingEnvironmentFile()->type() != type)
      continue;

    if(ctx->childContexts().count() != 0
        && (duchain == 0 || ctx->childContexts().count() > duchain->childContexts().count())) {
      duchain = ctx;
    }
    if(ctx->localDeclarations().count() != 0
        && (duchain == 0 || ctx->localDeclarations().count() > duchain->localDeclarations().count())) {
      duchain = ctx;
    }
  }

  if(!duchain && maxDepth != 0) {
    if(maxDepth != 0) {
      foreach(TopDUContext* ctx, duchains) {
        QList<TopDUContext*> children;
        foreach(const DUContext::Import &import, ctx->importedParentContexts())
          if(import.context(0))
            children << import.context(0)->topContext();

        duchain = pickContextWithData(children, maxDepth-1, type, false);
        if(duchain)
          break;
      }
    }
  }

  if(!duchain && !duchains.isEmpty() && forcePick)
    duchain = duchains.first();

  return duchain;
}

QString AbstractIncludeNavigationContext::html(bool shorten)
{
  clear();
  modifyHtml()  += "<html><body><p>" + fontSizePrefix(shorten);
  addExternalHtml(m_prefix);

  KUrl u(m_item.url());
  NavigationAction action(u, KTextEditor::Cursor(0,0));
  makeLink(u.pathOrUrl(), u.pathOrUrl(), action);
  modifyHtml() += "<br />";
  
  QList<TopDUContext*> duchains = DUChain::self()->chainsForDocument(u);
  //Pick the one duchain for this document that has the most child-contexts/declarations.
  //This prevents picking a context that is empty due to header-guards.
  TopDUContext* duchain = pickContextWithData(duchains, 2, m_type);

  if(duchain) {
    getFileInfo(duchain);
    if(!shorten) {
      modifyHtml() += labelHighlight(i18n("Declarations:")) + "<br />";
      bool first = true;
      addDeclarationsFromContext(duchain, first);
    }
  }else if(duchains.isEmpty()) {
    modifyHtml() += i18n("not parsed yet");
  }

  addExternalHtml(m_suffix);

  modifyHtml() += fontSizeSuffix(shorten) + "</p></body></html>";
  return currentHtml();
}

void AbstractIncludeNavigationContext::getFileInfo(TopDUContext* duchain)
{
    modifyHtml() += QString("%1: %2 %3: %4").arg(labelHighlight(i18nc("Files included into this file", "Includes"))).arg(duchain->importedParentContexts().count()).arg(labelHighlight(i18nc("Count of files this file was included into", "Included by"))).arg(duchain->importers().count());
    modifyHtml() += "<br />";
}

QString AbstractIncludeNavigationContext::name() const
{
  return m_item.name;
}

bool AbstractIncludeNavigationContext::filterDeclaration(Declaration* /*decl*/)
{
  return true;
}

void AbstractIncludeNavigationContext::addDeclarationsFromContext(KDevelop::DUContext* ctx, bool& first, QString indent)
{
  //modifyHtml() += indent + ctx->localScopeIdentifier().toString() + "{<br />";
  QVector<DUContext*> children = ctx->childContexts();
  QVector<Declaration*> declarations = ctx->localDeclarations();

  QVector<DUContext*>::const_iterator childIterator = children.constBegin();
  QVector<Declaration*>::const_iterator declarationIterator = declarations.constBegin();

  while(childIterator != children.constEnd() || declarationIterator != declarations.constEnd()) {

    //Show declarations/contexts in the order they appear in the file
    int currentDeclarationLine = -1;
    int currentContextLine = -1;
    if(declarationIterator != declarations.constEnd())
      currentDeclarationLine = (*declarationIterator)->range().textRange().start().line();

    if(childIterator != children.constEnd())
      currentDeclarationLine = (*childIterator)->range().textRange().start().line();

    if((currentDeclarationLine <= currentContextLine || currentContextLine == -1 || childIterator == children.constEnd()) && declarationIterator != declarations.constEnd() )
    {
      if(filterDeclaration(*declarationIterator) ) {
        //Show the declaration
        if(!first)
          modifyHtml() += Qt::escape(", ");
        else
          first = false;

        modifyHtml() += Qt::escape(indent + declarationKind(DeclarationPointer(*declarationIterator)) + " ");
        makeLink((*declarationIterator)->qualifiedIdentifier().toString(), DeclarationPointer(*declarationIterator), NavigationAction::NavigateDeclaration);
      }
      ++declarationIterator;
    } else {
      //Eventually Recurse into the context
      if((*childIterator)->type() == DUContext::Global || (*childIterator)->type() == DUContext::Namespace /*|| (*childIterator)->type() == DUContext::Class*/)
        addDeclarationsFromContext(*childIterator, first, indent + ' ');
      ++childIterator;
    }
  }
  //modifyHtml() += "}<br />";
}

}
