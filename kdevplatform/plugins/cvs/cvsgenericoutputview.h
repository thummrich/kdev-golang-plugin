/***************************************************************************
 *   Copyright 2007 Robert Gruber <rgruber@users.sourceforge.net>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef CVSGENERICOUTPUTVIEW_H
#define CVSGENERICOUTPUTVIEW_H

#include <QWidget>
#include <KJob>

#include "ui_cvsgenericoutputview.h"

class CvsPlugin;
class CvsJob;

/**
 * Shows plain text.
 *
 * Text can either be added directly by calling appendText().
 *
 * Or by connecting a job's result() signal to slotJobFinished().
 *
 * @author Robert Gruber <rgruber@users.sourceforge.net>
 */
class CvsGenericOutputView : public QWidget, private Ui::CvsGenericOutputViewBase {
    Q_OBJECT
public:
    explicit CvsGenericOutputView(CvsPlugin *plugin, CvsJob* job=0, QWidget* parent=0);
    virtual ~CvsGenericOutputView();

public slots:
    void appendText(const QString& text);
    void slotJobFinished(KJob* job);

private:
    CvsPlugin* m_plugin;
};

#endif
