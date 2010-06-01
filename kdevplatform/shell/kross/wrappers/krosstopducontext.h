#ifndef KROSSTOPDUCONTEXT_H
#define KROSSTOPDUCONTEXT_H

#include<QtCore/QVariant>

//This is file has been generated by xmltokross, you should not edit this file but the files used to generate it.

namespace KDevelop { class RecursiveImportRepository; }
namespace KDevelop { class ReferencedTopDUContext; }
namespace KDevelop { class TopDUContext; }
namespace KDevelop { class Cache; }
namespace Handlers
{
	QVariant kDevelopTopDUContextCacheHandler(KDevelop::TopDUContext::Cache* type);
	QVariant kDevelopTopDUContextCacheHandler(const KDevelop::TopDUContext::Cache* type);

	QVariant kDevelopTopDUContextHandler(KDevelop::TopDUContext* type);
	QVariant kDevelopTopDUContextHandler(const KDevelop::TopDUContext* type);

	QVariant kDevelopReferencedTopDUContextHandler(KDevelop::ReferencedTopDUContext* type);
	QVariant kDevelopReferencedTopDUContextHandler(const KDevelop::ReferencedTopDUContext* type);

	QVariant kDevelopRecursiveImportRepositoryHandler(KDevelop::RecursiveImportRepository* type);
	QVariant kDevelopRecursiveImportRepositoryHandler(const KDevelop::RecursiveImportRepository* type);

}

#endif