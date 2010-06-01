/*
  Copyright 2007 Roberto Raggi <roberto@kdevelop.org>
  Copyright 2007 Hamish Rodda <rodda@kde.org>

  Permission to use, copy, modify, distribute, and sell this software and its
  documentation for any purpose is hereby granted without fee, provided that
  the above copyright notice appear in all copies and that both that
  copyright notice and this permission notice appear in supporting
  documentation.

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  KDEVELOP TEAM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
  AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef SUBLIME_IDEAL_H
#define SUBLIME_IDEAL_H

#include <QtGui/QAction>
#include <QtGui/QActionEvent>
#include <QtGui/QToolButton>
#include <QtGui/QDockWidget>
#include <QtGui/QStyleOption>

#include "ideallayout.h"
#include "sublimedefs.h"

class KAction;
class KActionMenu;
class KActionCollection;
class KMenu;

namespace Sublime {

class Area;
class View;
class MainWindow;

class IdealToolButton: public QToolButton
{
    Q_OBJECT

public:
    IdealToolButton(Qt::DockWidgetArea area, QWidget *parent = 0);

    Qt::Orientation orientation() const;

    virtual QSize sizeHint() const;

protected:
    virtual void paintEvent(QPaintEvent *event);

private:
    Qt::DockWidgetArea _area;
};

class IdealButtonBarWidget: public QWidget
{
    Q_OBJECT

public:
    IdealButtonBarWidget(Qt::DockWidgetArea area, class IdealMainWidget *parent = 0);

    KAction *addWidget(const QString& title, IdealDockWidget *widget,
                       Area* area, View *view);
    void showWidget(IdealDockWidget* widget);
    void removeAction(QAction* action);

    IdealMainWidget* parentWidget() const;

    Qt::Orientation orientation() const;

    Qt::DockWidgetArea area() const;

    IdealDockWidget* widgetForAction(QAction* action) const;

    QWidget* corner();

private Q_SLOTS:
    void showWidget(bool checked);
    void anchor(bool anchor);
    void maximize(bool maximized);
    void actionToggled(bool state);

protected:
    virtual void actionEvent(QActionEvent *event);

private:
    Qt::DockWidgetArea _area;
    QHash<QAction *, IdealToolButton *> _buttons;
    QHash<QAction *, IdealDockWidget*> _widgets;
    QWidget *_corner;
};

class IdealDockWidget : public QDockWidget
{
    Q_OBJECT

public:
    IdealDockWidget(IdealMainWidget *parent);
    virtual ~IdealDockWidget();

    Area *area() const;
    void setArea(Area *area);

    View *view() const;
    void setView(View *view);

    Qt::DockWidgetArea dockWidgetArea() const;
    void setDockWidgetArea(Qt::DockWidgetArea dockingArea);

    bool isAnchored() const;
    void setAnchored(bool anchored, bool emitSignals);

    bool isMaximized() const;
    void setMaximized(bool maximized);

    virtual bool event(QEvent *event);

public slots:
    /// The IdealToolButton also connects to this slot to show the same context menu.
    void contextMenuRequested(const QPoint &point);

protected: // QWidget overrides
    virtual void mouseDoubleClickEvent(QMouseEvent *event);

Q_SIGNALS:
    void anchor(bool anchor);
    void maximize(bool maximize);
    void close();

private Q_SLOTS:
    void slotMaximize(bool maximized);
    void slotRemove();

private:
    Qt::Orientation m_orientation;
    QAbstractButton* m_anchor;
    QAbstractButton* m_close;
    Area *m_area;
    View *m_view;
    Qt::DockWidgetArea m_docking_area;
    bool m_maximized;
    IdealMainWidget *m_mainWidget;
};

class View;

class IdealMainWidget : public QWidget
{
    Q_OBJECT

public:
    IdealMainWidget(MainWindow* parent, KActionCollection* ac);

    // Public api
    void setCentralWidget(QWidget* widget);
    QAction* actionForView(View* view) const;
    void addView(Qt::DockWidgetArea area, View* View);
    void raiseView(View* view);
    /** Remove view.  If nondestructive true, view->widget()
        is not deleted, as is left with NULL parent.
        Otherwise, it's deleted.  */
    void removeView(View* view, bool nondestructive = false);
    void moveView(View *view, Qt::DockWidgetArea area);

    // Internal api

    // TODO can move the object filter here with judicious focusProxy?
    void centralWidgetFocused();

    void showDockWidget(IdealDockWidget* widget, bool show);
    void showDock(IdealMainLayout::Role role, bool show);

    void anchorDockWidget(IdealDockWidget* widget, bool anchor);

    IdealMainLayout* mainLayout() const;

    void anchorDockWidget(bool checked, IdealButtonBarWidget* bar);
    void maximizeDockWidget(bool checked, IdealButtonBarWidget* bar);

    QWidget* firstWidget(IdealMainLayout::Role role) const;

    IdealButtonBarWidget* barForRole(IdealMainLayout::Role role) const;
    IdealMainLayout::Role roleForBar(IdealButtonBarWidget* bar) const;
    KAction* actionForRole(IdealMainLayout::Role role) const;

    void setAnchorActionStatus(bool checked);
    void setMaximizeActionStatus(bool checked);
    void setShowDockStatus(IdealMainLayout::Role role, bool checked);

    QWidget *statusBarLocation() const;

