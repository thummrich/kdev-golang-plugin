/***************************************************************************
 *   Copyright 2007 Dukju Ahn <dukjuahn@gmail.com>                         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef SVN_COPYWIDGETS_H
#define SVN_COPYWIDGETS_H

#include <kdialog.h>
#include "svnmodels.h"
#include "ui_copyoptiondlg.h"

class KUrl;
class SvnRevision;

class SvnCopyOptionDlg : public KDialog
{
    Q_OBJECT
public:
    explicit SvnCopyOptionDlg( const KUrl &reqUrl, SvnInfoHolder *info, QWidget *parent );
    ~SvnCopyOptionDlg();

    KUrl source();
    SvnRevision sourceRev();
    KUrl dest();

private Q_SLOTS:
    void srcAsUrlClicked();
    void srcAsPathClicked();

private:
    Ui::SvnCopyOptionDlg ui;
    KUrl m_reqUrl;
    SvnInfoHolder *m_info;
};

#endif
