/***************************************************************************
 *   Copyright 2007 Robert Gruber <rgruber@users.sourceforge.net>          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "cvsmainview.h"

#include <KLocale>
#include <KDebug>

#include "cvsplugin.h"
#include "cvsjob.h"
#include "cvsgenericoutputview.h"

CvsMainView::CvsMainView( CvsPlugin *plugin, QWidget* parent )
 : QWidget( parent ),
   Ui::CvsMainViewBase(),
   m_plugin(plugin)
{
    Ui::CvsMainViewBase::setupUi(this);
    setWindowTitle(i18n("CVS"));

    // CvsPlugin will notify when a job finished
    connect(m_plugin, SIGNAL(jobFinished(KJob*)),
            this, SLOT(slotJobFinished(KJob*)));

    // allow appending of new views
    connect(m_plugin, SIGNAL(addNewTabToMainView(QWidget*, QString)),
            this, SLOT(slotAddTab(QWidget*, QString)) );

    // create a default output view
    m_mainview = new CvsGenericOutputView(m_plugin);
    tabwidget->addTab( m_mainview, i18n("CVS") );

    // add a close button as corner widget
    m_closeButton = new QToolButton(tabwidget);
    m_closeButton->setIcon( KIcon( "tab-close" ) );
    m_closeButton->adjustSize();
    m_closeButton->setAutoRaise(true);
    m_closeButton->setEnabled(false);
    tabwidget->setCornerWidget( m_closeButton );
    connect(m_closeButton, SIGNAL( clicked() ),
            this, SLOT( slotTabClose() ));
}

CvsMainView::~CvsMainView()
{
    delete m_mainview;
}

void CvsMainView::slotAddTab(QWidget * tab, const QString& label)
{
    kDebug(9500) << "adding tab:" << label;

    int idx = tabwidget->addTab( tab, label );
    tabwidget->setCurrentIndex(idx);

    if (tabwidget->count() > 1)
        m_closeButton->setEnabled(true);
}

void CvsMainView::slotJobFinished(KJob * job)
{
    m_mainview->slotJobFinished(job);
    tabwidget->setCurrentIndex(0);
}

void CvsMainView::slotTabClose()
{
    int idx = tabwidget->currentIndex();

    // don't allow to close the first tab
    if (idx != 0)
        tabwidget->removeTab( idx );

    // if only the first tab remains, disable the close button
    if (tabwidget->count() <= 1)
        m_closeButton->setEnabled(false);
}

#include "cvsmainview.moc"
