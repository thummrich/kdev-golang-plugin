/*
 * This file is part of KDevelop
 *
 * Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>
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

#include "quickopen.h"

#include <QDir>
#include <QIcon>
#include <QSet>

#include <klocale.h>
#include <kiconloader.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/icore.h>
#include <interfaces/idocument.h>
#include <ktexteditor/document.h>

#include <language/duchain/duchainlock.h>

#include "cppduchain/navigation/navigationwidget.h"
#include "codecompletion/model.h"
#include "cpplanguagesupport.h"
#include <interfaces/icore.h>
#include <interfaces/ilanguagecontroller.h>
#include <interfaces/ilanguage.h>
#include "cpputils.h"

using namespace KDevelop;
using namespace Cpp;

TopDUContextPointer getCurrentTopDUContext() {
  IDocument* doc = ICore::self()->documentController()->activeDocument();

  if( doc )
  {
    return TopDUContextPointer( ICore::self()->languageController()->language("C++")->languageSupport()->standardContext( doc->url() ) );
  }
  return TopDUContextPointer();
}

void collectImporters( QSet<IndexedString>& importers, DUContext* ctx )
{
  if( importers.contains( ctx->url() ) )
    return;
  
  importers.insert( ctx->url() );

  foreach( DUContext* ctx, ctx->importers() )
    collectImporters( importers, ctx );
}

IncludeFileData::IncludeFileData( const IncludeItem& item, const TopDUContextPointer& includedFrom ) : m_item(item), m_includedFrom(includedFrom) {
}

QString IncludeFileData::text() const
{
  if(m_item.isDirectory)
    return m_item.name + '/';
  else
    return m_item.name;
}

bool IncludeFileData::execute( QString& filterText ) {
  if( m_item.isDirectory ) {
    //Change the filter-text to match the sub-directory
    KUrl u( filterText );
//     kDebug() << "filter-text:" << u;
    QString addName = m_item.name;
    if(addName.contains('/'))
      addName = addName.split('/').last();
    u.setFileName( addName );                                   
//     kDebug() << "with added:" << u;
    filterText = u.toLocalFile( KUrl::AddTrailingSlash ); 
//     kDebug() << "new:" << filterText;
    return false;
  } else {
    KUrl u = m_item.url();
    
    ICore::self()->documentController()->openDocument( u );

    return true;
  }
}

QList<QVariant> IncludeFileData::highlighting() const {
  QTextCharFormat boldFormat;
  boldFormat.setFontWeight(QFont::Bold);
  QTextCharFormat normalFormat;
  
  QString txt = text();
  
  QList<QVariant> ret;

  KUrl url(m_item.name);
  int fileNameLength = url.fileName().length();
  if(m_item.isDirectory)
    ++fileNameLength;
  
  ret << 0;
  ret << txt.length() - fileNameLength;
  ret << QVariant(normalFormat);
  ret << txt.length() - fileNameLength;
  ret << fileNameLength;
  ret << QVariant(boldFormat);
    
  return ret;
}


QIcon IncludeFileData::icon() const {
  ///@todo Better icons?
  static QIcon standardIcon = KIconLoader::global()->loadIcon( "CTdisconnected_parents", KIconLoader::Small );
  static QIcon includedIcon = KIconLoader::global()->loadIcon( "CTparents", KIconLoader::Small );
  static QIcon importerIcon = KIconLoader::global()->loadIcon( "CTchildren", KIconLoader::Small );

  if( m_item.pathNumber == -1 )
    return importerIcon;
  else if( m_includedFrom )
    return includedIcon;
  else
    return standardIcon;
}

bool IncludeFileData::isExpandable() const {
  return true;
}

QWidget* IncludeFileData::expandingWidget() const {
  
  DUChainReadLocker lock( DUChain::lock() );
  QString htmlPrefix, htmlSuffix;
  
  QList<KUrl> inclusionPath; //Here, store the shortest way of intermediate includes to the included file.

  if( m_includedFrom && m_item.pathNumber != -1 )
  {
    //Find the trace from m_includedFrom to the this file
    KUrl u = m_item.url();

    QList<TopDUContext*> allChains = DUChain::self()->chainsForDocument(u);

    foreach( TopDUContext* t, allChains )
    {
      if( m_includedFrom.data()->imports( t, m_includedFrom->range().end ) )
      {
/*        KDevelop::ImportTrace inclusion = m_includedFrom.data()->importTrace(t);

        if( inclusionPath.isEmpty() || inclusionPath.count() > inclusion.count() ) {
          inclusionPath.clear();
          FOREACH_ARRAY(const KDevelop::ImportTraceItem& s, inclusion)
            inclusionPath << KUrl(s.ctx->url().str());
        }*/
      }
    }
  }else if( m_item.pathNumber == -1 && m_includedFrom )
  {
    //Find the trace from this file to m_includedFrom
    KUrl u = m_item.url();

    QList<TopDUContext*> allChains = DUChain::self()->chainsForDocument(u);

    foreach( TopDUContext* t, allChains )
    {
      if( t->imports( m_includedFrom.data(), m_includedFrom->range().end ) )
      {
/*        KDevelop::ImportTrace inclusion = t->importTrace(m_includedFrom.data());

        if( inclusionPath.isEmpty() || inclusionPath.count() > inclusion.count() ) {
          inclusionPath.clear();
          FOREACH_ARRAY(const KDevelop::ImportTraceItem& s, inclusion)
            inclusionPath << KUrl(s.ctx->url().str());
        }*/
      }
    }
  }

  if( m_item.pathNumber == -1 ) {
    htmlPrefix = i18n("This file imports the current open document<br/>");
  } else {
    if( !inclusionPath.isEmpty() )
      inclusionPath.pop_back(); //Remove the file itself from the list
    
    htmlSuffix = "<br/>" + i18n( "In include path %1", m_item.pathNumber );
  }
  
  foreach( const KUrl& u, inclusionPath )
    htmlPrefix += i18n("Included through %1 <br/>", QString("KDEV_FILE_LINK{%1}").arg(u.pathOrUrl()) );
  
  return new NavigationWidget( m_item, getCurrentTopDUContext(), htmlPrefix, htmlSuffix );
}

