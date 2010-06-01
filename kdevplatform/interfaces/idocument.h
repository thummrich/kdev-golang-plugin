/***************************************************************************
 *   Copyright 2006 Hamish Rodda <rodda@kde.org>                           *
 *   Copyright 2007 Alexander Dymo  <adymo@kdevelop.org>                   *
 *   Copyright 2007 Andreas Pakulat <apaku@gmx.de>                         *
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
#ifndef IDOCUMENT_H
#define IDOCUMENT_H

#include <kurl.h>
#include <kmimetype.h>
#include <KDE/KTextEditor/Cursor>
#include <KDE/KTextEditor/Range>

#include "interfacesexport.h"
namespace KParts { class Part; class MainWindow; }
namespace KTextEditor { class Document; }
namespace Sublime{ class View; }

namespace KDevelop {
class ICore;

/**
 * A single document being edited by the IDE.
 *
 * The base class for tracking a document.  It contains the URL,
 * the part, and any associated metadata for the document.
 *
 * The advantages are:
 * - an easier key for use in maps and the like
 * - a value which does not change when the filename changes
 * - clearer distinction in the code between an open document and a url
 *   (which may or may not be open)
 */
class KDEVPLATFORMINTERFACES_EXPORT IDocument {
public:
    virtual ~IDocument();

    /**Document state.*/
    enum DocumentState
    {
        Clean,             /**< Document is not touched.*/
        Modified,          /**< Document is modified inside the IDE.*/
        Dirty,             /**< Document is modified by an external process.*/
        DirtyAndModified   /**< Document is modified inside the IDE and at the same time by an external process.*/
    };

    enum DocumentSaveMode
    {
        Default = 0x0 /**< standard save mode, gives a warning message if the file was modified outside the editor */,
        Silent = 0x1 /**< silent save mode, doesn't warn the user if the file was modified outside the editor */,
        Discard = 0x2 /**< discard mode, don't save any unchanged data */
    };

    /**
     * Returns the URL of this document.
     */
    virtual KUrl url() const = 0;

    /**
     * Returns the mimetype of the document.
     */
    virtual KMimeType::Ptr mimeType() const = 0;

    /**
     * Returns the part for given @p view if this document is a KPart document or 0 otherwise.
     */
    virtual KParts::Part* partForView(QWidget *view) const = 0;

    /**
     * Returns whether this document is a text document.
     */
    virtual bool isTextDocument() const;

    /**
     * Returns the text editor, if this is a text document or 0 otherwise.
     */
    virtual KTextEditor::Document* textDocument() const = 0;

    /**
     * Saves the document.
     * @return true if the document was saved, false otherwise
     */
    virtual bool save(DocumentSaveMode mode = Default) = 0;

    /**
     * Reloads the document.
     */
    virtual void reload() = 0;

    /**
     * Requests that the document be closed.
     *
     * \returns whether the document was successfully closed.
     */
    virtual bool close(DocumentSaveMode mode = Default) = 0;

    /**
     * Enquires whether this document is currently active in the currently active mainwindow.
     */
    virtual bool isActive() const = 0;

    /**
    * Checks the state of this document.
    * @return The document state.
    */
    virtual DocumentState state() const = 0;

    /**
     * Access the current text cursor position, if possible.
     *
     * \returns the current text cursor position, or an invalid cursor otherwise.
     */
    virtual KTextEditor::Cursor cursorPosition() const = 0;

    /**
     * Set the current text cursor position, if possible.
     *
     * \param cursor new cursor position.
     */
    virtual void setCursorPosition(const KTextEditor::Cursor &cursor) = 0;

    /**
     * Retrieve the current text selection, if one exists.
     *
     * \returns the current text selection
     */
    virtual KTextEditor::Range textSelection() const;

    /**
     * Set the current text selection, if possible.
     *
     * \param range new cursor position.
     */
    virtual void setTextSelection(const KTextEditor::Range &range) = 0;

    /**
     * Retrieve the current text line, if one exists.
     *
     * @returns the current text line
     */
    virtual QString textLine() const;

    /**
     * Retrieve the current text word, if one exists.
     *
     * @returns the current text word
     */
    virtual QString textWord() const;

    /**
     * Performs document activation actions if any.
     * This needs to call notifyActivated()
     */
    virtual void activate(Sublime::View *activeView, KParts::MainWindow *mainWindow) = 0;

protected:
    ICore* core();
    IDocument( ICore* );
    void notifySaved();
    void notifyStateChanged();
    void notifyActivated();
    void notifyContentChanged();
    void notifyTextDocumentCreated();
    void notifyUrlChanged();
    void notifyLoaded();

private:
    friend class IDocumentPrivate;
    class IDocumentPrivate* const d;
};

}

#endif

