/* This file is part of KDevelop
    Copyright 2006 Hamish Rodda <rodda@kde.org>
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

#ifndef TOPDUCONTEXTUTILS_H
#define TOPDUCONTEXTUTILS_H

#include "topducontext.h"

namespace KDevelop
{
/// \todo move data to private d pointer classes
struct KDEVPLATFORMLANGUAGE_EXPORT TopDUContext::DeclarationChecker
{
  DeclarationChecker(const TopDUContext* _top, const SimpleCursor& _position, const AbstractType::Ptr& _dataType, DUContext::SearchFlags _flags, KDevVarLengthArray<IndexedDeclaration>* _createVisibleCache = 0);
  bool operator()(const Declaration* dec) const;

  mutable KDevVarLengthArray<IndexedDeclaration>* createVisibleCache;
  const TopDUContext* top;
  const TopDUContextData* topDFunc;
  const SimpleCursor& position;
  const AbstractType::Ptr& dataType;
  DUContext::SearchFlags flags;
};

}

#endif // TOPDUCONTEXTUTILS_H

// kate: space-indent on; indent-width 2; tab-width 4; replace-tabs on; auto-insert-doxygen on
