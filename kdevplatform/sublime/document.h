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
#ifndef SUBLIMEDOCUMENT_H
#define SUBLIMEDOCUMENT_H

#include <QtCore/QObject>
#include <QtCore/QList>


#include "sublimeexport.h"

class QIcon;

class QWidget;

namespace Sublime {

class Area;
class View;
class Controller;

/**
@short Abstract base class for all Sublime documents

Subclass from Document and implement createViewWidget() method
to return a new widget for a view.
*/
class SUBLIME_EXPORT Document: public QObject {
    Q_OBJECT
public:
    /**Creates a document and adds it to a @p controller.*/
    Document(const QString &title, Controller *controller);
    ~Document();

    /**@return the new view for this document.
    @note it will not create a widget, just return a view object.*/
    View *createView();
    /**@return the list of all views in all areas for this document.*/
    const QList<View*> &views() const;

    /**@return the controller for this document.*/
    Controller *controller() const;

    /**@return the document title.*/
    QString title() const;
    /**Set the document title.*/
    void setTitle(const QString& newTitle);
    void setToolTip(const QString& newToolTip);
    QString toolTip() const;
    /**@return the type of document which can be written to config.*/
    virtual QString documentType() const = 0;

    /**@return the specifics of this document which can be written to config.*/
    virtual QString documentSpecifier() const = 0;

    /**Should try closing the document, eventually asking the user for feedback.
      *
      *If closing is successful, all views should be deleted, and the document itself
      *be scheduled for deletion using deleteLater().
      *
      *The default implementation will close all views and then deletes the document itself.
      *Override this if you want to confirm closing with the user.
      *
      * Returns whether closing was successful (The user did not push 'Cancel') */
    virtual bool closeDocument();

    void setStatusIcon(QIcon icon);
    
    QIcon statusIcon() const;
    
    // return true if the document has unique view. SameWorkingset is false when one view in two (or more) areas with same workingset is not unique
    bool uniqueView(Area *area, View *view, bool sameWorkingset = true);
    
Q_SIGNALS:
    /**Emitted when the view is added or deleted. Use Document::views to find out
    which views and how many of them are still there.*/
    void viewNumberChanged(Sublime::Document *doc);
    /**Emitted when the document is about to be deleted but is still in valid state.*/
    void aboutToDelete(Sublime::Document *doc);
    /**Emitted when the document's title is changed.*/
    void titleChanged(Sublime::Document *doc);
   /**Emitted when the document status-icon has changed */
    void statusIconChanged(Sublime::Document *doc);

protected:
    /**Creates and returns the new view. Reimplement in subclasses to instantiate
    views of derived from Sublime::View classes.*/
    virtual View *newView(Document *doc);
    /**Reimplement this to create and return the new widget to display
    this document in the view. This method is used by View class when it
    is asked for its widget.*/
    virtual QWidget *createViewWidget(QWidget *parent = 0) = 0;

private:
    Q_PRIVATE_SLOT(d, void removeView(QObject*))

    struct DocumentPrivate *const d;

    friend struct DocumentPrivate;
    friend class View;
};

}

#endif

