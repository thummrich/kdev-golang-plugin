/***************************************************************************
 *   Copyright 2006-2007 Alexander Dymo  <adymo@kdevelop.org>       *
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
#include "mainwindow.h"
#include "mainwindow_p.h"

#include <KDE/KDebug>
#include <KDE/KGlobal>
#include <KDE/KConfig>
#include <KDE/KSharedConfig>
#include <KDE/KConfigGroup>
#include <KDE/KToolBar>
#include <KDE/KWindowSystem>

#include <QtGui/QApplication>
#include <QtGui/QDesktopWidget>
#include <KDE/KStatusBar>
#include <KDE/KMenuBar>

#include "area.h"
#include "view.h"
#include "controller.h"
#include "container.h"
#include "ideal.h"

namespace Sublime {

MainWindow::MainWindow(Controller *controller, Qt::WindowFlags flags)
: KParts::MainWindow(0, flags), d(new MainWindowPrivate(this, controller))
{
    connect(this, SIGNAL(destroyed()), controller, SLOT(areaReleased()));

    loadGeometry(KGlobal::config()->group("Main Window"));
    d->areaSwitcher = new AreaTabWidget(menuBar());
    menuBar()->setCornerWidget(d->areaSwitcher, Qt::TopRightCorner);
}

QWidget* MainWindow::customButtonForAreaSwitcher ( Area* /*area*/ )
{
    return 0;
}

bool MainWindow::containsView(View* view) const
{
    foreach(Area* area, areas())
        if(area->views().contains(view))
            return true;
    return false;
}

QList< Area* > MainWindow::areas() const
{
    QList< Area* > areas = controller()->areas(const_cast<MainWindow*>(this));
    if(areas.isEmpty())
        areas = controller()->defaultAreas();

    return areas;
}

void MainWindow::setupAreaSelector() {
    disconnect(d->areaSwitcher->tabBar, SIGNAL(currentChanged(int)), d, SLOT(toggleArea(int)));
    
    d->areaSwitcher->setUpdatesEnabled(false);
    
    d->areaSwitcher->tabBar->clearTabs();
    
    int currentIndex = -1;
    
    QList< Area* > areas = this->areas();
    
    QSet<QString> hadAreaName;
    
    for(int a = 0; a < areas.size(); ++a) {
        Area* theArea = areas[a];
        
        if(hadAreaName.contains(theArea->objectName()))
            continue;
        
        hadAreaName.insert(theArea->objectName());
        
        if(theArea->objectName() == area()->objectName()) {
            currentIndex = a;
        }
        
        d->areaSwitcher->tabBar->addCustomTab(theArea->title(), KIcon(theArea->iconName()), currentIndex == a, theArea->objectName(), customButtonForAreaSwitcher(theArea));
    }
    
    d->areaSwitcher->tabBar->setCurrentIndex(currentIndex);
    
    d->areaSwitcher->updateGeometry();
    
    d->areaSwitcher->setUpdatesEnabled(true);
    
    connect(d->areaSwitcher->tabBar, SIGNAL(currentChanged(int)), d, SLOT(toggleArea(int)));
}

MainWindow::~MainWindow()
{
    kDebug() << "destroying mainwindow";
    delete d;
}

void MainWindow::setArea(Area *area)
{
    if (d->area)
        disconnect(d->area, 0, this, 0);
    
    bool differentArea = (area != d->area);
    /* All views will be removed from dock area now.  However, this does
       not mean those are removed from area, so prevent slotDockShown
       from recording those views as no longer shown in the area.  */
    d->ignoreDockShown = true;
    
    if (d->autoAreaSettingsSave && differentArea)
        saveSettings();

    if (d->area)
        clearArea();

    d->area = area;
    d->reconstruct();
    
    if(d->area->activeView())
        activateView(d->area->activeView());
    else
        d->activateFirstVisibleView();
    
    initializeStatusBar();
    emit areaChanged(area);
    d->ignoreDockShown = false;
    
    loadSettings();
        
    connect(area, SIGNAL(viewAdded(Sublime::AreaIndex*, Sublime::View*)),
        this, SLOT(viewAdded(Sublime::AreaIndex*, Sublime::View*)));
    connect(area, SIGNAL(viewRemoved(Sublime::AreaIndex*,Sublime::View*)),
        this, SLOT(viewRemovedInternal(Sublime::AreaIndex*, Sublime::View*)));
    connect(area, SIGNAL(requestToolViewRaise(Sublime::View*)),
        this, SLOT(raiseToolView(Sublime::View*)));
    connect(area, SIGNAL(aboutToRemoveView(Sublime::AreaIndex*, Sublime::View*)),
        this, SLOT(aboutToRemoveView(Sublime::AreaIndex*, Sublime::View*)));
    connect(area, SIGNAL(toolViewAdded(Sublime::View*, Sublime::Position)),
        this, SLOT(toolViewAdded(Sublime::View*, Sublime::Position)));
    connect(area, SIGNAL(aboutToRemoveToolView(Sublime::View*, Sublime::Position)),
        this, SLOT(aboutToRemoveToolView(Sublime::View*, Sublime::Position)));
    connect(area, SIGNAL(toolViewMoved(Sublime::View*, Sublime::Position)),
        this, SLOT(toolViewMoved(Sublime::View*, Sublime::Position)));
     connect(area, SIGNAL(changedWorkingSet(Sublime::Area*,QString,QString)),
        this, SLOT(setupAreaSelector()));
}