QString IncludeFileData::htmlDescription() const
{
  KUrl path = m_item.url();
  
  if( m_item.isDirectory ) {
    return QString( i18n("Directory %1", path.pathOrUrl()) );
  } else {
    if(m_includedFrom) {
      DUChainReadLocker lock( DUChain::lock() );
      if(!m_includedFrom)
        return QString();

      KUrl u = m_item.url();

      QList<TopDUContext*> allChains = DUChain::self()->chainsForDocument(u);

      foreach( TopDUContext* t, allChains )
      {
        if( m_includedFrom.data()->imports( t, m_includedFrom->range().end ) )
        {
/*          QString ret = i18n("Included through") + " ";
          KDevelop::ImportTrace inclusion = m_includedFrom->importTrace(t);
          if(!inclusion.isEmpty()) {
            for(int a = 0; a < inclusion.size(); ++a) {
              if(a >= 1)
                ret += ", ";
              if(a > 2) {
                ret += "...";
                return ret;
              }else{
                ret += KUrl(inclusion[a].ctx->url().str()).fileName();
              }
            }
            return ret;
          }*/
        }
      }
    }else{
      return i18n( "In %1th include path", m_item.pathNumber );
    }
  }

  return " ";
}

IncludeFileDataProvider::IncludeFileDataProvider() : m_allowImports(true), m_allowPossibleImports(true), m_allowImporters(true) {
}

void allIncludedRecursion( QSet<const DUContext*>& used, QMap<IndexedString, IncludeItem>& ret, TopDUContextPointer ctx, QString prefixPath ) {

  if( !ctx )
    return;

  if( ret.contains(ctx->url()) )
    return;
  
  if( used.contains(ctx.data() ) )
    return;

  used.insert(ctx.data());
  
  foreach( const DUContext::Import &ctx2, ctx->importedParentContexts() ) {
    TopDUContextPointer d( dynamic_cast<TopDUContext*>(ctx2.context(0)) );
    allIncludedRecursion( used, ret, d, prefixPath );
  }

  IncludeItem i;

  i.name = ctx->url().str();

  if( !prefixPath.isEmpty() && !i.name.contains(prefixPath) )
    return;
  
  ret[ctx->url()] = i;
}

QList<IncludeItem> getAllIncludedItems( TopDUContextPointer ctx, QString prefixPath = QString() ) {

  DUChainReadLocker lock( DUChain::lock() );

  QMap<IndexedString, IncludeItem> ret;
  QSet<const DUContext*> used;
  allIncludedRecursion( used, ret, ctx, prefixPath );
  return ret.values();
}

