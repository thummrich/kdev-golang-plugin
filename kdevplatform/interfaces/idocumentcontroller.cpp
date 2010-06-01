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
#include "idocumentcontroller.h"

namespace KDevelop {

IDocumentController::IDocumentController(QObject *parent)
    :QObject(parent)
{
}

KDevelop::IDocument* IDocumentController::openDocument( const KUrl &url,
        const KTextEditor::Cursor& cursor,
        DocumentActivationParams activationParams,
        const QString& encoding)
{
    return openDocument(url, cursor.isValid() ? KTextEditor::Range(cursor, 0) : KTextEditor::Range::invalid(), activationParams, encoding);
}

}

#include "idocumentcontroller.moc"