void MainWindow::initializeStatusBar()
{
    //nothing here, reimplement in the subclasses if you want to have status bar
    //inside the bottom toolview buttons row
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    return KParts::MainWindow::resizeEvent(event);
}

void MainWindow::clearArea()
{
    emit areaCleared(d->area);
    d->clearArea();
}

QList<View*> MainWindow::toolDocks() const
{
    return d->docks;
}

Area *Sublime::MainWindow::area() const
{
    return d->area;
}

Controller *MainWindow::controller() const
{
    return d->controller;
}

View *MainWindow::activeView()
{
    return d->activeView;
}

View *MainWindow::activeToolView()
{
    return d->activeToolView;
}

void MainWindow::activateView(View *view)
{
    if (!d->viewContainers.contains(view))
        return;
    
    d->viewContainers[view]->setCurrentWidget(view->widget());

    setActiveView(view);
    d->area->setActiveView(view);
}

void MainWindow::setActiveView(View *view)
{
    View* oldActiveView = d->activeView;
    
    d->activeView = view;
    
    if (view && !view->widget()->hasFocus())
        view->widget()->setFocus();
    
    if(d->activeView != oldActiveView)
        emit activeViewChanged(view);
}

void Sublime::MainWindow::setActiveToolView(View *view)
{
    d->activeToolView = view;
    emit activeToolViewChanged(view);
}

void MainWindow::saveSettings()
{
    QString group = "MainWindow";
    if (area())
        group += '_' + area()->objectName();
    KConfigGroup cg = KGlobal::config()->group(group);
    /* This will try to save window size, too.  But it's OK, since we
       won't use this information when loading.  */
    saveMainWindowSettings(cg);

    //debugToolBar visibility is stored separately to allow a area dependent default value
    foreach (KToolBar* toolbar, toolBars()) {
        if (toolbar->objectName() == "debugToolBar") {
            cg.writeEntry("debugToolBarVisibility", toolbar->isVisible());
        }
    }

    cg.sync();
}

void MainWindow::loadSettings()
{
    kDebug() << "loading settings for " << (area() ? area()->objectName() : "");
    QString group = "MainWindow";
    if (area())
        group += '_' + area()->objectName();
    KConfigGroup cg = KGlobal::config()->group(group);

    // What follows is copy-paste from applyMainWindowSettings.  Unfortunately,
    // we don't really want that one to try restoring window size, and we also
    // cannot stop it from doing that in any clean way.
    QStatusBar* sb = qFindChild<KStatusBar *>(this);
    if (sb) {
        QString entry = cg.readEntry("StatusBar", "Enabled");
        if ( entry == "Disabled" )
           sb->hide();
        else
           sb->show();
    }

    QMenuBar* mb = qFindChild<KMenuBar *>(this);
    if (mb) {
        QString entry = cg.readEntry ("MenuBar", "Enabled");
        if ( entry == "Disabled" )
           mb->hide();
        else
           mb->show();
    }

    if ( !autoSaveSettings() || cg.name() == autoSaveGroup() ) {
        QString entry = cg.readEntry ("ToolBarsMovable", "Enabled");
        if ( entry == "Disabled" )
            KToolBar::setToolBarsLocked(true);
        else
            KToolBar::setToolBarsLocked(false);
    }

    // Utilise the QMainWindow::restoreState() functionality
    // Note that we're fixing KMainWindow bug here -- the original
    // code has this fragment above restoring toolbar properties.
    // As result, each save/restore would move the toolbar a bit to
    // the left.
    if (cg.hasKey("State")) {
        QByteArray state;
        state = cg.readEntry("State", state);
        state = QByteArray::fromBase64(state);
        // One day will need to load the version number, but for now, assume 0
        restoreState(state);
    } else {
        // If there's no state we use a default size of 870x650
        // Resize only when showing "code" area. If we do that for other areas,
        // then we'll hit bug https://bugs.kde.org/show_bug.cgi?id=207990
        // TODO: adymo: this is more like a hack, we need a proper first-start initialization
        if (area() && area()->objectName() == "code")
            resize(870,650);
    }

    int n = 1; // Toolbar counter. toolbars are counted from 1,
    foreach (KToolBar* toolbar, toolBars()) {
        QString group("Toolbar");
        // Give a number to the toolbar, but prefer a name if there is one,
        // because there's no real guarantee on the ordering of toolbars
        group += (toolbar->objectName().isEmpty() ? QString::number(n) : QString(" ")+toolbar->objectName());

        KConfigGroup toolbarGroup(&cg, group);
        toolbar->applySettings(toolbarGroup, false);

        if (toolbar->objectName() == "debugToolBar") {
            //debugToolBar visibility is stored separately to allow a area dependent default value
            bool visibility = cg.readEntry("debugToolBarVisibility", area()->objectName() == "debug");
            toolbar->setVisible(visibility);
        }
        n++;
    }

    KConfigGroup uiGroup = KGlobal::config()->group("UiSettings");
    foreach (Container *container, findChildren<Container*>())
    {
        container->setTabBarHidden(uiGroup.readEntry("TabBarVisibility", 1) == 0);
        container->setOpenAfterCurrent(uiGroup.readEntry("TabBarOpenAfterCurrent", 1) == 1);
    }

    cg.sync();

    emit settingsLoaded();
}

