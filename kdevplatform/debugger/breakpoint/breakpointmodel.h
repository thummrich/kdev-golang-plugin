/* This file is part of the KDE project
   Copyright (C) 2002 Matthias Hoelzer-Kluepfel <hoelzer@kde.org>
   Copyright (C) 2002 John Firebaugh <jfirebaugh@kde.org>
   Copyright (C) 2007 Hamish Rodda <rodda@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.
   If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef KDEV_BREAKPOINTMODEL_H
#define KDEV_BREAKPOINTMODEL_H

#include <QtCore/QAbstractTableModel>

#include <KDE/KTextEditor/MarkInterface>
#include "breakpoint.h"

class KUrl;

namespace KParts { class Part; }
namespace KTextEditor {
class Cursor;
class SmartCursor;
}

namespace KDevelop
{
class IDocument;
class Breakpoint;

class KDEVPLATFORMDEBUGGER_EXPORT BreakpointModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    BreakpointModel(QObject* parent);
    virtual ~BreakpointModel();

    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    Qt::ItemFlags flags(const QModelIndex &index) const;
    QModelIndex breakpointIndex(Breakpoint *b, int column);
    virtual bool removeRows(int row, int count, const QModelIndex& parent = QModelIndex());
    virtual int rowCount(const QModelIndex& parent = QModelIndex()) const;
    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const;
    virtual QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const;
    virtual bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole);
 
    void toggleBreakpoint(const KUrl &url, const KTextEditor::Cursor& cursor);


    KDevelop::Breakpoint* addCodeBreakpoint();
    KDevelop::Breakpoint* addCodeBreakpoint(const KUrl& location, int line);
    KDevelop::Breakpoint* addCodeBreakpoint(const QString& expression);
    KDevelop::Breakpoint* addWatchpoint();
    KDevelop::Breakpoint* addWatchpoint(const QString& expression);
    KDevelop::Breakpoint* addReadWatchpoint();
    KDevelop::Breakpoint* addReadWatchpoint(const QString& expression);
    KDevelop::Breakpoint* addAccessWatchpoint();
    KDevelop::Breakpoint* addAccessWatchpoint(const QString& expression);

    Breakpoint *breakpoint(int row);
    QList<Breakpoint*> breakpoints() const;

    void errorEmit(Breakpoint *b, const QString& message, int column) { emit error(b, message, column); }
    void hitEmit(Breakpoint *b) { emit hit(b); }

Q_SIGNALS:
    void error(KDevelop::Breakpoint *b, const QString& message, int column);
    void hit(KDevelop::Breakpoint *b);

public Q_SLOTS:
    void save();
    void load();

private:
    enum MarkType {
        BreakpointMark = KTextEditor::MarkInterface::BreakpointActive,
        ReachedBreakpointMark  = KTextEditor::MarkInterface::BreakpointReached,
        DisabledBreakpointMark = KTextEditor::MarkInterface::BreakpointDisabled,
        PendingBreakpointMark   = KTextEditor::MarkInterface::markType08,

        AllBreakpointMarks = BreakpointMark | ReachedBreakpointMark | DisabledBreakpointMark | PendingBreakpointMark
    };

Q_SIGNALS:
    /**
     * A breakpoint has been deleted by the user. The breakpoint object
     * still exists as is has eventualle be deleted from the debugger engine.
     */
    void breakpointDeleted(KDevelop::Breakpoint *breakpoint);
    void breakpointChanged(KDevelop::Breakpoint *breakpoint, KDevelop::Breakpoint::Column column);

private Q_SLOTS:

    void updateMarks();

    void slotPartAdded(KParts::Part* part);

    /**
    * Called by the TextEditor interface when the marks have changed position
    * because the user has added or removed source.
    * In here we figure out if we need to reset the breakpoints due to
    * these source changes.
    */
    void markChanged(KTextEditor::Document *document, KTextEditor::Mark mark, KTextEditor::MarkInterface::MarkChangeAction action);
    void textDocumentCreated(KDevelop::IDocument*);
    void documentSaved(KDevelop::IDocument*);
    void cursorDeleted(KTextEditor::SmartCursor* cursor);    
private:
    static const QPixmap* breakpointPixmap();
    static const QPixmap* pendingBreakpointPixmap();
    static const QPixmap* reachedBreakpointPixmap();
    static const QPixmap* disabledBreakpointPixmap();

private:
    friend class Breakpoint;
    void reportChange(Breakpoint *breakpoint, Breakpoint::Column column);
    uint breakpointType(Breakpoint *breakpoint);
    Breakpoint *breakpoint(const KUrl& url, int line);

    bool m_dontUpdateMarks;
    QList<Breakpoint*> m_breakpoints;
};


}

#endif
