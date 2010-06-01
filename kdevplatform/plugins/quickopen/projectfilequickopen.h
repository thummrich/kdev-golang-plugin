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

#ifndef PROJECT_FILE_QUICKOPEN
#define PROJECT_FILE_QUICKOPEN

#include <KIcon>
#include <KUrl>
#include <language/interfaces/quickopendataprovider.h>
#include <language/interfaces/quickopenfilter.h>
#include <language/duchain/indexedstring.h>

namespace KDevelop {
  class ICore;
}

struct ProjectFile {
  ProjectFile() {
  }
  KDevelop::IndexedString m_url;
  KDevelop::IndexedString m_projectUrl;
  KDevelop::IndexedString m_project;
  KIcon m_icon;
};

class ProjectFileData : public KDevelop::QuickOpenDataBase {
  public:
    ProjectFileData( const ProjectFile& file );
    
    virtual QString text() const;
    virtual QString htmlDescription() const;

    bool execute( QString& filterText );

    virtual bool isExpandable() const;
    virtual QWidget* expandingWidget() const;

    virtual QIcon icon() const;
    
    QList<QVariant> highlighting() const;
    
  private:
    KUrl totalUrl() const;
    
    ProjectFile m_file;
};

/**
 * A QuickOpenDataProvider for file-completion using project-files.
 * It provides all files from all open projects.
 * */

typedef KDevelop::FilterWithSeparator<ProjectFile> Base;

class ProjectFileDataProvider : public KDevelop::QuickOpenDataProviderBase, public Base, public KDevelop::QuickOpenFileSetInterface {
  public:
    ProjectFileDataProvider();
    virtual void setFilterText( const QString& text );
    virtual void reset();
    virtual uint itemCount() const;
    virtual QList<KDevelop::QuickOpenDataPointer> data( uint start, uint end ) const;
    virtual QSet<KDevelop::IndexedString> files() const;

  private:
  
    //Reimplemented from Base<..>
    virtual QString itemText( const ProjectFile& data ) const;
};


class OpenFilesDataProvider : public ProjectFileDataProvider
{
public:
    virtual void reset();
    virtual QSet<KDevelop::IndexedString> files() const;
};

#endif