bool MainWindow::queryClose()
{
//    saveSettings();
    KConfigGroup config(KGlobal::config(), "Main Window");
    saveGeometry(config);
    config.sync();
    
    return KParts::MainWindow::queryClose();
}

void MainWindow::saveGeometry(KConfigGroup &config)
{
    int scnum = QApplication::desktop()->screenNumber(parentWidget());
    QRect desk = QApplication::desktop()->screenGeometry(scnum);

    // if the desktop is virtual then use virtual screen size
    if (QApplication::desktop()->isVirtualDesktop())
        desk = QApplication::desktop()->screenGeometry(QApplication::desktop()->screen());

    QString key = QString::fromLatin1("Desktop %1 %2")
        .arg(desk.width()).arg(desk.height());
    config.writeEntry(key, geometry());

}
void MainWindow::loadGeometry(const KConfigGroup &config)
{
    // The below code, essentially, is copy-paste from
    // KMainWindow::restoreWindowSize.  Right now, that code is buggy,
    // as per http://permalink.gmane.org/gmane.comp.kde.devel.core/52423
    // so we implement a less theoretically correct, but working, version
    // below
    const int scnum = QApplication::desktop()->screenNumber(parentWidget());
    QRect desk = QApplication::desktop()->screenGeometry(scnum);

    // if the desktop is virtual then use virtual screen size
    if (QApplication::desktop()->isVirtualDesktop())
        desk = QApplication::desktop()->screenGeometry(QApplication::desktop()->screen());

    QString key = QString::fromLatin1("Desktop %1 %2")
        .arg(desk.width()).arg(desk.height());
    QRect g = config.readEntry(key, QRect());
    if (!g.isEmpty())
        setGeometry(g);
}

void MainWindow::enableAreaSettingsSave()
{
    d->autoAreaSettingsSave = true;
}

QWidget *MainWindow::statusBarLocation()
{
    return d->idealMainWidget->statusBarLocation();
}

void MainWindow::setAreaSwitcherCornerWidget(QWidget* widget)
{
    d->areaSwitcher->setTabSideWidget(widget);
}

void MainWindow::setTabBarLeftCornerWidget(QWidget* widget)
{
    d->setTabBarLeftCornerWidget(widget);
}

void MainWindow::tabContextMenuRequested(View* , KMenu* )
{
    // do nothing
}

void MainWindow::tabToolTipRequested(View*, QPoint)
{
    // do nothing
}

void MainWindow::dockBarContextMenuRequested(Qt::DockWidgetArea , const QPoint& )
{
    // do nothing
}

View* MainWindow::viewForPosition(QPoint globalPos) const
{
    foreach(Container* container, d->viewContainers.values())
    {
        QRect globalGeom = QRect(container->mapToGlobal(QPoint(0,0)), container->mapToGlobal(QPoint(container->width(), container->height())));
       if(globalGeom.contains(globalPos))
       {
           return d->widgetToView[container->currentWidget()];
       }
    }
    
    return 0;
}

}

#include "mainwindow.moc"
