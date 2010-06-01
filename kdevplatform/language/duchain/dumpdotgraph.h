/***************************************************************************
   Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>
***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef DUMPDOTGRAPH_H
#define DUMPDOTGRAPH_H

#include <QtCore/QMap>
#include <QtCore/QString>
#include <kurl.h>
#include "../languageexport.h"

namespace KDevelop {
  class TopDUContext;
  class DUContext;
  /**
   * A helper-class for debugging, that nicely visualizes the whole structure of a du-context.
   * */
  class KDEVPLATFORMLANGUAGE_EXPORT DumpDotGraph {
      Q_DISABLE_COPY(DumpDotGraph)
    public:
      DumpDotGraph();
      ~DumpDotGraph();
    /**
     * The context, it's, and if it is not a top-context also all contexts importing it we be drawn.
     * Parent-contexts will not be respected, so if you want the whole structure, you will need to pass the top-context.
     * @param shortened if this is given sub-items like declarations, definitions, child-contexts, etc. will not be shown as separate nodes
     * @param isMaster must always be true when called from outside
     * @param allFiles is for internal use only.
     * */
      QString dotGraph(KDevelop::DUContext* context, bool shortened = false);

    private:
      class DumpDotGraphPrivate* const d;
  };
}

#endif // DUMPDOTGRAPH_H
