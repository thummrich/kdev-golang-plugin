/* This file is part of the KDE libraries
   Copyright (C) 2007 David Nolden <david.nolden.kdevelop@art-master.de>

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
#ifndef PROJECT_ITEM_QUICKOPEN
#define PROJECT_ITEM_QUICKOPEN

#include "duchainitemquickopen.h"
#include <language/duchain/indexedstring.h>
#include <language/duchain/identifier.h>

struct CodeModelViewItem {
    CodeModelViewItem() {
    }
    CodeModelViewItem(KDevelop::IndexedString file, KDevelop::QualifiedIdentifier id) : m_file(file), m_id(id) {
    }
    KDevelop::IndexedString m_file;
    KDevelop::QualifiedIdentifier m_id;
};

class ProjectItemDataProvider : public KDevelop::QuickOpenDataProviderBase {
public:
  enum ItemTypes {
    NoItems = 0,
    Classes = 1,
    Functions = 2,
    AllItemTypes = Classes + Functions
  };
  
  ProjectItemDataProvider( KDevelop::IQuickOpen* quickopen );

  virtual void enableData( const QStringList& items, const QStringList& scopes );

  virtual void setFilterText( const QString& text );

  virtual QList<KDevelop::QuickOpenDataPointer> data( uint start, uint end ) const;
  
  virtual void reset();

  virtual uint itemCount() const;

  static QStringList supportedItemTypes();
private:
  KDevelop::QuickOpenDataPointer data( uint pos ) const;
  QSet<KDevelop::IndexedString> m_usingFiles;
  ItemTypes m_itemTypes;
  KDevelop::IQuickOpen* m_quickopen;
  QVector<CodeModelViewItem> m_currentItems;
  QString m_currentFilter;
  QVector<CodeModelViewItem> m_filteredItems;
  //Maps positions to the additional items behind those positions
  //Here additional inserted items are stored, that are not represented in m_filteredItems.
  //This is needed at least to also show overloaded function declarations
  mutable QMap<uint, QList<KDevelop::QuickOpenDataPointer> > m_addedItems; 
};

#endif
