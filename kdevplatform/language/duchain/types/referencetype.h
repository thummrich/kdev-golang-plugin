/* This file is part of KDevelop
    Copyright 2006 Roberto Raggi <roberto@kdevelop.org>
    Copyright 2006-2008 Hamish Rodda <rodda@kde.org>
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

#ifndef REFERENCETYPE_H
#define REFERENCETYPE_H

#include "abstracttype.h"

namespace KDevelop
{
class ReferenceTypeData;

/**
 * \short A type representing reference types.
 *
 * ReferenceType is used to represent types which hold a reference to a
 * variable.
 */
class KDEVPLATFORMLANGUAGE_EXPORT ReferenceType: public AbstractType
{
public:
  typedef TypePtr<ReferenceType> Ptr;

  /// Default constructor
  ReferenceType ();
  /// Copy constructor. \param rhs type to copy
  ReferenceType (const ReferenceType& rhs);
  /// Constructor using raw data. \param data internal data.
  ReferenceType(ReferenceTypeData& data);
  /// Destructor
  virtual ~ReferenceType();

  /**
   * Retrieve the referenced type, ie. what type of data this type references.
   *
   * \returns the base type.
   */
  AbstractType::Ptr baseType () const;

  /**
   * Sets the referenced type, ie. what type of data this type references.
   *
   * \param baseType the base type.
   */
  void setBaseType(AbstractType::Ptr baseType);

  virtual QString toString() const;

  virtual uint hash() const;

  virtual WhichType whichType() const;

  virtual AbstractType* clone() const;

  virtual bool equals(const AbstractType* rhs) const;

  virtual void exchangeTypes( TypeExchanger* exchanger );

  enum {
    Identity = 4
  };

  typedef ReferenceTypeData Data;

protected:
  virtual void accept0 (TypeVisitor *v) const;

  TYPE_DECLARE_DATA(ReferenceType)
};

template<>
inline ReferenceType* fastCast<ReferenceType*>(AbstractType* from) {
  if(!from || from->whichType() != AbstractType::TypeReference)
    return 0;
  else
    return static_cast<ReferenceType*>(from);
}

}

#endif // TYPESYSTEM_H

// kate: space-indent on; indent-width 2; tab-width 4; replace-tabs on; auto-insert-doxygen on
