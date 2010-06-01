/* KDevelop Standard OutputView
 *
 * Copyright 2006-2007 Andreas Pakulat <apaku@gmx.de>
 * Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef STANDARDOUTPUTVIEW_H
#define STANDARDOUTPUTVIEW_H

#include <outputview/ioutputview.h>
#include <interfaces/iplugin.h>
#include <QtCore/QVariant>

template <typename T> class QList;
class QAbstractItemModel;
class QString;
class QModelIndex;
class QAbstractItemDelegate;
class OutputWidget;

/**
@author Andreas Pakulat
*/

namespace Sublime
{
class View;
}

class ToolViewData;

class StandardOutputView : public KDevelop::IPlugin, public KDevelop::IOutputView
{
Q_OBJECT
Q_INTERFACES( KDevelop::IOutputView )

public:
    explicit StandardOutputView(QObject *parent = 0, const QVariantList &args = QVariantList());
    virtual ~StandardOutputView();

    int standardToolView( KDevelop::IOutputView::StandardToolView view );
    int registerToolView( const QString& title,
                          KDevelop::IOutputView::ViewType type = KDevelop::IOutputView::OneView,
                          const KIcon& icon = KIcon() );

    int registerOutputInToolView( int toolviewId, const QString& title,
                                  KDevelop::IOutputView::Behaviours behaviour
                                        = KDevelop::IOutputView::AllowUserClose );

    void raiseOutput( int id );
    void setModel( int id, QAbstractItemModel*, Ownership takeOwnership );

    void setDelegate( int id, QAbstractItemDelegate*, Ownership takeOwnership );

    OutputWidget* outputWidgetForId( int id ) const;

    virtual void removeToolView( int id );
    virtual void removeOutput( int id );

    virtual void scrollOutputTo( int id, const QModelIndex& idx );

public Q_SLOTS:
    void removeSublimeView( Sublime::View* );

Q_SIGNALS:
    void activated( const QModelIndex& );
    void selectNextItem();
    void selectPrevItem();
    void outputRemoved( int toolviewId, int id );
    void toolViewRemoved( int toolviewId );

private:
    QMap<int, ToolViewData*> toolviews;
    QList<int> ids;
    QMap<KDevelop::IOutputView::StandardToolView,int> standardViews;
    friend class StandardOutputViewViewFactory;
};

#endif // STANDARDOUTPUTVIEW_H

