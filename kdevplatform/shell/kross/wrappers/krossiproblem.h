#ifndef KROSSIPROBLEM_H
#define KROSSIPROBLEM_H

#include<QtCore/QVariant>

//This is file has been generated by xmltokross, you should not edit this file but the files used to generate it.

namespace KDevelop { class ProblemData; }
namespace KDevelop { class Problem; }
namespace Handlers
{
	QVariant kDevelopProblemHandler(KDevelop::Problem* type);
	QVariant kDevelopProblemHandler(const KDevelop::Problem* type);

	QVariant kDevelopProblemDataHandler(KDevelop::ProblemData* type);
	QVariant kDevelopProblemDataHandler(const KDevelop::ProblemData* type);

}

#endif
