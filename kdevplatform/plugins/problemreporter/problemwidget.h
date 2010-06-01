/*
 * KDevelop Problem Reporter
 *
 * Copyright (c) 2007 Hamish Rodda <rodda@kde.org>
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

#ifndef PROBLEMWIDGET_H
#define PROBLEMWIDGET_H

#include <QtGui/QTreeView>

#include <KUrl>
#include <KTextEditor/Range>

#include <language/interfaces/iproblem.h>

namespace KDevelop {
  class ParseJob;
  class TopDUContext;
  class IDocument;
}

class ProblemReporterPlugin;

class ProblemWidget : public QTreeView
{
  Q_OBJECT

public:
  ProblemWidget(QWidget* parent, ProblemReporterPlugin* plugin);
  virtual ~ProblemWidget();

  class ProblemModel* model() const;

  static void collectProblems(QList<KDevelop::ProblemPointer>& allProblems, KDevelop::TopDUContext* context, QSet<KDevelop::TopDUContext*>& hadContexts);

  virtual void contextMenuEvent(QContextMenuEvent* );
  
protected:
  virtual void showEvent(QShowEvent* event);

private Q_SLOTS:
  void itemActivated(const QModelIndex& index);

  void parseJobFinished(KDevelop::ParseJob* job);

  void documentActivated(KDevelop::IDocument*);
  
  void forceFullUpdate();
  
private:
  void showProblems(KDevelop::TopDUContext* ctx, KDevelop::IDocument* doc);
  ProblemReporterPlugin* m_plugin;
  KUrl m_activeDirectory;
  KUrl m_activeUrl;
  KAction* m_fullUpdateAction;
};

#endif

// kate: space-indent on; indent-width 2; tab-width: 4; replace-tabs on; auto-insert-doxygen on
