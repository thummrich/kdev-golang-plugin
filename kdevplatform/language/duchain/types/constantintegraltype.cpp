/* This file is part of KDevelop
    Copyright 2002-2005 Roberto Raggi <roberto@kdevelop.org>
    Copyright 2006 Adam Treat <treat@kde.org>
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

#include "constantintegraltype.h"

#include "typesystemdata.h"
#include "typeregister.h"

namespace KDevelop {

REGISTER_TYPE(ConstantIntegralType);

ConstantIntegralType::ConstantIntegralType(const ConstantIntegralType& rhs)
  : IntegralType(copyData<ConstantIntegralType>(*rhs.d_func()))
{
}

ConstantIntegralType::ConstantIntegralType(ConstantIntegralTypeData& data)
  : IntegralType(data)
{
}

ConstantIntegralType::ConstantIntegralType(uint type)
  : IntegralType(createData<ConstantIntegralType>())
{
  setDataType(type);
  setModifiers(ConstModifier);
}

qint64 ConstantIntegralType::plainValue() const
{
  return d_func()->m_value;
}

AbstractType* ConstantIntegralType::clone() const
{
  return new ConstantIntegralType(*this);
}

bool ConstantIntegralType::equals(const AbstractType* _rhs) const
{
  if( this == _rhs )
    return true;

  if (!IntegralType::equals(_rhs))
    return false;

  Q_ASSERT(fastCast<const ConstantIntegralType*>(_rhs));

  const ConstantIntegralType* rhs = static_cast<const ConstantIntegralType*>(_rhs);

  return d_func()->m_value == rhs->d_func()->m_value;
}

QString ConstantIntegralType::toString() const
{
  QString ret;

  switch(dataType()) {
    case TypeNone:
      ret += "none";
      break;
    case TypeChar:
      ret += QString("%1").arg((char)d_func()->m_value);
      break;
    case TypeWchar_t:
      ret += QString("%1").arg((wchar_t)d_func()->m_value);
      break;
    case TypeBoolean:
      ret += d_func()->m_value ? "true" : "false";
      break;
    case TypeInt:
      ret += (modifiers() & UnsignedModifier) ? QString("%1u").arg((uint)d_func()->m_value) : QString("%1").arg((int)d_func()->m_value);
      break;
    case TypeFloat:
      ret += QString("%1").arg( value<float>() );
      break;
    case TypeDouble:
      ret += QString("%1").arg( value<double>() );
      break;
    case TypeVoid:
      ret += "void";
      break;
    default:
      ret += "<unknown_value>";
      break;
  }

  return ret;
}

uint ConstantIntegralType::hash() const
{
  uint ret = IntegralType::hash();
  ret += 47 * (uint)d_func()->m_value;
  return ret;
}

template<>
KDEVPLATFORMLANGUAGE_EXPORT
void ConstantIntegralType::setValueInternal<qint64>(qint64 value) {
  if((modifiers() & UnsignedModifier)) {
    kDebug() << "setValue(signed) called on unsigned type";
  }
  d_func_dynamic()->m_value = value;
}

template<>
KDEVPLATFORMLANGUAGE_EXPORT
void ConstantIntegralType::setValueInternal<quint64>(quint64 value) {
  if(!(modifiers() & UnsignedModifier)) {
    kDebug() << "setValue(unsigned) called on not unsigned type";
  }
  d_func_dynamic()->m_value = (qint64)value;
}

template<>
KDEVPLATFORMLANGUAGE_EXPORT
void ConstantIntegralType::setValueInternal<float>(float value) {
  if(dataType() != TypeFloat) {
    kDebug() << "setValue(float) called on non-float type";
  }
  memcpy(&d_func_dynamic()->m_value, &value, sizeof(float));
}

template<>
KDEVPLATFORMLANGUAGE_EXPORT
void ConstantIntegralType::setValueInternal<double>(double value) {
  if(dataType() != TypeDouble) {
    kDebug() << "setValue(double) called on non-double type";
  }
  memcpy(&d_func_dynamic()->m_value, &value, sizeof(double));
}

}
