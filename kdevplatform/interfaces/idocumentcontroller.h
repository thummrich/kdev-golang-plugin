/***************************************************************************
 *   Copyright 2007 Alexander Dymo <adymo@kdevelop.org>                    *
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
#ifndef IDOCUMENTCONTROLLER_H
#define IDOCUMENTCONTROLLER_H

#include <QtCore/QObject>

#include <kurl.h>
#include <ktexteditor/cursor.h>
#include <ktexteditor/range.h>

#include "interfacesexport.h"

#include "idocument.h"

namespace KDevelop {

class ICore;

class KDEVPLATFORMINTERFACES_EXPORT IDocumentFactory {
public:
    virtual ~IDocumentFactory() {}
    virtual IDocument* create(const KUrl&, ICore* ) = 0;
};

/**
 *
 * Allows to access the open documents and also open new ones
 *
 * @class IDocumentController
 */
class KDEVPLATFORMINTERFACES_EXPORT IDocumentController: public QObject {
    Q_OBJECT
public:
    enum DocumentActivation
    {
        DefaultMode = 0,            /**Activate document and create a view if no other flags passed.*/
        DoNotActivate = 1,          /**Don't activate the Document.*/
        DoNotCreateView = 2,         /**Don't create and show the view for the Document.*/
        DoNotForceCurrentView = 4 /**If the document is open in another existing view, switch to that one instead of opening it in the current one*/
    };
    Q_DECLARE_FLAGS(DocumentActivationParams, DocumentActivation)

    IDocumentController(QObject *parent);

    /**Call this before a call to @ref editDocument to set the encoding of the
    document to be opened.
    @param encoding The encoding to open as.*/
    Q_SCRIPTABLE virtual void setEncoding( const QString &encoding ) = 0;
    Q_SCRIPTABLE virtual QString encoding() const = 0;

    /**Finds the first document object corresponding to a given url.
    @param url The Url of the document.
    @return The corresponding document, or null if not found.*/
    Q_SCRIPTABLE virtual KDevelop::IDocument* documentForUrl( const KUrl & url ) const = 0;

    /**@return The list of open documents*/
    Q_SCRIPTABLE virtual QList<KDevelop::IDocument*> openDocuments() const = 0;

    /**Refers to the document currently active or focused.
    @return The active document.*/
    Q_SCRIPTABLE virtual KDevelop::IDocument* activeDocument() const = 0;

    Q_SCRIPTABLE virtual void activateDocument( KDevelop::IDocument * document, const KTextEditor::Range& range = KTextEditor::Range::invalid() ) = 0;

    virtual void registerDocumentForMimetype( const QString&, KDevelop::IDocumentFactory* ) = 0;

    Q_SCRIPTABLE virtual bool saveAllDocuments(KDevelop::IDocument::DocumentSaveMode mode = KDevelop::IDocument::Default) = 0;

    /** Opens a text document containing the @p data text. */
    Q_SCRIPTABLE virtual KDevelop::IDocument* openDocumentFromText( const QString& data ) = 0;

    virtual void notifyDocumentClosed(IDocument* doc) = 0;

    virtual IDocumentFactory* factory(const QString& mime) const = 0;
    
    Q_SCRIPTABLE virtual KTextEditor::Document* globalTextEditorInstance()=0;
public Q_SLOTS:
    /**Opens a new or existing document.
    @param url The full Url of the document to open.
    @param range The location information, if applicable.
    @param activate Indicates whether to fully activate the document.*/
    KDevelop::IDocument* openDocument( const KUrl &url,
            const KTextEditor::Cursor& cursor,
            DocumentActivationParams activationParams = 0,
            const QString& encoding = "");

    /**Opens a new or existing document.
    @param url The full Url of the document to open.
    @param range The range of text to select, if applicable.
    @param activate Indicates whether to fully activate the document.*/
    virtual KDevelop::IDocument* openDocument( const KUrl &url,
            const KTextEditor::Range& range = KTextEditor::Range::invalid(),
            DocumentActivationParams activationParams = 0,
            const QString& encoding = "") = 0;
    
    /** Opens a document from the IDocument instance.
    @param doc The IDocument to add
    @param range The location information, if applicable.
    @param activationParams Indicates whether to fully activate the document.*/
    virtual Q_SCRIPTABLE bool openDocument(IDocument* doc,
            const KTextEditor::Range& range = KTextEditor::Range::invalid(),
            DocumentActivationParams activationParams = 0) = 0;
            
    /**Opens a new or existing document.
    @param url The full Url of the document to open.
    @param prefName The name of the preferred KPart to open that document,*/
    virtual KDevelop::IDocument* openDocument( const KUrl &url, const QString& prefname ) = 0;

    virtual void closeAllDocuments() = 0;

Q_SIGNALS:
    /**Emitted when the document has been activated.*/
    void documentActivated( KDevelop::IDocument* document );

    ///Emitted whenever the active cursor jumps from one document+cursor to another, caused by a call to openDocument(..)
    ///This is also emitted when a document is only activated, then previousDocument is zero
    void documentJumpPerformed( KDevelop::IDocument* newDocument, KTextEditor::Cursor newCursor, KDevelop::IDocument* previousDocument, KTextEditor::Cursor previousCursor);
    
    /**Emitted when a document has been saved.*/
    void documentSaved( KDevelop::IDocument* document );

    /**Emitted when a document has been opened.
    The document may not be loaded from disk/network at this point.
    Note, no views exist for the document at the time this signal is emitted.*/
    void documentOpened( KDevelop::IDocument* document );

    /**Emitted when a document has been loaded.
    Note, no views exist for the document at the time this signal is emitted.*/
    void documentLoaded( KDevelop::IDocument* document );

    /**Emitted when a text document has been loaded, and the text document created.
    Note, no views exist for the document at the time this signal is emitted.*/
    void textDocumentCreated( KDevelop::IDocument* document );

    /**Emitted when a document has been closed.*/
    void documentClosed( KDevelop::IDocument* document );

    /**This is emitted when the document state(the relationship
     * between the file in the editor and the file stored on disk) changes.*/
    void documentStateChanged( KDevelop::IDocument* document );

    /**This is emitted when the document content changed.*/
    void documentContentChanged( KDevelop::IDocument* document );

    /**Emitted when a document has been loaded, but before documentLoaded(..) is emitted.
     * this allows parts of kdevplatform to prepare data-structures that can be used by other parts
     * during documentLoaded(..).*/
    void documentLoadedPrepare( KDevelop::IDocument* document );

    /**Emitted when a document url has changed.*/
    void documentUrlChanged( KDevelop::IDocument* document );

    friend class IDocument;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(IDocumentController::DocumentActivationParams)

}

#endif

