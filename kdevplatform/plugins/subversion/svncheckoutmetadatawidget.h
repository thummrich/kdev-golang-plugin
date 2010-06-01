/***************************************************************************
 *   Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef SVN_CHECKOUTMETADATAWIDGET_H
#define SVN_CHECKOUTMETADATAWIDGET_H

#include <QWidget>
#include <vcs/interfaces/ibasicversioncontrol.h>

namespace Ui
{
class SvnCheckoutMetadataWidget;
}

class SvnCheckoutMetadataWidget : public QWidget
{
    Q_OBJECT
public:
    SvnCheckoutMetadataWidget( QWidget *parent );
    virtual ~SvnCheckoutMetadataWidget();
    void setDestinationLocation( const KUrl& );
    virtual KDevelop::VcsLocation source() const;
    virtual KUrl destination() const;
    virtual KDevelop::IBasicVersionControl::RecursionMode recursionMode() const;

private:
    Ui::SvnCheckoutMetadataWidget* m_ui;
};

#endif
