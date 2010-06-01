/*
   Copyright 2008 David Nolden <david.nolden.kdevelop@art-master.de>

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

#ifndef NAVIGATIONTOOLTIP_H
#define NAVIGATIONTOOLTIP_H

#include "../../util/activetooltip.h"
#include "../languageexport.h"

namespace KDevelop {

class AbstractNavigationWidget;

///A tooltip that just emebed the given widget.
class KDEVPLATFORMLANGUAGE_EXPORT NavigationToolTip : public ActiveToolTip
{
    Q_OBJECT
public:
    ///@param parent The parent.
    ///@param point Global coordinate of the point where the tooltip should be shown.
    ///@param navigationWidget The widget that should be embedded.
    NavigationToolTip(QWidget* parent, const QPoint& point, QWidget* navigationWidget);
    private Q_SLOTS:
        void sizeHintChanged();
    private:
        void setNavigationWidget(QWidget*);
        QWidget* m_navigationWidget;
};

}

#endif // NAVIGATIONTOOLTIP_H
