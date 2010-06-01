/*
 * KDevelop Generic Code Completion Support
 *
 * Copyright 2006-2008 Hamish Rodda <rodda@kde.org>
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

#ifndef KDEV_CODECOMPLETIONWORKER_H
#define KDEV_CODECOMPLETIONWORKER_H

#include <QtCore/QThread>
#include <QtCore/QList>

#include <KDE/KTextEditor/Cursor>
#include <KDE/KTextEditor/Range>

#include "../languageexport.h"
#include "../duchain/duchainpointer.h"
#include "../codecompletion/codecompletioncontext.h"

class QMutex;

namespace KTextEditor {
  class Range;
  class View;
  class Cursor;
}

namespace KDevelop
{

class CodeCompletion;
class CompletionTreeElement;
class CodeCompletionModel;


class KDEVPLATFORMLANGUAGE_EXPORT CodeCompletionWorker : public QObject
{
  Q_OBJECT

  public:
    CodeCompletionWorker(QObject* parent);
    virtual ~CodeCompletionWorker();

    virtual void abortCurrentCompletion();

    void setFullCompletion(bool);
    bool fullCompletion() const;

    KDevelop::CodeCompletionModel* model() const;

    ///When this is called, the result is shown in the completion-list.
    ///Call this from within your code
    void foundDeclarations(QList<KSharedPtr<CompletionTreeElement> >, KSharedPtr<CodeCompletionContext> completionContext);
    
  Q_SIGNALS:

    ///Internal connections into the foreground completion model
    void foundDeclarationsReal(QList<KSharedPtr<CompletionTreeElement> >, KSharedPtr<CodeCompletionContext> completionContext);
    
  protected:
    
    virtual void computeCompletions(DUContextPointer context, const KTextEditor::Cursor& position, KTextEditor::View* view, const KTextEditor::Range& contextRange, const QString& contextText);
    virtual QList<KSharedPtr<CompletionTreeElement> > computeGroups(QList<CompletionTreeItemPointer> items, KSharedPtr<CodeCompletionContext> completionContext);
    ///If you don't need to reimplement computeCompletions, you can implement only this.
    virtual KDevelop::CodeCompletionContext* createCompletionContext(KDevelop::DUContextPointer context, const QString &contextText, const QString &followingText, const SimpleCursor &position) const;

    ///Can be used to retrieve and set the aborting flag(Enabling it is equivalent to caling abortCompletion())
    ///Is always reset from within computeCompletions
    bool& aborting();
    
    ///Emits foundDeclarations() with an empty list. Always call this when you abort the process of computing completions
    void failed();
    
  public Q_SLOTS:
    ///Connection from the foreground thread within CodeCompletionModel
    void computeCompletions(KDevelop::DUContextPointer context, const KTextEditor::Cursor& position, KTextEditor::View* view);
    ///This can be used to do special processing within the background, completely bypassing the normal computeCompletions(..) etc. system.
    ///It will be executed within the background when the model emits doSpecialProcessingInBackground
    virtual void doSpecialProcessing(uint data);

  private:
    bool m_hasFoundDeclarations;
    QMutex* m_mutex;
    bool m_abort;
    bool m_fullCompletion;
};

}

#endif // KDEVCODECOMPLETIONWORKER_H
