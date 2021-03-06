/***************************************************************************
 *   Copyright 2006 Adam Treat  <treat@kde.org>                     *
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

#include "partcontroller.h"
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <QFile>
#include <QTimer>
#include <QMutexLocker>

#include <kdebug.h>
#include <klocale.h>
#include <kmimetype.h>
#include <kmimetypetrader.h>

#include <kparts/part.h>
#include <kparts/factory.h>
#include <kparts/partmanager.h>
#include <kparts/browserextension.h>

#include <ktexteditor/view.h>
#include <ktexteditor/editor.h>
#include <ktexteditor/document.h>
#include <ktexteditor/factory.h>
#include <ktexteditor/smartinterface.h>

#include "core.h"

namespace KDevelop
{

class PartControllerPrivate
{
public:
    PartControllerPrivate(): m_textEditor(0) {}

    QString m_editor;
    QStringList m_textTypes;

    Core *m_core;

    KTextEditor::Editor *m_textEditor;

};

PartController::PartController(Core *core, QWidget *toplevel)
        : IPartController( toplevel ), d(new PartControllerPrivate)

{
    setObjectName("PartController");
    d->m_core = core;
    //Cache this as it is too expensive when creating parts
    //     KConfig * config = Config::standard();
    //     config->setGroup( "General" );
    //
    //     d->m_textTypes = config->readEntry( "TextTypes", QStringList() );
    //
    //     config ->setGroup( "Editor" );
    //     d->m_editor = config->readPathEntry( "EmbeddedKTextEditor", QString() );
}

PartController::~PartController()
{
    delete d;
}

//MOVE BACK TO DOCUMENTCONTROLLER OR MULTIBUFFER EVENTUALLY
bool PartController::isTextType( KMimeType::Ptr mimeType )
{
    bool isTextType = false;
    if ( d->m_textTypes.contains( mimeType->name() ) )
    {
        isTextType = true;
    }

    bool isKDEText = false;
    QVariant v = mimeType->property( "X-KDE-text" );
    if ( v.isValid() )
        isKDEText = v.toBool();

    // is this regular text - open in editor
    return ( isTextType || isKDEText
             || mimeType->is( "text/plain" )
             || mimeType->is( "text/html" )
             || mimeType->is( "application/x-zerosize" ) );
}

KTextEditor::Document* PartController::createTextPart(const QString &encoding)
{
    if (!d->m_textEditor)
    {
        KTextEditor::Factory * editorFactory = qobject_cast<KTextEditor::Factory*>(findPartFactory(
            "text/plain",
            "KTextEditor/Document",
            "KTextEditor::Editor" ));

        //TODO: turn into KMessageBox with translatable error message
        Q_ASSERT_X(editorFactory, "PartController::createTextPart", "could not find KTextEditor::Factory, check your installation");

        d->m_textEditor = editorFactory->editor();
    }
    KTextEditor::Document* doc = d->m_textEditor->createDocument(this);

    if ( !encoding.isNull() )
    {
        KParts::OpenUrlArguments args = doc->arguments();
        args.setMimeType( QString::fromLatin1( "text/plain;" ) + encoding );
        doc->setArguments( args );
    }

    return doc;
}

void PartController::removePart( KParts::Part *part )
{
#if 0
    if ( KTextEditor::Document * doc = qobject_cast<KTextEditor::Document *>( part ) )
    {
        if ( KTextEditor::SmartInterface * smart = dynamic_cast<KTextEditor::SmartInterface*>( doc ) )
        {
            // FIXME not supposed to delete locked mutexes...
            QMutexLocker lock ( smart->smartMutex() );
            KParts::PartManager::removePart( part );
            return ;
        }

        kWarning() << "Deleting text editor" << doc << "which does not have a smart interface." ;
    }
#endif

    KParts::PartManager::removePart( part );
}

KParts::Part* PartController::createPart( const QString & mimeType,
        const QString & partType,
        const QString & className,
        const QString & preferredName )
{
    KParts::Factory * editorFactory = findPartFactory(
                                          mimeType,
                                          partType,
                                          preferredName );

    if ( !className.isEmpty() && editorFactory )
    {
        return editorFactory->createPart(
                   0,
                   this,
                   className.toLatin1() );
    }

    return 0;
}

bool PartController::canCreatePart(const KUrl& url)
{
    if (!url.isValid()) return false;

    QString mimeType;
    if ( url.isEmpty() )
        mimeType = QString::fromLatin1("text/plain");
    else
        mimeType = KMimeType::findByUrl( url )->name();

    KService::List offers = KMimeTypeTrader::self()->query(
                                mimeType,
                                "KParts/ReadOnlyPart" );

    return offers.count() > 0;
}

KParts::Part* PartController::createPart( const KUrl & url, const QString& preferredPart )
{
    kDebug() << "creating part with url" << url << "and pref part:" << preferredPart;
    QString mimeType;
    if ( url.isEmpty() )
        //create a part for empty text file
        mimeType = QString::fromLatin1("text/plain");
    else if ( !url.isValid() )
        return 0;
    else
        mimeType = KMimeType::findByUrl( url )->name();

    KParts::Part* part = createPart( mimeType, preferredPart );
    if( part )
    {
        readOnly( part ) ->openUrl( url );
        return part;
    }

    return 0;
}

KParts::ReadOnlyPart* PartController::activeReadOnly() const
{
    return readOnly( activePart() );
}

KParts::ReadWritePart* PartController::activeReadWrite() const
{
    return readWrite( activePart() );
}

KParts::ReadOnlyPart* PartController::readOnly( KParts::Part * part ) const
{
    return qobject_cast<KParts::ReadOnlyPart*>( part );
}

KParts::ReadWritePart* PartController::readWrite( KParts::Part * part ) const
{
    return qobject_cast<KParts::ReadWritePart*>( part );
}

void PartController::loadSettings( bool projectIsLoaded )
{
    Q_UNUSED( projectIsLoaded );
}

void PartController::saveSettings( bool projectIsLoaded )
{
    Q_UNUSED( projectIsLoaded );
}

void PartController::initialize()
{}

void PartController::cleanup()
{}

}
#include "partcontroller.moc"

