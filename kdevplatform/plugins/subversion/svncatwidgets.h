/***************************************************************************
 *   Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef SVN_CATWIDGETS_H
#define SVN_CATWIDGETS_H

#include <kdialog.h>
#include "ui_catoptiondlg.h"
class KUrl;
class SvnRevision;

class SvnCatOptionDlg : public KDialog
{
    Q_OBJECT
public:
    explicit SvnCatOptionDlg( const KUrl &path, QWidget *parent );
    ~SvnCatOptionDlg();

    KUrl url();
    SvnRevision revision();

private:
    Ui::SvnCatOptionDlg ui;
};

#endif
