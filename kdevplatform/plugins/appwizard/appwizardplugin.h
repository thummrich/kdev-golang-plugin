/***************************************************************************
 *   Copyright 2001 Bernd Gehrmann <bernd@kdevelop.org>                    *
 *   Copyright 2004-2005 Sascha Cunz <sascha@kdevelop.org>                 *
 *   Copyright 2007 Alexander Dymo <adymo@kdevelop.org>                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef _APPWIZARDPLUGIN_H_
#define _APPWIZARDPLUGIN_H_

#include <interfaces/iplugin.h>

#include <QHash>
#include <QVariant>

class ProjectTemplatesModel;
class ApplicationInfo;
class KArchiveDirectory;
class KTempDir;
class KUrl;

class AppWizardPlugin: public KDevelop::IPlugin {
    Q_OBJECT
public:
    AppWizardPlugin( QObject *parent, const QVariantList & = QVariantList() );
    ~AppWizardPlugin();
    virtual KDevelop::ContextMenuExtension contextMenuExtension(KDevelop::Context* context);

private slots:
    void slotNewProject();

private:
    QString createProject(const ApplicationInfo& );
    bool unpackArchive(const KArchiveDirectory *dir, const QString &dest);
    bool copyFileAndExpandMacros(const QString &source, const QString &dest);

    ProjectTemplatesModel *m_templatesModel;
    QAction* m_newFromTemplate;
    QHash<QString, QString> m_variables;
};

#endif

