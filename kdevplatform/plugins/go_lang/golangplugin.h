/*
*/

#ifndef GOLANGPLUGIN_H
#define GOLANGPLUGIN_H

#include <interfaces/iplugin.h>
#include <QVariantList>
#include <kservice.h>
#include <kurl.h>

class QSignalMapper;
namespace KDevelop
{
class ContextMenuExtension;
class Context;
}

class GoLangPlugin : public KDevelop::IPlugin
{
    Q_OBJECT
public:
    GoLangPlugin ( QObject* parent, const QVariantList& args  );
    virtual ~GoLangPlugin();
    virtual KDevelop::ContextMenuExtension contextMenuExtension ( KDevelop::Context* context );
private slots:
    void open( const QString& );
    void openDefault();
private:
    QList<QAction*> actionsForServices( const KService::List& list, KService::Ptr pref );
    QSignalMapper* actionMap;
    QList<KUrl> urls;
};

#endif // GOLANGPLUGIN_H
