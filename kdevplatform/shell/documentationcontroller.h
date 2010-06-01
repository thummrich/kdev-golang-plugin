/*
   Copyright 2009 Aleix Pol Gonzalez <aleixpol@kde.org>
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef DOCUMENTATIONCONTROLLER_H
#define DOCUMENTATIONCONTROLLER_H

#include <interfaces/idocumentationcontroller.h>
#include <QObject>

class DocumentationViewFactory;
class QAction;

namespace KDevelop
{

class IDocumentationProvider;
class Core;
class Context;
class ContextMenuExtension;

class DocumentationController : public QObject, public KDevelop::IDocumentationController
{
        Q_OBJECT
    public:
        DocumentationController(Core* core);
        
        void initialize();
        
        virtual QList<IDocumentationProvider*> documentationProviders() const;
        virtual KSharedPtr< KDevelop::IDocumentation > documentationForDeclaration(KDevelop::Declaration* declaration);
        virtual void showDocumentation(KSharedPtr< KDevelop::IDocumentation > doc);
        ContextMenuExtension contextMenuExtension( Context* context );
        
    private slots:
        void doShowDocumentation();
    private:
        DocumentationViewFactory* m_factory;

        QAction* m_showDocumentation;
};

}

#endif // DOCUMENTATIONCONTROLLER_H
