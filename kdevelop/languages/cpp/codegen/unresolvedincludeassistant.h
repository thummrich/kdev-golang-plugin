/*
   Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>
   
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

#ifndef CPP_MISSINGINCLUDEASSISTANT_H
#define CPP_MISSINGINCLUDEASSISTANT_H

#include <interfaces/iassistant.h>
#include <language/duchain/indexedstring.h>
#include <kurl.h>


namespace Cpp {

class AddCustomIncludePathAction : public KDevelop::IAssistantAction {
  Q_OBJECT
  public:
    AddCustomIncludePathAction(KDevelop::IndexedString url, QString directive);
    virtual QString description() const;
    virtual void execute();
    private:
    KDevelop::IndexedString m_url;
    QString m_directive;
};

class OpenProjectForFileAssistant : public KDevelop::IAssistantAction {
  public:
    OpenProjectForFileAssistant(KUrl);
    virtual QString description() const;
    virtual void execute();
    private:
    KUrl m_url;
};

class MissingIncludePathAssistant : public KDevelop::IAssistant
{
public:
  MissingIncludePathAssistant(KDevelop::IndexedString url, QString directive);
  virtual QString title() const;
  virtual QList< KDevelop::IAssistantAction::Ptr > actions() const;
  private:
  KDevelop::IndexedString m_url;
  QString m_directive;
};

}

#endif // CPP_MISSINGINCLUDEASSISTANT_H
