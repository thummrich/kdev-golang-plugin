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

#ifndef DELAYEDTYPE_H
#define DELAYEDTYPE_H

#include "abstracttype.h"
#include "../identifier.h"

namespace KDevelop
{
class DelayedTypeData;

/**
 * \short A type which has not yet been resolved.
 *
 * Delayed types can be used for any types that cannot be resolved in the moment they are encountered.
 * They can be used for example in template-classes, or to store the names of unresolved types.
 * In a template-class, many types can not be evaluated at the time they are used, because they depend on unknown template-parameters.
 * Delayed types store the way the type would be searched, and can be used to find the type once the template-paremeters have values.
 * */
class KDEVPLATFORMLANGUAGE_EXPORT DelayedType : public KDevelop::AbstractType
{
public:
  typedef TypePtr<DelayedType> Ptr;

  /// An enumeration of
  enum Kind {
    Delayed /**< The type should be resolved later. This is the default. */,
    Unresolved /**< The type could not be resolved */
  };

  /// Default constructor
  DelayedType();
  /// Copy constructor. \param rhs type to copy
  DelayedType(const DelayedType& rhs);
  /// Constructor using raw data. \param data internal data.
  DelayedType(DelayedTypeData& data);
  /// Destructor
  virtual ~DelayedType();

  /**
   * Access the type identifier which this type represents.
   *
   * \returns the type identifier.
   */
  KDevelop::IndexedTypeIdentifier identifier() const;

  /**
   * Set the type identifier which this type represents.
   *
   * \param identifier the type identifier.
   */
  void setIdentifier(const KDevelop::IndexedTypeIdentifier& identifier);

  virtual QString toString() const;

  virtual AbstractType* clone() const;

  virtual bool equals(const AbstractType* rhs) const;

  Kind kind() const;
  void setKind(Kind kind);

  virtual uint hash() const;

  virtual WhichType whichType() const;

  enum {
    Identity = 8
  };

  typedef DelayedTypeData Data;

  protected:
    virtual void accept0 (KDevelop::TypeVisitor *v) const ;
    TYPE_DECLARE_DATA(DelayedType)
};

template<>
inline DelayedType* fastCast<DelayedType*>(AbstractType* from) {
  if(!from || from->whichType() != AbstractType::TypeDelayed)
    return 0;
  else
    return static_cast<DelayedType*>(from);
}

}

#endif

// kate: space-indent on; indent-width 2; tab-width 4; replace-tabs on; auto-insert-doxygen on