Q_SIGNALS:
    void dockShown(Sublime::View*, Sublime::Position pos, bool shown);

    /// Emitted, when a context menu is requested on one of the dock bars.
    /// When no actions gets associated to the KMenu, it won't be shown.
    void dockBarContextMenuRequested(Qt::DockWidgetArea area, const QPoint& position);

public Q_SLOTS:
    void showLeftDock(bool show);
    void showRightDock(bool show);
    void showBottomDock(bool show);
    void showTopDock(bool show);
    void toggleDocksShown();
    void focusEditor();
    void anchorCurrentDock(bool anchor);
    void maximizeCurrentDock(bool maximized);
    void selectNextDock();
    void selectPreviousDock();
    void removeView();

private Q_SLOTS:
    /// Map context menu event from dockbar to area and propagate to main window.
    /// @see dockBarContextMenuRequested()
    void slotDockBarContextMenuRequested(const QPoint& position);

private:
    // helpers for toggleDocksShown
    bool allDocksHidden();
    bool someDockMaximized();
    void restorePreviouslyShownDocks();
    void hideAllShownDocks();

private:
    IdealButtonBarWidget *leftBarWidget;
    IdealButtonBarWidget *rightBarWidget;
    IdealButtonBarWidget *bottomBarWidget;
    IdealButtonBarWidget *topBarWidget;
    QWidget *bottomStatusBarLocation;

    KAction* m_showLeftDock;
    KAction* m_showRightDock;
    KAction* m_showBottomDock;
    KAction* m_showTopDock;
    KAction* m_anchorCurrentDock;
    KAction* m_maximizeCurrentDock;
    KActionMenu* m_docks;

    class IdealMainLayout* m_mainLayout;

    QMap<IdealDockWidget*, Qt::DockWidgetArea> docks;
    /** Map from View to an action that shows/hides
        the IdealDockWidget containing that view.  */
    QMap<View*, QAction*> m_view_to_action;
    /** Map from IdealDockWidget  to an action that shows/hides
        that IdealDockWidget.  */
    QMap<IdealDockWidget*, QAction*> m_dockwidget_to_action;

    bool m_centralWidgetFocusing;

    /** Tooldock views that were shown before hideAllDocks activated */
    QList<View*> m_previouslyShownDocks;
    bool m_switchingDocksShown;
};

class IdealSplitterHandle : public QWidget
{
    Q_OBJECT

public:
    IdealSplitterHandle(Qt::Orientation orientation, QWidget* parent, IdealMainLayout::Role resizeRole);

Q_SIGNALS:
    void resize(int thickness, IdealMainLayout::Role resizeRole);

protected:
    virtual void paintEvent(QPaintEvent* event);
    virtual void mouseMoveEvent(QMouseEvent* event);
    virtual void mousePressEvent(QMouseEvent* event);
    virtual void mouseReleaseEvent(QMouseEvent* event);
    virtual void enterEvent(QEvent* event);
    virtual void leaveEvent(QEvent* event);

private:
    inline int convert(const QPoint& pos) const { return m_orientation == Qt::Horizontal ? pos.y() : pos.x(); }

    Qt::Orientation m_orientation;
    bool m_hover;
    bool m_pressed;
    int m_dragStart;
    IdealMainLayout::Role m_resizeRole;
};

}

#endif
