/***************************************************************************
 *   This file is part of KDevelop                                         *
 *   Copyright 2008 David Nolden <david.nolden.kdevelop@art-master.de>     *
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

#ifndef SIMPLEREFACTORING_H
#define SIMPLEREFACTORING_H

#include <interfaces/contextmenuextension.h>
#include <interfaces/context.h>

namespace KDevelop {
  class IndexedDeclaration;
}

class SimpleRefactoring : public QObject {
  Q_OBJECT

public:
  static SimpleRefactoring& self();
  void doContextMenu(KDevelop::ContextMenuExtension& extension, KDevelop::Context* context);

  void startInteractiveRename(KDevelop::IndexedDeclaration decl);
  void createNewClass(KDevelop::ProjectBaseItem* item);
  
public slots:
  void executeRenameAction();
  void executeNewClassAction();
  void executeMoveIntoSourceAction();
  private:
    ///Duchain does not need to be read-locked
    ///If @p allowUse is false, a declaration that is declared in the current line is returned(if one exists)
    KDevelop::IndexedDeclaration declarationUnderCursor(bool allowUse = true);
};

#endif
