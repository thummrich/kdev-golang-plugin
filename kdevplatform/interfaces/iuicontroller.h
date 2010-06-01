/***************************************************************************
 *   Copyright 2007 Alexander Dymo  <adymo@kdevelop.org>            *
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
#ifndef IUICONTROLLER_H
#define IUICONTROLLER_H

#include "interfacesexport.h"

#include <QtCore/QStringList>
#include <QtGui/QWidget>
#include <kurl.h>

class QAction;

template<class T>
class KSharedPtr;

namespace KParts {
    class MainWindow;
}
namespace Sublime{
    class Controller;
    class View;
}

namespace KDevelop {

class IDocument;
class IAssistant;

class KDEVPLATFORMINTERFACES_EXPORT IToolViewFactory {
public:
    virtual ~IToolViewFactory() {}
    /**
     * called to create a new widget for this toolview
     * @param parent the parent to use as parent for the widget
     * @returns the new widget for the toolview
     */
    virtual QWidget* create(QWidget *parent = 0) = 0;
    /** 
     * @returns the identifier of this toolview.  The identifier
     * is used to remember which areas the tool view should appear
     * in, and must never change.
     */
    virtual QString id() const = 0;
    /**
     * @returns the default position where this toolview should appear
     */
    virtual Qt::DockWidgetArea defaultPosition() = 0;
    /**
     * Fetch a list of actions to add to the toolbar of the toolview @p view
     * @param view the view to which the actions should be added
     * @returns a list of actions to be added to the toolbar
     */
    virtual QList<QAction*> toolBarActions( QWidget* viewWidget ) const { return viewWidget->actions(); }
    
    /**
     * called when a new view is created from this template
     * @param view the new sublime view that is being shown
     */
    virtual void viewCreated(Sublime::View* view);
};

/**
 *
 * Allows to access various parts of the user-interface, like the toolviews or the mainwindow
 */
class KDEVPLATFORMINTERFACES_EXPORT IUiController {
public:
    virtual ~IUiController();

    enum SwitchMode {
        ThisWindow /**< indicates that the area switch should be in the this window */,
        NewWindow  /**< indicates that the area switch should be using a new window */
    };

    virtual void switchToArea(const QString &areaName, SwitchMode switchMode) = 0;

    virtual void addToolView(const QString &name, IToolViewFactory *factory) = 0;
    virtual void removeToolView(IToolViewFactory *factory) = 0;
    
    enum FindFlags {
        None = 0,
        Create = 1, ///The tool-view is created if it doesn't exist in the current area yet
        Raise = 2,  ///The tool-view is raised if it was found/created
        CreateAndRaise = Create | Raise ///The tool view is created and raised
    };
    
    /**  Makes sure that this tool-view exists in the current area, raises it, and returns the contained widget
       * Returns zero on failure */
    virtual QWidget* findToolView(const QString& name, IToolViewFactory *factory, FindFlags flags = CreateAndRaise) = 0;

    /** @return active mainwindow or 0 if no such mainwindow is active.*/
    virtual KParts::MainWindow *activeMainWindow() = 0;

    /*! @p status must implement KDevelop::IStatus */
    virtual void registerStatus(QObject* status) = 0;

    /**
     * Shows an assistant popup at bottom within the current central widget
     * @p assistant the assistant that will be shown in a popup */
    virtual void popUpAssistant(const KSharedPtr<IAssistant>& assistant) = 0;

    /**
     * Hides the assistant if it is currently being shown
     */
    virtual void hideAssistant(const KSharedPtr<IAssistant>& assistant) = 0;
    
    /**
     * This is meant to be used by IDocument subclasses to initialize the
     * Sublime::Document.
     */
    virtual Sublime::Controller* controller() = 0;

    /** Shows an error message in the status bar.
      *
      * Unlike all other functions in this class, this function is thread-safe.
      * You can call it from the background.
      *
      * @p message The message
      * @p timeout The timeout in seconds how long to show the message */
    virtual void showErrorMessage(const QString& message, int timeout = 1) = 0;
        
protected:
    IUiController();
};

}

#endif

