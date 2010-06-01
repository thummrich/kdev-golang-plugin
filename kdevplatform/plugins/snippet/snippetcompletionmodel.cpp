/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2008 Andreas Pakulat <apaku@gmx.de>                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Library General Public License as       *
 *   published by the Free Software Foundation; either version 2 of the    *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this program; if not, write to the                 *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "snippetcompletionmodel.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

#include <kdeversion.h>

#if KDE_VERSION >= KDE_MAKE_VERSION(4, 4, 0)
    #define HAVE_HIGHLIGHT_IFACE
    #include <KTextEditor/HighlightInterface>
#endif

#include "snippetstore.h"
#include "snippetrepository.h"
#include "snippet.h"
#include "snippetcompletionitem.h"

#include <KLocalizedString>

SnippetCompletionModel::SnippetCompletionModel()
    : KTextEditor::CodeCompletionModel2(0)
{
    setHasGroups(false);
}

SnippetCompletionModel::~SnippetCompletionModel()
{
    qDeleteAll(m_snippets);
    m_snippets.clear();
}

QVariant SnippetCompletionModel::data( const QModelIndex& idx, int role ) const
{
    // grouping of snippets
    if (role == KTextEditor::CodeCompletionModel::InheritanceDepth) {
        return 1;
    }
    if (!idx.parent().isValid()) {
        if (role == Qt::DisplayRole) {
            return i18n("Snippets");
        }
        if (role == KTextEditor::CodeCompletionModel::GroupRole) {
            return Qt::DisplayRole;
        }
        return QVariant();
    }
    // snippets
    if( !idx.isValid() || idx.row() < 0 || idx.row() >= m_snippets.count() ) {
        return QVariant();
    } else {
        return m_snippets.at( idx.row() )->data(idx, role, 0);
    }
}

void SnippetCompletionModel::executeCompletionItem2(KTextEditor::Document* document, const KTextEditor::Range& word,
                                                    const QModelIndex& index) const
{
    if ( index.parent().isValid() ) {
        m_snippets[index.row()]->execute(document, word);
    }
}

void SnippetCompletionModel::completionInvoked(KTextEditor::View *view, const KTextEditor::Range &range, InvocationType invocationType)
{
    Q_UNUSED( range );
    Q_UNUSED( invocationType );
    initData(view);
}

void SnippetCompletionModel::initData(KTextEditor::View* view)
{
    QString mode;
    #ifdef HAVE_HIGHLIGHT_IFACE
        if ( KTextEditor::HighlightInterface* iface = qobject_cast<KTextEditor::HighlightInterface*>(view->document()) ) {
            mode = iface->highlightingModeAt(view->cursorPosition());
        }
    #endif // HAVE_HIGHLIGHT_IFACE

    if ( mode.isEmpty() ) {
        mode = view->document()->highlightingMode();
    }

    qDeleteAll(m_snippets);
    m_snippets.clear();
    SnippetStore* store = SnippetStore::self();
    for(int i = 0; i < store->rowCount(); i++ )
    {
        if ( store->item(i, 0)->checkState() != Qt::Checked ) {
            continue;
        }
        SnippetRepository* repo = dynamic_cast<SnippetRepository*>( store->item( i, 0 ) );
        if( repo && (repo->fileTypes().isEmpty() || repo->fileTypes().contains(mode)) )
        {
            for ( int j = 0; j < repo->rowCount(); ++j ) {
                if ( Snippet* snippet = dynamic_cast<Snippet*>(repo->child(j)) ) {
                    m_snippets << new SnippetCompletionItem(snippet, repo);
                }
            }
        }
    }
    reset();
}

QModelIndex SnippetCompletionModel::parent(const QModelIndex& index) const {
    if (index.internalId()) {
        return createIndex(0, 0, 0);
    } else {
        return QModelIndex();
    }
}

QModelIndex SnippetCompletionModel::index(int row, int column, const QModelIndex& parent) const {
    if (!parent.isValid()) {
        if (row == 0) {
            return createIndex(row, column, 0); //header  index
        } else {
            return QModelIndex();
        }
    } else if (parent.parent().isValid()) { //we only have header and children, no subheaders
        return QModelIndex();
    }

    if (row < 0 || row >= m_snippets.count() || column < 0 || column >= ColumnCount ) {
        return QModelIndex();
    }

    return createIndex(row, column, 1); // normal item index
}

int SnippetCompletionModel::rowCount (const QModelIndex & parent) const {
    if (!parent.isValid() && !m_snippets.isEmpty()) {
        return 1; //one toplevel node (group header)
    } else if(parent.parent().isValid()) {
        return 0; //we don't have sub children
    } else {
        return m_snippets.count(); // only the children
    }
}
KTextEditor::Range SnippetCompletionModel::completionRange(KTextEditor::View* view, const KTextEditor::Cursor& position)
{
    const QString& line = view->document()->line(position.line());
    KTextEditor::Range range(position, position);
    // include everything non-space before
    for ( int i = position.column() - 1; i >= 0; --i ) {
        if ( line.at(i).isSpace() ) {
            break;
        } else {
            range.start().setColumn(i);
        }
    }
    // include everything non-space after
    for ( int i = position.column() + 1; i < line.length(); ++i ) {
        if ( line.at(i).isSpace() ) {
            break;
        } else {
            range.end().setColumn(i);
        }
    }
    return range;
}

bool SnippetCompletionModel::shouldAbortCompletion(KTextEditor::View* view, const KTextEditor::SmartRange& range, const QString& currentCompletion)
{
    if(view->cursorPosition() < range.start() || view->cursorPosition() > range.end()) {
        return true; //Always abort when the completion-range has been left
    }

    for ( int i = 0; i < currentCompletion.length(); ++i ) {
        if ( currentCompletion.at(i).isSpace() ) {
            return true;
        }
    }
    // else it's valid
    return false;
}


#include "snippetcompletionmodel.moc"
