/*
 * KDevelop C++ Code Completion Support
 *
 * Copyright 2006-2007 Hamish Rodda <rodda@kde.org>
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

#ifndef KDEVCPPCODECOMPLETIONMODEL_H
#define KDEVCPPCODECOMPLETIONMODEL_H

#include <QPair>
#include <QMap>
#include <QPointer>
#include <language/codecompletion/codecompletionmodel.h>
#include <ksharedptr.h>
#include <language/duchain/duchainpointer.h>
#include <language/codecompletion/codecompletioncontext.h>
#include "item.h"
#include <ktexteditor/codecompletionmodelcontrollerinterface.h>
#include <kdeversion.h>

class QIcon;
class QString;
class QMutex;

namespace KDevelop {
  class DUContext;
  class Declaration;
  class CompletionTreeElement;
}

namespace Cpp {
  class CodeCompletionContext;
  class NavigationWidget;
  class CodeCompletionWorker;

class CodeCompletionModel : public KDevelop::CodeCompletionModel
{
  Q_OBJECT

  public:
    CodeCompletionModel(QObject* parent);
    virtual ~CodeCompletionModel();

  protected:
    virtual void foundDeclarations(QList<KSharedPtr<KDevelop::CompletionTreeElement> > item, KSharedPtr<KDevelop::CodeCompletionContext> completionContext);
    
    virtual KTextEditor::CodeCompletionModelControllerInterface2::MatchReaction matchingItem(const QModelIndex& matched);
    
    virtual QString filterString ( KTextEditor::View* view, const KTextEditor::SmartRange& range, const KTextEditor::Cursor& position );
    virtual KTextEditor::Range completionRange(KTextEditor::View* view, const KTextEditor::Cursor& position);
    virtual void updateCompletionRange(KTextEditor::View* view, KTextEditor::SmartRange& range);
    virtual void aborted(KTextEditor::View* view);
    virtual bool shouldAbortCompletion (KTextEditor::View* view, const KTextEditor::SmartRange& range, const QString& currentCompletion);
    virtual bool shouldStartCompletion (KTextEditor::View*, const QString&, bool userInsertion, const KTextEditor::Cursor&);
    virtual KDevelop::CodeCompletionWorker* createCompletionWorker();

  private:
    KSharedPtr<Cpp::CodeCompletionContext> m_completionContext;
};

}

#endif
