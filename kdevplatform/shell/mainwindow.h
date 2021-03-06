/* This file is part of the KDevelop project
Copyright 2003 Falk Brettschneider <falkbr@kdevelop.org>
Copyright 2003 John Firebaugh <jfirebaugh@kde.org>
Copyright 2003 Amilcar do Carmo Lucas <amilcar@kdevelop.org>
Copyright 2004, 2007 Alexander Dymo <adymo@kdevelop.org>
Copyright 2006 Adam Treat <treat@kde.org>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public License
along with this library; see the file COPYING.LIB.  If not, write to
the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.
*/
#ifndef KDEV_MAINWINDOW_H
#define KDEV_MAINWINDOW_H

#include <KDE/KXmlGuiWindow>

#include "core.h"
#include <sublime/mainwindow.h>

#include "shellexport.h"

namespace KTextEditor { class View; }

namespace KDevelop
{

class Context;
class IDocument;

/**
KDevelop main window.
Provides methods to control the main window of an application.
*/
class KDEVPLATFORMSHELL_EXPORT MainWindow: public Sublime::MainWindow {
    friend class UiController;
    Q_OBJECT
public:
    explicit MainWindow( Sublime::Controller *parent = 0, Qt::WFlags flags = KDE_DEFAULT_WINDOWFLAGS );
    virtual ~MainWindow();

    /*! @p status must implement KDevelop::IStatus */
    void registerStatus(QObject* status);
    
    virtual QWidget* customButtonForAreaSwitcher( Sublime::Area* area );

public Q_SLOTS:
    /*! Shows an error message in the status bar.
        @p message The message
        @p timeout The timeout in milliseconds how long to show the message */
    void showErrorMessage(const QString& message, int timeout);
        
    
public Q_SLOTS:
    virtual void setVisible( bool visible );
    void configureShortcuts();

Q_SIGNALS:
    void finishedLoading();
    void contextMenu( KMenu *menu, const Context *context );

protected:
    //FIXME DOCUMENT!!!  queryClose() must call all of the Core cleanup() methods!
    virtual bool queryClose();
    virtual void setupAreaSelector();
    //reimplemented from KXMLGUIBuilder to support visible menubar separators
    QAction *createCustomElement(QWidget *parent, int index, const QDomElement &element);
    QWidget *createContainer(QWidget *parent, int index,
        const QDomElement &element, QAction*& containerAction);

public Q_SLOTS:
    virtual void loadSettings();
    virtual void saveSettings();
protected:
    virtual void initialize();
    virtual void cleanup();
    virtual void initializeStatusBar();
    void dragEnterEvent( QDragEnterEvent* );
    void dropEvent( QDropEvent* );
    virtual void applyMainWindowSettings ( const KConfigGroup& config, bool force = false );


protected Q_SLOTS:
    virtual void tabContextMenuRequested(Sublime::View* , KMenu* );
    virtual void tabToolTipRequested(Sublime::View* view, QPoint point);    
    virtual void dockBarContextMenuRequested(Qt::DockWidgetArea, const QPoint&);

private Q_SLOTS:
    void updateCaption();

private:
    class MainWindowPrivate *d;
    friend class MainWindowPrivate;
};

}

#endif

