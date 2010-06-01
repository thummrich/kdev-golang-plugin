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

#ifndef FUNCTIONTYPE_H
#define FUNCTIONTYPE_H

#include "abstracttype.h"

namespace KDevelop
{
class FunctionTypeData;

/**
 * \short A type representing function types.
 *
 * A FunctionType is represents the type of a function.  It provides access
 * to the return type, and number and types of the arguments.
 */
class KDEVPLATFORMLANGUAGE_EXPORT FunctionType : public AbstractType
{
public:
  typedef TypePtr<FunctionType> Ptr;

  /// An enumeration of sections of the function signature that can be returned.
  enum SignaturePart {
    SignatureWhole /**< When this is given to toString(..), a string link "RETURNTYPE (ARGTYPE1, ARGTYPE1, ..)" is returned */,
    SignatureReturn /**< When this is given, only a string that represents the return-type is returned */,
    SignatureArguments /**< When this is given, a string that represents the arguments like "(ARGTYPE1, ARGTYPE1, ..)" 
                        * is returend. This does _not_ include a trailing "const" if the function is constant */
  };

  /// Default constructor
  FunctionType();
  /// Copy constructor. \param rhs type to copy
  FunctionType(const FunctionType& rhs);
  /// Constructor using raw data. \param data internal data.
  FunctionType(FunctionTypeData& data);
  /// Destructor
  ~FunctionType();

  /**
   * Retrieve the return type of the function.
   *
   * \returns the return type.
   */
  AbstractType::Ptr returnType () const;

  /**
   * Sets the return type of the function.
   *
   * \param returnType the return type.
   */
  void setReturnType(AbstractType::Ptr returnType);

  /**
   * Retrieve the list of types of the function's arguments.
   *
   * \returns the argument types.
   */
  QList<AbstractType::Ptr> arguments () const;

  /**
   * Returns the same arguemtns as arguments(), but without converting them to a QList.
   * This is much faster, and should be preferred for very tight loops when the performance counts.
   * \return an array that contains the arguments. For the count of arguments, call indexedArgumentsSize
   */
  const IndexedType* indexedArguments() const;
  
  /**
   * Returns the size of the array returned by indexedArguments(). This is much faster than working with arguments().
   */
  uint indexedArgumentsSize() const;
  
  /**
   * Add an argument to the function, specifying what type it takes.
   *
   * \param argument the argument's type
   */
  void addArgument(AbstractType::Ptr argument);

  /**
   * Remove an argument type from the function.
   *
   * \param argument the argument type to remove
   * \todo this function doesn't seem to be used, remove it?
   */
  void removeArgument(AbstractType::Ptr argument);

  virtual AbstractType* clone() const;

  virtual bool equals(const AbstractType* rhs) const;

  virtual QString toString() const;

  /**
   * This function creates a string that represents the requested part of
   * this function's signature.
   *
   * \param sigPart part of the signature requested.
   * \returns the signature as text.
   */
  virtual QString partToString( SignaturePart sigPart ) const;

  virtual uint hash() const;

  virtual WhichType whichType() const;

  virtual void exchangeTypes( TypeExchanger* exchanger );

  enum {
    Identity = 5
  };

  typedef FunctionTypeData Data;

protected:
  virtual void accept0 (TypeVisitor *v) const;

  TYPE_DECLARE_DATA(FunctionType)
};

template<>
inline FunctionType* fastCast<FunctionType*>(AbstractType* from) {
  if(!from || from->whichType() != AbstractType::TypeFunction)
    return 0;
  else
    return static_cast<FunctionType*>(from);
}

}

#endif

// kate: space-indent on; indent-width 2; tab-width 4; replace-tabs on; auto-insert-doxygen on
