/***************************************************************************
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
#include "idocument.h"

#include "icore.h"
#include "idocumentcontroller.h"

namespace KDevelop {

class IDocumentPrivate
{
public:
    inline IDocumentPrivate(KDevelop::ICore *core)
        : m_core(core), scriptWrapper(0)
    {}

    KDevelop::ICore* m_core;
    QObject *scriptWrapper;

    /* Internal access to the wrapper script object */
    static inline QObject *&getWrapper(IDocument *doc)
    {
        return doc->d->scriptWrapper;
    }
};

/* This allows the scripting backend to register the scripting
   wrapper. Not beautiful, but makes sure it doesn't expand to much code.
*/
QObject *&getWrapper(IDocument *doc)
{
    return IDocumentPrivate::getWrapper(doc);
}

IDocument::IDocument( KDevelop::ICore* core )
  : d(new IDocumentPrivate(core))
{
}

IDocument::~IDocument()
{
    delete d->scriptWrapper;
    delete d;
}

KDevelop::ICore* IDocument::core()
{
    return d->m_core;
}

void IDocument::notifySaved()
{
    emit core()->documentController()->documentSaved(this);
}

void IDocument::notifyStateChanged()
{
    emit core()->documentController()->documentStateChanged(this);
}

void IDocument::notifyActivated()
{
    emit core()->documentController()->documentActivated(this);
}

void IDocument::notifyContentChanged()
{
    emit core()->documentController()->documentContentChanged(this);
}

bool IDocument::isTextDocument() const
{
    return false;
}

void IDocument::notifyTextDocumentCreated()
{
    emit core()->documentController()->textDocumentCreated(this);
}

KTextEditor::Range IDocument::textSelection() const
{
    return KTextEditor::Range::invalid();
}

QString IDocument::textLine() const
{
    return QString();
}

QString IDocument::textWord() const
{
    return QString();
}

void IDocument::notifyUrlChanged()
{
    emit core()->documentController()->documentUrlChanged(this);
}

void IDocument::notifyLoaded()
{
    emit core()->documentController()->documentLoadedPrepare(this);
    emit core()->documentController()->documentLoaded(this);
}

}

