/* KDevelop CMake Support
 *
 * Copyright 2009 Aleix Pol <aleixpol@kde.org>
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

#ifndef CMAKENAVIGATIONWIDGET_H
#define CMAKENAVIGATIONWIDGET_H

#include <language/duchain/navigation/abstractnavigationcontext.h>
#include <language/duchain/navigation/abstractnavigationwidget.h>

namespace KDevelop { class IDocumentation; }

class CMakeNavigationWidget : public KDevelop::AbstractNavigationWidget
{
    public:
        CMakeNavigationWidget(KDevelop::TopDUContextPointer top, const KSharedPtr<KDevelop::IDocumentation>& doc);
        CMakeNavigationWidget(KDevelop::TopDUContextPointer top, KDevelop::Declaration* decl);
        
    private:
        KDevelop::NavigationContextPointer ptr;
};

#endif // CMAKENAVIGATIONWIDGET_H
