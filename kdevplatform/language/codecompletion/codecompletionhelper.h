/*
   Copyright 2007-2008 David Nolden <david.nolden.kdevelop@art-master.de>

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
#ifndef KDEV_CODECOMPLETIONHELPER_H
#define KDEV_CODECOMPLETIONHELPER_H

#include "../languageexport.h"
#include "../duchain/duchainpointer.h"
#include "../duchain/declaration.h"

namespace KTextEditor {
  class Document;
  class Range;
}
namespace KDevelop {
  class Declaration;

  void KDEVPLATFORMLANGUAGE_EXPORT insertFunctionParenText(KTextEditor::Document* document, const KTextEditor::Range& word, DeclarationPointer declaration, bool jumpForbidden = false);

}
#endif
