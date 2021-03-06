/***************************************************************************
 *   Copyright 2006 Adam Treat <treat@kde.org>                         *
 *   Copyright 2007 Alexander Dymo <adymo@kdevelop.org>             *
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
#include "languagecontroller.h"

#include <QHash>
#include <QMutexLocker>

#include <kmimetype.h>

#include <interfaces/idocument.h>
#include <interfaces/idocumentcontroller.h>
#include <interfaces/iplugin.h>
#include <interfaces/iplugincontroller.h>
#include <language/interfaces/ilanguagesupport.h>
#include <language/backgroundparser/backgroundparser.h>
#include <language/duchain/duchain.h>

#include "core.h"
#include "language.h"
#include "settings/ccpreferences.h"
#include "completionsettings.h"
#include <QThread>

namespace KDevelop {


typedef QHash<QString, ILanguage*> LanguageHash;
typedef QHash<QString, QList<ILanguage*> > LanguageCache;

struct LanguageControllerPrivate {
    LanguageControllerPrivate(LanguageController *controller)
        : dataMutex(QMutex::Recursive), backgroundParser(new BackgroundParser(controller)), m_cleanedUp(false), m_controller(controller) {}

    void documentActivated(KDevelop::IDocument *document)
    {
        KUrl url = document->url();
        if (!url.isValid()) {
            return;
        }

        foreach (ILanguage *lang, activeLanguages) {
            lang->deactivate();
        }

        activeLanguages.clear();

        QList<ILanguage*> languages = m_controller->languagesForUrl(url);
        foreach (ILanguage *lang, languages) {
            lang->activate();
            activeLanguages << lang;
        }
    }

    QList<ILanguage*> activeLanguages;

    mutable QMutex dataMutex;
    
    LanguageHash languages; //Maps language-names to languages
    LanguageCache languageCache; //Maps mimetype-names to languages
    typedef QMultiMap<KMimeType::Ptr, ILanguage*> MimeTypeCache;
    MimeTypeCache mimeTypeCache; //Maps mimetypes to languages

    BackgroundParser *backgroundParser;
    bool m_cleanedUp;
    
    ILanguage* addLanguageForSupport(ILanguageSupport* support);

private:
    LanguageController *m_controller;
};

ILanguage* LanguageControllerPrivate::addLanguageForSupport(KDevelop::ILanguageSupport* languageSupport) {

    if(languages.contains(languageSupport->name()))
        return languages[languageSupport->name()];

    Q_ASSERT(dynamic_cast<IPlugin*>(languageSupport));

    ILanguage* ret = new Language(languageSupport, m_controller);
    languages.insert(languageSupport->name(), ret);

    QVariant mimetypes = Core::self()->pluginController()->pluginInfo(dynamic_cast<IPlugin*>(languageSupport)).property("X-KDevelop-SupportedMimeTypes");

    foreach(const QString& mimeTypeName, mimetypes.toStringList()) {
        kDebug() << "adding supported mimetype:" << mimeTypeName << "language:" << languageSupport->name();
        languageCache[mimeTypeName] << ret;
        KMimeType::Ptr mime = KMimeType::mimeType(mimeTypeName);
        if(mime) {
            mimeTypeCache.insert(mime, ret);
        } else {
            kWarning() << "could not create mime-type" << mimeTypeName;
        }
    }

    return ret;
}

LanguageController::LanguageController(QObject *parent)
    : ILanguageController(parent)
{
    setObjectName("LanguageController");
    d = new LanguageControllerPrivate(this);
}

LanguageController::~LanguageController()
{
    delete d;
}

void LanguageController::initialize()
{
    // make sure the DUChain is setup before we try to access it from different threads at the same time
    DUChain::self();

    connect(Core::self()->documentController(), SIGNAL(documentActivated(KDevelop::IDocument*)),
            SLOT(documentActivated(KDevelop::IDocument*)));
}

void LanguageController::cleanup()
{
    QMutexLocker lock(&d->dataMutex);
    d->m_cleanedUp = true;
}

QList<ILanguage*> LanguageController::activeLanguages()
{
    QMutexLocker lock(&d->dataMutex);
    
    return d->activeLanguages;
}

ICompletionSettings *LanguageController::completionSettings() const {
    return &CompletionSettings::self();
}

QList<ILanguage*> LanguageController::loadedLanguages() const
{
    QMutexLocker lock(&d->dataMutex);
    QList<ILanguage*> ret;
    
    if(d->m_cleanedUp)
        return ret;
    
    foreach(ILanguage* lang, d->languages)
        ret << lang;
    return ret;
}

ILanguage *LanguageController::language(const QString &name) const
{
    QMutexLocker lock(&d->dataMutex);
    
    if(d->m_cleanedUp)
        return 0;
    
    if(d->languages.contains(name))
        return d->languages[name];
    else{
        ILanguage* ret = 0;
        QStringList constraints;
        constraints << QString("'%1' == [X-KDevelop-Language]").arg(name);
        QList<IPlugin*> supports = Core::self()->pluginController()->
            allPluginsForExtension("ILanguageSupport", constraints);
        if(!supports.isEmpty()) {
            ILanguageSupport *languageSupport = supports[0]->extension<ILanguageSupport>();
            if(supports[0])
                ret = d->addLanguageForSupport(languageSupport);
        }
        return ret;
    }
}

QList<ILanguage*> LanguageController::languagesForUrl(const KUrl &url)
{
    QMutexLocker lock(&d->dataMutex);
    
    QList<ILanguage*> languages;
    
    if(d->m_cleanedUp)
        return languages;
    
    QString fileName = url.fileName();

    ///TODO: cache regexp or simple string pattern for endsWith matching
    QRegExp exp("", Qt::CaseInsensitive, QRegExp::Wildcard);
    ///non-crashy part: Use the mime-types of known languages
    for(LanguageControllerPrivate::MimeTypeCache::const_iterator it = d->mimeTypeCache.constBegin();
        it != d->mimeTypeCache.constEnd(); ++it)
    {
        foreach(QString pattern, it.key()->patterns()) {
            if(pattern.startsWith('*')) {
                pattern = pattern.mid(1);
                if (!pattern.contains('*')) {
                    //optimize: we can skip the expensive QRegExp in this case
                    //and do a simple string compare (much faster)
                    if (fileName.endsWith(pattern)) {
                        languages << *it;
                    }
                    continue;
                }
            }

            exp.setPattern(pattern);
            if(int position = exp.indexIn(fileName)) {
                if(position != -1 && exp.matchedLength() + position == fileName.length())
                    languages << *it;
            }
        }
    }

    //Never use findByUrl from within a background thread, and never load a language support
    //from within the backgruond thread. Both is unsafe, and can lead to crashes
    if(!languages.isEmpty() || QThread::currentThread() != thread())
        return languages;

    ///Crashy and unsafe part: Load missing language-supports
    KMimeType::Ptr mimeType = KMimeType::findByUrl(url);

    LanguageCache::ConstIterator it = d->languageCache.constFind(mimeType->name());
    if (it != d->languageCache.constEnd()) {
        languages = it.value();
    } else {
        QStringList constraints;
        constraints << QString("'%1' in [X-KDevelop-SupportedMimeTypes]").arg(mimeType->name());
        QList<IPlugin*> supports = Core::self()->pluginController()->
            allPluginsForExtension("ILanguageSupport", constraints);

        if (supports.isEmpty()) {
            d->languageCache.insert(mimeType->name(), QList<ILanguage*>());
        } else {
            foreach (IPlugin *support, supports) {
                ILanguageSupport* languageSupport = support->extension<ILanguageSupport>();
                kDebug() << "language-support:" << languageSupport;
                if(languageSupport)
                    languages << d->addLanguageForSupport(languageSupport);
            }
        }
    }

    return languages;
}

BackgroundParser *LanguageController::backgroundParser() const
{
    return d->backgroundParser;
}

}

#include "languagecontroller.moc"

