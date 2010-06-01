/*
 * This file is part of KDevelop
 *
 * Copyright 2008 Vladimir Prus <ghost@cs.msu.su>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
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

#ifndef TREEVIEW_H
#define TREEVIEW_H

#include <QtGui/QTreeView>

#include "../debuggerexport.h"


namespace KDevelop
{
class TreeModel;

    class KDEVPLATFORMDEBUGGER_EXPORT AsyncTreeView : public QTreeView
    {
        Q_OBJECT
    public:
        AsyncTreeView(TreeModel* model, QWidget *parent);

        // Well, I really, really, need this.
        using QTreeView::indexRowSizeHint;

    private Q_SLOTS:
        void slotExpanded(const QModelIndex &index);
        void slotCollapsed(const QModelIndex &index);
        void slotClicked(const QModelIndex &index);
    };

}



#endif