void IncludeFileDataProvider::setFilterText( const QString& _text )
{
  QString text(_text);
    ///If the text contains '/', list items under the given prefix additionally

  if( text.contains( '/' ) )
  {
    KUrl::List addIncludePaths;
    QList<IncludeItem> allIncludeItems = m_baseItems;
    
    bool explicitPath = false;
    if(text.startsWith('/')) {
      addIncludePaths << KUrl("/");
      allIncludeItems.clear();
      text = text.mid(1);
      explicitPath = true;
    } else if(text.startsWith("~/")) {
      addIncludePaths << KUrl(QDir::homePath());
      allIncludeItems.clear();
      text = text.mid(2);
      explicitPath = true;
    }else if(text.startsWith("../")) {
      KUrl u(m_baseUrl);
      u.setFileName(QString());
      if(!u.isEmpty())
        u = u.upUrl();
      addIncludePaths << u;
      allIncludeItems.clear();
      text = text.mid(3);
      explicitPath = true;
    }else if(text.startsWith("./")) {
      KUrl u(m_baseUrl);
      u.setFileName(QString());
      addIncludePaths << u;
      allIncludeItems.clear();
      text = text.mid(2);
      explicitPath = true;
    }
    
    KUrl u( text );

    u.setFileName( QString() );
    QString prefixPath = u.toLocalFile();

    if( prefixPath != m_lastSearchedPrefix && !prefixPath.isEmpty() )
    {
      kDebug(9007) << "extracted prefix " << prefixPath;

      if( m_allowPossibleImports || explicitPath )
        allIncludeItems += CppUtils::allFilesInIncludePath( m_baseUrl, true, prefixPath, addIncludePaths, explicitPath, true, true );

      if( m_allowImports )
        allIncludeItems += getAllIncludedItems( m_duContext, prefixPath );
      
        setItems( allIncludeItems );

      m_lastSearchedPrefix = prefixPath;
    }
  }else{
    if( !m_lastSearchedPrefix.isEmpty() || text.isEmpty() ) {
      ///We were searching in a sub-path, but are not any more, or we are initializing the search with an empty text.
      m_lastSearchedPrefix = QString();
      setItems(m_baseItems);
    }
  }

  setFilter( text.split('/'), QChar('/') );
}

void IncludeFileDataProvider::reset()
{
  m_lastSearchedPrefix = QString();
  m_duContext = TopDUContextPointer();
  m_baseUrl = KUrl();
  m_importers.clear();
  
  IDocument* doc = ICore::self()->documentController()->activeDocument();

  if( doc )
  {
    m_baseUrl = doc->url();

    {
      DUChainReadLocker lock( DUChain::lock() );
      m_duContext = TopDUContextPointer( ICore::self()->languageController()->language("C++")->languageSupport()->standardContext( doc->url() )  );

      if( m_allowImporters && m_duContext ) {
        QSet<IndexedString> importers;

        collectImporters( importers, m_duContext.data() );

        m_importers = importers.values();
      }
    }
  }
  
  QList<IncludeItem> allIncludeItems;

  if( m_allowPossibleImports )
    allIncludeItems += CppUtils::allFilesInIncludePath( m_baseUrl, true, QString(), KUrl::List(), false, true, true );

  if( m_allowImports )
    allIncludeItems += getAllIncludedItems( m_duContext );
  
  foreach( const IndexedString &u, m_importers ) {
    IncludeItem i;
    i.isDirectory = false;
    i.name = u.str();
    i.pathNumber = -1; //We mark this as an importer by putting pathNumber to -1
    allIncludeItems << i;
  }
  
  m_baseItems = allIncludeItems;
  
  clearFilter();
}

uint IncludeFileDataProvider::itemCount() const
{
  return filteredItems().count();
}

QList<QuickOpenDataPointer> IncludeFileDataProvider::data( uint start, uint end ) const
{
  QList<QuickOpenDataPointer> ret;

  const QList<KDevelop::IncludeItem>& items( filteredItems() );
  
  if( end > (uint)items.count() )
    end = items.count();
  
  DUChainReadLocker lock( DUChain::lock() );
  
  for( uint a = start; a < end; a++ )
  {
    //Find out whether the url is included into the current file
    bool isIncluded = false;

    if( m_duContext )
    {
      KUrl u = items[a].url();

      QList<TopDUContext*> allChains = DUChain::self()->chainsForDocument(u);

      foreach( TopDUContext* t, allChains )
      {
        if( m_duContext.data()->imports( t, m_duContext->range().end ) )
        {
          isIncluded = true;
          break;
        }
      }
    }

    //If it is an importer(marked by pathNumber -1), give m_duContext so we can search the inclusion-path later
    ret << QuickOpenDataPointer( new IncludeFileData( items[a], ( isIncluded || items[a].pathNumber == -1 ) ? m_duContext : TopDUContextPointer() ) );
  }
  
  return ret;
}

QString IncludeFileDataProvider::itemText( const KDevelop::IncludeItem& data ) const
{
  return data.name;
}

QSet<IndexedString> IncludeFileDataProvider::files() const {
  QSet<IndexedString> set;
  foreach(const KDevelop::IncludeItem& item, items()) {
    if( !item.basePath.isEmpty() ) {
      KUrl path = item.basePath;
      path.addPath( item.name );
      set << IndexedString(path.pathOrUrl());
    }else{
      set << IndexedString(item.name);
    }
  }
  return set;
}

QStringList IncludeFileDataProvider::scopes() {
  QStringList ret;
  ret << i18n("Includes");
  ret << i18n("Include Path");
  ret << i18n("Includers");
  return ret;
}

void IncludeFileDataProvider::enableData( const QStringList& /*items*/, const QStringList& scopes ) {
  m_allowImports = scopes.contains( i18n("Includes") );
  m_allowPossibleImports = scopes.contains( i18n("Include Path") );
  m_allowImporters = scopes.contains( i18n("Includers") );
}

