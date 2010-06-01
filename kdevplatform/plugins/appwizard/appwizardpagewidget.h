/***************************************************************************
*   This file is part of KDevelop                                         *
*   Copyright 2009 Kris Wong <kris.p.wong@gmail.com>                      *
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
#ifndef APPWIZARDPAGEWIDGET_H
#define APPWIZARDPAGEWIDGET_H

#include <QWidget>

class AppWizardPageWidget : public QWidget
{
    Q_OBJECT
public:
    AppWizardPageWidget(QWidget* parent = 0);
    virtual ~AppWizardPageWidget();

    virtual bool shouldContinue();
};

#endif // APPWIZARDPAGEWIDGET_H

//kate: space-indent on; indent-width 4; replace-tabs on; auto-insert-doxygen on; indent-mode cstyle;

