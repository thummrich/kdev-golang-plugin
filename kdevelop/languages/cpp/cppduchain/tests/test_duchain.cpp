/* This file is part of KDevelop
    Copyright 2006 Hamish Rodda <rodda@kde.org>
    Copyright 2007-2009 David Nolden <david.nolden.kdevelop@art-master.de>

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

#include "test_duchain.h"

#include <QtTest/QtTest>

#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/forwarddeclaration.h>
#include <language/duchain/functiondefinition.h>
#include "declarationbuilder.h"
#include "usebuilder.h"
#include <language/duchain/declarationid.h>
#include <language/duchain/declaration.h>
#include <language/duchain/dumpdotgraph.h>
#include <typeinfo>
#include <time.h>
#include <set>
#include <algorithm>
#include <iterator>
#include <time.h>
#include "cpptypes.h"
#include "templateparameterdeclaration.h"
#include "cppeditorintegrator.h"
#include "dumptypes.h"
#include "environmentmanager.h"
#include <language/util/setrepository.h>

#include <language/editor/hashedstring.h>
#include <language/editor/documentrange.h>

#include "typeutils.h"
#include "templatedeclaration.h"
#include "rpp/chartools.h"
#include "rpp/pp-engine.h"
#include "rpp/preprocessor.h"

#include <language/duchain/indexedstring.h>
#include <language/duchain/classdeclaration.h>
#include <language/duchain/types/alltypes.h>
#include <language/duchain/persistentsymboltable.h>
#include <language/duchain/codemodel.h>
#include <language/codegen/coderepresentation.h>

#include <tests/testcore.h>
#include <tests/autotestshell.h>

#include "tokens.h"
#include "parsesession.h"
#include "astutilities.h"

//Extremely slow
// #define TEST_NORMAL_IMPORTS

#include <typeinfo>
#include <language/duchain/duchainutils.h>
#include <qtfunctiondeclaration.h>
#include <qwidget.h>
#include <language/duchain/navigation/abstractnavigationwidget.h>
#include <sourcemanipulation.h>

//Uncomment the following line to get additional output from the string-repository test
//#define DEBUG_STRINGREPOSITORY

using namespace KTextEditor;
using namespace TypeUtils;
using namespace KDevelop;
using namespace Cpp;
using namespace Utils;

QTEST_MAIN(TestDUChain)

void release(TopDUContext* top)
{
  //KDevelop::EditorIntegrator::releaseTopRange(top->textRangePtr());

  TopDUContextPointer tp(top);
  DUChain::self()->removeDocumentChain(static_cast<TopDUContext*>(top));
  Q_ASSERT(!tp);
}

/// Object that locks the duchain for writing and destroys its TopDUContext on destruction
struct LockedTopDUContext
{
  LockedTopDUContext(TopDUContext* top) : m_top(top)
  {
  }
  
  ~LockedTopDUContext() {
    DUChainWriteLocker lock(DUChain::lock());
    DUChain::self()->removeDocumentChain(m_top);
  }
  
  TopDUContext* operator->() {
    return m_top;
  }
  
  TopDUContext* m_top;
  DUChainWriteLocker m_writeLock;
};

namespace QTest {
  template<>
  char* toString(const Cursor& cursor)
  {
    QByteArray ba = "Cursor(";
    ba += QByteArray::number(cursor.line()) + ", " + QByteArray::number(cursor.column());
    ba += ')';
    return qstrdup(ba.data());
  }
  template<>
  char *toString(const SimpleRange &range)
  {
      QByteArray ba = "[ (";
      ba += QByteArray::number(range.start.line) + ", " + QByteArray::number(range.start.column);
      ba += ") -> (";
      ba += QByteArray::number(range.end.line) + ", " + QByteArray::number(range.end.column);
      ba += ") ]";
      return qstrdup(ba.data());
  }
  template<>
  char* toString(const QualifiedIdentifier& id)
  {
    QByteArray arr = id.toString().toLatin1();
    return qstrdup(arr.data());
  }
  template<>
  char* toString(const Identifier& id)
  {
    QByteArray arr = id.toString().toLatin1();
    return qstrdup(arr.data());
  }
  /*template<>
  char* toString(QualifiedIdentifier::MatchTypes t)
  {
    QString ret;
    switch (t) {
      case QualifiedIdentifier::NoMatch:
        ret = "No Match";
        break;
      case QualifiedIdentifier::Contains:
        ret = "Contains";
        break;
      case QualifiedIdentifier::ContainedBy:
        ret = "Contained By";
        break;
      case QualifiedIdentifier::ExactMatch:
        ret = "Exact Match";
        break;
    }
    QByteArray arr = ret.toString().toLatin1();
    return qstrdup(arr.data());
  }*/
  template<>
  char* toString(const Declaration& def)
  {
    QString s = QString("Declaration %1 (%2): %3").arg(def.identifier().toString()).arg(def.qualifiedIdentifier().toString()).arg(reinterpret_cast<long>(&def));
    return qstrdup(s.toLatin1().constData());
  }
  template<>
  char* toString(const TypePtr<AbstractType>& type)
  {
    QString s = QString("Type: %1 (%2 %3)").arg(type ? type->toString() : QString("<null>")).arg(typeid(*type).name()).arg((size_t)type.unsafeData());
    return qstrdup(s.toLatin1().constData());
  }
}

Declaration* getDeclaration( AbstractType::Ptr base, TopDUContext* top ) {
  if( !base ) return 0;

  IdentifiedType* idType = dynamic_cast<IdentifiedType*>(base.unsafeData());
  if( idType ) {
    return idType->declaration(top);
  } else {
    return 0;
  }
}


#define TEST_FILE_PARSE_ONLY if (testFileParseOnly) QSKIP("Skip", SkipSingle);
TestDUChain::TestDUChain()
{
  testFileParseOnly = false;
}

void TestDUChain::initTestCase()
{
  noDef = 0;

  AutoTestShell::init();
  TestCore* core = new TestCore();
  core->initialize(KDevelop::Core::NoUi);
  EnvironmentManager::init();

  DUChain::self()->disablePersistentStorage();
  KDevelop::CodeRepresentation::setDiskChangesForbidden(true);

  file1 = "file:///media/data/kdedev/4.0/kdevelop/languages/cpp/parser/duchain.cpp";
  file2 = "file:///media/data/kdedev/4.0/kdevelop/languages/cpp/parser/dubuilder.cpp";

  topContext = new TopDUContext(IndexedString(file1.pathOrUrl()), SimpleRange(SimpleCursor(0,0),SimpleCursor(25,0)));
  DUChainWriteLocker lock(DUChain::lock());

  DUChain::self()->addDocumentChain(topContext);

  typeVoid = AbstractType::Ptr(new IntegralType(IntegralType::TypeVoid))->indexed();
  typeInt = AbstractType::Ptr(new IntegralType(IntegralType::TypeInt))->indexed();
  AbstractType::Ptr s(new IntegralType(IntegralType::TypeInt));
  s->setModifiers(AbstractType::ShortModifier);
  typeShort = s->indexed();
}

void TestDUChain::testFinalCleanup() {
  //Just a few tests so there is always something cleared away
  QualifiedIdentifier id("test_bla12310915205342");
  IndexedQualifiedIdentifier indexedId(id);
  DelayedType::Ptr delayed(new DelayedType);
  delayed->setIdentifier(IndexedTypeIdentifier("frokkoflasdasotest_bla12310915205342"));
  IndexedType indexed = delayed->indexed();
  DUChain::self()->finalCleanup();
}

void TestDUChain::cleanupTestCase()
{
  /*delete type1;
  delete type2;
  delete type3;*/

  {
  DUChainWriteLocker lock(DUChain::lock());

  //EditorIntegrator::releaseTopRange(topContext->textRangePtr());
  topContext->deleteSelf();
  }
}

Declaration* TestDUChain::findDeclaration(DUContext* context, const Identifier& id, const SimpleCursor& position)
{
  QList<Declaration*> ret = context->findDeclarations(id, position);
  if (ret.count())
    return ret.first();
  return 0;
}

Declaration* TestDUChain::findDeclaration(DUContext* context, const QualifiedIdentifier& id, const SimpleCursor& position)
{
  QList<Declaration*> ret = context->findDeclarations(id, position);
  if (ret.count())
    return ret.first();
  return 0;
}

void TestDUChain::testIdentifiers()
{
  TEST_FILE_PARSE_ONLY

  QualifiedIdentifier aj("::Area::jump");
  QCOMPARE(aj.count(), 2);
  QCOMPARE(aj.explicitlyGlobal(), true);
  QCOMPARE(aj.at(0), Identifier("Area"));
  QCOMPARE(aj.at(1), Identifier("jump"));

  QualifiedIdentifier aj2 = QualifiedIdentifier("Area::jump");
  QCOMPARE(aj2.count(), 2);
  QCOMPARE(aj2.explicitlyGlobal(), false);
  QCOMPARE(aj2.at(0), Identifier("Area"));
  QCOMPARE(aj2.at(1), Identifier("jump"));
kDebug() << aj.toString() << aj2.toString();
kDebug() << aj.index() << aj2.index();
  QCOMPARE(aj == aj2, true);

  QVERIFY(QualifiedIdentifier("") == QualifiedIdentifier());
  QVERIFY(QualifiedIdentifier("").index() == QualifiedIdentifier().index());
  
//   QCOMPARE(aj.match(aj2), QualifiedIdentifier::ExactMatch);

  QualifiedIdentifier ajt("Area::jump::test");
  QualifiedIdentifier jt("jump::test");
  QualifiedIdentifier ajt2("Area::jump::tes");

//   QCOMPARE(aj2.match(ajt), QualifiedIdentifier::NoMatch);
//   QCOMPARE(ajt.match(aj2), QualifiedIdentifier::NoMatch);
//   //QCOMPARE(jt.match(aj2), QualifiedIdentifier::ContainedBy); ///@todo reenable this(but it fails)
//   QCOMPARE(ajt.match(jt), QualifiedIdentifier::EndsWith);

/*  QCOMPARE(aj2.match(ajt2), QualifiedIdentifier::NoMatch);
  QCOMPARE(ajt2.match(aj2), QualifiedIdentifier::NoMatch);*/
  QualifiedIdentifier t(" Area<A,B>::jump <F> ::tes<C>");
  QCOMPARE(t.count(), 3);
  QCOMPARE(t.at(0).templateIdentifiersCount(), 2u);
  QCOMPARE(t.at(1).templateIdentifiersCount(), 1u);
  QCOMPARE(t.at(2).templateIdentifiersCount(), 1u);
  QCOMPARE(t.at(0).identifier().str(), QString("Area"));
  QCOMPARE(t.at(1).identifier().str(), QString("jump"));
  QCOMPARE(t.at(2).identifier().str(), QString("tes"));

  QualifiedIdentifier op1("operator<");
  QualifiedIdentifier op2("operator<=");
  QualifiedIdentifier op3("operator>");
  QualifiedIdentifier op4("operator>=");
  QualifiedIdentifier op5("operator()");
  QualifiedIdentifier op6("operator( )");
  QCOMPARE(op1.count(), 1);
  QCOMPARE(op2.count(), 1);
  QCOMPARE(op3.count(), 1);
  QCOMPARE(op4.count(), 1);
  QCOMPARE(op5.count(), 1);
  QCOMPARE(op6.count(), 1);
  QCOMPARE(op4.toString(), QString("operator>="));
  QCOMPARE(op3.toString(), QString("operator>"));
  QCOMPARE(op1.toString(), QString("operator<"));
  QCOMPARE(op2.toString(), QString("operator<="));
  QCOMPARE(op5.toString(), QString("operator()"));
  QCOMPARE(op6.toString(), QString("operator( )"));
  QCOMPARE(QualifiedIdentifier("Area<A,B>::jump <F> ::tes<C>").index(), t.index());
  QCOMPARE(op4.index(), QualifiedIdentifier("operator>=").index());
  ///@todo create a big randomized test for the identifier repository(check that indices are the same)
}

void TestDUChain::testContextRelationships()
{
  TEST_FILE_PARSE_ONLY

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(DUChain::self()->chainForDocument(file1), topContext);

  DUContext* firstChild = new DUContext(SimpleRange(SimpleCursor(4,4), SimpleCursor(10,3)), topContext);

  QCOMPARE(firstChild->parentContext(), topContext);
  QCOMPARE(firstChild->childContexts().count(), 0);
  QCOMPARE(topContext->childContexts().count(), 1);
  QCOMPARE(topContext->childContexts().last(), firstChild);

  DUContext* secondChild = new DUContext(SimpleRange(SimpleCursor(14,4), SimpleCursor(19,3)), topContext);

  QCOMPARE(topContext->childContexts().count(), 2);
  QCOMPARE(topContext->childContexts()[1], secondChild);

  DUContext* thirdChild = new DUContext(SimpleRange(SimpleCursor(10,4), SimpleCursor(14,3)), topContext);

  QCOMPARE(topContext->childContexts().count(), 3);
  QCOMPARE(topContext->childContexts()[1], thirdChild);

  topContext->deleteChildContextsRecursively();
  QVERIFY(topContext->childContexts().isEmpty());
}

void TestDUChain::testDeclareInt()
{
  TEST_FILE_PARSE_ONLY

  QByteArray method("int i;");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 0);
  QCOMPARE(top->localDeclarations().count(), 1);
  QVERIFY(top->localScopeIdentifier().isEmpty());

  Declaration* def = top->localDeclarations().first();
  QCOMPARE(def->identifier(), Identifier("i"));
  QCOMPARE(findDeclaration(top, def->identifier()), def);

  release(top);
}

void TestDUChain::testMultiByteCStrings()
{
  ///@todo This currently does not work, the range has an offset of 1 to the right
#if 0
  int c;
  QByteArray method("char* c=\"ä\";void test() { c = 1; }");
  TopDUContext* top = parse(method, DumpNone);
  DUChainWriteLocker lock(DUChain::lock());
  Declaration* cDec = top->localDeclarations().first();
  QCOMPARE(cDec->uses().size(), 1);
  QCOMPARE(cDec->uses().begin()->size(), 1);
  kDebug() << cDec->uses().begin()->first().textRange();
  
//   QVERIFY(cDec->uses().begin()->first() == SimpleRange(0, 28, 0, 29));
  release(top);
#endif
}

void TestDUChain::testEllipsis()
{
  QByteArray method("void infinity(int i, ...) { }");
  TopDUContext* top = parse(method, DumpNone);
  DUChainWriteLocker lock(DUChain::lock());
  Declaration* defInfinity = top->localDeclarations().first();
  QCOMPARE(defInfinity->type<FunctionType>()->arguments().count(), 2);
  QCOMPARE(defInfinity->type<FunctionType>()->arguments()[1]->toString(), QString("..."));
  release(top);
}

void TestDUChain::testContextSearch() {
  {
    QByteArray method("int t; struct C { }; void test() { C c; c.t = 3;}");
    
    TopDUContext* top = parse(method, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 3);
    QVERIFY(top->localDeclarations()[0]->uses().isEmpty());
    release(top);
  }
  {
    QByteArray method("typedef union { char __size[2]; long int __align; } pthread_attr_t; struct Stru {};");
    
    TopDUContext* top = parse(method, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    release(top);
  }
}

void TestDUChain::testSeparateVariableDefinition() {
  {
    QByteArray method("struct S {static int testValue; enum { A = 5 }; class C {}; int test(C); }; int S::testValue(A); int S::test(C) {}; ");
    
    /**
    * A prefix-context is created that surrounds S::testValue to represent the "S" part of the scope.
    */
    
    TopDUContext* top = parse(method, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->localDeclarations()[0]->qualifiedIdentifier(), QualifiedIdentifier("S"));
    QCOMPARE(top->childContexts().count(), 5); ///There is one 'garbage' context 
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 4);
    QCOMPARE(top->childContexts()[0]->childContexts().count(), 3);
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations().count(), 1);
    ClassMemberDeclaration* staticDeclaration = dynamic_cast<ClassMemberDeclaration*>(top->childContexts()[0]->localDeclarations()[0]);
    QVERIFY(staticDeclaration);
    QCOMPARE(staticDeclaration->qualifiedIdentifier(), QualifiedIdentifier("S::testValue"));
    QVERIFY(staticDeclaration->isStatic());
    
    QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 1);
    Declaration* actualDeclaration = top->childContexts()[2]->localDeclarations()[0];
    QVERIFY(!dynamic_cast<AbstractFunctionDeclaration*>(actualDeclaration));
    QCOMPARE(actualDeclaration->qualifiedIdentifier(), QualifiedIdentifier("S::testValue"));

    QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations()[0]->uses().count(), 1);
    
    ///@todo declaration/definition relationship

    
    QVERIFY(dynamic_cast<AbstractFunctionDeclaration*>(top->localDeclarations()[1]));

  }
  
  {
    //Declaration + definition
    QByteArray method("extern int x; int x;");
    
    TopDUContext* top = parse(method, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    ///@todo declaration/definition relationship
  }
}

void TestDUChain::testIntegralTypes()
{
  TEST_FILE_PARSE_ONLY

  QByteArray method("const unsigned int i, k; volatile long double j; int* l; double * const * m; const int& n = l;");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 0);
  QCOMPARE(top->localDeclarations().count(), 6);
  QVERIFY(top->localScopeIdentifier().isEmpty());

  Declaration* defI = top->localDeclarations().first();
  QCOMPARE(defI->identifier(), Identifier("i"));
  QCOMPARE(findDeclaration(top, defI->identifier()), defI);
  QVERIFY(defI->type<IntegralType>());
  QCOMPARE(defI->type<IntegralType>()->dataType(), (uint)IntegralType::TypeInt);
  QCOMPARE(defI->type<IntegralType>()->modifiers(), (unsigned long long)(AbstractType::UnsignedModifier | AbstractType::ConstModifier));

  Declaration* defK = top->localDeclarations()[1];
  QCOMPARE(defK->identifier(), Identifier("k"));
  QCOMPARE(defK->type<IntegralType>()->indexed(), defI->type<IntegralType>()->indexed());

  Declaration* defJ = top->localDeclarations()[2];
  QCOMPARE(defJ->identifier(), Identifier("j"));
  QCOMPARE(findDeclaration(top, defJ->identifier()), defJ);
  QVERIFY(defJ->type<IntegralType>());
  QCOMPARE(defJ->type<IntegralType>()->dataType(), (uint)IntegralType::TypeDouble);
  QCOMPARE(defJ->type<IntegralType>()->modifiers(), (unsigned long long)AbstractType::LongModifier | (unsigned long long)AbstractType::VolatileModifier);

  Declaration* defL = top->localDeclarations()[3];
  QCOMPARE(defL->identifier(), Identifier("l"));
  QVERIFY(defL->type<PointerType>());
  QCOMPARE(defL->type<PointerType>()->baseType()->indexed(), typeInt);
  QCOMPARE(defL->type<PointerType>()->modifiers(), (unsigned long long)AbstractType::NoModifiers);

  Declaration* defM = top->localDeclarations()[4];
  QCOMPARE(defM->identifier(), Identifier("m"));
  PointerType::Ptr firstpointer = defM->type<PointerType>();
  QVERIFY(firstpointer);
  QCOMPARE(firstpointer->modifiers(), (unsigned long long)AbstractType::NoModifiers);
  PointerType::Ptr secondpointer = PointerType::Ptr::dynamicCast(firstpointer->baseType());
  QVERIFY(secondpointer);
  QCOMPARE(secondpointer->modifiers(), (unsigned long long)AbstractType::ConstModifier);
  IntegralType::Ptr base = IntegralType::Ptr::dynamicCast(secondpointer->baseType());
  QVERIFY(base);
  QCOMPARE(base->dataType(), (uint)IntegralType::TypeDouble);
  QCOMPARE(base->modifiers(), (unsigned long long)AbstractType::NoModifiers);

  Declaration* defN = top->localDeclarations()[5];
  QCOMPARE(defN->identifier(), Identifier("n"));
  QVERIFY(defN->type<ReferenceType>());
  base = IntegralType::Ptr::dynamicCast(defN->type<ReferenceType>()->baseType());
  QVERIFY(base);
  QCOMPARE(base->dataType(), (uint)IntegralType::TypeInt);
  QCOMPARE(base->modifiers(), (unsigned long long)AbstractType::ConstModifier);

  //Even if reference/pointer types have an invalid target, they should still preserve the pointer
  QVERIFY(AbstractType::Ptr(new ReferenceType)->toString().endsWith("&"));
  QVERIFY(AbstractType::Ptr(new PointerType)->toString().endsWith("*"));
  
  release(top);
}

void TestDUChain::testConversionReturn() {
  TEST_FILE_PARSE_ONLY

  QByteArray method("class A {}; class B{ operator A*() const ; };");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations().count(), 2);

  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  FunctionType::Ptr funType = top->childContexts()[1]->localDeclarations()[0]->type<KDevelop::FunctionType>();
  QVERIFY(funType);
  QVERIFY(funType->returnType().cast<PointerType>());

  release(top);
}

void TestDUChain::testTypeof() {
  TEST_FILE_PARSE_ONLY
  
  {
    QByteArray method("__typeof__(wchar_t) x; typedef typeof(++x) x_t;");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(unAliasedType(top->localDeclarations()[1]->abstractType())->toString(), QString("wchar_t"));
    
    release(top);
  }
}

void TestDUChain::testArrayType()
{
  TEST_FILE_PARSE_ONLY

  QByteArray method("const unsigned int ArraySize = 3; int i[ArraySize];");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 0);
  QCOMPARE(top->localDeclarations().count(), 2);
  QVERIFY(top->localScopeIdentifier().isEmpty());

  kDebug() << top->localDeclarations()[0]->abstractType()->toString();
  QVERIFY(top->localDeclarations()[0]->uses().size());
  QVERIFY(top->localDeclarations()[0]->type<ConstantIntegralType>());

  Declaration* defI = top->localDeclarations().last();
  QCOMPARE(defI->identifier(), Identifier("i"));
  QCOMPARE(findDeclaration(top, defI->identifier()), defI);

  ArrayType::Ptr array = defI->type<ArrayType>();
  QVERIFY(array);
  IntegralType::Ptr element = IntegralType::Ptr::dynamicCast(array->elementType());
  QVERIFY(element);
  QCOMPARE(element->dataType(), (uint)IntegralType::TypeInt);
  QCOMPARE(array->dimension(), 3);

  release(top);
}

void TestDUChain::testProblematicUses()
{
  TEST_FILE_PARSE_ONLY
  
  {
    QByteArray method("struct S { int a; }; int c; int q; int a; S* s; void test() { if(s->a < q && a > c) { } if(s->a < q && a > c) { } }");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 3);
    QCOMPARE(top->localDeclarations().count(), 6);
    QCOMPARE(top->localDeclarations()[1]->uses().size(), 1);
    QCOMPARE(top->localDeclarations()[1]->uses().begin()->size(), 2); //c uses
    QCOMPARE(top->localDeclarations()[2]->uses().size(), 1);
    QCOMPARE(top->localDeclarations()[2]->uses().begin()->size(), 2); //q uses
    QCOMPARE(top->localDeclarations()[3]->uses().size(), 1);
    QCOMPARE(top->localDeclarations()[3]->uses().begin()->size(), 2); //a uses
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->size(), 2); //a uses

    release(top);
  }

{
    //                 0         1         2         3         4         5
    //                 012345678901234567890123456789012345678901234567890123456789
    QByteArray method("enum { b = 5 }; template<bool b> struct A { enum { Mem }; }; void test(int a) { if(a < 0 && a > -1) { } if(a < 0 && a < -1) { } if(A<(b < 10 && b < 0)>::Mem) { } if(A<(b < 10 && b > 0)>::Mem) { } }");

///@todo This works in g++, but not in kdevelops parser. Although it's butt ugly.
//     if(A<b < 10 && b < 0>::Mem) {}
    
    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 5);
    QCOMPARE(top->childContexts()[3]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[3]->localDeclarations()[0]->uses().size(), 1);
    QCOMPARE(top->childContexts()[3]->localDeclarations()[0]->uses().begin()->size(), 4); //a uses
    
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->size(), 4); //b uses

    release(top);
  }
}

void TestDUChain::testBaseUses()
{
  TEST_FILE_PARSE_ONLY

{
    //                 0         1         2         3         4         5         6         7         8
    //                 012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
    QByteArray method("namespace N { struct A { A(int); int m; }; }; struct B : public N::A { B() : N::A(5) { this->N::A::m = 5; } };");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QVERIFY(top->problems().isEmpty()); //N::A must be found in the constructor
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().size(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations().count(), 2);
    QVERIFY(!top->childContexts()[0]->childContexts()[0]->localDeclarations()[1]->uses().isEmpty());
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->size(), 3);
    // the ctor usage
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations()[0]->uses().count(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations()[0]->uses().values().first().count(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations()[0]->uses().values().first().first(), SimpleRange(0, 81, 0, 82));
    release(top);
  }

  {
    //                 0         1         2         3         4         5
    //                 012345678901234567890123456789012345678901234567890123456789
    QByteArray method("class A{ class B {}; }; class C : public A::B { C() : A::B() {} };");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[1]->usesCount(), 2);

    QCOMPARE(top->childContexts()[1]->uses()[0].m_range, SimpleRange(0, 41, 0, 42));
    QCOMPARE(top->childContexts()[1]->uses()[1].m_range, SimpleRange(0, 44, 0, 45));

    QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().count(), 1);

    QCOMPARE(top->childContexts()[1]->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->childContexts()[1]->usesCount(), 2);

    release(top);
  }
  {
    //                 0         1         2         3         4         5
    //                 012345678901234567890123456789012345678901234567890123456789
    QByteArray method("template<typename T> class A{ A(){} };\n"
                      "template<typename T> class B : public A<T> { B() : A<T>() {} };");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 2);

    // uses of class A
    QEXPECT_FAIL("", "Cookie for the one fixing that!", Abort);
    QCOMPARE(top->localDeclarations().first()->uses().size(), 1);
    QCOMPARE(top->localDeclarations().first()->uses().begin()->size(), 2);
    // use in class B : public A<T>
    QCOMPARE(top->localDeclarations().first()->uses().begin()->at(0), SimpleRange(1, 38, 1, 39));
    // use in B() : A<T>()
    QCOMPARE(top->localDeclarations().first()->uses().begin()->at(1), SimpleRange(1, 51, 1, 52));

    // use of A's ctor
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[1]->localDeclarations().first()->uses().size(), 1);
    QCOMPARE(top->childContexts()[1]->localDeclarations().first()->uses().begin()->size(), 1);
    QCOMPARE(top->childContexts()[1]->localDeclarations().first()->uses().begin()->first(), SimpleRange(1, 55, 1, 56));

    release(top);
  }
}


void TestDUChain::testTypedefUses()
{
  {

    //                 0         1         2         3         4         5
    //                 012345678901234567890123456789012345678901234567890123456789
    QByteArray method("namespace Search { typedef int StateHypothesisIndexwqeqrqwe; StateHypothesisIndexwqeqrqwe i; }");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->usesCount(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);
    QVERIFY(top->childContexts()[0]->localDeclarations()[0]->inSymbolTable());
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().size(), 1);
    QCOMPARE(unAliasedType(top->childContexts()[0]->localDeclarations()[1]->abstractType())->toString(), QString("int"));
    
    release(top);
  }
  {
    //                 0         1         2         3         4         5
    //                 012345678901234567890123456789012345678901234567890123456789
    QByteArray method("class A{}; typedef A B; A c; B d;");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 4);
    QCOMPARE(top->usesCount(), 3);
    QCOMPARE(top->localDeclarations()[0]->uses().count(), 1); //1 File
    QCOMPARE(top->localDeclarations()[1]->uses().count(), 1); //1 File

    QCOMPARE(top->localDeclarations()[0]->uses().begin()->count(), 2); //Typedef and "A c;"
    QCOMPARE(top->localDeclarations()[1]->uses().begin()->count(), 1); //"B d;"

    release(top);
  }
}

void TestDUChain::testConstructorOperatorUses()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7         8
  //                 012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789
  QByteArray method("class A{A(int); int operator()( ) {}; }; void test() { A a(1); A b = A(2); b(); }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 2);
//   QCOMPARE(top->usesCount(), 3);
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 2);

  QCOMPARE(top->childContexts()[2]->localDeclarations()[1]->uses().count(), 1);
  
  QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);
  QCOMPARE(top->localDeclarations()[0]->uses().begin()->count(), 3);
  QCOMPARE(top->localDeclarations()[0]->uses().begin()->at(0), SimpleRange(0, 55, 0, 56));
  QCOMPARE(top->localDeclarations()[0]->uses().begin()->at(1), SimpleRange(0, 63, 0, 64));
  QCOMPARE(top->localDeclarations()[0]->uses().begin()->at(2), SimpleRange(0, 69, 0, 70));

  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().count(), 1);
  
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->count(), 2);
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->at(0), SimpleRange(0, 58, 0, 59));
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->at(1), SimpleRange(0, 70, 0, 71));
  
  QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().count(), 1);
  QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->count(), 1);
  QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->at(0), SimpleRange(0, 76, 0, 77));
  
  release(top);
}
void TestDUChain::testDeclareFor()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5
  //                 012345678901234567890123456789012345678901234567890123456789
  QByteArray method("int main() { for (int i = 0; i < 10; i++) { if (i == 4) return; int i5[5]; i5[i] = 1; } }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations().count(), 1);
  QVERIFY(top->localScopeIdentifier().isEmpty());

  Declaration* defMain = top->localDeclarations().first();
  QCOMPARE(defMain->identifier(), Identifier("main"));
  QCOMPARE(findDeclaration(top, defMain->identifier()), defMain);
  QVERIFY(defMain->type<FunctionType>());
  QCOMPARE(defMain->type<FunctionType>()->returnType()->indexed(), typeInt);
  QCOMPARE(defMain->type<FunctionType>()->arguments().count(), 0);
  QCOMPARE(defMain->type<FunctionType>()->modifiers(), (unsigned long long)AbstractType::NoModifiers);

  QCOMPARE(findDeclaration(top, Identifier("i")), noDef);

  DUContext* main = top->childContexts()[1];
  QVERIFY(main->parentContext());
  QCOMPARE(main->importedParentContexts().count(), 1);
  QCOMPARE(main->childContexts().count(), 2);
  QCOMPARE(main->localDeclarations().count(), 0);
  QCOMPARE(main->localScopeIdentifier(), QualifiedIdentifier("main"));

  QCOMPARE(findDeclaration(main, Identifier("i")), noDef);

  DUContext* forCtx = main->childContexts()[1];
  QVERIFY(forCtx->parentContext());
  QCOMPARE(forCtx->importedParentContexts().count(), 1);
  QCOMPARE(forCtx->childContexts().count(), 2);
  QCOMPARE(forCtx->localDeclarations().count(), 1);
  QVERIFY(forCtx->localScopeIdentifier().isEmpty());

  DUContext* forParamCtx = forCtx->importedParentContexts().first().context(0);
  QVERIFY(forParamCtx->parentContext());
  QCOMPARE(forParamCtx->importedParentContexts().count(), 0);
  QCOMPARE(forParamCtx->childContexts().count(), 0);
  QCOMPARE(forParamCtx->localDeclarations().count(), 1);
  QVERIFY(forParamCtx->localScopeIdentifier().isEmpty());

  Declaration* defI = forParamCtx->localDeclarations().first();
  QCOMPARE(defI->identifier(), Identifier("i"));
  QCOMPARE(defI->uses().count(), 1);
  QCOMPARE(defI->uses().begin()->count(), 4);

  QCOMPARE(findDeclaration(forCtx, defI->identifier()), defI);

  DUContext* ifCtx = forCtx->childContexts()[1];
  QVERIFY(ifCtx->parentContext());
  QCOMPARE(ifCtx->importedParentContexts().count(), 1);
  QCOMPARE(ifCtx->childContexts().count(), 0);
  QCOMPARE(ifCtx->localDeclarations().count(), 0);
  QVERIFY(ifCtx->localScopeIdentifier().isEmpty());

  QCOMPARE(findDeclaration(ifCtx,  defI->identifier()), defI);

  release(top);
}


void TestDUChain::testEnum()
{
  TEST_FILE_PARSE_ONLY

/*                   0         1         2         3         4         5         6         7
                     01234567890123456789012345678901234567890123456789012345678901234567890123456789*/
  QByteArray method("enum Enum { Value1 = 5 //Comment\n, value2 //Comment1\n }; enum Enum2 { Value21, value22 = 0x02 }; union { int u1; float u2; };");

  TopDUContext* top = parse(method, DumpNone);
/*  {
  DUChainWriteLocker lock(DUChain::lock());
    Declaration* decl = findDeclaration(top, QualifiedIdentifier("Value1"));
    QVERIFY(decl);
    AbstractType::Ptr t = decl->abstractType();
    QVERIFY(t);
    IdentifiedType* id = dynamic_cast<IdentifiedType*>(t.unsafeData());
    QVERIFY(id);
    QCOMPARE(id->declaration(top), decl);
  }*/

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 3);
  QCOMPARE(top->childContexts().count(), 3);

  QVERIFY(top->childContexts()[2]->inSymbolTable());
  
  ///@todo Test for the enum ranges and fix them, they overlap
  kDebug() << top->childContexts()[0]->range().textRange();
  kDebug() << top->localDeclarations()[0]->range().textRange();

  Declaration* decl = findDeclaration(top, Identifier("Enum"));
  Declaration* enumDecl = decl;
  QVERIFY(decl);
  QVERIFY(decl->internalContext());
  AbstractType::Ptr t = decl->abstractType();
  QVERIFY(dynamic_cast<EnumerationType*>(t.unsafeData()));
  EnumerationType* en = static_cast<EnumerationType*>(t.unsafeData());
  QVERIFY(en->declaration(top));
  QCOMPARE(en->qualifiedIdentifier(), QualifiedIdentifier("Enum"));
  Declaration* EnumDecl = decl;

  {
    QVERIFY(!top->findLocalDeclarations(Identifier("Value1")).isEmpty());

    decl = findDeclaration(top, QualifiedIdentifier("Value1"));
    QVERIFY(decl);

    QCOMPARE(decl->context()->owner(), enumDecl);
//     QCOMPARE(decl->context()->scopeIdentifier(), QualifiedIdentifier("Enum"));


    t = decl->abstractType();
    QVERIFY(dynamic_cast<EnumeratorType*>(t.unsafeData()));
    EnumeratorType* en = static_cast<EnumeratorType*>(t.unsafeData());
    QCOMPARE((int)en->value<qint64>(), 5);
    kDebug() << decl->qualifiedIdentifier().toString();
    kDebug() << en->toString();
    QCOMPARE(en->declaration(top), top->childContexts()[0]->localDeclarations()[0]);

    decl = findDeclaration(top, Identifier("value2"));
    QVERIFY(decl);
    t = decl->abstractType();
    QVERIFY(dynamic_cast<EnumeratorType*>(t.unsafeData()));
    en = static_cast<EnumeratorType*>(t.unsafeData());
    QCOMPARE((int)en->value<qint64>(), 6);
    QCOMPARE(en->declaration(top), top->childContexts()[0]->localDeclarations()[1]);
    QCOMPARE(en->declaration(top)->context()->owner(), EnumDecl);
  }
  decl = findDeclaration(top, Identifier("Enum2"));
  QVERIFY(decl);
  t = decl->abstractType();
  QVERIFY(dynamic_cast<EnumerationType*>(t.unsafeData()));
  en = static_cast<EnumerationType*>(t.unsafeData());
  QVERIFY(en->declaration(top));
  QCOMPARE(en->qualifiedIdentifier(), QualifiedIdentifier("Enum2"));
  Declaration* Enum2Decl = decl;

  {
    decl = findDeclaration(top, Identifier("Value21"));
    QVERIFY(decl);
    t = decl->abstractType();
    QVERIFY(dynamic_cast<EnumeratorType*>(t.unsafeData()));
    EnumeratorType* en = static_cast<EnumeratorType*>(t.unsafeData());
    QCOMPARE((int)en->value<qint64>(), 0);
    QCOMPARE(en->declaration(top)->context()->owner(), Enum2Decl);

    decl = findDeclaration(top, Identifier("value22"));
    QVERIFY(decl);
    t = decl->abstractType();
    QVERIFY(dynamic_cast<EnumeratorType*>(t.unsafeData()));
    en = static_cast<EnumeratorType*>(t.unsafeData());
    QCOMPARE((int)en->value<qint64>(), 2);
    QCOMPARE(en->declaration(top)->context()->owner(), Enum2Decl);
  }
  {
    //Verify that the union members were propagated up
    decl = findDeclaration(top, Identifier("u1"));
    QVERIFY(decl);

    decl = findDeclaration(top, Identifier("u2"));
    QVERIFY(decl);
  }
  release(top);
}

/*! Custom assertion which verifies that @p topcontext contains a
 *  single class with a single memberfunction declaration in it.
 *  @p memberFun is an output parameter and should have type
 *  pointer to ClassFunctionDeclaration */
#define ASSERT_SINGLE_MEMBER_FUNCTION_IN(topcontext, memberFun) \
{ \
    QVERIFY(top);\
    QCOMPARE(1, top->childContexts().count());\
    QCOMPARE(1, top->localDeclarations().count()); \
    DUContext* clazzCtx = top->childContexts()[0]; \
    QCOMPARE(1, clazzCtx->localDeclarations().count()); \
    Declaration* member = clazzCtx->localDeclarations()[0]; \
    memberFun = dynamic_cast<ClassFunctionDeclaration*>(member); \
    QVERIFY(memberFun); \
} (void)(0)

/*! Custom assertion which verifies that @p topcontext contains a
 *  single class with two memberfunction declarations in it.
 *  @p memberFun1 and @p memberFUn2 are output parameter and should
 *  have type pointer to ClassFunctionDeclaration */
#define ASSERT_TWO_MEMBER_FUNCTIONS_IN(topcontext, memberFun1, memberFun2) \
{ \
    QVERIFY(top);\
    QCOMPARE(1, top->childContexts().count());\
    QCOMPARE(1, top->localDeclarations().count()); \
    DUContext* clazzCtx = top->childContexts()[0]; \
    QCOMPARE(2, clazzCtx->localDeclarations().count()); \
    Declaration* member = clazzCtx->localDeclarations()[0]; \
    memberFun1 = dynamic_cast<ClassFunctionDeclaration*>(member); \
    QVERIFY(memberFun1); \
    member = clazzCtx->localDeclarations()[1]; \
    memberFun2 = dynamic_cast<ClassFunctionDeclaration*>(member); \
    QVERIFY(memberFun2); \
} (void)(0)

void TestDUChain::testVirtualMemberFunction()
{
  {
    QByteArray text("class Foo { public: virtual void bar(const float i, const int i) = 0; }; \n");
    TopDUContext* top = parse(text, DumpAll);
    DUChainWriteLocker lock(DUChain::lock());

    ClassFunctionDeclaration* memberFun; // filled by assert macro below
    ASSERT_SINGLE_MEMBER_FUNCTION_IN(top, memberFun);
    QVERIFY(memberFun->isVirtual());
    QVERIFY(memberFun->isAbstract());
    FunctionType::Ptr funType = memberFun->abstractType().cast<FunctionType>();
    QVERIFY(funType);
    QCOMPARE(funType->arguments().size(), 2);
    QCOMPARE(funType->arguments()[0]->toString(), QString("const float"));
    QCOMPARE(funType->arguments()[1]->toString(), QString("const int"));
    release(top);
  }
  {
    QByteArray text("class Foo { public: virtual void bar(); }; \n");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    ClassFunctionDeclaration* memberFun; // filled by assert macro below
    ASSERT_SINGLE_MEMBER_FUNCTION_IN(top, memberFun);
    QVERIFY(memberFun->isVirtual());
    QVERIFY(!memberFun->isAbstract());
    release(top);
  }
  {
    //Forward-declarations with "struct" or "class" are considered equal, so make sure the override is detected correctly.
    QByteArray text("class S; struct A { virtual S* ret(); }; struct S { }; struct B : public A { virtual S* ret(); };");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->childContexts().count(), 3);
    QCOMPARE(top->localDeclarations().count(), 4);
    QCOMPARE(top->localDeclarations()[0]->indexedType(), top->localDeclarations()[2]->indexedType());
    QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 1);
    QVERIFY(DUChainUtils::getOverridden(top->childContexts()[2]->localDeclarations()[0]));

    release(top);
  }
}

void TestDUChain::testMultipleVirtual()
{
    QByteArray text("class Foo { public: virtual void bar(); virtual void baz(); }; \n");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    ClassFunctionDeclaration *bar, *baz; // filled by assert macro below
    ASSERT_TWO_MEMBER_FUNCTIONS_IN(top, bar, baz);
    QVERIFY(bar->isVirtual());
    QVERIFY(baz->isVirtual());
    QVERIFY(!baz->isAbstract());
    release(top);
}

void TestDUChain::testMixedVirtualNormal()
{
  {
    QByteArray text("class Foo { public: virtual void bar(); void baz(); }; \n");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    ClassFunctionDeclaration *bar, *baz; // filled by assert macro below
    ASSERT_TWO_MEMBER_FUNCTIONS_IN(top, bar, baz);
    QVERIFY(bar->isVirtual());
    QVERIFY(!baz->isVirtual());
    QVERIFY(!baz->isAbstract());
    release(top);
  }
}

void TestDUChain::testNonVirtualMemberFunction()
{
  {
    QByteArray text("class Foo \n { public: void bar(); };\n");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    ClassFunctionDeclaration* memberFun; // filled by assert macro below
    ASSERT_SINGLE_MEMBER_FUNCTION_IN(top, memberFun);
    QVERIFY(!memberFun->isVirtual());
    QVERIFY(!memberFun->isAbstract());
    release(top);
  }
}

/*! Extract memberfunction from @p topcontext in the @p nrofClass 'th class
 *  declaration. Assert that it is named @p expectedName. Fill the outputparameter
 *  @p memberFun */
#define FETCH_MEMBER_FUNCTION(nrofClass, topcontext, expectedName, memberFun) \
{ \
    QVERIFY(top);\
    DUContext* clazzCtx = top->childContexts()[nrofClass]; \
    QCOMPARE(1, clazzCtx->localDeclarations().count()); \
    Declaration* member = clazzCtx->localDeclarations()[0]; \
    memberFun = dynamic_cast<ClassFunctionDeclaration*>(member); \
    QVERIFY(memberFun); \
    QCOMPARE(QString(expectedName), memberFun->toString()); \
} (void)(0)

void TestDUChain::testMemberFunctionModifiers()
{
  // NOTE this only verifies
  //      {no member function modifiers in code} => {no modifiers in duchain}
  // it does not check that (but probably should, with all permutations ;))
  //      {member function modifiers in code} => {exactly the same modifiers in duchain}
  //      {illegal combinations, eg explicit not on a constructor} => {error/warning}
  {
      QByteArray text("class FuBarr { void foo(); };\n"
                      "class ZooLoo { void bar(); };\n\n"
                      "class MooFoo \n { public: void loo(); };\n"
                      "class GooRoo { void baz(); };\n"
                      "class PooYoo { \n void zoo(); };\n");
      // at one point extra '\n' characters would affect these modifiers.
      TopDUContext* top = parse(text, DumpNone);
      DUChainWriteLocker lock(DUChain::lock());

      ClassFunctionDeclaration *foo, *bar, *loo, *baz, *zoo; // filled below
      FETCH_MEMBER_FUNCTION(0, top, "void foo ()", foo);
      FETCH_MEMBER_FUNCTION(1, top, "void bar ()", bar);
      FETCH_MEMBER_FUNCTION(2, top, "void loo ()", loo);
      FETCH_MEMBER_FUNCTION(3, top, "void baz ()", baz);
      FETCH_MEMBER_FUNCTION(4, top, "void zoo ()", zoo);

      assertNoMemberFunctionModifiers(foo);
      assertNoMemberFunctionModifiers(bar);
      assertNoMemberFunctionModifiers(loo);
      assertNoMemberFunctionModifiers(baz);
      assertNoMemberFunctionModifiers(zoo);

      release(top);
  }

}

void TestDUChain::assertNoMemberFunctionModifiers(ClassFunctionDeclaration* memberFun)
{
    AbstractType::Ptr t(memberFun->abstractType());
    Q_ASSERT(t);
    bool isConstant = t->modifiers() & AbstractType::ConstModifier;
    bool isVolatile = t->modifiers() & AbstractType::VolatileModifier;

    kDebug() << memberFun->toString() << "virtual?"  << memberFun->isVirtual()
                                      << "explicit?" << memberFun->isExplicit()
                                      << "inline?"   << memberFun->isInline()
                                      << "constant?" << isConstant
                                      << "volatile?" << isVolatile
                                      << "static?"   << memberFun->isStatic();
    QVERIFY(!memberFun->isVirtual());
    QVERIFY(!memberFun->isExplicit());
    QVERIFY(!memberFun->isInline());
    QVERIFY(!isConstant);
    QVERIFY(!isVolatile);
    QVERIFY(!memberFun->isStatic());
}


void TestDUChain::testAssignedContexts()
{
  {
    QByteArray method("void test() { int i; if(i) { int q; }  int c; while(c) { char q; } int w; { float q;  } }");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->type(), DUContext::Other);
    QCOMPARE(top->childContexts()[1]->childContexts().count(), 4);
    QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 3);
    QVERIFY(!top->childContexts()[1]->childContexts()[0]->owner());
    QVERIFY(!top->childContexts()[1]->childContexts()[1]->owner());
    QVERIFY(!top->childContexts()[1]->childContexts()[2]->owner());
    QVERIFY(!top->childContexts()[1]->childContexts()[3]->owner());
    release(top);
  }
}

void TestDUChain::testTryCatch() {
  TEST_FILE_PARSE_ONLY
  {
    QByteArray method("void test() { try{ int o = 3; o += 3; } catch (...) { } }");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->type(), DUContext::Other);
    QCOMPARE(top->childContexts()[1]->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->childContexts()[0]->usesCount(), 1);
    release(top);
  }
}

void TestDUChain::testDeclareStruct()
{
  TEST_FILE_PARSE_ONLY
//   {
//     QByteArray method("struct { short i; } instance;");
// 
//     TopDUContext* top = parse(method, DumpNone);
// 
//     DUChainWriteLocker lock(DUChain::lock());
// 
//     QVERIFY(!top->parentContext());
//     QCOMPARE(top->childContexts().count(), 1);
//     QCOMPARE(top->localDeclarations().count(), 2);
//     QVERIFY(top->localScopeIdentifier().isEmpty());
// 
//     AbstractType::Ptr t = top->localDeclarations()[0]->abstractType();
//     IdentifiedType* idType = dynamic_cast<IdentifiedType*>(t.unsafeData());
//     QVERIFY(idType);
//     QVERIFY(idType->qualifiedIdentifier().count() == 1);
//     QVERIFY(idType->qualifiedIdentifier().at(0).uniqueToken());
// 
//     Declaration* defStructA = top->localDeclarations().first();
// 
//     QCOMPARE(top->localDeclarations()[1]->abstractType()->indexed(), top->localDeclarations()[0]->abstractType()->indexed());
// 
//     QCOMPARE(idType->declaration(top), defStructA);
//     QVERIFY(defStructA->type<CppClassType>());
//     QVERIFY(defStructA->internalContext());
//     QCOMPARE(defStructA->internalContext()->localDeclarations().count(), 1);
//     Cpp::ClassDeclaration* classDecl = dynamic_cast<Cpp::ClassDeclaration*>(top->localDeclarations()[0]);
//     QVERIFY(classDecl);
// 
//     QCOMPARE(classDecl->classType(), Cpp::ClassDeclarationData::Struct);
// 
//     release(top);
//   }
    //                 0         1         2         3         4         5         6         7
    //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  {
    QByteArray method("class A { public: union { struct { int x; }; int p; }; struct { int b; }; struct { int c; } cc; }; A test; void test() { test.x = 1; test.b = 1; test.p = 1; test.cc.c = 1; }");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 3);
    QCOMPARE(top->childContexts()[0]->childContexts().count(), 3);
    QCOMPARE(top->childContexts()[0]->childContexts()[2]->localDeclarations().count(), 1);
    QVERIFY(!top->childContexts()[0]->childContexts()[2]->localDeclarations()[0]->uses().isEmpty());
    QCOMPARE(top->childContexts()[0]->childContexts()[1]->localDeclarations().count(), 1);
    QVERIFY(top->childContexts()[0]->childContexts()[1]->isPropagateDeclarations());
    QVERIFY(top->childContexts()[0]->childContexts()[0]->isPropagateDeclarations());
    QVERIFY(top->childContexts()[0]->childContexts()[0]->inSymbolTable());
    QVERIFY(top->childContexts()[0]->childContexts()[1]->inSymbolTable());
    QVERIFY(top->childContexts()[0]->childContexts()[0]->localScopeIdentifier().isEmpty());
    QVERIFY(top->childContexts()[0]->childContexts()[1]->localScopeIdentifier().isEmpty());
    QVERIFY(!top->childContexts()[0]->childContexts()[1]->localDeclarations()[0]->uses().isEmpty());
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->childContexts()[0]->localDeclarations().count(), 1);
    QVERIFY(top->childContexts()[0]->childContexts()[0]->isPropagateDeclarations());
    QVERIFY(top->childContexts()[0]->childContexts()[0]->childContexts()[0]->isPropagateDeclarations());
    QVERIFY(!top->childContexts()[0]->childContexts()[0]->childContexts()[0]->localDeclarations()[0]->uses().isEmpty());
    QCOMPARE(top->childContexts()[2]->usesCount(), 9);
             
    release(top);
  }

  {
    //                 0         1         2         3         4         5         6         7
    //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
    QByteArray method("typedef struct { short i; } A; A instance;");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QVERIFY(!top->parentContext());
    QCOMPARE(top->childContexts().count(), 1);
    QCOMPARE(top->localDeclarations().count(), 3);
    QVERIFY(top->localScopeIdentifier().isEmpty());

    AbstractType::Ptr t = top->localDeclarations()[0]->abstractType();
    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(t.unsafeData());
    QVERIFY(idType);
    QVERIFY(idType->qualifiedIdentifier().count() == 1);
    QVERIFY(idType->qualifiedIdentifier().at(0).uniqueToken());

    QVERIFY(top->localDeclarations()[1]->isTypeAlias());
    QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("A"));
    QCOMPARE(top->localDeclarations()[2]->abstractType()->indexed(), top->localDeclarations()[1]->abstractType()->indexed());

    QCOMPARE(top->localDeclarations()[1]->uses().count(), 1);
    QCOMPARE(top->localDeclarations()[1]->uses().begin()->count(), 1);

    Declaration* defStructA = top->localDeclarations().first();
    QVERIFY(!defStructA->isTypeAlias());
    QVERIFY(defStructA->identifier().uniqueToken());

    QCOMPARE(idType->declaration(top), defStructA);
    QVERIFY(defStructA->type<CppClassType>());
    QVERIFY(defStructA->internalContext());
    QCOMPARE(defStructA->internalContext()->localDeclarations().count(), 1);
    ClassDeclaration* classDecl = dynamic_cast<ClassDeclaration*>(defStructA);
    QVERIFY(classDecl);
    QCOMPARE(classDecl->classType(), ClassDeclarationData::Struct);

    QVERIFY(!findDeclaration(top, Identifier("i")));
    QVERIFY(findDeclaration(top, QualifiedIdentifier("A::i")));
    
    release(top);
  }
  {
    //                 0         1         2         3         4         5         6         7
    //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
    QByteArray method("typedef struct B A; A instance;");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QVERIFY(!top->parentContext());
    QCOMPARE(top->localDeclarations().count(), 3);
    QVERIFY(top->localScopeIdentifier().isEmpty());

    AbstractType::Ptr t = top->localDeclarations()[0]->abstractType();
    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(t.unsafeData());
    QVERIFY(idType);
    QVERIFY(idType->qualifiedIdentifier().count() == 1);

    QVERIFY(top->localDeclarations()[1]->isTypeAlias());
    QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("A"));
    QCOMPARE(top->localDeclarations()[2]->abstractType()->indexed(), top->localDeclarations()[1]->abstractType()->indexed());

    QCOMPARE(top->localDeclarations()[1]->uses().count(), 1);
    QCOMPARE(top->localDeclarations()[1]->uses().begin()->count(), 1);

    Declaration* defStructA = top->localDeclarations().first();
    QVERIFY(!defStructA->isTypeAlias());
    QVERIFY(defStructA->isForwardDeclaration());
    
    release(top);
  }  
  {
    //                 0         1         2         3         4         5         6         7
    //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
    QByteArray method("struct A { short i; A(int b, int c) : i(c) { } virtual void test(int j) = 0; }; A instance;");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QVERIFY(!top->parentContext());
    QCOMPARE(top->childContexts().count(), 1);
    QCOMPARE(top->localDeclarations().count(), 2);
    QVERIFY(top->localScopeIdentifier().isEmpty());

    AbstractType::Ptr t = top->localDeclarations()[1]->abstractType();
    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(t.unsafeData());
    QVERIFY(idType);
    QCOMPARE( idType->qualifiedIdentifier(), QualifiedIdentifier("A") );

    Declaration* defStructA = top->localDeclarations().first();
    QCOMPARE(defStructA->identifier(), Identifier("A"));
    QCOMPARE(defStructA->uses().count(), 1);
    QCOMPARE(defStructA->uses().begin()->count(), 1);
    QVERIFY(defStructA->type<CppClassType>());
    ClassDeclaration* classDecl = dynamic_cast<ClassDeclaration*>(defStructA);
    QVERIFY(classDecl);
    QCOMPARE(classDecl->classType(), ClassDeclarationData::Struct);

    DUContext* structA = top->childContexts().first();
    QVERIFY(structA->parentContext());
    QCOMPARE(structA->importedParentContexts().count(), 0);
    QCOMPARE(structA->childContexts().count(), 3);
    QCOMPARE(structA->localDeclarations().count(), 3);
    QCOMPARE(structA->localScopeIdentifier(), QualifiedIdentifier("A"));

    Declaration* defI = structA->localDeclarations().first();
    QCOMPARE(defI->identifier(), Identifier("i"));
    QCOMPARE(defI->uses().count(), 1);
    QCOMPARE(defI->uses().begin()->count(), 1);

    QCOMPARE(findDeclaration(structA,  Identifier("i")), defI);
    QCOMPARE(findDeclaration(structA,  Identifier("b")), noDef);
    QCOMPARE(findDeclaration(structA,  Identifier("c")), noDef);

    DUContext* ctorImplCtx = structA->childContexts()[1];
    QVERIFY(ctorImplCtx->parentContext());
    QCOMPARE(ctorImplCtx->importedParentContexts().count(), 1);
    QCOMPARE(ctorImplCtx->childContexts().count(), 1);
    QCOMPARE(ctorImplCtx->localDeclarations().count(), 0);
    QVERIFY(!ctorImplCtx->localScopeIdentifier().isEmpty());
    QVERIFY(ctorImplCtx->owner());

    DUContext* ctorCtx = ctorImplCtx->importedParentContexts().first().context(0);
    QVERIFY(ctorCtx->parentContext());
    QCOMPARE(ctorCtx->childContexts().count(), 0);
    QCOMPARE(ctorCtx->localDeclarations().count(), 2);
    QCOMPARE(ctorCtx->localScopeIdentifier(), QualifiedIdentifier("A")); ///@todo check if it should really be this way

    Declaration* defB = ctorCtx->localDeclarations().first();
    QCOMPARE(defB->identifier(), Identifier("b"));
    QCOMPARE(defB->uses().count(), 0);

    Declaration* defC = ctorCtx->localDeclarations()[1];
    QCOMPARE(defC->identifier(), Identifier("c"));
    QCOMPARE(defC->uses().count(), 1);
    QCOMPARE(defC->uses().begin()->count(), 1);

    Declaration* defTest = structA->localDeclarations()[2];
    QCOMPARE(defTest->identifier(), Identifier("test"));
    ClassFunctionDeclaration* classFunDecl = dynamic_cast<ClassFunctionDeclaration*>(defTest);
    QVERIFY(classFunDecl);
    QVERIFY(classFunDecl->isAbstract());

    QCOMPARE(findDeclaration(ctorCtx,  Identifier("i")), defI);
    QCOMPARE(findDeclaration(ctorCtx,  Identifier("b")), defB);
    QCOMPARE(findDeclaration(ctorCtx,  Identifier("c")), defC);

    DUContext* testCtx = structA->childContexts().last();
    QCOMPARE(testCtx->childContexts().count(), 0);
    QCOMPARE(testCtx->localDeclarations().count(), 1);
    QCOMPARE(testCtx->localScopeIdentifier(), QualifiedIdentifier("test")); ///@todo check if it should really be this way

    Declaration* defJ = testCtx->localDeclarations().first();
    QCOMPARE(defJ->identifier(), Identifier("j"));
    QCOMPARE(defJ->uses().count(), 0);

    /*DUContext* insideCtorCtx = ctorCtx->childContexts().first();
    QCOMPARE(insideCtorCtx->childContexts().count(), 0);
    QCOMPARE(insideCtorCtx->localDeclarations().count(), 0);
    QVERIFY(insideCtorCtx->localScopeIdentifier().isEmpty());*/

    release(top);
  }
  {
    //                 0         1         2         3         4         5         6         7
    //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
    QByteArray method("struct A { short i; }; struct A instance; void test(struct A * a) { a->i = instance.i; };");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QVERIFY(!top->parentContext());
    QCOMPARE(top->childContexts().count(), 3);
    QCOMPARE(top->localDeclarations().count(), 3);
    QVERIFY(top->localScopeIdentifier().isEmpty());

    Declaration* defStructA = top->localDeclarations().first();
    QCOMPARE(defStructA->identifier(), Identifier("A"));
    QCOMPARE(defStructA->uses().count(), 1);
    QCOMPARE(defStructA->uses().begin()->count(), 2);
    QVERIFY(defStructA->type<CppClassType>());
    ClassDeclaration* classDecl = dynamic_cast<ClassDeclaration*>(defStructA);
    QVERIFY(classDecl);
    QCOMPARE(classDecl->classType(), ClassDeclarationData::Struct);

    DUContext* structA = top->childContexts().first();
    QCOMPARE(structA->parentContext(), top);
    QCOMPARE(structA->importedParentContexts().count(), 0);
    QCOMPARE(structA->childContexts().count(), 0);
    QCOMPARE(structA->localDeclarations().count(), 1);
    QCOMPARE(structA->localScopeIdentifier(), QualifiedIdentifier("A"));

    Declaration* defI = structA->localDeclarations().first();
    QCOMPARE(defI->identifier(), Identifier("i"));
    QCOMPARE(defI->uses().count(), 1);
    QCOMPARE(defI->uses().begin()->count(), 2);

    QCOMPARE(findDeclaration(structA,  Identifier("i")), defI);

    release(top);
  }
}

void TestDUChain::testDeclareStructInNamespace()
{
  TEST_FILE_PARSE_ONLY

  {
    QByteArray method("namespace A { class B { class C; } ; } namespace A { class A::B::C { }; }");
    
    TopDUContext* top = parse(method, DumpAll);
    
    DUChainWriteLocker lock(DUChain::lock());
    
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[0]->childContexts().size(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations().size(), 1);
    
    ForwardDeclaration* forwardDecl = dynamic_cast<ForwardDeclaration*>(top->childContexts()[0]->childContexts()[0]->localDeclarations()[0]);
    QVERIFY(forwardDecl);
    QVERIFY(forwardDecl->resolve(top));
    
    release(top);
  }

  {
    //                 0         1         2         3         4         5         6         7
    //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
    QByteArray method("struct A {A(); struct B;}; struct A::B { B(); struct C; }; struct A::B::C { A mem; B mem2; void test(A param); }; void A::B::C::test(A param) {};");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QVERIFY(!top->parentContext());
    QCOMPARE(top->childContexts().count(), 5);
    QCOMPARE(top->localDeclarations().count(), 2); //Only one declaration, because the others are nested within helper scope contexts
    
    QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[3]->localDeclarations().count(), 1);
    
    QCOMPARE(top->childContexts()[2]->localScopeIdentifier(), QualifiedIdentifier("A::B"));
    QCOMPARE(top->childContexts()[2]->childContexts()[0]->localScopeIdentifier(), QualifiedIdentifier("C"));
    QCOMPARE(top->childContexts()[2]->childContexts()[0]->scopeIdentifier(true), QualifiedIdentifier("A::B::C"));
    QCOMPARE(top->childContexts()[2]->localDeclarations()[0]->qualifiedIdentifier(), QualifiedIdentifier("A::B::C"));
    QVERIFY(top->childContexts()[2]->inSymbolTable());
    QVERIFY(top->childContexts()[2]->childContexts()[0]->inSymbolTable());
    QVERIFY(top->childContexts()[2]->localDeclarations()[0]->inSymbolTable());
    
    QualifiedIdentifier search("::A::B::C");
    Declaration* cDecl = findDeclaration(top, search);
    QVERIFY(cDecl);
    QVERIFY(cDecl->logicalInternalContext(top));
    QVERIFY(cDecl);
    QVERIFY(!cDecl->isForwardDeclaration());
    QVERIFY(cDecl->internalContext());
    
    QCOMPARE(top->childContexts()[2]->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[2]->childContexts()[0]->localDeclarations().count(), 3);
    kDebug() << top->childContexts()[2]->childContexts()[0]->localDeclarations()[0]->abstractType()->toString();
    kDebug() << top->localDeclarations()[0]->abstractType()->toString();
    QCOMPARE(top->childContexts()[2]->childContexts()[0]->localDeclarations()[0]->indexedType(), top->localDeclarations()[0]->indexedType());
    QCOMPARE(top->childContexts()[2]->childContexts()[0]->localDeclarations()[1]->indexedType(), top->childContexts()[1]->localDeclarations()[0]->indexedType());
    kDebug() << top->childContexts()[3]->localDeclarations()[0]->abstractType()->toString();
    kDebug() << top->localDeclarations()[0]->abstractType()->toString();
    QCOMPARE(top->childContexts()[3]->localDeclarations()[0]->indexedType(), top->localDeclarations()[0]->indexedType());

    release(top);
  }
  //                 0         1         2         3         4         5         6         7
  //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  {
    QByteArray method("namespace B {class C {struct A;void test();};};using namespace B;struct C::A {};");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QVERIFY(!top->parentContext());
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations().count(), 2);
    QVERIFY(top->childContexts()[0]->childContexts()[0]->localDeclarations()[0]->isForwardDeclaration());
    KDevelop::ForwardDeclaration* forward = dynamic_cast<KDevelop::ForwardDeclaration*>(top->childContexts()[0]->childContexts()[0]->localDeclarations()[0]);
    QVERIFY(forward);
    QVERIFY(forward->resolve(top));

    release(top);
  }
}
void TestDUChain::testCStruct()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  QByteArray method("struct A {int i; }; struct A instance; typedef struct { int a; } B, *BPointer;");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations().count(), 5);

  QVERIFY(top->localDeclarations()[0]->kind() == Declaration::Type);
  QVERIFY(top->localDeclarations()[1]->kind() == Declaration::Instance);

  release(top);
}

void TestDUChain::testCStruct2()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  {
  QByteArray method("struct A {struct C c; }; struct C { int v; };");

  //Expected result: the elaborated type-specifier "struct C" within the declaration "struct A" should create a new global declaration.
  

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations().count(), 3); //3 declarations because the elaborated type-specifier "struct C" creates a forward-declaration on the global scope
  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
  kDebug() << "TYPE:" << top->childContexts()[0]->localDeclarations()[0]->abstractType()->toString() << typeid(*top->childContexts()[0]->localDeclarations()[0]->abstractType()).name();
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->indexedType(), top->localDeclarations()[1]->indexedType());

  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->kind(), Declaration::Instance);
  //QVERIFY(top->localDeclarations()[0]->kind() == Declaration::Type);
  //QVERIFY(top->localDeclarations()[1]->kind() == Declaration::Instance);
  release(top);
  }
  {
  QByteArray method("struct A {inline struct C c() {}; }; struct C { int v; };");

  //Expected result: the elaborated type-specifier "struct C" within the declaration "struct A" should create a new global declaration.
  

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations().count(), 3); //3 declarations because the elaborated type-specifier "struct C" creates a forward-declaration on the global scope
  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
  kDebug() << "TYPE:" << top->childContexts()[0]->localDeclarations()[0]->abstractType()->toString() << typeid(*top->childContexts()[0]->localDeclarations()[0]->abstractType()).name();
  FunctionType::Ptr function(top->childContexts()[0]->localDeclarations()[0]->type<FunctionType>());
  QVERIFY(function);
  kDebug() << "RETURN:" << function->returnType()->toString() << typeid(*function->returnType()).name();
  QCOMPARE(function->returnType()->indexed(), top->localDeclarations()[1]->indexedType());
  //QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->indexedType(), top->localDeclarations()[1]->indexedType());

  //QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->kind(), Declaration::Instance);
  //QVERIFY(top->localDeclarations()[0]->kind() == Declaration::Type);
  //QVERIFY(top->localDeclarations()[1]->kind() == Declaration::Instance);
  release(top);
  }

}

void TestDUChain::testVariableDeclaration()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
    QByteArray method("int c; A instance(c); A instance(2, 3); A instance(q); bla() {int* i = new A(c); }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations().count(), 5);
  QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);
  QCOMPARE(top->localDeclarations()[0]->uses().begin()->count(), 2);
  QVERIFY(top->localScopeIdentifier().isEmpty());

//   IdentifiedType* idType = dynamic_cast<IdentifiedType*>(top->localDeclarations()[1]->abstractType().data());
//   QVERIFY(idType);
//   QCOMPARE( idType->identifier(), QualifiedIdentifier("A") );
//
//   Declaration* defStructA = top->localDeclarations().first();
//   QCOMPARE(defStructA->identifier(), Identifier("A"));

  release(top);
}

void TestDUChain::testDeclareClass()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  QByteArray method("class A { A() {}; A rec; void test(int); }; void A::test(int j) {}");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  DumpDotGraph dump;
//  kDebug() << "dot-graph: \n" << dump.dotGraph(top, false);

//  kDebug() << "ENDE ENDE ENDE";
//  kDebug() << "dot-graph: \n" << dump.dotGraph(top, false);

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->localDeclarations().count(), 2);
  QVERIFY(top->localScopeIdentifier().isEmpty());

  Declaration* defClassA = top->localDeclarations().first();
  QCOMPARE(defClassA->identifier(), Identifier("A"));
  QCOMPARE(defClassA->uses().count(), 1);
  QCOMPARE(defClassA->uses().begin()->count(), 2);
  QVERIFY(defClassA->type<CppClassType>());
  QVERIFY(defClassA->internalContext());
  QCOMPARE(defClassA->internalContext()->range().start.column, 8); //The class-context should start directly behind the name
//   QVERIFY(function);
//   QCOMPARE(function->returnType(), typeVoid);
//   QCOMPARE(function->arguments().count(), 1);
//   QCOMPARE(function->arguments().first(), typeInt);

  QVERIFY(defClassA->internalContext());

  QCOMPARE(defClassA->internalContext()->localDeclarations().count(), 3);
  QCOMPARE(defClassA->internalContext()->localDeclarations()[1]->abstractType()->indexed(), defClassA->abstractType()->indexed());

  DUContext* classA = top->childContexts().first();
  QVERIFY(classA->parentContext());
  QCOMPARE(classA->importedParentContexts().count(), 0);
  QCOMPARE(classA->childContexts().count(), 3);
  QCOMPARE(classA->localDeclarations().count(), 3);
  QCOMPARE(classA->localScopeIdentifier(), QualifiedIdentifier("A"));

  Declaration* defRec = classA->localDeclarations()[1];
  QVERIFY(defRec->abstractType());
  QCOMPARE(defRec->abstractType()->indexed(), defClassA->abstractType()->indexed());
           
  release(top);
}

void TestDUChain::testDeclareFriend()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  QByteArray method("class B {}; class A { friend class F; friend class B; }; ");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  DumpDotGraph dump;
//  kDebug() << "dot-graph: \n" << dump.dotGraph(top, false);

//  kDebug() << "ENDE ENDE ENDE";
//  kDebug() << "dot-graph: \n" << dump.dotGraph(top, false);

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations().count(), 3); //1 Forward-declaration of F
  QVERIFY(top->localScopeIdentifier().isEmpty());

  Declaration* defClassA = top->localDeclarations()[1];
  QCOMPARE(defClassA->identifier(), Identifier("A"));
  QCOMPARE(defClassA->uses().count(), 0);
  QVERIFY(defClassA->type<CppClassType>());
  QVERIFY(defClassA->internalContext());

  QCOMPARE(top->localDeclarations()[2]->identifier(), Identifier("F"));
  
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 2); //friend-declaration
  QCOMPARE(top->childContexts()[1]->localDeclarations()[0]->identifier(), Identifier("friend"));
  QCOMPARE(top->childContexts()[1]->localDeclarations()[1]->identifier(), Identifier("friend"));
  
  QVERIFY(Cpp::isFriend(top->localDeclarations()[1], top->localDeclarations()[2]));
  QVERIFY(Cpp::isFriend(top->localDeclarations()[1], top->localDeclarations()[0]));

  QVERIFY(top->localDeclarations()[0]->uses().size());
  QVERIFY(top->localDeclarations()[0]->uses().begin()->size());
  
  release(top);
}

void TestDUChain::testDeclareNamespace()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  QByteArray method("namespace foo { int bar; } int bar; int test() { return foo::bar; }");

  TopDUContext* top = parse(method, DumpNone);


  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->localDeclarations().count(), 3);
  QVERIFY(top->localScopeIdentifier().isEmpty());
  QCOMPARE(findDeclaration(top, Identifier("foo")), top->localDeclarations()[0]);
  QCOMPARE(top->childContexts()[0]->range(), SimpleRange(0, 14, 0, 26));
  QVERIFY(top->localDeclarations()[0]->inSymbolTable());

  QVERIFY(top->localDeclarations()[0]->inSymbolTable());
  QVERIFY(top->localDeclarations()[0]->uses().size());
  
  DUContext* fooCtx = top->childContexts().first();
  QVERIFY(fooCtx->inSymbolTable());
  QCOMPARE(fooCtx->childContexts().count(), 0);
  QCOMPARE(fooCtx->localDeclarations().count(), 1);
  QCOMPARE(fooCtx->localScopeIdentifier(), QualifiedIdentifier("foo"));
  QCOMPARE(fooCtx->scopeIdentifier(), QualifiedIdentifier("foo"));

  DUContext* testCtx = top->childContexts()[2];
  QCOMPARE(testCtx->childContexts().count(), 0);
  QCOMPARE(testCtx->localDeclarations().count(), 0);
  QCOMPARE(testCtx->localScopeIdentifier(), QualifiedIdentifier("test"));
  QCOMPARE(testCtx->scopeIdentifier(), QualifiedIdentifier("test"));

  Declaration* bar2 = top->localDeclarations()[1];
  QCOMPARE(bar2->identifier(), Identifier("bar"));
  QCOMPARE(bar2->qualifiedIdentifier(), QualifiedIdentifier("bar"));
  QCOMPARE(bar2->uses().count(), 0);

  Declaration* bar = fooCtx->localDeclarations().first();
  QCOMPARE(bar->identifier(), Identifier("bar"));
  QCOMPARE(bar->qualifiedIdentifier(), QualifiedIdentifier("foo::bar"));
  QCOMPARE(findDeclaration(testCtx,  QualifiedIdentifier("foo::bar")), bar);
  QCOMPARE(bar->uses().count(), 1);
  QCOMPARE(bar->uses().begin()->count(), 1);
  QCOMPARE(findDeclaration(top, bar->identifier()), bar2);
  QCOMPARE(findDeclaration(top, bar->qualifiedIdentifier()), bar);

  QCOMPARE(findDeclaration(top, QualifiedIdentifier("bar")), bar2);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("::bar")), bar2);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("foo::bar")), bar);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("::foo::bar")), bar);

  IndexedQualifiedIdentifier fooId(QualifiedIdentifier("foo"));

  uint itemCount;
  const KDevelop::CodeModelItem* items;
  CodeModel::self().items(top->url(), itemCount, items);
  for(uint a = 0; a < itemCount; ++a) {
    if(items[a].id == fooId) {
      QCOMPARE(items[a].referenceCount, 1u); //Once by the namespace, once by the declaration
    }
  }

  release(top);
}

void TestDUChain::testDeclareNamespace2()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("struct A {}; namespace B { struct A : ::A {}; }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->localDeclarations().count(), 2);
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  
  CppClassType::Ptr outerA = top->localDeclarations()[0]->abstractType().cast<CppClassType>();
  CppClassType::Ptr innerA = top->childContexts()[1]->localDeclarations()[0]->abstractType().cast<CppClassType>();
  
  QVERIFY(outerA);
  QVERIFY(innerA);
  
  Declaration* klassDecl = innerA->declaration(top);
  ClassDeclaration* cppClassDecl = dynamic_cast<ClassDeclaration*>(klassDecl);

  QVERIFY(cppClassDecl);
  QCOMPARE(cppClassDecl->baseClassesSize(), (uint)1);
  QCOMPARE(cppClassDecl->baseClasses()->baseClass, outerA->indexed());

  release(top);
}

void TestDUChain::testSearchAcrossNamespace()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("namespace A { class B{}; } class B{}; namespace A{ B bla; }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 1);
  QVERIFY(top->childContexts()[2]->localDeclarations()[0]->abstractType());
  QCOMPARE(top->childContexts()[2]->localDeclarations()[0]->abstractType()->indexed(), top->childContexts()[0]->localDeclarations()[0]->abstractType()->indexed());
  QVERIFY(!top->childContexts()[2]->localDeclarations()[0]->abstractType().cast<DelayedType>());

  release(top);
}

void TestDUChain::testSearchAcrossNamespace2()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("namespace A {class B{}; } namespace A { class C{ void member(B);};  } void A::C::member(B b) {B c;}");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 4);
  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[2]->type(), DUContext::Function);
  QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[2]->localScopeIdentifier(), QualifiedIdentifier("A::C::member"));
  QVERIFY(top->childContexts()[2]->localDeclarations()[0]->abstractType()); //B should be found from that position
  QVERIFY(!top->childContexts()[2]->localDeclarations()[0]->abstractType().cast<DelayedType>());
  QCOMPARE(top->childContexts()[3]->type(), DUContext::Other);
  QCOMPARE(top->childContexts()[3]->localDeclarations().count(), 1);
  QVERIFY(top->childContexts()[3]->localDeclarations()[0]->abstractType()); //B should be found from that position
  QCOMPARE(top->childContexts()[3]->localScopeIdentifier(), QualifiedIdentifier("A::C::member"));
  QVERIFY(!top->childContexts()[3]->localDeclarations()[0]->abstractType().cast<DelayedType>());

  release(top);
}

void TestDUChain::testSearchAcrossNamespace3()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("namespace B { class C { }; } namespace A { class C; } using namespace B; namespace A { C c; };");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->localDeclarations().count(), 4);
  QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[2]->localDeclarations()[0]->abstractType()->toString(), QString("A::C"));
}

#define V_CHILD_COUNT(context, cnt) QCOMPARE(context->childContexts().count(), cnt)
#define V_DECLARATION_COUNT(context, cnt) QCOMPARE(context->localDeclarations().count(), cnt)

#define V_USE_COUNT(declaration, cnt) QCOMPARE(declaration->uses().count(), 1); QCOMPARE(declaration->uses().begin()->count(), cnt);


DUContext* childContext(DUContext* ctx, const QString& name) {
  foreach(DUContext* child, ctx->childContexts())
    if(child->localScopeIdentifier().toString() == name)
      return child;
  return 0;
}

Declaration* localDeclaration(DUContext* ctx, const QString& name) {
  foreach(Declaration* decl, ctx->localDeclarations())
    if(decl->identifier().toString() == name)
      return decl;
  return 0;
}

void TestDUChain::testUsingDeclaration()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("namespace n1 { class C{}; C c2; } namespace n2{ using n1::C; using n1::c2; C c = c2; }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  V_CHILD_COUNT(top, 2);
  QVERIFY(childContext(top, "n1"));
  V_DECLARATION_COUNT(childContext(top, "n1"), 2);
  QVERIFY(childContext(top, "n2"));
  V_DECLARATION_COUNT(childContext(top, "n2"), 3);
  V_USE_COUNT(top->childContexts()[0]->localDeclarations()[0], 3);
  V_USE_COUNT(top->childContexts()[0]->localDeclarations()[1], 2);
  QCOMPARE(localDeclaration(childContext(top, "n2"), "c")->abstractType()->indexed(), top->childContexts()[0]->localDeclarations()[0]->abstractType()->indexed());

  release(top);
}

void TestDUChain::testUsingDeclarationInTemplate()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("template<class T> class A { T i; }; template<class Q> struct B: private A<Q> { using A<T>::i; };");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  Declaration* basicDecl = findDeclaration(top, QualifiedIdentifier("B::i"));
  QVERIFY(basicDecl);
  kDebug() << typeid(*basicDecl).name() << basicDecl->toString();
  QVERIFY(basicDecl->abstractType());
  kDebug() << basicDecl->abstractType()->toString();

  Declaration* decl = findDeclaration(top, QualifiedIdentifier("B<int>::i"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QVERIFY(decl->type<IntegralType>());

  release(top);
}

void TestDUChain::testDeclareUsingNamespace2()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("namespace foo2 {int bar2; namespace SubFoo { int subBar2; } }; namespace foo { int bar; using namespace foo2; } namespace GFoo{ namespace renamedFoo2 = foo2; using namespace renamedFoo2; using namespace SubFoo; int gf; } using namespace GFoo; int test() { return bar; }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  release(top);
  return;

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 5);
  QCOMPARE(top->localDeclarations().count(), 5);
  QVERIFY(top->localScopeIdentifier().isEmpty());
  QVERIFY(findDeclaration(top, Identifier("foo")));

  QCOMPARE( top->childContexts().first()->localDeclarations().count(), 2);
  Declaration* bar2 = top->childContexts().first()->localDeclarations()[0];
  QVERIFY(bar2);
  QCOMPARE(bar2->identifier(), Identifier("bar2"));

  QCOMPARE( top->childContexts()[1]->localDeclarations().count(), 2);
  Declaration* bar = top->childContexts()[1]->localDeclarations()[0];
  QVERIFY(bar);
  QCOMPARE(bar->identifier(), Identifier("bar"));

  QCOMPARE( top->childContexts()[2]->localDeclarations().count(), 4);
  Declaration* gf = top->childContexts()[2]->localDeclarations()[3];
  QVERIFY(gf);
  QCOMPARE(gf->identifier(), Identifier("gf"));

  //QCOMPARE(top->namespaceAliases().count(), 1);
  //QCOMPARE(top->namespaceAliases().first()->nsIdentifier, QualifiedIdentifier("foo"));

  //QCOMPARE(findDeclaration(top, QualifiedIdentifier("bar2")), bar2);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("::bar")), noDef);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("foo::bar2")), bar2);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("foo2::bar2")), bar2);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("::foo::bar2")), bar2);

  //bar2 can be found from GFoo
  QCOMPARE(findDeclaration(top->childContexts()[2], QualifiedIdentifier("bar2")), bar2);
  QCOMPARE(findDeclaration(top->childContexts()[2], QualifiedIdentifier("bar")), noDef);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("gf")), gf);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("bar2")), bar2);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("::GFoo::renamedFoo2::bar2")), bar2);
  QVERIFY(findDeclaration(top, QualifiedIdentifier("subBar2")) != noDef);

  release(top);
}

void TestDUChain::testLocalNamespaceAlias()                                                                                                                                
{
  QByteArray method("namespace foo { int bar(); } int test() { namespace afoo = foo; afoo::bar(); }");

  LockedTopDUContext top( parse(method, DumpAll) );

  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->childContexts()[2]->localDeclarations().size(), 1);
  NamespaceAliasDeclaration* aliasDecl = dynamic_cast<NamespaceAliasDeclaration*>(top->childContexts()[2]->localDeclarations()[0]);
  QVERIFY(aliasDecl);
  QCOMPARE(aliasDecl->importIdentifier(), QualifiedIdentifier("foo"));
  QCOMPARE(aliasDecl->identifier(), Identifier("afoo"));
  QCOMPARE(top->childContexts()[0]->localDeclarations().size(), 1);
  
  QEXPECT_FAIL("", "Local namespace aliases currently don't work, bug 207548", Abort);
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().size(), 1);
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->size(), 1);
}

void TestDUChain::testDeclareUsingNamespace()
{
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 01234567890123456789012345678901234567890123456789012345678901234567890123456789
  QByteArray method("namespace foo { int bar; } using namespace foo; namespace alternativeFoo = foo; int test() { return bar; }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->localDeclarations().count(), 4);
  QVERIFY(top->localScopeIdentifier().isEmpty());
  QVERIFY(findDeclaration(top, Identifier("foo")));
  QCOMPARE(top->localDeclarations()[0]->uses().size(), 1);
  QCOMPARE(top->localDeclarations()[0]->uses().begin()->size(), 2);
  QCOMPARE(top->localDeclarations()[0]->uses().begin()->at(0), SimpleRange(0, 65-22, 0, 68-22));
  QCOMPARE(top->localDeclarations()[0]->uses().begin()->at(1), SimpleRange(0, 97-22, 0, 100-22));


//   QCOMPARE(top->localDeclarations()[0]->range()
  QCOMPARE(top->localDeclarations()[1]->range(), SimpleRange(0, 33, 0, 42));
  kDebug() << top->localDeclarations()[2]->range().textRange();
  QCOMPARE(top->localDeclarations()[2]->range(), SimpleRange(0, 58, 0, 72));
  
  
  DUContext* fooCtx = top->childContexts().first();
  QCOMPARE(fooCtx->childContexts().count(), 0);
  QCOMPARE(fooCtx->localDeclarations().count(), 1);
  QCOMPARE(fooCtx->localScopeIdentifier(), QualifiedIdentifier("foo"));
  QCOMPARE(fooCtx->scopeIdentifier(), QualifiedIdentifier("foo"));

  Declaration* bar = fooCtx->localDeclarations().first();
  QCOMPARE(bar->identifier(), Identifier("bar"));
  QCOMPARE(bar->qualifiedIdentifier(), QualifiedIdentifier("foo::bar"));
  QCOMPARE(bar->uses().count(), 1);
  QCOMPARE(bar->uses().begin()->count(), 1);
  //kDebug() << findDeclaration(top, bar->identifier(), top->range().start)->qualifiedIdentifier().toString();
  QCOMPARE(findDeclaration(top, bar->identifier(), top->range().start), noDef);
  QCOMPARE(findDeclaration(top, bar->identifier()), bar);
  QCOMPARE(findDeclaration(top, bar->qualifiedIdentifier()), bar);

  QCOMPARE(findDeclaration(top, QualifiedIdentifier("bar")), bar);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("::bar")), bar); //iso c++ 3.4.3 says this must work
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("foo::bar")), bar);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("::foo::bar")), bar);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("alternativeFoo::bar")), bar);
  QCOMPARE(findDeclaration(top, QualifiedIdentifier("::alternativeFoo::bar")), bar);

  DUContext* testCtx = top->childContexts()[2];
  QVERIFY(testCtx->parentContext());
  QCOMPARE(testCtx->importedParentContexts().count(), 1);
  QCOMPARE(testCtx->childContexts().count(), 0);
  QCOMPARE(testCtx->localDeclarations().count(), 0);
  QCOMPARE(testCtx->localScopeIdentifier(), QualifiedIdentifier("test"));
  QCOMPARE(testCtx->scopeIdentifier(), QualifiedIdentifier("test"));

  release(top);
}

void TestDUChain::testSignalSlotDeclaration() {
  {
    QByteArray text("class C {__qt_signals__: void signal2(); public __qt_slots__: void slot2();}; ");

    TopDUContext* top = parse(text, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);

    ClassFunctionDeclaration* classFun = dynamic_cast<ClassFunctionDeclaration*>(top->childContexts()[0]->localDeclarations()[0]);
    QVERIFY(classFun);
    QVERIFY(classFun->accessPolicy() == ClassMemberDeclaration::Protected);
    QVERIFY(classFun->isSignal());

    classFun = dynamic_cast<ClassFunctionDeclaration*>(top->childContexts()[0]->localDeclarations()[1]);
    QVERIFY(classFun);
    QVERIFY(classFun->accessPolicy() == ClassMemberDeclaration::Public);
    QVERIFY(classFun->isSlot());

    release(top);
  }
  {
    QByteArray text("namespace A { class B;} class Q { public __qt_slots__: void slot(A::B b, const Q* q); }; ");

    TopDUContext* top = parse(text, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
    QtFunctionDeclaration* qtDecl = dynamic_cast<QtFunctionDeclaration*>(top->childContexts()[1]->localDeclarations()[0]);
    QVERIFY(qtDecl);
    QCOMPARE(qtDecl->normalizedSignature().str(), QString("A::B,const Q*"));

    release(top);
  }
  {
    // should be valid signals even if the arguments are not found
    QByteArray text("class C {__qt_signals__: void signal2(NotFound); public __qt_slots__: void slot2(NotFound);}; ");

    TopDUContext* top = parse(text, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);

    ClassFunctionDeclaration* classFun = dynamic_cast<ClassFunctionDeclaration*>(top->childContexts()[0]->localDeclarations()[0]);
    QVERIFY(classFun);
    QVERIFY(classFun->accessPolicy() == ClassMemberDeclaration::Protected);
    QVERIFY(classFun->isSignal());

    classFun = dynamic_cast<ClassFunctionDeclaration*>(top->childContexts()[0]->localDeclarations()[1]);
    QVERIFY(classFun);
    QVERIFY(classFun->accessPolicy() == ClassMemberDeclaration::Public);
    QVERIFY(classFun->isSlot());

    release(top);
  }
}

void TestDUChain::testSignalSlotUse() {
  {
    QByteArray text("class TE; class QObject { void connect(QObject* from, const char* signal, QObject* to, const char* slot); void connect(QObject* from, const char* signal, const char* slot); }; class A : public QObject { public __qt_slots__: void slot1(); void slot2(TE*); __qt_signals__: void signal1(TE*, char);void signal2(); public: void test() { \nconnect(this, __qt_signal__( signal1(TE*, const char&)), this, __qt_slot__(slot2(TE*))); \nconnect(this, __qt_signal__(signal2()), \n__qt_slot__(slot1())); } };");

    TopDUContext* top = parse(text, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 3);
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 5);
    QtFunctionDeclaration* qtDecl = dynamic_cast<QtFunctionDeclaration*>(top->childContexts()[1]->localDeclarations()[0]);
    QVERIFY(qtDecl);
    QVERIFY(top->childContexts()[1]->localDeclarations()[0]->uses().count());
    //kDebug() << top->childContexts()[1]->localDeclarations()[0]->uses().begin()->first().textRange();
    //kDebug() << top->childContexts()[1]->localDeclarations()[1]->uses().begin()->first().textRange();
    //kDebug() << top->childContexts()[1]->localDeclarations()[2]->uses().begin()->first().textRange();
    //kDebug() << top->childContexts()[1]->localDeclarations()[3]->uses().begin()->first().textRange();
    QCOMPARE(top->childContexts()[1]->localDeclarations()[0]->uses().begin()->first().textRange(), SimpleRange(3, 12, 3, 17).textRange());
    QVERIFY(top->childContexts()[1]->localDeclarations()[1]->uses().count());
    QCOMPARE(top->childContexts()[1]->localDeclarations()[1]->uses().begin()->first().textRange(), SimpleRange(1, 75, 1, 80).textRange());
    QVERIFY(top->childContexts()[1]->localDeclarations()[2]->uses().count());
    QCOMPARE(top->childContexts()[1]->localDeclarations()[2]->uses().begin()->first(), SimpleRange(1, 29, 1, 36));
    QVERIFY(top->childContexts()[1]->localDeclarations()[3]->uses().count());
    QCOMPARE(top->childContexts()[1]->localDeclarations()[3]->uses().begin()->first(), SimpleRange(2, 28, 2, 35));

    QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->count(), 4);

    release(top);
  }
  {
    QByteArray text("class QObject { void connect(QObject* from, const char* signal, QObject* to, const char* slot); void connect(QObject* from, const char* signal, const char* slot); }; struct AA : QObject { __qt_signals__: void signal1(int); void signal2(); };struct T{operator AA*() const; };class A : AA { public __qt_slots__: void slot1(); void slot2(); __qt_signals__: void signal1();public: void test() {T t;connect(t, __qt_signal__(signal2()), this, __qt_slot__(slot2()));connect(this, __qt_signal__(signal1(int)), __qt_slot__(slot1())); } }; ");

    TopDUContext* top = parse(text, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 4);
    QCOMPARE(top->childContexts().count(), 4);
    QCOMPARE(top->childContexts()[3]->localDeclarations().count(), 4);
    QVERIFY(top->childContexts()[3]->localDeclarations()[0]->uses().count());
    QVERIFY(top->childContexts()[3]->localDeclarations()[1]->uses().count());
    QVERIFY(!top->childContexts()[3]->localDeclarations()[2]->uses().count()); //signal1() is not used

    QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 2);
    ClassDeclaration* classAA = dynamic_cast<ClassDeclaration*>(top->localDeclarations()[1]);
    QVERIFY(classAA);
    QCOMPARE(classAA->baseClassesSize(), 1u);
    QCOMPARE(classAA->baseClasses()[0].access, Declaration::Public);
    ClassDeclaration* classA = dynamic_cast<ClassDeclaration*>(top->localDeclarations()[3]);
    QVERIFY(classA);
    QCOMPARE(classA->baseClassesSize(), 1u);
    QCOMPARE(classA->baseClasses()[0].access, Declaration::Private);

    QVERIFY(top->childContexts()[1]->localDeclarations()[0]->uses().count());
    QVERIFY(top->childContexts()[1]->localDeclarations()[1]->uses().count());

    release(top);
  }
  {
    // test signals without full signal signature specification
    QByteArray text("class QObject { void connect(QObject* from, const char* signal, QObject* to, const char* slot);\n"
                    "  void connect(QObject* from, const char* signal, const char* slot); };\n"
                    "struct AA : QObject { __qt_signals__: void signal1(bool arg1 = false); };\n"
                    "class A : AA { public __qt_slots__: void slot1();\n"
                    "  public: void test() { connect(this, __qt_signal__(signal1()), this, __qt_slot__(slot1()));} };");
    TopDUContext* top = parse(text, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QVERIFY(top->problems().isEmpty());

    QCOMPARE(top->localDeclarations().count(), 3);
    QCOMPARE(top->childContexts().count(), 3);
    QCOMPARE(top->childContexts().at(1)->localDeclarations().size(), 1);
    ClassFunctionDeclaration* sig = dynamic_cast<ClassFunctionDeclaration*>(top->childContexts().at(1)->localDeclarations().first());
    QVERIFY(sig);
    QVERIFY(sig->identifier() == Identifier("signal1"));
    QVERIFY(sig->isSignal());
    QCOMPARE(sig->uses().size(), 1);
    QCOMPARE(sig->uses().begin()->count(), 1);
    QCOMPARE(sig->uses().begin()->first(), SimpleRange(4, 52, 4, 59));
  }
}

void TestDUChain::testFunctionDefinition() {
  //               0         1         2         3         4         5         6         7
  //               01234567890123456789012345678901234567890123456789012345678901234567890123456789
  QByteArray text("class B{}; class A { char at(B* b); A(); ~A(); }; \n char A::at(B* b) {B* b; at(b); }; A::A() : i(3) {}; A::~A() {}; ");

  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 8);
  QCOMPARE(top->childContexts()[1]->childContexts().count(), 3);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 3);

  QCOMPARE(top->childContexts()[4]->type(), DUContext::Function);

  Declaration* atInA = top->childContexts()[1]->localDeclarations()[0];

  QVERIFY(dynamic_cast<AbstractFunctionDeclaration*>(atInA));

  QVERIFY(atInA->internalContext());
  QCOMPARE(atInA->internalContext()->range(), SimpleRange(0, 29, 0, 33));
  
  QCOMPARE(top->localDeclarations().count(), 5);

  QVERIFY(top->localDeclarations()[1]->logicalInternalContext(top));
  QVERIFY(!top->childContexts()[1]->localDeclarations()[0]->isDefinition());
  QVERIFY(!top->childContexts()[1]->localDeclarations()[1]->isDefinition());
  QVERIFY(!top->childContexts()[1]->localDeclarations()[2]->isDefinition());

  QVERIFY(top->localDeclarations()[2]->isDefinition());
  QVERIFY(top->localDeclarations()[3]->isDefinition());
  QVERIFY(top->localDeclarations()[4]->isDefinition());

  QVERIFY(top->localDeclarations()[2]->internalContext());
  QVERIFY(top->localDeclarations()[3]->internalContext());
  QVERIFY(top->localDeclarations()[4]->internalContext());

  QCOMPARE(top->localDeclarations()[2]->internalContext()->owner(), top->localDeclarations()[2]);
  QCOMPARE(top->localDeclarations()[3]->internalContext()->owner(), top->localDeclarations()[3]);
  QCOMPARE(top->localDeclarations()[4]->internalContext()->owner(), top->localDeclarations()[4]);

  QVERIFY(top->localDeclarations()[1]->logicalInternalContext(top));

  QVERIFY(dynamic_cast<FunctionDefinition*>(top->localDeclarations()[2]));
  QCOMPARE(static_cast<FunctionDefinition*>(top->localDeclarations()[2])->declaration(), top->childContexts()[1]->localDeclarations()[0]);
  QCOMPARE(top->localDeclarations()[2], FunctionDefinition::definition(top->childContexts()[1]->localDeclarations()[0]));

  QVERIFY(dynamic_cast<FunctionDefinition*>(top->localDeclarations()[3]));
  QCOMPARE(static_cast<FunctionDefinition*>(top->localDeclarations()[3])->declaration(), top->childContexts()[1]->localDeclarations()[1]);
  QCOMPARE(top->localDeclarations()[3], FunctionDefinition::definition(top->childContexts()[1]->localDeclarations()[1]));
  QCOMPARE(top->childContexts()[5]->owner(), top->localDeclarations()[3]);
  QVERIFY(!top->childContexts()[5]->localScopeIdentifier().isEmpty());

  QVERIFY(top->localDeclarations()[1]->logicalInternalContext(top));
  QVERIFY(dynamic_cast<FunctionDefinition*>(top->localDeclarations()[4]));
  QCOMPARE(static_cast<FunctionDefinition*>(top->localDeclarations()[4])->declaration(), top->childContexts()[1]->localDeclarations()[2]);
  QCOMPARE(top->localDeclarations()[4], FunctionDefinition::definition(top->childContexts()[1]->localDeclarations()[2]));

  QVERIFY(top->localDeclarations()[1]->logicalInternalContext(top));

  QCOMPARE(top->childContexts()[3]->owner(), top->localDeclarations()[2]);
  QCOMPARE(top->childContexts()[3]->importedParentContexts().count(), 1);
  QCOMPARE(top->childContexts()[3]->importedParentContexts()[0].context(0), top->childContexts()[2]);

  QCOMPARE(top->childContexts()[2]->importedParentContexts().count(), 1);
  QCOMPARE(top->childContexts()[2]->importedParentContexts()[0].context(0), top->childContexts()[1]);


  QCOMPARE(findDeclaration(top, QualifiedIdentifier("at")), noDef);

  QVERIFY(top->localDeclarations()[2]->internalContext());
  QCOMPARE(top->localDeclarations()[2]->internalContext()->localDeclarations().count(), 1);

  release(top);
}

Declaration* declaration(Declaration* decl, TopDUContext* top = 0) {
  FunctionDefinition* def = dynamic_cast<FunctionDefinition*>(decl);
  if(!def)
    return 0;
  return def->declaration(top);
}

void TestDUChain::testFunctionDefinition2() {
  {
    QByteArray text("//????\nclass B{B();}; B::B() {} "); //the ???? tests whether the column-numbers are resistant to special characters

    TopDUContext* top = parse(text, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->childContexts().count(), 3);

    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations().count(), 2);

    QCOMPARE(top->childContexts()[0]->localDeclarations()[0], declaration(top->localDeclarations()[1], top->topContext()));

    QCOMPARE(top->childContexts()[1]->type(), DUContext::Function);
    QCOMPARE(top->childContexts()[1]->range().start.column, 20);
    QCOMPARE(top->childContexts()[1]->range().end.column, 20);
    //Many parts of kdevelop assume that the compound parens are included in the range, so it has to stay like that
    QCOMPARE(top->childContexts()[0]->range(), SimpleRange(1, 7, 1, 13));
    QCOMPARE(top->childContexts()[2]->range(), SimpleRange(1, 22, 1, 24));

    QVERIFY(!top->childContexts()[2]->inSymbolTable());

    release(top);
  }
  {
    QByteArray text("void test(int a, int cc);"); //the ???? tests whether the column-numbers are resistant to special characters

    TopDUContext* top = parse(text, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->childContexts().count(), 1);

    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);

    QCOMPARE(top->childContexts()[0]->range().start.column, 10);
    QCOMPARE(top->childContexts()[0]->range().end.column, 23);

    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->range().start.column, 21);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->range().end.column, 23);

    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->range().start.column, 14);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->range().end.column, 15);

    release(top);
  }

  {
    QByteArray text("void test(int, int&);");

    TopDUContext* top = parse(text, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->childContexts().count(), 1);

    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);

    release(top);
  }

}

void TestDUChain::testFunctionDefinition4() {
  QByteArray text("class B{ B(); }; namespace A{class B{B();};} namespace A{ B::B() {} } ");

  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 3);

  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[1]->childContexts().count(), 1);
  QCOMPARE(top->childContexts()[1]->childContexts()[0]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 1);

  QVERIFY(top->childContexts()[2]->localDeclarations()[0]->isDefinition());
  QCOMPARE(declaration(top->childContexts()[2]->localDeclarations()[0]), top->childContexts()[1]->childContexts()[0]->localDeclarations()[0]);
  //Verify that the function-definition context also imports the correct class context
  QVERIFY(top->childContexts()[2]->localDeclarations()[0]->internalContext()->imports(top->childContexts()[1]->childContexts()[0]));
  release(top);
}

void TestDUChain::testFunctionDefinition3() {
  QByteArray text("class B{template<class T> void test(T t); B(int i); int test(int a); int test(char a); template<class T2, class T3> void test(T2 t, T3 t3); int test(Unknown k); int test(Unknown2 k); }; template<class T> void B::test(T t) {}; B::B(int) {}; int B::test(int a){}; int B::test(char a){}; template<class T2, class T3> void B::test(T2 t, T3 t3) {}; int B::test(Unknown k){}; int B::test( Unknown2 k) {}; ");

  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 17);

  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 7);
  QCOMPARE(top->localDeclarations().count(), 8);

  QVERIFY(top->localDeclarations()[0]->isDefinition()); //Class-declarations are considered definitions too.
  QVERIFY(top->localDeclarations()[1]->isDefinition());
  QVERIFY(top->localDeclarations()[2]->isDefinition());
  QVERIFY(top->localDeclarations()[3]->isDefinition());
  QVERIFY(top->localDeclarations()[4]->isDefinition());
  QVERIFY(top->localDeclarations()[5]->isDefinition());
  QVERIFY(top->localDeclarations()[6]->isDefinition());

  kDebug() << top->childContexts()[0]->localDeclarations()[0]->toString();
  kDebug() << declaration(top->localDeclarations()[1], top->topContext())->toString();

  QCOMPARE(top->childContexts()[0]->localDeclarations()[0], declaration(top->localDeclarations()[1], top->topContext()));
  QCOMPARE(top->childContexts()[0]->localDeclarations()[1], declaration(top->localDeclarations()[2], top->topContext()));
  QCOMPARE(top->childContexts()[0]->localDeclarations()[2], declaration(top->localDeclarations()[3], top->topContext()));
  QCOMPARE(top->childContexts()[0]->localDeclarations()[3], declaration(top->localDeclarations()[4], top->topContext()));
  QCOMPARE(top->childContexts()[0]->localDeclarations()[4], declaration(top->localDeclarations()[5], top->topContext()));
  QCOMPARE(top->childContexts()[0]->localDeclarations()[5], declaration(top->localDeclarations()[6], top->topContext()));

  release(top);
}

void TestDUChain::testFunctionDefinition5() {
  QByteArray text("class Class {typedef Class ClassDef;void test(ClassDef);Class();}; void Class::test(ClassDef i) {Class c;}");
  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 3);
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().count(), 1); //Used from 1 file
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->count(), 2); //Used 2 times
  QCOMPARE(top->localDeclarations().count(), 2);
  QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 1);
  release(top);
}

void TestDUChain::testFunctionDefinition6() {
  QByteArray text("class Class {Class(); void test();}; void Class::test(Class c) {int i;}");
  ReferencedTopDUContext top = parse(text, DumpNone);
  
  //Here we do an update, since there was a bug where updating caused problems here
  parse(text, DumpNone, top.data());

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);
  QCOMPARE(top->localDeclarations().count(), 2);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  QList<Declaration*> decls = top->findDeclarations(QualifiedIdentifier("Class"));
  QCOMPARE(decls.size(), 1);
  kDebug() << "qualified identifier:" << top->childContexts()[0]->localDeclarations()[0]->qualifiedIdentifier();
  QVERIFY(!dynamic_cast<FunctionDefinition*>(top->childContexts()[0]->localDeclarations()[0]));
  QVERIFY(dynamic_cast<ClassFunctionDeclaration*>(top->childContexts()[0]->localDeclarations()[0]));
  QVERIFY(static_cast<ClassFunctionDeclaration*>(top->childContexts()[0]->localDeclarations()[0])->isConstructor());
  kDebug() << top->childContexts()[1]->localDeclarations()[0]->abstractType()->toString();
  QCOMPARE(top->localDeclarations()[1], top->childContexts()[2]->owner());
  QCOMPARE(Cpp::localClassFromCodeContext(top->childContexts()[2]), top->localDeclarations()[0]);
  QCOMPARE(Cpp::localClassFromCodeContext(top->childContexts()[1]), top->localDeclarations()[0]);
  QVERIFY(top->childContexts()[1]->importers().contains(top->childContexts()[2]));
  QCOMPARE(top->childContexts()[1]->localDeclarations()[0]->abstractType()->indexed(), top->localDeclarations()[0]->abstractType()->indexed());
  release(top.data());
}

void TestDUChain::testLoopNamespaceImport() {
  QByteArray text("namespace A {int i;} namespace B { using namespace A; } namespace A{ using namespace B; }; using namespace B; void test() { i += 5; }");
  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().count(), 5);
  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
  QVERIFY(!top->childContexts()[0]->localDeclarations()[0]->uses().isEmpty());

  release(top);
}

void TestDUChain::testConstructorUses()
{
  {
    QByteArray text = "struct A { A(A); A(A,A); }; void test() { A w; A a(A(w), A(w, w), w); }";
    TopDUContext* top = parse(text, DumpAll);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().size(), 3);
    QCOMPARE(top->childContexts()[2]->localDeclarations().size(), 2);
    QCOMPARE(top->childContexts()[2]->localDeclarations()[0]->uses().size(), 1);
    QCOMPARE(top->childContexts()[2]->localDeclarations()[0]->uses().begin()->size(), 4);

    QCOMPARE(top->childContexts()[0]->localDeclarations().size(), 2);
    
    ///@todo Make these work! (ExpressionVisitor::buildParametersFromDeclaration)
    #if 0
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0].uses().size(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1].uses().size(), 1);
    #endif
    
    release(top);
  }
  {
    // NOTE: This test is currently broken because only the 2nd case is reported.
    QByteArray text;
    text += "class Q {\n";
    text += "public:\n";
    text += "  Q(int var) { }\n";
    text += "  Q() { }\n";
    text += "};\n";
    text += "class B : public Q {\n";
    text += "public:\n";
    text += "  B(int var) : Q(var) { }\n";
    text += "  B() : Q() { }\n";
    text += "};\n";
    text += "int main(int argc, char* argv[]) {\n";
    text += "  Q  a1(123);\n";
    text += "  Q  a2 = Q(123);\n";
    text += "  Q *a3 = new Q(123);\n";
    text += "  Q  a4(argc);\n";
    text += "  Q  a12();\n";
    text += "  Q  a22 = Q();\n";
    text += "  Q *a32 = new Q();\n";
    text += "}\n";

    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->childContexts().size(), 4);
    QCOMPARE(top->childContexts()[0]->localDeclarations().size(), 2);

    // Q(int var)
    Declaration *ctorDecl = top->childContexts()[0]->localDeclarations()[0];
    QCOMPARE(ctorDecl->uses().size(), 1);
    QList<SimpleRange> uses = ctorDecl->uses().values().first();

    QCOMPARE(uses.size(), 5);
    QCOMPARE(uses[0], SimpleRange(7, 16, 7, 17));
    QCOMPARE(uses[1], SimpleRange(11, 7, 11, 8));
    QCOMPARE(uses[2], SimpleRange(12, 11, 12, 12));
    QCOMPARE(uses[3], SimpleRange(13, 15, 13, 16));
    QCOMPARE(uses[4], SimpleRange(14, 7, 14, 8));

    // Q()
    ctorDecl = top->childContexts()[0]->localDeclarations()[1];
    QCOMPARE(ctorDecl->uses().size(), 1);
    uses = ctorDecl->uses().values().first();

    QCOMPARE(uses.size(), 4);
    QCOMPARE(uses[0], SimpleRange(8, 9, 8, 10));
    QCOMPARE(uses[1], SimpleRange(15, 8, 15, 9));
    QCOMPARE(uses[2], SimpleRange(16, 12, 16, 13));
    QCOMPARE(uses[3], SimpleRange(17, 16, 17, 17));

    release(top);
  }
  {
    QByteArray text;
    text += "template<typename T>\n";
    text += "class Q {\n";
    text += "public:\n";
    text += "  Q(T var) { }\n";
    text += "  Q() { }\n";
    text += "};\n";
    text += "template<typename T>\n";
    text += "class B : public Q<T> {\n";
    text += "public:\n";
    text += "  B(T var) : Q<T>(var) { }\n";
    text += "  B() : Q<T>() { }\n";
    text += "};\n";
    text += "int main(int argc, char* argv[]) {\n";
    text += "  Q<int>  a1(123);\n";
    text += "  Q<int>  a2 = Q<int>(123);\n";
    text += "  Q<int> *a3 = new Q<int>(123);\n";
    text += "  Q<int>  a4(argc);\n";
    text += "  Q<int>  a12();\n";
    text += "  Q<int>  a22 = Q<int>();\n";
    text += "  Q<int> *a32 = new Q<int>();\n";
    text += "}\n";

    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->childContexts().size(), 6);
    // first one is the template context
    QCOMPARE(top->childContexts()[1]->type(), DUContext::Class);
    QCOMPARE(top->childContexts()[1]->localDeclarations().size(), 2);

    // Q(T var)
    Declaration *ctorDecl = top->childContexts()[1]->localDeclarations()[0];
    QEXPECT_FAIL("", "no uses get reported for ctors of template classes", Abort);
    QCOMPARE(ctorDecl->uses().size(), 1);
    ///TODO
    /*
    QList<SimpleRange> uses = ctorDecl->uses().values().first();

    QCOMPARE(uses.size(), 5);
    QCOMPARE(uses[0], SimpleRange(7, 16, 7, 17));
    QCOMPARE(uses[1], SimpleRange(11, 7, 11, 8));
    QCOMPARE(uses[2], SimpleRange(12, 11, 12, 12));
    QCOMPARE(uses[3], SimpleRange(13, 15, 13, 16));
    QCOMPARE(uses[4], SimpleRange(14, 7, 14, 8));

    // Q()
    ctorDecl = top->childContexts()[1]->localDeclarations()[1];
    QCOMPARE(ctorDecl->uses().size(), 1);
    uses = ctorDecl->uses().values().first();

    QCOMPARE(uses.size(), 4);
    QCOMPARE(uses[0], SimpleRange(8, 9, 8, 10));
    QCOMPARE(uses[1], SimpleRange(15, 8, 15, 9));
    QCOMPARE(uses[2], SimpleRange(16, 12, 16, 13));
    QCOMPARE(uses[3], SimpleRange(17, 16, 17, 17));
    */
    release(top);
  }
}

void TestDUChain::testCodeModel() {
  QByteArray text("class C{}; void test() {}; ");
  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  uint itemCount;
  const CodeModelItem* items;
  CodeModel::self().items( top->url(), itemCount, items );

  uint validCount = 0;
  for(uint a = 0; a < itemCount; ++a) {
    if(items[a].id.isValid()) {
      if(items[a].id == QualifiedIdentifier("C"))
        QVERIFY(items[a].kind == CodeModelItem::Class);
      if(items[a].id == QualifiedIdentifier("test"))
        QVERIFY(items[a].kind == CodeModelItem::Function);
      ++validCount;
    }
  }

  release(top);
}

void TestDUChain::testDoWhile() {
  QByteArray text("void test() { int i; do { i = 2; i -= 3; } while(1); ");
  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[1]->childContexts().count(), 2);
  kDebug() << top->childContexts()[1]->childContexts()[0] << top->childContexts()[1] << top;
  QCOMPARE(top->childContexts()[1]->childContexts()[0]->usesCount(), 2);

  release(top);
}

void TestDUChain::testEnumOverride() {
  QByteArray text("class B{class BA{};};class A{enum {B}; B::BA ba; };");
  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().size(), 2);
  QCOMPARE(top->childContexts()[0]->localDeclarations().size(), 1);
  QCOMPARE(top->childContexts()[1]->localDeclarations().size(), 2);
  QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->indexedType(), top->childContexts()[1]->localDeclarations()[1]->indexedType());

  release(top);
}

//Makes sure constructores cannot shadow the class itself
void TestDUChain::testDeclareSubClass() {
  QByteArray text("class Enclosing { class Class {Class(); class SubClass; class Frog; }; class Beach; }; class Enclosing::Class::SubClass { Class c; Frog f; Beach b; };");
  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);
  QCOMPARE(top->childContexts()[0]->childContexts().count(), 1);
  QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations().count(), 3);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[1]->childContexts().count(), 1);
  QCOMPARE(top->childContexts()[1]->childContexts()[0]->localDeclarations().count(), 3); //A virtual context is placed around SubClass

  QCOMPARE(top->childContexts()[0]->childContexts()[0]->scopeIdentifier(true), QualifiedIdentifier("Enclosing::Class"));
  QVERIFY(top->childContexts()[0]->childContexts()[0]->inSymbolTable());

  //Verify that "Class c" is matched correctly
  QVERIFY(top->childContexts()[1]->childContexts()[0]->localDeclarations()[0]->abstractType().unsafeData());
  QVERIFY(!top->childContexts()[1]->childContexts()[0]->localDeclarations()[0]->type<DelayedType>().unsafeData());
  QCOMPARE(top->childContexts()[1]->childContexts()[0]->localDeclarations()[0]->abstractType()->indexed(), top->childContexts()[0]->localDeclarations()[0]->abstractType()->indexed());

  //Verify that Frog is found
  QVERIFY(top->childContexts()[1]->childContexts()[0]->localDeclarations()[1]->abstractType().unsafeData());
  QVERIFY(!top->childContexts()[1]->childContexts()[0]->localDeclarations()[1]->type<DelayedType>().unsafeData());
  QCOMPARE(top->childContexts()[1]->childContexts()[0]->localDeclarations()[1]->abstractType()->indexed(), top->childContexts()[0]->childContexts()[0]->localDeclarations()[2]->abstractType()->indexed());

  //Make sure Beach is found
  QVERIFY(top->childContexts()[1]->childContexts()[0]->localDeclarations()[2]->abstractType().unsafeData());
  QVERIFY(!top->childContexts()[1]->childContexts()[0]->localDeclarations()[2]->type<DelayedType>().unsafeData());
  QCOMPARE(top->childContexts()[1]->childContexts()[0]->localDeclarations()[2]->abstractType()->indexed(), top->childContexts()[0]->localDeclarations()[1]->abstractType()->indexed());

  release(top);
}

void TestDUChain::testBaseClasses() {
  QByteArray text("class A{int aValue; }; class B{int bValue;}; class C : public A{int cValue;}; class D : public A, B {int dValue;}; template<class Base> class F : public Base { int fValue;};");

  TopDUContext* top = parse(text, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 5);
  Declaration* defClassA = top->localDeclarations().first();
  QCOMPARE(defClassA->identifier(), Identifier("A"));
  QVERIFY(defClassA->type<CppClassType>());

  Declaration* defClassB = top->localDeclarations()[1];
  QCOMPARE(defClassB->identifier(), Identifier("B"));
  QVERIFY(defClassB->type<CppClassType>());

  ClassDeclaration* defClassC = dynamic_cast<ClassDeclaration*>(top->localDeclarations()[2]);
  QVERIFY(defClassC);
  QCOMPARE(defClassC->identifier(), Identifier("C"));
  QVERIFY(defClassC->type<CppClassType>());
  QCOMPARE( defClassC->baseClassesSize(), 1u );

  ClassDeclaration* defClassD = dynamic_cast<ClassDeclaration*>(top->localDeclarations()[3]);
  QVERIFY(defClassD);
  QCOMPARE(defClassD->identifier(), Identifier("D"));
  QVERIFY(defClassD->type<CppClassType>());
  QCOMPARE( defClassD->baseClassesSize(), 2u );

  QVERIFY( findDeclaration( defClassD->internalContext(), Identifier("dValue") ) );
  QVERIFY( !findDeclaration( defClassD->internalContext(), Identifier("cValue") ) );
  QVERIFY( findDeclaration( defClassD->internalContext(), Identifier("aValue") ) );
  QVERIFY( findDeclaration( defClassD->internalContext(), Identifier("bValue") ) );

  QVERIFY( !findDeclaration( defClassC->internalContext(), Identifier("dValue") ) );
  QVERIFY( findDeclaration( defClassC->internalContext(), Identifier("cValue") ) );
  QVERIFY( !findDeclaration( defClassC->internalContext(), Identifier("bValue") ) );
  QVERIFY( findDeclaration( defClassC->internalContext(), Identifier("aValue") ) );

  QVERIFY( !findDeclaration( defClassB->internalContext(), Identifier("dValue") ) );
  QVERIFY( !findDeclaration( defClassB->internalContext(), Identifier("cValue") ) );
  QVERIFY( findDeclaration( defClassB->internalContext(), Identifier("bValue") ) );
  QVERIFY( !findDeclaration( defClassB->internalContext(), Identifier("aValue") ) );

  QVERIFY( !findDeclaration( defClassA->internalContext(), Identifier("dValue") ) );
  QVERIFY( !findDeclaration( defClassA->internalContext(), Identifier("cValue") ) );
  QVERIFY( !findDeclaration( defClassA->internalContext(), Identifier("bValue") ) );
  QVERIFY( findDeclaration( defClassA->internalContext(), Identifier("aValue") ) );

  ///Now test a template-class as base-class
  ClassDeclaration* defClassF = dynamic_cast<ClassDeclaration*>(top->localDeclarations()[4]);
  QVERIFY(defClassF);
  QCOMPARE(defClassF->identifier(), Identifier("F"));
  QVERIFY(defClassF->type<CppClassType>());
  QCOMPARE( defClassF->baseClassesSize(), 1u );
  QVERIFY( defClassF->baseClasses()[0].baseClass.type<DelayedType>().unsafeData() );

  Declaration* FDDecl = findDeclaration(top, QualifiedIdentifier("F<D>") );
  QVERIFY(FDDecl);
  QVERIFY(FDDecl->internalContext() != defClassF->internalContext());

  QVERIFY( findDeclaration( FDDecl->internalContext(), Identifier("dValue") ) );
  QVERIFY( !findDeclaration( FDDecl->internalContext(), Identifier("cValue") ) );
  QVERIFY( findDeclaration( FDDecl->internalContext(), Identifier("aValue") ) );
  QVERIFY( findDeclaration( FDDecl->internalContext(), Identifier("bValue") ) );
  QVERIFY( findDeclaration( FDDecl->internalContext(), Identifier("fValue") ) );

  release(top);
}

void TestDUChain::testTypedefUnsignedInt() {
  QByteArray method("typedef long unsigned int MyInt; MyInt v;");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->localDeclarations().count(), 2);
  QVERIFY(top->localDeclarations()[0]->abstractType());
  QVERIFY(top->localDeclarations()[1]->abstractType());
  QCOMPARE(top->localDeclarations()[0]->abstractType()->toString(), QString("MyInt"));
  QCOMPARE(top->localDeclarations()[1]->abstractType()->toString(), QString("MyInt"));
  QCOMPARE(unAliasedType(top->localDeclarations()[0]->abstractType())->toString(), QString("long unsigned int"));
  QCOMPARE(unAliasedType(top->localDeclarations()[1]->abstractType())->toString(), QString("long unsigned int"));
}

void TestDUChain::testTypedef() {
  QByteArray method("/*This is A translation-unit*/ \n/*This is class A*/class A { }; \ntypedef A B;//This is a typedef\nvoid test() { };\nconst B c;");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->localDeclarations().count(), 4);

  Declaration* defClassA = top->localDeclarations().first();
  QCOMPARE(defClassA->identifier(), Identifier("A"));
  QVERIFY(defClassA->type<CppClassType>());
  QCOMPARE(QString::fromUtf8(defClassA->comment()), QString("This is class A"));

  DUContext* classA = top->childContexts().first();
  QVERIFY(classA->parentContext());
  QCOMPARE(classA->importedParentContexts().count(), 0);
  QCOMPARE(classA->localScopeIdentifier(), QualifiedIdentifier("A"));

  Declaration* defB = findDeclaration(top,  Identifier("B"));
  QVERIFY(defB->indexedType() != defClassA->indexedType());
  QVERIFY(defB->isTypeAlias());
  TypeAliasType::Ptr aliasType = defB->type<TypeAliasType>();
  QVERIFY(aliasType);
  QVERIFY(aliasType->type());
  QCOMPARE(aliasType->type()->indexed(),  defClassA->indexedType());
  QCOMPARE(aliasType->declaration(top), defB);
  QCOMPARE(defB->kind(), Declaration::Type);
  QCOMPARE(QString::fromUtf8(defB->comment()), QString("This is a typedef"));

  QCOMPARE(defB->logicalInternalContext(top), defClassA->logicalInternalContext(top));
  
  Declaration* defC = findDeclaration(top, QualifiedIdentifier("c"));
  QVERIFY(defC);
  //The "const" has to be moved into the pointed-to type so it is correctly respected during type-conversions and such
  QVERIFY(TypeUtils::unAliasedType(defC->abstractType()));
  QVERIFY(TypeUtils::unAliasedType(defC->abstractType())->modifiers() & AbstractType::ConstModifier);
  QVERIFY(defC->abstractType()->modifiers() & AbstractType::ConstModifier);
  
  release(top);
}

void TestDUChain::testContextAssignment() {
  QByteArray text("template<class A>class Class { enum{ Bla = A::a }; }; ");
  TopDUContext* top = parse(text, DumpNone);
  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 1);

  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[1]->owner(), top->localDeclarations()[0]);

  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts()[1]->childContexts().count(), 1);

  QCOMPARE((void*)top->childContexts()[0]->owner(), (void*)0);
  QVERIFY((void*)top->childContexts()[1]->localDeclarations()[0]->internalContext());
  QCOMPARE((void*)top->childContexts()[1]->localDeclarations()[0]->internalContext(), top->childContexts()[1]->childContexts()[0]);

  release(top);
}

void TestDUChain::testSpecializedTemplates() {
  {
    QByteArray text("struct C{}; template<class T>class AsPointer {typedef T* Ptr;}; template<class T>class AsPointer<T*> {typedef T* Ptr;}; template<class T>class AsPointer<const T*> {typedef T* Ptr2;}; template<class T>class AsPointer<const T> {typedef T* Ptr2;};");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
  
    QCOMPARE(top->localDeclarations().count(), 5);
    TemplateDeclaration* base = dynamic_cast<TemplateDeclaration*>(top->localDeclarations()[1]);
    QVERIFY(base);
    QCOMPARE(base->specializationsSize(), 3u);
    
    TemplateDeclaration* sp3AsTemplate = dynamic_cast<TemplateDeclaration*>(top->localDeclarations()[3]);
    //Must be const T*
    QVERIFY(sp3AsTemplate);
    QCOMPARE(sp3AsTemplate->specializedWith().information().templateParametersSize(), 1u);
    kDebug() << "specialized with" << sp3AsTemplate->specializedWith().information().templateParameters()[0].abstractType()->toString();
    QVERIFY(sp3AsTemplate->specializedWith().information().templateParameters()[0].abstractType());
    QVERIFY(sp3AsTemplate->specializedWith().information().templateParameters()[0].type<PointerType>());
    QVERIFY(! (sp3AsTemplate->specializedWith().information().templateParameters()[0].abstractType()->modifiers() & AbstractType::ConstModifier) );
    QVERIFY(sp3AsTemplate->specializedWith().information().templateParameters()[0].type<PointerType>()->baseType());
    QVERIFY(sp3AsTemplate->specializedWith().information().templateParameters()[0].type<PointerType>()->baseType().cast<DelayedType>());
    QVERIFY(sp3AsTemplate->specializedWith().information().templateParameters()[0].type<PointerType>()->baseType().cast<DelayedType>()->identifier().isConstant());
    
    
    
    Declaration* decl2 = findDeclaration(top, QualifiedIdentifier("AsPointer<const C>::Ptr2"));
    Declaration* decl1 = findDeclaration(top, QualifiedIdentifier("AsPointer<C>::Ptr"));
    Declaration* decl3 = findDeclaration(top, QualifiedIdentifier("AsPointer<C*>::Ptr"));
    Declaration* decl4 = findDeclaration(top, QualifiedIdentifier("AsPointer<const C*>::Ptr2"));
    
    QVERIFY(decl1);
    QVERIFY(decl2);
    QVERIFY(decl3);
    QVERIFY(decl4);
    
    QVERIFY(unAliasedType(decl1->abstractType()).cast<PointerType>());
    QCOMPARE(unAliasedType(decl1->abstractType())->indexed(), unAliasedType(decl2->abstractType())->indexed());
    QCOMPARE(unAliasedType(decl1->abstractType())->indexed(), unAliasedType(decl3->abstractType())->indexed());
    QCOMPARE(unAliasedType(decl1->abstractType())->indexed(), unAliasedType(decl4->abstractType())->indexed());
    
    release(top);
  }
  {
    QByteArray text("class A{}; class B{}; class C{}; template<class T,class T2> class E{typedef A Type1;}; template<class T2> class E<A,T2> { typedef B Type2; typedef T2 NotA; }; template<class T2> class E<T2,A> { typedef C Type3; typedef T2 NotA; };");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
  
    QCOMPARE(top->localDeclarations().count(), 6);
    
    Declaration* EDecl = top->localDeclarations()[3];
    Declaration* E1Decl = top->localDeclarations()[4];
    Declaration* E2Decl = top->localDeclarations()[5];
  
    QVERIFY(EDecl->internalContext());
    QVERIFY(EDecl->internalContext()->importedParentContexts().count());
    QVERIFY(EDecl->internalContext()->importedParentContexts()[0].context(top)->type() == DUContext::Template);
    QCOMPARE(EDecl->internalContext()->importedParentContexts()[0].context(top)->localDeclarations().count(), 2);
  
    QVERIFY(E1Decl->internalContext());
    QCOMPARE(E1Decl->internalContext()->importedParentContexts().count(), 1);
    QCOMPARE(E1Decl->internalContext()->importedParentContexts()[0].context(top)->type(), DUContext::Template);
    QCOMPARE(E1Decl->internalContext()->importedParentContexts()[0].context(top)->localDeclarations().count(), 1);
    QCOMPARE(E1Decl->internalContext()->localDeclarations().count(), 2);
    QCOMPARE(E1Decl->identifier().templateIdentifiersCount(), 2u);
    QVERIFY(E2Decl->internalContext());
    QCOMPARE(E2Decl->internalContext()->importedParentContexts().count(), 1);
    QCOMPARE(E2Decl->internalContext()->importedParentContexts()[0].context(top)->type(), DUContext::Template);
    QCOMPARE(E2Decl->internalContext()->importedParentContexts()[0].context(top)->localDeclarations().count(), 1);
    QCOMPARE(E2Decl->identifier().templateIdentifiersCount(), 2u);
    
    TemplateDeclaration* templateEDecl = dynamic_cast<TemplateDeclaration*>(EDecl);
    TemplateDeclaration* templateE1Decl = dynamic_cast<TemplateDeclaration*>(E1Decl);
    TemplateDeclaration* templateE2Decl = dynamic_cast<TemplateDeclaration*>(E2Decl);
    kDebug() << E1Decl->identifier().toString(); 
    QVERIFY(templateEDecl);
    QVERIFY(templateE1Decl);
    QVERIFY(templateE2Decl);
    QCOMPARE(templateE1Decl->specializedWith().information().templateParametersSize(), 2u);
    QCOMPARE(templateE2Decl->specializedWith().information().templateParametersSize(), 2u);
    QVERIFY(!templateE1Decl->specializedWith().information().templateParameters()[0].type<DelayedType>());
    kDebug() << typeid(*templateE1Decl->specializedWith().information().templateParameters()[1].abstractType()).name();
    QVERIFY(templateE1Decl->specializedWith().information().templateParameters()[1].type<DelayedType>());
    QVERIFY(templateE2Decl->specializedWith().information().templateParameters()[0].type<DelayedType>());
    QVERIFY(!templateE2Decl->specializedWith().information().templateParameters()[1].type<DelayedType>());
  
    QCOMPARE(dynamic_cast<TemplateDeclaration*>(templateE1Decl->specializedFrom().data()), templateEDecl);
    QCOMPARE(dynamic_cast<TemplateDeclaration*>(templateE2Decl->specializedFrom().data()), templateEDecl);
    
    Declaration* foundE1Specialization = findDeclaration(top, QualifiedIdentifier("E<A,C>::Type2"));
    QVERIFY(foundE1Specialization);
    Declaration* foundE1Specialization2 = findDeclaration(top, QualifiedIdentifier("E<A,C>::NotA"));
    QVERIFY(foundE1Specialization2);
    QCOMPARE(unAliasedType(foundE1Specialization2->abstractType())->indexed(), top->localDeclarations()[2]->indexedType());
  
    Declaration* foundE2Specialization = findDeclaration(top, QualifiedIdentifier("E<C,A>::Type3"));
    QVERIFY(foundE2Specialization);
    Declaration* foundE2Specialization2 = findDeclaration(top, QualifiedIdentifier("E<C,A>::NotA"));
    QVERIFY(foundE2Specialization2);
    QCOMPARE(unAliasedType(foundE2Specialization2->abstractType())->indexed(), top->localDeclarations()[2]->indexedType());
  
    QVERIFY(findDeclaration(top, QualifiedIdentifier("E<C,A>")));
    QCOMPARE(findDeclaration(top, QualifiedIdentifier("E<C,A>"))->identifier(), Identifier("E<C,A>"));
    
    release(top);
  }
  {
    //                 0         1         2         3         4         5
    //                 012345678901234567890123456789012345678901234567890123456789
    QByteArray text("template<class T>\n"
                    "class A { void foo(T arg); };\n"
                    "template<class T> void A<T>::foo(T arg) {}");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
  
    QCOMPARE(top->localDeclarations().count(), 2);
    TemplateDeclaration* base = dynamic_cast<TemplateDeclaration*>(top->localDeclarations()[0]);
    QVERIFY(base);
    QCOMPARE(base->specializationsSize(), 0u);
    QCOMPARE(base->instantiations().size(), 1);
    QCOMPARE(base->instantiations().begin().value()->specializationsSize(), 0u);
    // use of class T in A<T>
    QCOMPARE(top->childContexts().size(), 5);
    QCOMPARE(top->childContexts().at(2)->type(), DUContext::Template);
    QCOMPARE(top->childContexts().at(2)->localDeclarations().size(), 1);
    QEXPECT_FAIL("", "The uses of T are not reported when we define the default implementation outside the class body", Abort);
    QCOMPARE(top->childContexts().at(2)->localDeclarations().first()->uses().size(), 1);
    QCOMPARE(top->childContexts().at(2)->localDeclarations().first()->uses().begin()->size(), 2);
    QCOMPARE(top->childContexts().at(2)->localDeclarations().first()->uses().begin()->at(0), SimpleRange(2, 22, 2, 23));
    QCOMPARE(top->childContexts().at(2)->localDeclarations().first()->uses().begin()->at(1), SimpleRange(2, 31, 2, 32));
    release(top);
  }
}

int value( const AbstractType::Ptr& type ) {
  const ConstantIntegralType* integral = dynamic_cast<const ConstantIntegralType*>(type.unsafeData());
  if( integral )
    return (int)integral->value<qint64>();
  else
    return 0;
}

void TestDUChain::testTemplateRecursiveInstantiation()
{
  {
    QByteArray text("template<bool b> class A { public: bool member; enum { SizeWithFalse = sizeof(A<false>) }; };");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    Declaration* aTrueDecl = findDeclaration(top, Identifier("A<true>"));
    Declaration* aFalseDecl = findDeclaration(top, Identifier("A<false>"));
    QVERIFY(aTrueDecl);
    QVERIFY(aFalseDecl);
    QVERIFY(aTrueDecl->internalContext());
    QVERIFY(aFalseDecl->internalContext());

    QCOMPARE(aFalseDecl->internalContext()->localDeclarations(top).count(), 2);
    QCOMPARE(aTrueDecl->internalContext()->localDeclarations(top).count(), 2);

    release(top);
  }
}

void TestDUChain::testTemplateEnums()
{
  {
    QByteArray text("template<bool num> struct No {};  No<true> n;");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 2);
    QVERIFY(top->localDeclarations()[1]->abstractType());
    QCOMPARE(top->localDeclarations()[1]->abstractType()->toString(), QString("No< true >"));

    release(top);
  }
  {
    QByteArray text("template<int num=5> struct No {};  No<> n;");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 2);
    QVERIFY(top->localDeclarations()[1]->abstractType());
    QCOMPARE(top->localDeclarations()[1]->abstractType()->toString(), QString("No< 5 >"));
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->kind(), Declaration::Instance);


    QCOMPARE(top->usesCount(), 1);
    Declaration* used = top->usedDeclarationForIndex(top->uses()[0].m_declarationIndex);
    QVERIFY(used);
    QVERIFY(used->abstractType());
    QCOMPARE(used->abstractType()->toString(), QString("No< 5 >"));
    release(top);
  }
  {
    QByteArray text("template<int num> struct No {};  No<9> n;");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 2);
    QVERIFY(top->localDeclarations()[1]->abstractType());
    QCOMPARE(top->localDeclarations()[1]->abstractType()->toString(), QString("No< 9 >"));

    QCOMPARE(top->usesCount(), 1);
    Declaration* used = top->usedDeclarationForIndex(top->uses()[0].m_declarationIndex);
    QVERIFY(used);
    QVERIFY(used->abstractType());
    QCOMPARE(used->abstractType()->toString(), QString("No< 9 >"));
    release(top);
  }
  {
    QByteArray text("class A {enum { Val = 5}; }; class B { enum{ Val = 7 }; }; template<class C, int i> class Test { enum { TempVal = C::Val, Num = i, Sum = TempVal + i }; };");
    TopDUContext* top = parse(text, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 3);
    Declaration* testDecl = top->localDeclarations()[2];

    TemplateDeclaration* templateTestDecl = dynamic_cast<TemplateDeclaration*>(testDecl);
    QVERIFY(templateTestDecl);

    Declaration* tempDecl = findDeclaration( top, QualifiedIdentifier("Test<A, 3>::TempVal") );
    QVERIFY(tempDecl);
    AbstractType::Ptr t = tempDecl->abstractType();
    QVERIFY(!DelayedType::Ptr::dynamicCast(t));
    QVERIFY(ConstantIntegralType::Ptr::dynamicCast(t));
    QCOMPARE(value(tempDecl->abstractType()), 5);

    tempDecl = findDeclaration( top, QualifiedIdentifier("Test<A, 3>::Num") );
    QVERIFY(tempDecl);
    QCOMPARE(value(tempDecl->abstractType()), 3);

    tempDecl = findDeclaration( top, QualifiedIdentifier("Test<A, 3>::Sum") );
    QVERIFY(tempDecl->abstractType());
    QCOMPARE(value(tempDecl->abstractType()), 8);

    tempDecl = findDeclaration( top, QualifiedIdentifier("Test<B, 9>::TempVal") );
    QVERIFY(tempDecl->abstractType());
    QCOMPARE(value(tempDecl->abstractType()), 7);

    tempDecl = findDeclaration( top, QualifiedIdentifier("Test<B, 9>::Num") );
    QVERIFY(tempDecl->abstractType());
    t = tempDecl->abstractType();
    kDebug() << "id" << typeid(*t).name();
    kDebug() << "text:" << tempDecl->abstractType()->toString() << tempDecl->toString();
    QCOMPARE(value(tempDecl->abstractType()), 9);

    tempDecl = findDeclaration( top, QualifiedIdentifier("Test<B, 9>::Sum") );
    QVERIFY(tempDecl->abstractType());
    QCOMPARE(value(tempDecl->abstractType()), 16);

    release(top);
  }
}

void TestDUChain::testIntegralTemplates()
{
  QByteArray text("template<class T> class A { T i; }; ");
  TopDUContext* top = parse(text, DumpNone);
  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 1);
  QCOMPARE(top->childContexts().count(), 2);

  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  Declaration* dec = findDeclaration( top, QualifiedIdentifier( "A<int>::i") );
  QVERIFY(dec);
  AbstractType::Ptr t = dec->abstractType();
  IntegralType* integral = dynamic_cast<IntegralType*>( t.unsafeData() );
  QVERIFY( integral );
  QCOMPARE( integral->dataType(), (uint)IntegralType::TypeInt );

  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  dec = findDeclaration( top, QualifiedIdentifier( "A<unsigned int>::i") );
  t = dec->abstractType();
  integral = dynamic_cast<IntegralType*>( t.unsafeData() );
  QVERIFY( integral );
  QCOMPARE( integral->dataType(), (uint)IntegralType::TypeInt );
  QCOMPARE( integral->modifiers(), (unsigned long long)AbstractType::UnsignedModifier );

  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  dec = findDeclaration( top, QualifiedIdentifier( "A<long double>::i") );
  t = dec->abstractType();
  integral = dynamic_cast<IntegralType*>( t.unsafeData() );
  QVERIFY( integral );
  QCOMPARE( integral->dataType(), (uint)IntegralType::TypeDouble );
  QCOMPARE( integral->modifiers(), (unsigned long long)AbstractType::LongModifier );

  release(top);
}

void TestDUChain::testFunctionTemplates() {
  QByteArray method("template<class T> T test(const T& t) {};");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().size(), 3);
  Declaration* defTest = top->localDeclarations()[0];
  QVERIFY(top->childContexts()[0]->type() == DUContext::Template);
  QCOMPARE(top->childContexts()[0]->importers().size(), 1);
  kDebug() << top->childContexts()[0]->importers()[0] << top->childContexts()[1] << top->childContexts()[2];
  QCOMPARE(top->childContexts()[0]->importers()[0], top->childContexts()[1]);
  QCOMPARE(defTest->identifier(), Identifier("test"));
  QVERIFY(defTest->type<FunctionType>());
  QVERIFY( isTemplateDeclaration(defTest) );
  QVERIFY(defTest->internalContext());
  QCOMPARE(defTest->internalContext()->importedParentContexts().size(), 1);
  KDevelop::DUContext* tempCtx = getTemplateContext(defTest);
  QVERIFY(tempCtx);

  QCOMPARE( defTest->type<FunctionType>()->arguments().count(), 1 );
  QVERIFY( realType(defTest->type<FunctionType>()->arguments()[0], 0).cast<DelayedType>() );

  release(top);
}

void TestDUChain::testTemplateFunctions() {
  QByteArray method("class A {}; template<class T> T a(T& q) {};template<class T> struct TC { void test(const T&); };");

  TopDUContext* top = parse(method, DumpNone);
  parse(method, DumpNone, top);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->localDeclarations().count(), 3);
  Declaration* d = findDeclaration(top, QualifiedIdentifier("a<A>"));
  QVERIFY(d);
  FunctionType::Ptr cppFunction = d->type<FunctionType>();
  QVERIFY(cppFunction);
  QCOMPARE(cppFunction->arguments().count(), 1);
  QCOMPARE(cppFunction->returnType()->indexed(), top->localDeclarations()[0]->abstractType()->indexed());
  QCOMPARE(cppFunction->arguments()[0]->toString(), QString("A&"));
  QVERIFY(d->internalContext());
  QVERIFY(d->internalContext()->type() == DUContext::Other);
  QCOMPARE(d->internalContext()->importedParentContexts().count(), 1);
  QCOMPARE(d->internalContext()->importedParentContexts()[0].context(0)->type(), DUContext::Function);
  QCOMPARE(d->internalContext()->importedParentContexts()[0].context(0)->importedParentContexts().count(), 1);
  QCOMPARE(d->internalContext()->importedParentContexts()[0].context(0)->importedParentContexts().count(), 1);
  QCOMPARE(d->internalContext()->importedParentContexts()[0].context(0)->importedParentContexts()[0].context(0)->type(), DUContext::Template);

  QList<QPair<Declaration*, int> > visibleDecls = d->internalContext()->allDeclarations(d->internalContext()->range().end, top, false);
  for(int a = 0; a < visibleDecls.size(); ++a) {
    kDebug() << "decl:" << visibleDecls[a].first->toString();
  }
  QCOMPARE(visibleDecls.size(), 2); //Must be q and T
  QCOMPARE(visibleDecls[0].first->identifier().toString(), QString("q"));
  QVERIFY(visibleDecls[0].first->abstractType());
  QCOMPARE(visibleDecls[0].first->abstractType()->toString(), QString("A&"));
  QVERIFY(visibleDecls[1].first->abstractType());
  QCOMPARE(visibleDecls[1].first->abstractType()->toString(), QString("A"));

  Declaration* found = findDeclaration(d->internalContext(), Identifier("q"));
  QVERIFY(found);
  QVERIFY(found->abstractType());
  QCOMPARE(found->abstractType()->toString(), QString("A&"));

  Declaration* instTC = findDeclaration(top, QualifiedIdentifier("TC<int>"));
  QVERIFY(instTC);
  QVERIFY(instTC->internalContext());
  QList<Declaration*> decls = instTC->internalContext()->findLocalDeclarations(Identifier("test"));
  QCOMPARE(decls.count(), 1);
  Declaration* testDecl = decls.first();
  QVERIFY(testDecl);
  QVERIFY(testDecl->abstractType());
  QVERIFY(testDecl->internalContext());
  QVERIFY(testDecl->type<KDevelop::FunctionType>());
  QCOMPARE(testDecl->internalContext()->localDeclarations(top).count(), 1);
  kDebug() << testDecl->abstractType()->toString();
  QCOMPARE(testDecl->type<KDevelop::FunctionType>()->arguments().count(), 1);
  
  DUContext* argContext = KDevelop::DUChainUtils::getArgumentContext(testDecl);
  QVERIFY(argContext);
  QCOMPARE(argContext->type(), DUContext::Function);
  QCOMPARE(argContext->localDeclarations(top).count(), 1);
  
  
  release(top);
}

void TestDUChain::testTemplateDependentClass() {
  QByteArray method("class A {}; template<class T> class B { class Q{ typedef T Type; }; }; B<A>::Q::Type t;");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  Declaration* d = findDeclaration(top, QualifiedIdentifier("t"));
  QVERIFY(d);
  kDebug() << d->toString();
  QCOMPARE(unAliasedType(d->abstractType())->indexed(), top->localDeclarations()[0]->abstractType()->indexed());

  release(top);
}

void TestDUChain::testMetaProgramming() {
  QByteArray method("template<int value> class Factorial{ enum { Value = value * Factorial<value-1>::Value };}; template<> class Factorial<0> { enum { Value = 1 };};");
  
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 2);
  TemplateDeclaration* templateBase = dynamic_cast<TemplateDeclaration*>(top->localDeclarations()[0]);
  TemplateDeclaration* templateSpecialization = dynamic_cast<TemplateDeclaration*>(top->localDeclarations()[1]);
  QVERIFY(templateBase);
  QVERIFY(templateSpecialization);
  QVERIFY(templateBase->specializationsSize() == 1);
  QCOMPARE(templateSpecialization->specializedFrom().data(), top->localDeclarations()[0]);
  QCOMPARE(templateSpecialization->specializedWith().information().templateParametersSize(), 1u);
  QVERIFY(templateSpecialization->specializedWith().information().templateParameters()[0].abstractType());
  QVERIFY(templateSpecialization->specializedWith().information().templateParameters()[0].type<ConstantIntegralType>());
  QCOMPARE(templateSpecialization->specializedWith().information().templateParameters()[0].type<ConstantIntegralType>()->value<int>(), 0);
  kDebug() << "searching";
  Declaration* factorial0Container = findDeclaration(top, QualifiedIdentifier("Factorial<0>"));
  QCOMPARE(factorial0Container, top->localDeclarations()[1]);
  QVERIFY(factorial0Container);
  QCOMPARE(factorial0Container->internalContext()->childContexts().count(), 1);
  QCOMPARE(factorial0Container->internalContext()->childContexts()[0]->localDeclarations().count(), 1);
  QVERIFY(factorial0Container->internalContext()->childContexts()[0]->localDeclarations()[0]->type<ConstantIntegralType>());
  
  Declaration* factorial0 = findDeclaration(top, QualifiedIdentifier("Factorial<0>::Value"));
  QVERIFY(factorial0);
  QVERIFY(factorial0->type<ConstantIntegralType>());
  QCOMPARE(factorial0->type<ConstantIntegralType>()->value<int>(), 1);
  
  Declaration* factorial2 = findDeclaration(top, QualifiedIdentifier("Factorial<2>::Value"));
  QVERIFY(factorial2);
  QVERIFY(factorial2->type<ConstantIntegralType>());
  QCOMPARE(factorial2->type<ConstantIntegralType>()->value<int>(), 2);
  
  Declaration* factorial3 = findDeclaration(top, QualifiedIdentifier("Factorial<3>::Value"));
  QVERIFY(factorial3);
  QVERIFY(factorial3->type<ConstantIntegralType>());
  QCOMPARE(factorial3->type<ConstantIntegralType>()->value<int>(), 6);  
  
  Declaration* factorial4 = findDeclaration(top, QualifiedIdentifier("Factorial<4>::Value"));
  QVERIFY(factorial4);
  QVERIFY(factorial4->type<ConstantIntegralType>());
  QCOMPARE(factorial4->type<ConstantIntegralType>()->value<int>(), 24);  
  
  release(top);
}

void TestDUChain::testMetaProgramming3() {
  QByteArray method("template<int value, int than>class bigger_than {enum {Result = ((value > than) ? 2 : ((value == than) ? 0 : -2))};};");
  
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 1);

  kDebug() << "test 1";
  Declaration* decl = findDeclaration(top, QualifiedIdentifier("bigger_than<5, 3>::Result"));
  QVERIFY(decl);
  QVERIFY(decl->type<ConstantIntegralType>());
  QCOMPARE(decl->type<ConstantIntegralType>()->value<int>(), 2);
  kDebug() << "test 2";
  decl = findDeclaration(top, QualifiedIdentifier("bigger_than<5, 5>::Result"));
  QVERIFY(decl);
  QVERIFY(decl->type<ConstantIntegralType>());
  QCOMPARE(decl->type<ConstantIntegralType>()->value<int>(), 0);
  
  decl = findDeclaration(top, QualifiedIdentifier("bigger_than<5, 6>::Result"));
  QVERIFY(decl);
  QVERIFY(decl->type<ConstantIntegralType>());
  QCOMPARE(decl->type<ConstantIntegralType>()->value<int>(), -2);
  
  release(top);
}


void TestDUChain::testMetaProgramming2() {
  QByteArray method("template<int N, int R>class Permutations {public:enum { value = N*(Permutations<N-1,R-1>::value) };};template<int N>class Permutations<N, 0> {public:enum { value = 1 };};");
  
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 2);
  TemplateDeclaration* templateBase = dynamic_cast<TemplateDeclaration*>(top->localDeclarations()[0]);
  TemplateDeclaration* templateSpecialization = dynamic_cast<TemplateDeclaration*>(top->localDeclarations()[1]);
  QVERIFY(templateBase);
  QVERIFY(templateSpecialization);
  QVERIFY(templateBase->specializationsSize() == 1);
  QCOMPARE(templateSpecialization->specializedFrom().data(), top->localDeclarations()[0]);
  QCOMPARE(templateSpecialization->specializedWith().information().templateParametersSize(), 2u);
  QVERIFY(templateSpecialization->specializedWith().information().templateParameters()[1].abstractType());
  QVERIFY(templateSpecialization->specializedWith().information().templateParameters()[1].type<ConstantIntegralType>());
  QCOMPARE(templateSpecialization->specializedWith().information().templateParameters()[1].type<ConstantIntegralType>()->value<int>(), 0);

  Declaration* permutations0 = findDeclaration(top, QualifiedIdentifier("Permutations<5, 0>::value"));
  QVERIFY(permutations0);
  QVERIFY(permutations0->type<ConstantIntegralType>());
  QCOMPARE(permutations0->type<ConstantIntegralType>()->value<int>(), 1);
  
  Declaration* permutations2 = findDeclaration(top, QualifiedIdentifier("Permutations<2, 1>::value"));
  QVERIFY(permutations2);
  QVERIFY(permutations2->type<ConstantIntegralType>());
  QCOMPARE(permutations2->type<ConstantIntegralType>()->value<int>(), 2);
  
  Declaration* permutations3 = findDeclaration(top, QualifiedIdentifier("Permutations<4, 2>::value"));
  QVERIFY(permutations3);
  QVERIFY(permutations3->type<ConstantIntegralType>());
  QCOMPARE(permutations3->type<ConstantIntegralType>()->value<int>(), 12);
  
  Declaration* permutations4 = findDeclaration(top, QualifiedIdentifier("Permutations<10, 5>::value"));
  QVERIFY(permutations4);
  QVERIFY(permutations4->type<ConstantIntegralType>());
  kDebug() << permutations4->abstractType()->toString();
  QCOMPARE(permutations4->type<ConstantIntegralType>()->value<int>(), 30240);  
  
  release(top);
}
void TestDUChain::testTemplateInternalSearch() {
  QByteArray method("class A {}; template<class T> class B { B mem(); const B mem2;}; ");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->childContexts()[2]->localDeclarations().count(), 2);
  QVERIFY(top->childContexts()[2]->localDeclarations()[1]->type<DelayedType>());
  QVERIFY(top->childContexts()[2]->localDeclarations()[0]->type<FunctionType>());
  QVERIFY(top->childContexts()[2]->localDeclarations()[0]->type<FunctionType>()->returnType().cast<DelayedType>());
  
  Declaration* d = findDeclaration(top, QualifiedIdentifier("B<A>::mem"));
  QVERIFY(d);
  kDebug() << d->toString();
  FunctionType::Ptr fType = d->type<FunctionType>();
  QVERIFY(fType);
  QVERIFY(fType->returnType());
  kDebug() << fType->toString();
  QCOMPARE(fType->returnType()->toString(), QString("B< A >"));
  
  d = findDeclaration(top, QualifiedIdentifier("B<A>::mem2"));
  QVERIFY(d);
  QCOMPARE(d->abstractType()->toString(), QString("const B< A >"));

  release(top);
}

void TestDUChain::testTemplateImplicitInstantiations()
{
  {
  QByteArray method("class A { template<typename T> static void foo(T){} };\n"
                    "void test() { A::foo(5); A::foo('x'); A::foo(\"asdfasdfadf\");\n }");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().first()->localDeclarations().size(), 1);
  QCOMPARE(top->localDeclarations().size(), 2);
  QCOMPARE(top->childContexts().first()->localDeclarations().size(), 1);
  TemplateDeclaration* tpl = dynamic_cast<TemplateDeclaration*>(top->childContexts().first()->localDeclarations().first());
  QVERIFY(tpl);
  QCOMPARE(tpl->instantiations().size(), 3);
  release(top);
  }
  {
  QByteArray method("template<typename T> void foo(T){}\n"
                    "void test() { foo(5); foo('x'); foo(\"asdfasdfadf\"); }\n");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->localDeclarations().size(), 2);
  TemplateDeclaration* tpl = dynamic_cast<TemplateDeclaration*>(top->localDeclarations().first());
  QVERIFY(tpl);
  QCOMPARE(tpl->instantiations().size(), 3);
  release(top);
  }
}

void TestDUChain::testSourceCodeInsertion()
{
  {
    QByteArray method("namespace A {\nclass B {};\n}\n");
    
    TopDUContext* top = parse(method, DumpNone);
    
    DUChainWriteLocker lock(DUChain::lock());
    InsertArtificialCodeRepresentation repr(top->url(), QString::fromUtf8(method));
    
    QCOMPARE(top->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);

    QCOMPARE(repr.text().trimmed().split('\n').count(), 3);
    {
      Cpp::SourceCodeInsertion ins(top);
      ins.insertForwardDeclaration(top->childContexts()[0]->localDeclarations()[0]);
      
      ins.changes().setReplacementPolicy(KDevelop::DocumentChangeSet::StopOnFailedChange);
      DocumentChangeSet::ChangeResult result = ins.changes().applyAllChanges();
      QVERIFY(result);
      kDebug() << repr.text();
//       QVERIFY(repr.text().trimmed().remove(' ').remove('\n').contains(QString("int testVar;").remove(' ')));
      
      //Only one newline should be added
      QCOMPARE(repr.text().trimmed().split('\n').count(), 4);
    }
    
    top = parse(repr.text().toUtf8(), DumpNone, top);
    
    QCOMPARE(top->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);
    
    release(top);
  }
  
  {
    QByteArray method("");
    
    TopDUContext* top = parse(method, DumpNone);
    
    DUChainWriteLocker lock(DUChain::lock());
    InsertArtificialCodeRepresentation repr(top->url(), QString::fromUtf8(method));
    
    {
      Cpp::SourceCodeInsertion ins(top);
      ins.insertFunctionDeclaration(Identifier("test"), AbstractType::Ptr(new IntegralType(IntegralType::TypeVoid)), QList<Cpp::SourceCodeInsertion::SignatureItem>(), false, "{ this is the body; }");
      ins.changes().setReplacementPolicy(KDevelop::DocumentChangeSet::StopOnFailedChange);
      DocumentChangeSet::ChangeResult result = ins.changes().applyAllChanges();
      kDebug() << result.m_failureReason;
      QVERIFY(result);
      kDebug() << repr.text();
      QVERIFY(repr.text().trimmed().remove(' ').remove('\n').contains(QString("void test() { this is the body; }").remove(' ')));
      
      //Only one newline should be added
      QCOMPARE(repr.text().split('\n').count(), 2);
    }
    
    top = parse(repr.text().toUtf8(), DumpNone, top);
    
    QCOMPARE(top->localDeclarations().count(), 1);

    {
      Cpp::SourceCodeInsertion ins(top);
      ins.insertVariableDeclaration(Identifier("testVar"), AbstractType::Ptr(new IntegralType(IntegralType::TypeInt)));
      ins.changes().setReplacementPolicy(KDevelop::DocumentChangeSet::StopOnFailedChange);
      DocumentChangeSet::ChangeResult result = ins.changes().applyAllChanges();
      QVERIFY(result);
      kDebug() << repr.text();
      QVERIFY(repr.text().trimmed().remove(' ').remove('\n').contains(QString("int testVar;").remove(' ')));
      
      //Only one newline should be added
      QCOMPARE(repr.text().split('\n').count(), 3);
    }
    
    top = parse(repr.text().toUtf8(), DumpNone, top);
    
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("test"));
    QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("testVar"));

    {
      Cpp::SourceCodeInsertion ins(top);
      ins.setInsertBefore(top->localDeclarations()[1]->range().start);
      ins.insertForwardDeclaration(top->localDeclarations()[1]);
      ins.changes().setReplacementPolicy(KDevelop::DocumentChangeSet::StopOnFailedChange);
      DocumentChangeSet::ChangeResult result = ins.changes().applyAllChanges();
      QVERIFY(result);
      kDebug() << repr.text();
//       QVERIFY(repr.text().trimmed().remove(' ').remove('\n').contains(QString("int testVar;").remove(' ')));
      
      //Only one newline should be added
      QCOMPARE(repr.text().trimmed().split('\n').count(), 3);
    }
    
    top = parse(repr.text().toUtf8(), DumpNone, top);
    
    QCOMPARE(top->localDeclarations().count(), 3);
    ///@todo Wrong order (Minor issue when updating without smart-ranges)
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("test"));
    QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("testVar"));
    QCOMPARE(top->localDeclarations()[2]->identifier(), Identifier("testVar"));
    
    release(top);
  }
}

void TestDUChain::testSimplifiedTypeString()
{
  {
    QByteArray method("namespace A { struct B { B(); }; };");
    TopDUContext* top = parse(method, DumpNone);
    
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().size(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts().size(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts()[0]->localDeclarations().size(), 1);
    
    QualifiedIdentifier constructorId = top->childContexts()[0]->childContexts()[0]->localDeclarations()[0]->qualifiedIdentifier();
    QCOMPARE(constructorId.toString(), QString("A::B::B"));
    QCOMPARE(stripPrefixes(top->childContexts()[0], constructorId).toString(), QString("B::B"));
    QCOMPARE(stripPrefixes(top, constructorId).toString(), QString("A::B::B"));
    
    release(top);
  }
  {
    QByteArray method("typedef int *honk, **honk2; honk k;");
    TopDUContext* top = parse(method, DumpNone);
    
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 3);
    
    QVERIFY(top->localDeclarations()[0]->abstractType().cast<TypeAliasType>());
    QVERIFY(top->localDeclarations()[1]->abstractType().cast<TypeAliasType>());
    QVERIFY(top->localDeclarations()[2]->abstractType().cast<TypeAliasType>());
    
    QCOMPARE(top->localDeclarations()[0]->abstractType().cast<TypeAliasType>()->qualifiedIdentifier(), QualifiedIdentifier("honk"));
    QCOMPARE(top->localDeclarations()[0]->abstractType().cast<TypeAliasType>()->type()->toString(), QString("int*"));
    
    QCOMPARE(top->localDeclarations()[1]->abstractType().cast<TypeAliasType>()->qualifiedIdentifier(), QualifiedIdentifier("honk2"));
    ///@todo Make this work as well, the init-declarators need to have separate types
//     QCOMPARE(top->localDeclarations()[1]->abstractType().cast<TypeAliasType>()->type()->toString(), QString("int**"));
    
    QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[2]->abstractType(), top).remove(' '), QString("honk").remove(' '));
    release(top);
  }
  {
    QByteArray method("typedef int const * const honkolo; honkolo k;");
    TopDUContext* top = parse(method, DumpNone);
    
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    
    QVERIFY(top->localDeclarations()[0]->abstractType().cast<TypeAliasType>());
    QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[0]->abstractType().cast<TypeAliasType>()->type(), top).remove(' '), QString("const int *const").remove(' '));
    release(top);
  }
  {
    QByteArray method("class C; typedef C* honk; honk k;");
    TopDUContext* top = parse(method, DumpNone);
    
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 3);
    
    QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[2]->abstractType(), top).remove(' '), QString("honk").remove(' '));
    release(top);
  }
  {
    QByteArray method("const int i;\n");
    
    TopDUContext* top = parse(method, DumpNone);
    
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    
    QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[0]->abstractType(), top).remove(' '), QString("const int").remove(' '));
    release(top);
  }
  
  {
    QByteArray method("template<class T> class Template { class Member; Member mem; }; Template< Template< int >* >::Member q;\n");
    
    TopDUContext* top = parse(method, DumpNone);
    
    InsertArtificialCodeRepresentation repr(top->url(), QString::fromUtf8(method));

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    
    QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[1]->abstractType(), top).remove(' '), QString("Template<Template<int>*>::Member").remove(' '));
    release(top);
  }
  {
  ///@todo Add more tests for this
    QByteArray method("template<typename T> struct F; template<class T> class Tc; Tc<const F<int*>*> t; const Tc<const F<int*>*>**const*& Test1; Tc<F<int*>*>* test();\n");
    
    TopDUContext* top = parse(method, DumpNone);
    
    InsertArtificialCodeRepresentation repr(top->url(), QString::fromUtf8(method));

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 5);
    QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[2]->abstractType(), top).remove(' '), QString("Tc<const F<int*>*>").remove(' '));
    QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[3]->abstractType(), top).remove(' '), QString("constTc<const F<int*>*>**const*&").remove(' '));
    FunctionType::Ptr funType = top->localDeclarations()[4]->abstractType().cast<FunctionType>();
    QVERIFY(funType);
    QVERIFY(funType->returnType());
    
    //For loop is needed to test the updating, as there was a problem with that
    for(int a = 0; a < 3; ++a) {
      kDebug() << "run" << a;
      lock.unlock();
      parse(method, DumpNone, top);
      lock.lock();
      repr.setText(QString::fromUtf8(method));
      Cpp::SourceCodeInsertion ins(top);
      ins.insertFunctionDeclaration(Identifier("test"), funType->returnType(), QList<Cpp::SourceCodeInsertion::SignatureItem>(), false, "{ this is the body; }");
      ins.changes().setReplacementPolicy(KDevelop::DocumentChangeSet::StopOnFailedChange);
      DocumentChangeSet::ChangeResult result = ins.changes().applyAllChanges();
      kDebug() << result.m_failureReason;
      QVERIFY(result);
      kDebug() << repr.text();
      QVERIFY(repr.text().trimmed().remove(' ').remove('\n').contains(QString("Tc< F< int* >* >* test() { this is the body; }").remove(' ')));
      lock.unlock();
      parse(repr.text().toUtf8(), DumpNone, top);
      lock.lock();
      QVERIFY(top->localDeclarations().count() == 6);
      
      FunctionType::Ptr funType2 = top->localDeclarations()[5]->abstractType().cast<FunctionType>();
      QVERIFY(funType2);
      QVERIFY(funType2->returnType());
      QVERIFY(funType2->returnType()->equals(funType->returnType().unsafeData()));
      QCOMPARE(Cpp::simplifiedTypeString(funType2->returnType(), top).remove(' '), QString("Tc< F< int* >* >*").remove(' '));
    }
    
    release(top);
  }
}

void TestDUChain::testTemplateReference() {
  QByteArray method("class A; template<class T> class CC; void test(CC<const A*>& item); const A& a;const A*** b;CC<const A>  cca;");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->localDeclarations().count(), 6);
  QVERIFY(top->localDeclarations()[2]->abstractType());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  AbstractType::Ptr argType = top->childContexts()[1]->localDeclarations()[0]->abstractType();
  QVERIFY(argType.cast<ReferenceType>());
  QCOMPARE(argType->toString().remove(' '), QString("CC<constA*>&"));
  {
//     QWidget* navigationWidget = top->childContexts()[1]->createNavigationWidget(top->childContexts()[1]->localDeclarations()[0]);
//     QVERIFY(navigationWidget);
//     KDevelop::AbstractNavigationWidget* nWidget = dynamic_cast<KDevelop::AbstractNavigationWidget*>(navigationWidget);
//     QVERIFY(nWidget);
//     QVERIFY(nWidget->context());
//     QString html = nWidget->context()->html();
//     kDebug() << "html:" << html;
  }
  QCOMPARE(Cpp::simplifiedTypeString(top->childContexts()[1]->localDeclarations()[0]->abstractType(), top).remove(' '), QString("CC<constA*>&"));
  QVERIFY(top->localDeclarations()[3]->abstractType());
  QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[3]->abstractType(), top).remove(' '), QString("constA&"));
  QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[4]->abstractType(), top).remove(' '), QString("constA***"));
  AbstractType::Ptr type = top->localDeclarations()[5]->abstractType();
  QVERIFY(type);
  QCOMPARE(type->toString().remove(' '), QString("CC<constA>"));
  QCOMPARE(Cpp::simplifiedTypeString(top->localDeclarations()[5]->abstractType(), top).remove(' '), QString("CC<constA>"));
  release(top);
}

void TestDUChain::testTemplates() {
  QByteArray method("template<class T> T test(const T& t) {}; template<class T, class T2> class A {T2 a; typedef T Template1; }; class B{int b;}; class C{int c;}; template<class T>class A<B,T>{};  typedef A<B,C> D;");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  Declaration* defTest = top->localDeclarations()[0];
  QCOMPARE(defTest->identifier(), Identifier("test"));
  QVERIFY(defTest->type<FunctionType>());
  QVERIFY( isTemplateDeclaration(defTest) );

  Declaration* defClassA = top->localDeclarations()[1];
  QCOMPARE(defClassA->identifier(), Identifier("A"));
  QVERIFY(defClassA->type<CppClassType>());
  QVERIFY( isTemplateDeclaration(defClassA) );

  Declaration* defClassB = top->localDeclarations()[2];
  QCOMPARE(defClassB->identifier(), Identifier("B"));
  QVERIFY(defClassB->type<CppClassType>());
  QVERIFY( !isTemplateDeclaration(defClassB) );

  Declaration* defClassC = top->localDeclarations()[3];
  QCOMPARE(defClassC->identifier(), Identifier("C"));
  QVERIFY(defClassC->type<CppClassType>());
  QVERIFY( !isTemplateDeclaration(defClassC) );

  DUContext* classA = defClassA->internalContext();
  QVERIFY(classA);
  QVERIFY(classA->parentContext());
  QCOMPARE(classA->importedParentContexts().count(), 1); //The template-parameter context is imported
  QCOMPARE(classA->localScopeIdentifier(), QualifiedIdentifier("A"));

  DUContext* classB = defClassB->internalContext();
  QVERIFY(classB);
  QVERIFY(classB->parentContext());
  QCOMPARE(classB->importedParentContexts().count(), 0);
  QCOMPARE(classB->localScopeIdentifier(), QualifiedIdentifier("B"));

  DUContext* classC = defClassC->internalContext();
  QVERIFY(classC);
  QVERIFY(classC->parentContext());
  QCOMPARE(classC->importedParentContexts().count(), 0);
  QCOMPARE(classC->localScopeIdentifier(), QualifiedIdentifier("C"));

  ///Test getting the typedef for the unset template
  {
    Declaration* typedefDecl = findDeclaration(classA, Identifier("Template1"));
    QVERIFY(typedefDecl);
    QVERIFY(typedefDecl->isTypeAlias());
    QVERIFY(typedefDecl->abstractType());
    QVERIFY(unAliasedType(typedefDecl->abstractType()));
    DelayedType::Ptr delayed = unAliasedType(typedefDecl->abstractType()).cast<DelayedType>();
    QVERIFY(delayed);
    QCOMPARE(delayed->identifier(), IndexedTypeIdentifier("T"));
  }

  ///Test creating a template instance of class A<B,C>
  {
    Identifier ident("A");
    ident.appendTemplateIdentifier(IndexedTypeIdentifier("B"));
    ident.appendTemplateIdentifier(IndexedTypeIdentifier("C"));
    Declaration* instanceDefClassA = findDeclaration(top, ident);
    Declaration* instanceTypedefD = findDeclaration(top, Identifier("D"));
    QVERIFY(instanceTypedefD);
    QVERIFY(instanceDefClassA);
    QVERIFY(unAliasedType(instanceTypedefD->abstractType()));
    QCOMPARE(unAliasedType(instanceTypedefD->abstractType())->toString(), instanceDefClassA->abstractType()->toString() );
    //QCOMPARE(instanceTypedefD->abstractType().data(), instanceDefClassA->abstractType().data() ); Re-enable once specializations are re-used
    Cpp::TemplateDeclaration* templateDecl = dynamic_cast<Cpp::TemplateDeclaration*>(instanceDefClassA);
    QVERIFY(templateDecl);
    QVERIFY(instanceDefClassA != defClassA);
    QVERIFY(instanceDefClassA->context() == defClassA->context());
    QVERIFY(instanceDefClassA->internalContext() != defClassA->internalContext());
    QCOMPARE(instanceDefClassA->identifier(), Identifier("A<B,C>"));
    QCOMPARE(instanceDefClassA->identifier().toString(), QString("A< B, C >"));
    QVERIFY(instanceDefClassA->abstractType());
    AbstractType::Ptr t = instanceDefClassA->abstractType();
    IdentifiedType* identifiedType = dynamic_cast<IdentifiedType*>(t.unsafeData());
    QVERIFY(identifiedType);
    QVERIFY(identifiedType->declaration(top));
    kDebug() << identifiedType->declaration(top)->toString() << identifiedType->declaration(top)->range().textRange() << instanceDefClassA->toString() << instanceDefClassA->range().textRange();
    QCOMPARE(identifiedType->declaration(top), instanceDefClassA);
    QCOMPARE(identifiedType->qualifiedIdentifier().toString(), Identifier("A<B,C>").toString());
    QVERIFY(instanceDefClassA->internalContext());
    QVERIFY(instanceDefClassA->internalContext() != defClassA->internalContext());
    QVERIFY(instanceDefClassA->context() == defClassA->context());
    QVERIFY(instanceDefClassA->internalContext()->importedParentContexts().size() == 1);
    QVERIFY(defClassA->internalContext()->importedParentContexts().size() == 1);
    QCOMPARE(instanceDefClassA->internalContext()->importedParentContexts().front().context(0)->type(), DUContext::Template);
    QVERIFY(defClassA->internalContext()->importedParentContexts().front().context(0) != instanceDefClassA->internalContext()->importedParentContexts().front().context(0)); //The template-context has been instantiated

    QualifiedIdentifier ident2(ident);
    ident2.push(Identifier("Template1"));

    Declaration* template1InstanceDecl1 = findDeclaration(instanceDefClassA->internalContext(), Identifier("Template1"));
    QVERIFY(!template1InstanceDecl1);

    Declaration* template1InstanceDecl2 = findDeclaration(instanceDefClassA->internalContext(), Identifier("a"));
    QVERIFY(!template1InstanceDecl2);

    Declaration* template1InstanceDecl = findDeclaration(top, ident2);
    QVERIFY(!template1InstanceDecl);
  }

  release(top);
}

void TestDUChain::testTemplateParameters() {
  return;
  QByteArray method("template<class T, class N = T, int q = 5> class A {};");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[0]->type(), DUContext::Template);
  QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 3);
  
  Declaration* param1 = top->childContexts()[0]->localDeclarations()[0];
  QVERIFY(param1->type<CppTemplateParameterType>());
  
  Declaration* param2 = top->childContexts()[0]->localDeclarations()[1];
  QVERIFY(param2->type<CppTemplateParameterType>());
  TemplateParameterDeclaration* param2Decl = dynamic_cast<TemplateParameterDeclaration*>(param2);
  QVERIFY(param2Decl);
  QVERIFY(!param2Decl->defaultParameter().isEmpty());
  
  Declaration* param3 = top->childContexts()[0]->localDeclarations()[2];
  QVERIFY(param3->type<IntegralType>());
  TemplateParameterDeclaration* param3Decl = dynamic_cast<TemplateParameterDeclaration*>(param3);
  QVERIFY(param3Decl);
  QVERIFY(!param3Decl->defaultParameter().isEmpty());
  
  release(top);
}

void TestDUChain::testTemplateDefaultParameters() {
  QByteArray method("struct S {} ; namespace std { template<class T> class Template1 { }; } template<class _TT, typename TT2 = std::Template1<_TT> > class Template2 { typedef TT2 T1; };");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  kDebug() << "searching";
//  Declaration* memberDecl = findDeclaration(top, QualifiedIdentifier("Template2<S>::T1"));
  Declaration* memberDecl = findDeclaration(top, QualifiedIdentifier("Template2<S>::TT2"));
  QVERIFY(memberDecl);
  QVERIFY(memberDecl->abstractType());
  QVERIFY(!memberDecl->type<DelayedType>());
  QCOMPARE(memberDecl->abstractType()->toString(), QString("std::Template1< S >"));

  release(top);
}

void TestDUChain::testTemplates3() {
  QByteArray method("typedef int quakka; template<class T> struct Test { typedef T Value; const Value cv; const T cv2; ; typedef Value& ValueRef; typedef const ValueRef ConstValueRef; };");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  Declaration* qDecl = findDeclaration(top, QualifiedIdentifier("quakka"));
  QVERIFY(qDecl);
  QCOMPARE(qDecl->abstractType()->toString(), QString("quakka"));
  
  Declaration* cvDecl = findDeclaration(top, QualifiedIdentifier("Test<quakka>::cv"));
  QVERIFY(cvDecl);
  QVERIFY(cvDecl->abstractType());
  
  AbstractType::Ptr type = cvDecl->abstractType();
  IdentifiedType* idType = dynamic_cast<IdentifiedType*>(type.unsafeData());
  QVERIFY(idType);
  QVERIFY(idType->declaration(top));
  
  QVERIFY(cvDecl->abstractType()->modifiers() & AbstractType::ConstModifier);
  QVERIFY(TypeUtils::unAliasedType(cvDecl->abstractType())->modifiers() & AbstractType::ConstModifier);

  QCOMPARE(unAliasedType(cvDecl->abstractType())->toString(), QString("const int"));
  Declaration* cv2Decl = findDeclaration(top, QualifiedIdentifier("Test<quakka>::cv2"));
  QVERIFY(cv2Decl);
  QVERIFY(cv2Decl->abstractType());
  QVERIFY(cv2Decl->abstractType()->modifiers() & AbstractType::ConstModifier);
  QCOMPARE(cv2Decl->abstractType()->toString(), QString("const quakka"));
  QCOMPARE(unAliasedType(cv2Decl->abstractType())->toString(), QString("const int"));
  QVERIFY(TypeUtils::unAliasedType(cv2Decl->abstractType())->modifiers() & AbstractType::ConstModifier);

  {
    Declaration* cvrDecl = findDeclaration(top, QualifiedIdentifier("Test<quakka>::ConstValueRef"));
    QVERIFY(cvrDecl);
    
    AbstractType::Ptr type = cvrDecl->abstractType();
    IdentifiedType* idType = dynamic_cast<IdentifiedType*>(type.unsafeData());
    QVERIFY(idType);
    QVERIFY(idType->declaration(top));
    
    QVERIFY(cvrDecl->abstractType());
    QVERIFY(unAliasedType(cvrDecl->abstractType())->modifiers() & AbstractType::ConstModifier);
    QCOMPARE(realType(cvrDecl->abstractType(), 0, 0)->toString(), QString("const int"));
    QCOMPARE(targetType(cvrDecl->abstractType(), 0, 0)->toString(), QString("const int"));
    TypeAliasType::Ptr alias = cvrDecl->abstractType().cast<TypeAliasType>();
    QVERIFY(alias);
    QVERIFY(alias->type());
    QCOMPARE(targetTypeKeepAliases(Cpp::shortenTypeForViewing(cvrDecl->abstractType()), 0, 0)->toString(), QString("const quakka"));
    //When the target is a typedef type, the duchain does not know whether it should write "const" before or behind, so it puts it behind "quakka"
    QCOMPARE(Cpp::shortenTypeForViewing(cvrDecl->abstractType())->toString(), QString("quakka const&"));
    QVERIFY(TypeUtils::unAliasedType(cvrDecl->abstractType())->modifiers() & AbstractType::ConstModifier);
  }
  {
    Declaration* cvrDecl = findDeclaration(top, QualifiedIdentifier("Test<quakka*>::ConstValueRef"));
    QVERIFY(cvrDecl);
    QVERIFY(cvrDecl->abstractType());
    QVERIFY(unAliasedType(cvrDecl->abstractType())->modifiers() & AbstractType::ConstModifier);
    QCOMPARE(realType(cvrDecl->abstractType(), 0, 0)->toString(), QString("quakka* const"));
    TypeAliasType::Ptr alias = cvrDecl->abstractType().cast<TypeAliasType>();
    QVERIFY(alias);
    QVERIFY(alias->type());
    QCOMPARE(Cpp::shortenTypeForViewing(cvrDecl->abstractType())->toString(), QString("quakka* const&"));
    QVERIFY(TypeUtils::unAliasedType(cvrDecl->abstractType())->modifiers() & AbstractType::ConstModifier);
  
    release(top);
  }
  {
    QByteArray method("template<class T> struct Cnt { typedef T Val; }; struct Item; template<class Value> struct Freqto { struct Item { typedef Value Value2; }; struct Pattern : public Cnt<Item> { }; };");

    TopDUContext* top = parse(method, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 4);
    QCOMPARE(top->childContexts()[3]->childContexts().count(), 2);
    
    //The import should have been delayed, since it needs 'Item'
    QVERIFY(top->childContexts()[3]->childContexts()[1]->owner());
    ClassDeclaration* classDecl = dynamic_cast<ClassDeclaration*>(top->childContexts()[3]->childContexts()[1]->owner());
    QVERIFY(classDecl);
    QCOMPARE(classDecl->baseClassesSize(), 1u);
    QVERIFY(top->childContexts()[3]->childContexts()[1]->importedParentContexts().isEmpty());
    QVERIFY(classDecl->baseClasses()[0].baseClass.type<DelayedType>());
    kDebug() << classDecl->baseClasses()[0].baseClass.abstractType()->toString();
    Declaration* val2Decl = findDeclaration(top, QualifiedIdentifier("Freqto<int>::Pattern::Val::Value2"));
    QVERIFY(val2Decl);
    QCOMPARE(unAliasedType(val2Decl->abstractType())->toString(), QString("int"));

    release(top);
  }
}

void TestDUChain::testTemplates4()
{
  {
    QByteArray method("template<class A> class Temp { typedef A Mem; }; class B {class A { typedef int AMember; }; }; ");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 3);
    Declaration* memDecl = findDeclaration(top->childContexts()[2], QualifiedIdentifier("Temp<A>::Mem"));;
    QVERIFY(memDecl);
    QCOMPARE(unAliasedType(memDecl->abstractType())->toString(), QString("B::A"));
    QVERIFY(findDeclaration(top->childContexts()[2], QualifiedIdentifier("Temp<A>::Mem::AMember")));

    release(top);
  }
  {
    QByteArray method("template<class T> struct Cnt { typedef T Val; }; struct Item; template<class Value> struct Freqto { struct Item { typedef Value Value2; }; struct Pattern : public Cnt<Item> { }; };");

    TopDUContext* top = parse(method, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 4);
    QCOMPARE(top->childContexts()[3]->childContexts().count(), 2);
    
    //The import should have been delayed, since it needs 'Item'
    QVERIFY(top->childContexts()[3]->childContexts()[1]->owner());
    ClassDeclaration* classDecl = dynamic_cast<ClassDeclaration*>(top->childContexts()[3]->childContexts()[1]->owner());
    QVERIFY(classDecl);
    QCOMPARE(classDecl->baseClassesSize(), 1u);
    QVERIFY(top->childContexts()[3]->childContexts()[1]->importedParentContexts().isEmpty());
    QVERIFY(classDecl->baseClasses()[0].baseClass.type<DelayedType>());
    kDebug() << classDecl->baseClasses()[0].baseClass.abstractType()->toString();
    Declaration* val2Decl = findDeclaration(top, QualifiedIdentifier("Freqto<int>::Pattern::Val::Value2"));
    QVERIFY(val2Decl);
    QCOMPARE(unAliasedType(val2Decl->abstractType())->toString(), QString("int"));

    release(top);
  }
}

void TestDUChain::testTemplates2() {
  QByteArray method("struct S {} ; template<class TT> class Base { struct Alloc { typedef TT& referenceType; }; }; template<class T> struct Class : public Base<T> { typedef typename Base<T>::Alloc Alloc; typedef typename Alloc::referenceType reference; reference member; }; Class<S*&> instance;");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(top->childContexts().count() > 2);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  QVERIFY(!top->childContexts()[1]->localDeclarations()[0]->type<TypeAliasType>());
  QVERIFY(top->childContexts().count() == 5);
  QVERIFY(top->childContexts()[4]->localDeclarations().count() > 1);
  
  QVERIFY(top->childContexts()[4]->localDeclarations()[0]->type<TypeAliasType>());
  QVERIFY(top->childContexts()[4]->localDeclarations()[1]->type<TypeAliasType>());
  QVERIFY(top->childContexts()[4]->localDeclarations()[0]->type<TypeAliasType>()->type().cast<DelayedType>());
  QVERIFY(top->childContexts()[4]->localDeclarations()[1]->type<TypeAliasType>()->type().cast<DelayedType>());
  QVERIFY(top->childContexts()[4]->localDeclarations()[2]->type<DelayedType>());
  
  Declaration* memberDecl;

  kDebug() << "checking member";
  
  memberDecl = findDeclaration(top, QualifiedIdentifier("Class<S>::member"));
  QVERIFY(memberDecl);
  QVERIFY(unAliasedType(memberDecl->abstractType()));
  QCOMPARE(unAliasedType(memberDecl->abstractType())->toString(), QString("S&"));
  
  memberDecl = findDeclaration(top, QualifiedIdentifier("instance"));
  QVERIFY(memberDecl);
  QVERIFY(memberDecl->abstractType());
  QCOMPARE(memberDecl->abstractType()->toString(), QString("Class< S*& >"));

//   memberDecl = findDeclaration(top, QualifiedIdentifier("Class<S>::Alloc<S>::referenceType"));
//   QVERIFY(memberDecl);
//   QVERIFY(memberDecl->abstractType());
//   QCOMPARE(memberDecl->abstractType()->toString(), QString("S&"/*));

  memberDecl = findDeclaration(top, QualifiedIdentifier("Class<S*>::member"));
  QVERIFY(memberDecl);
  QVERIFY(unAliasedType(memberDecl->abstractType()));
  QCOMPARE(unAliasedType(memberDecl->abstractType())->toString(), QString("S*&"));

  release(top);
}

void TestDUChain::testTemplatesRebind() {
  QByteArray method("struct A {}; struct S {typedef A Value;} ; template<class TT> class Base { template<class T> struct rebind { typedef Base<T> other; }; typedef TT Type; }; template<class T> class Class { typedef Base<T>::rebind<T>::other::Type MemberType; MemberType member; Base<T>::template rebind<T>::other::Type member2; T::Value value; }; };");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 6);
  QCOMPARE(top->childContexts()[3]->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[3]->childContexts()[1]->localDeclarations().count(), 1);
  {
  QVERIFY(findDeclaration(top, QualifiedIdentifier("Base<S>")));
  QVERIFY(!findDeclaration(top, QualifiedIdentifier("Base<S>"))->type<DelayedType>());
  QVERIFY(findDeclaration(top, QualifiedIdentifier("Base<S>::rebind<A>")));
  QVERIFY(!findDeclaration(top, QualifiedIdentifier("Base<S>::rebind<A>::other"))->type<DelayedType>());
  QVERIFY(findDeclaration(top, QualifiedIdentifier("Base<S>::rebind<A>::other::Type")));
  QVERIFY(!findDeclaration(top, QualifiedIdentifier("Base<S>::rebind<A>::other::Type"))->type<DelayedType>());

  Declaration* memberDecl = findDeclaration(top, QualifiedIdentifier("Base<S>::rebind<A>::other::Type"));
  QVERIFY(memberDecl);
  QVERIFY(memberDecl->abstractType());
  QVERIFY(memberDecl->abstractType());
  QVERIFY(unAliasedType(memberDecl->abstractType()));
  QCOMPARE(unAliasedType(memberDecl->abstractType())->toString(), QString("A"));
  }

  Declaration* memberDecl = findDeclaration(top, QualifiedIdentifier("Class<S>::member"));
  QVERIFY(memberDecl);
  QVERIFY(memberDecl->abstractType());
  QVERIFY(unAliasedType(memberDecl->abstractType()));
  QCOMPARE(unAliasedType(memberDecl->abstractType())->toString(), QString("S"));

  Declaration* member3Decl = findDeclaration(top, QualifiedIdentifier("Class<S>::value"));
  QVERIFY(member3Decl);
  QVERIFY(member3Decl->abstractType());
  QVERIFY(unAliasedType(member3Decl->abstractType()));
  QCOMPARE(unAliasedType(member3Decl->abstractType())->toString(), QString("A"));

  Declaration* member2Decl = findDeclaration(top, QualifiedIdentifier("Class<S>::member2"));
  QVERIFY(member2Decl);
  QVERIFY(unAliasedType(member2Decl->abstractType()));
  QCOMPARE(unAliasedType(member2Decl->abstractType())->toString(), QString("S"));

  release(top);
}

void TestDUChain::testTemplatesRebind2() {
  QByteArray method("struct A {}; struct S {typedef A Value;} ;template<class T> class Test { Test(); }; template<class T> class Class { typedef T::Value Value; T::Value value; typedef Test<Value> ValueClass; Test<const Value> ValueClass2;}; };");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QList<Declaration*> constructors;
  TypeUtils::getConstructors( top->localDeclarations()[2]->abstractType().cast<CppClassType>(), top, constructors );
  QCOMPARE(constructors.size(), 1);
  OverloadResolver resolution( DUContextPointer(top->localDeclarations()[2]->internalContext()), TopDUContextPointer(top) );
  QVERIFY(resolution.resolveConstructor( OverloadResolver::ParameterList() ));

  QVERIFY(top->childContexts().count() >= 6);
  QCOMPARE(top->childContexts()[5]->localDeclarations().count(), 4);
  AbstractType::Ptr valueType = top->childContexts()[5]->localDeclarations()[0]->abstractType();
  QVERIFY(containsDelayedType(valueType));
  QVERIFY(containsDelayedType(top->childContexts()[5]->localDeclarations()[1]->abstractType()));
  QVERIFY(containsDelayedType(top->childContexts()[5]->localDeclarations()[2]->abstractType()));
  QVERIFY(containsDelayedType(top->childContexts()[5]->localDeclarations()[3]->abstractType()));
  
  Declaration* valueAliasDecl = findDeclaration(top, QualifiedIdentifier("Class<S>::Value"));
  QVERIFY(valueAliasDecl);
  TypeAliasType::Ptr alias = valueAliasDecl->type<TypeAliasType>();
  QVERIFY(alias);
  QVERIFY(alias->type());
  QVERIFY(!alias->type().cast<DelayedType>());
  QCOMPARE(unAliasedType(alias.cast<AbstractType>())->toString(), QString("A"));
  kDebug() << "aliased type:" << alias->type()->toString();
  kDebug() << "typedef type:" << alias->toString();
  kDebug() << "un-aliased type:" << unAliasedType(alias.cast<AbstractType>())->toString();
  
  Declaration* member5Decl = findDeclaration(top, QualifiedIdentifier("Class<S>::ValueClass2"));
  QVERIFY(member5Decl);
  QVERIFY(unAliasedType(member5Decl->abstractType()));
  AbstractType::Ptr type = unAliasedType(member5Decl->abstractType());
  QCOMPARE(unAliasedType(member5Decl->abstractType())->toString(), QString("Test< S::Value >")); ///@todo This will fail once we parse "const" correctly, change it to "Test< const A >" then

  Declaration* member4Decl = findDeclaration(top, QualifiedIdentifier("Class<S>::ValueClass"));
  QVERIFY(member4Decl);
  QVERIFY(unAliasedType(member4Decl->abstractType()));
  QCOMPARE(unAliasedType(member4Decl->abstractType())->toString(), QString("Test< S::Value >"));

  Declaration* member3Decl = findDeclaration(top, QualifiedIdentifier("Class<S>::value"));
  QVERIFY(member3Decl);
  QVERIFY(unAliasedType(member3Decl->abstractType()));
  QCOMPARE(unAliasedType(member3Decl->abstractType())->toString(), QString("A"));

  release(top);
}

void TestDUChain::testForwardDeclaration()
{
  QByteArray method("class Test; Test t; class Test {int i; class SubTest; }; Test::SubTest t2; class Test::SubTest{ int i;};");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QVERIFY(top->inSymbolTable());
  QCOMPARE(top->localDeclarations().count(), 4); //Test::SubTest is in a prefix context
  QCOMPARE(top->childContexts().count(), 2); //Test::SubTest is in a prefix context
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1); //Test::SubTest is in a prefix context

  QVERIFY(dynamic_cast<ForwardDeclaration*>(top->localDeclarations()[0]));
  QVERIFY(top->localDeclarations()[0]->inSymbolTable());
  QVERIFY(top->localDeclarations()[1]->inSymbolTable());
  QVERIFY(top->localDeclarations()[2]->inSymbolTable());
  QVERIFY(top->localDeclarations()[3]->inSymbolTable());
  QVERIFY(!top->localDeclarations()[2]->isForwardDeclaration());

  QCOMPARE(top->localDeclarations()[0]->additionalIdentity(), top->localDeclarations()[2]->additionalIdentity());
  QVERIFY(!dynamic_cast<TemplateDeclaration*>(top->localDeclarations()[0]));
  QVERIFY(!dynamic_cast<TemplateDeclaration*>(top->localDeclarations()[2]));

  CppClassType::Ptr type1 = top->localDeclarations()[0]->type<CppClassType>();
  kDebug() << typeid(*top->localDeclarations()[1]->abstractType()).name();
  CppClassType::Ptr type2 = top->localDeclarations()[1]->type<CppClassType>();
  CppClassType::Ptr type3 = top->localDeclarations()[2]->type<CppClassType>();
  CppClassType::Ptr type4 = top->localDeclarations()[3]->type<CppClassType>();
  CppClassType::Ptr type5 = top->childContexts()[1]->localDeclarations()[0]->type<CppClassType>();


  QCOMPARE(top->localDeclarations()[0]->kind(), Declaration::Type);
  QCOMPARE(top->localDeclarations()[1]->kind(), Declaration::Instance);
  QCOMPARE(top->localDeclarations()[2]->kind(), Declaration::Type);
  QCOMPARE(top->localDeclarations()[3]->kind(), Declaration::Instance);
  QCOMPARE(top->childContexts()[1]->localDeclarations()[0]->kind(), Declaration::Type);
  QVERIFY(type1);
  QVERIFY(type2);
  QVERIFY(type3);
  QVERIFY(type4);
  QVERIFY(type5);

  Declaration* TestDecl = top->localDeclarations()[2];
  QVERIFY(TestDecl->internalContext());
  QCOMPARE(TestDecl->internalContext()->localDeclarations().count(), 2);

  CppClassType::Ptr subType = TestDecl->internalContext()->localDeclarations()[1]->type<CppClassType>();
  QVERIFY(subType);

  QCOMPARE(subType->declaration(0)->abstractType()->indexed(), type5->indexed());
  QCOMPARE(type1->indexed(), type2->indexed());
  QCOMPARE(type1->declaration(top), type2->declaration(top));
  QVERIFY(type1->equals(type3.unsafeData()));
  QVERIFY(type3->equals(type1.unsafeData()));
  QCOMPARE(type1->declaration(0)->abstractType()->indexed(), type3->indexed());
  kDebug() << typeid(*type3->declaration(top)).name();
  kDebug() << typeid(*type1->declaration(top)).name();
  QCOMPARE(type1->declaration(top)->logicalInternalContext(0), type3->declaration(top)->internalContext());
  QCOMPARE(type2->declaration(top)->logicalInternalContext(0), type3->declaration(top)->internalContext());

  kDebug() << subType->qualifiedIdentifier().toString(); //declaration(0)->toString();
  kDebug() << type5->qualifiedIdentifier().toString(); //declaration(0)->toString();

  release(top);
}

void TestDUChain::testCaseUse()
{
  QByteArray method("enum Bla { Val }; char* c; int a; void test() { switch(a) { case Val: a += 1; break; } delete c; }   ");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->localDeclarations().count(), 4);
  QVERIFY(top->localDeclarations()[0]->internalContext());
  QCOMPARE(top->localDeclarations()[0]->internalContext()->localDeclarations().count(), 1);
  QCOMPARE(top->localDeclarations()[0]->internalContext()->localDeclarations()[0]->uses().count(), 1);
  QCOMPARE(top->localDeclarations()[1]->uses().count(), 1);
  QCOMPARE(top->localDeclarations()[0]->internalContext()->localDeclarations()[0]->uses().begin()->count(), 1);
  QCOMPARE(top->localDeclarations()[1]->uses().begin()->count(), 1);

  release(top);
}

void TestDUChain::testSizeofUse()
{
  QByteArray method("class C{}; const unsigned int i = sizeof(C);");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->localDeclarations().count(), 2);
  QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);

  release(top);
}

void TestDUChain::testDefinitionUse()
{
  QByteArray method("class A{}; class C : public A{ void test(); }; C::test() {} ");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->localDeclarations().count(), 3);
  QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);
  QCOMPARE(top->localDeclarations()[1]->uses().count(), 1);

  release(top);
}


void TestDUChain::testOperatorUses()
{
  {
    QByteArray method("template<class T> struct Fruk { Fruk<T>& operator[](int); }; Fruk<int> f; void test(){ const int mog; Fruk q (f[mog]); }");

    TopDUContext* top = parse(method, DumpAll);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 4);
    QCOMPARE(top->childContexts()[3]->localDeclarations().size(), 2);
    QCOMPARE(top->childContexts()[3]->localDeclarations()[0]->uses().size(), 1);

    release(top);
  }
  {
    QByteArray method("struct S { bool operator() () const {}  };void test() { S s; s(); S()(); } ");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->localDeclarations()[0]->uses().size(), 1);
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->size(), 2);
    QCOMPARE(top->childContexts().count(), 3);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().size(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->size(), 2);

    release(top);
  }
  {
    QByteArray method("struct S { S operator() () const {}; S(int) {};  };void test() { S s(1); s(); S(1)()(); } ");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->localDeclarations()[0]->uses().size(), 1);
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->size(), 3);
    QCOMPARE(top->childContexts().count(), 3);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().size(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[0]->uses().begin()->size(), 3);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().size(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->size(), 2);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->at(0).textRange(), KTextEditor::Range(0, 68, 0, 69));
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->at(1).textRange(), KTextEditor::Range(0, 79, 0, 80));

    release(top);
  }
  {
    //                 0         1         2         3         4         5
    //                 012345678901234567890123456789012345678901234567890123456789
    QByteArray method("struct foo { bool operator==(const foo&){} };\n"
                      "int main() { foo t1; foo t2;\n"
                      // not valid in global context, hence put it into main
                      "bool b1 = t1 == t2;\n"
                      "bool b2 = t1.operator==(t2); }");

    TopDUContext* top = parse(method, DumpAll);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->childContexts().first()->localDeclarations().size(), 1);
    QCOMPARE(top->childContexts().first()->localDeclarations().first()->uses().count(), 1);
    QCOMPARE(top->childContexts().first()->localDeclarations().first()->uses().begin()->size(), 2);
    QCOMPARE(top->childContexts().first()->localDeclarations().first()->uses().begin()->at(0), SimpleRange(2, 13, 2, 15));
    QCOMPARE(top->childContexts().first()->localDeclarations().first()->uses().begin()->at(1), SimpleRange(3, 13, 3, 23));

    release(top);
  }
  {
    //                 0         1         2         3         4         5
    //                 012345678901234567890123456789012345678901234567890123456789
    QByteArray method("struct foo { foo& operator=(const foo&){} };\n"
                      "int main() { foo t1; foo t2 = t1;\n"
                      // not valid in global context, hence put it into main
                      "t1.operator=(t2); return 0; }");

    TopDUContext* top = parse(method, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->childContexts().first()->localDeclarations().size(), 1);
    QCOMPARE(top->childContexts().first()->localDeclarations().first()->uses().count(), 1);
    QCOMPARE(top->childContexts().first()->localDeclarations().first()->uses().begin()->size(), 2);
    QCOMPARE(top->childContexts().first()->localDeclarations().first()->uses().begin()->at(0), SimpleRange(1, 28, 1, 29));
    QCOMPARE(top->childContexts().first()->localDeclarations().first()->uses().begin()->at(1), SimpleRange(2, 3, 2, 12));

    release(top);
  }
}

struct TestContext {
  TestContext() {
    static int number = 0;
    ++number;
    DUChainWriteLocker lock(DUChain::lock());
    m_context = new TopDUContext(IndexedString(QString("/test1/%1").arg(number)), SimpleRange());
    m_normalContext = new DUContext(SimpleRange(), m_context);
    DUChain::self()->addDocumentChain(m_context);
    Q_ASSERT(IndexedDUContext(m_context).context() == m_context);
  }

  ~TestContext() {
    foreach(TestContext* importer, importers)
      importer->unImport(QList<TestContext*>() << this);
    unImport(imports);
    DUChainWriteLocker lock(DUChain::lock());
    release(m_context);//->deleteSelf();
  }

  void verify(QList<TestContext*> allContexts) {

    {
      DUChainReadLocker lock(DUChain::lock());
      QCOMPARE(m_context->importedParentContexts().count(), imports.count());
    }
    //Compute a closure of all children, and verify that they are imported.
    QSet<TestContext*> collected;
    collectImports(collected);
    collected.remove(this);

    DUChainReadLocker lock(DUChain::lock());
    foreach(TestContext* context, collected) {
      QVERIFY(m_context->imports(context->m_context, SimpleCursor::invalid()));
#ifdef TEST_NORMAL_IMPORTS
      QVERIFY(m_normalContext->imports(context->m_normalContext));
#endif
    }
    //Verify that no other contexts are imported

    foreach(TestContext* context, allContexts)
      if(context != this) {
        QVERIFY(collected.contains(context) || !m_context->imports(context->m_context, SimpleCursor::invalid()));
#ifdef TEST_NORMAL_IMPORTS
        QVERIFY(collected.contains(context) || !m_normalContext->imports(context->m_normalContext, SimpleCursor::invalid()));
#endif
      }
  }

  void collectImports(QSet<TestContext*>& collected) {
    if(collected.contains(this))
      return;
    collected.insert(this);
    foreach(TestContext* context, imports)
      context->collectImports(collected);
  }
  void import(TestContext* ctx) {
    if(imports.contains(ctx) || ctx == this)
      return;
    imports << ctx;
    ctx->importers << this;
    DUChainWriteLocker lock(DUChain::lock());
    m_context->addImportedParentContext(ctx->m_context);
#ifdef TEST_NORMAL_IMPORTS
    m_normalContext->addImportedParentContext(ctx->m_normalContext);
#endif
  }

  void unImport(QList<TestContext*> ctxList) {
    QList<TopDUContext*> list;
    QList<DUContext*> normalList;

    foreach(TestContext* ctx, ctxList) {
      if(!imports.contains(ctx))
        continue;
      list << ctx->m_context;
      normalList << ctx->m_normalContext;

      imports.removeAll(ctx);
      ctx->importers.removeAll(this);
    }
    DUChainWriteLocker lock(DUChain::lock());
    m_context->removeImportedParentContexts(list);
    
#ifdef TEST_NORMAL_IMPORTS
    foreach(DUContext* ctx, normalList)
      m_normalContext->removeImportedParentContext(ctx);
#endif
  }
  
  void clearImports() {
    
    {
      DUChainWriteLocker lock(DUChain::lock());
    
      m_context->clearImportedParentContexts();
      m_normalContext->clearImportedParentContexts();
    }
    foreach(TestContext* ctx, imports) {
      imports.removeAll(ctx);
      ctx->importers.removeAll(this);
    }
  }

  QList<TestContext*> imports;

  private:

  TopDUContext* m_context;
  DUContext* m_normalContext;
  QList<TestContext*> importers;
};

void collectReachableNodes(QSet<uint>& reachableNodes, uint currentNode) {
  if(!currentNode)
    return;
  reachableNodes.insert(currentNode);
  const Utils::SetNodeData* node = KDevelop::RecursiveImportRepository::repository()->nodeFromIndex(currentNode);
  Q_ASSERT(node);
  collectReachableNodes(reachableNodes, node->leftNode);
  collectReachableNodes(reachableNodes, node->rightNode);
}

uint collectNaiveNodeCount(uint currentNode) {
  if(!currentNode)
    return 0;
  uint ret = 1;
  const Utils::SetNodeData* node = KDevelop::RecursiveImportRepository::repository()->nodeFromIndex(currentNode);
  Q_ASSERT(node);
  ret += collectNaiveNodeCount(node->leftNode);
  ret += collectNaiveNodeCount(node->rightNode);
  return ret;
}

void TestDUChain::testImportCache()
{
  KDevelop::globalItemRepositoryRegistry().printAllStatistics();

  KDevelop::RecursiveImportRepository::repository()->printStatistics();
  
  //Analyze the whole existing import-cache
  //This is very expensive, since it involves loading all existing top-contexts
  uint topContextCount = DUChain::self()->newTopContextIndex();
  
  uint analyzedCount = 0;
  uint totalImportCount = 0;
  uint naiveNodeCount = 0;
  QSet<uint> reachableNodes;
  
  DUChainReadLocker lock(DUChain::lock());
  for(uint a = 0; a < topContextCount; ++a) {
    if(a % (topContextCount / 100) == 0) {
      kDebug() << "progress:" << (a * 100) / topContextCount;
    }
    TopDUContext* context = DUChain::self()->chainForIndex(a);
    if(context) {
      TopDUContext::IndexedRecursiveImports imports = context->recursiveImportIndices();
      ++analyzedCount;
      totalImportCount += imports.set().count();
      collectReachableNodes(reachableNodes, imports.setIndex());
      naiveNodeCount += collectNaiveNodeCount(imports.setIndex());
    }
  }
  
  kDebug() << "average total count of imports:" << totalImportCount / analyzedCount;
  kDebug() << "count of reachable nodes:" << reachableNodes.size();
  kDebug() << "naive node-count:" << naiveNodeCount << "sharing compression factor:" << ((float)reachableNodes.size()) / ((float)naiveNodeCount);
}

void TestDUChain::testImportStructure()
{
  clock_t startClock = clock();
  kDebug() << "before: " << KDevelop::RecursiveImportRepository::repository()->getDataRepository().statistics().print();
  
  ///Maintains a naive import-structure along with a real top-context import structure, and allows comparing both.
  int cycles = 5;
  //int cycles = 100;
  //srand(time(NULL));
  for(int t = 0; t < cycles; ++t) {
    QList<TestContext*> allContexts;
    //Create a random structure
    int contextCount = 120;
    int verifyOnceIn = contextCount/*((contextCount*contextCount)/20)+1*/; //Verify once in every chances(not in all cases, becase else the import-structure isn't built on-demand!)
    int clearOnceIn = contextCount;
    for(int a = 0; a < contextCount; a++)
      allContexts << new TestContext();
    for(int c = 0; c < cycles; ++c) {
      //kDebug() << "main-cycle" << t  << "sub-cycle" << c;
      //Add random imports and compare
      for(int a = 0; a < contextCount; a++) {
        //Import up to 5 random other contexts into each context
        int importCount = rand() % 5;
        //kDebug()   << "cnt> " << importCount;
        for(int i = 0; i < importCount; ++i)
        {
          //int importNr = rand() % contextCount;
          //kDebug() << "nmr > " << importNr;
          //allContexts[a]->import(allContexts[importNr]);
          allContexts[a]->import(allContexts[rand() % contextCount]);
        }
        for(int b = 0; b < contextCount; b++)
          if(rand() % verifyOnceIn == 0)
            allContexts[b]->verify(allContexts);
      }

      //Remove random imports and compare
      for(int a = 0; a < contextCount; a++) {
        //Import up to 5 random other contexts into each context
        int removeCount = rand() % 3;
        QSet<TestContext*> removeImports;
        for(int i = 0; i < removeCount; ++i)
          if(allContexts[a]->imports.count())
            removeImports.insert(allContexts[a]->imports[rand() % allContexts[a]->imports.count()]);
        allContexts[a]->unImport(removeImports.toList());

        for(int b = 0; b < contextCount; b++)
          if(rand() % verifyOnceIn == 0)
            allContexts[b]->verify(allContexts);
      }
      
      for(int a = 0; a < contextCount; a++) {
        if(rand() % clearOnceIn == 0) {
          allContexts[a]->clearImports();
          allContexts[a]->verify(allContexts);
        }
      }
    }

  kDebug() << "after: " << KDevelop::RecursiveImportRepository::repository()->getDataRepository().statistics().print();

  for(int a = 0; a < contextCount; ++a)
    delete allContexts[a];
  allContexts.clear();
  kDebug() << "after cleanup: " << KDevelop::RecursiveImportRepository::repository()->getDataRepository().statistics().print();
  
  }
  clock_t endClock = clock();
  kDebug() << "total clock cycles needed for import-structure test:" << endClock - startClock;
}

void TestDUChain::testForwardDeclaration2()
{
  QByteArray method("class Test; Test t; class Test {int i; class SubTest; }; class Test; Test::SubTest t2; class Test::SubTest{ int i;};");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 5);
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);

  QCOMPARE(top->childContexts()[1]->scopeIdentifier(), QualifiedIdentifier("Test")); //This is the prefix-context around Test::SubTest

  QVERIFY(dynamic_cast<ForwardDeclaration*>(top->localDeclarations()[0]));

  CppClassType::Ptr type1 = top->localDeclarations()[0]->type<CppClassType>();
  CppClassType::Ptr type2 = top->localDeclarations()[1]->type<CppClassType>();
  CppClassType::Ptr type3 = top->localDeclarations()[2]->type<CppClassType>();
  CppClassType::Ptr type4 = top->localDeclarations()[4]->type<CppClassType>();
  CppClassType::Ptr type5 = top->childContexts()[1]->localDeclarations()[0]->type<CppClassType>();
  CppClassType::Ptr type12 = top->localDeclarations()[3]->type<CppClassType>();

  QCOMPARE(top->localDeclarations()[0]->kind(), Declaration::Type);
  QCOMPARE(top->localDeclarations()[1]->kind(), Declaration::Instance);
  QCOMPARE(top->localDeclarations()[2]->kind(), Declaration::Type);
  QCOMPARE(top->localDeclarations()[3]->kind(), Declaration::Type);
  QCOMPARE(top->localDeclarations()[4]->kind(), Declaration::Instance);
  QCOMPARE(top->childContexts()[1]->localDeclarations()[0]->kind(), Declaration::Type);

  QVERIFY(type1);
  QVERIFY(type12);
  QVERIFY(type2);
  QVERIFY(type3);
  QVERIFY(type4);
  QVERIFY(type5);

  Declaration* TestDecl = top->localDeclarations()[2];
  QVERIFY(TestDecl->internalContext());
  QCOMPARE(TestDecl->internalContext()->localDeclarations().count(), 2);

  CppClassType::Ptr subType = TestDecl->internalContext()->localDeclarations()[1]->type<CppClassType>();
  QVERIFY(subType);

  QCOMPARE(type1->declaration(0)->abstractType()->indexed(), type2->declaration(0)->abstractType()->indexed());
  QCOMPARE(type2->declaration(0)->abstractType()->indexed(), type3->indexed());
  QCOMPARE(type1->declaration(0)->abstractType()->indexed(), type12->declaration(0)->abstractType()->indexed());
  kDebug() << type4->qualifiedIdentifier().toString();
  kDebug() << type4->declaration(0)->toString();
  QCOMPARE(type4->declaration(0)->abstractType()->indexed(), type5->indexed());
  QCOMPARE(type3->declaration(top)->internalContext()->scopeIdentifier(true), QualifiedIdentifier("Test"));
  QCOMPARE(type5->declaration(top)->internalContext()->scopeIdentifier(true), QualifiedIdentifier("Test::SubTest"));
  QCOMPARE(type3->declaration(top)->qualifiedIdentifier(), QualifiedIdentifier("Test"));
  QCOMPARE(type5->declaration(top)->qualifiedIdentifier(), QualifiedIdentifier("Test::SubTest"));

  ///@todo think about this
  QCOMPARE(type5->declaration(top)->identifier(), Identifier("SubTest"));
  QVERIFY(subType->equals(type5.unsafeData()));
  QVERIFY(type5->equals(subType.unsafeData()));
  kDebug() << type5->toString() << type4->toString();
  kDebug() << type4->qualifiedIdentifier().toString() << type5->qualifiedIdentifier().toString();
  QVERIFY(type4->equals(type5.unsafeData()));
  QVERIFY(type5->equals(type4.unsafeData()));
  kDebug() << subType->toString();
  QCOMPARE(subType->declaration(0)->abstractType()->indexed(), type5->indexed());

  release(top);
}

void TestDUChain::testForwardDeclaration3()
{
  QByteArray method("namespace B {class Test;} B::Test t; namespace B { class Test {int i; class SubTest; };} B::Test::SubTest t2; namespace B {class Test::SubTest{ int i;};}");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 5);
  QCOMPARE(top->childContexts().count(), 3);

  QVERIFY(dynamic_cast<ForwardDeclaration*>(top->childContexts()[0]->localDeclarations()[0]));

  ForwardDeclaration* forwardDecl = static_cast<ForwardDeclaration*>(top->childContexts()[0]->localDeclarations()[0]);
  kDebug() << forwardDecl->toString();
  QVERIFY(forwardDecl->resolve(0));
  QCOMPARE(forwardDecl->resolve(0), top->childContexts()[1]->localDeclarations()[0]);

  CppClassType::Ptr type1 = top->childContexts()[0]->localDeclarations()[0]->type<CppClassType>();
  CppClassType::Ptr type2 = top->localDeclarations()[1]->type<CppClassType>();
  CppClassType::Ptr type3 = top->childContexts()[1]->localDeclarations()[0]->type<CppClassType>();
  CppClassType::Ptr type4 = top->localDeclarations()[3]->type<CppClassType>();
  CppClassType::Ptr type5 = top->childContexts()[2]->childContexts()[0]->localDeclarations()[0]->type<CppClassType>();
  
  QCOMPARE(top->childContexts()[2]->localScopeIdentifier(), QualifiedIdentifier("B"));
  QCOMPARE(top->childContexts()[2]->childContexts()[0]->type(), DUContext::Helper);
  QCOMPARE(top->childContexts()[2]->childContexts()[0]->localScopeIdentifier(), QualifiedIdentifier("Test"));
  


  QVERIFY(type1);
  QVERIFY(type2);
  QVERIFY(type3);
  QVERIFY(type4);
  QVERIFY(type5);

  Declaration* TestDecl = top->childContexts()[1]->localDeclarations()[0];
  QVERIFY(TestDecl->internalContext());
  QCOMPARE(TestDecl->internalContext()->localDeclarations().count(), 2);
  CppClassType::Ptr subType = TestDecl->internalContext()->localDeclarations()[1]->type<CppClassType>();
  QVERIFY(subType);

  QCOMPARE(type1->indexed(), type2->indexed());
  QCOMPARE(type2->declaration(top)->abstractType()->indexed(), type3->indexed());
  QCOMPARE(subType->declaration(top)->abstractType()->indexed(), type4->declaration(top)->abstractType()->indexed());
  kDebug() << type4->declaration(top)->qualifiedIdentifier().toString() << type5->qualifiedIdentifier().toString();
  QCOMPARE(type4->declaration(top)->abstractType()->indexed(), type5->indexed());

  release(top);
}

void TestDUChain::testForwardDeclaration4()
{
  QByteArray method("class Forward;\n"
                    "class Test\n"
                    "{\n"
                    "    virtual Test a(Forward) const=0;\n"
                    "    virtual Forward b(Forward) const=0;\n"
                    "    virtual Forward c(Forward);\n"
                    "    virtual Test d(Forward);\n"
                    "};");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 2);
  QCOMPARE(top->childContexts().count(), 1);

  QVERIFY(dynamic_cast<ForwardDeclaration*>(top->localDeclarations()[0]));

  ForwardDeclaration* forwardDecl = static_cast<ForwardDeclaration*>(top->localDeclarations()[0]);

  QCOMPARE(forwardDecl->uses().size(), 1);
  QCOMPARE(forwardDecl->uses().begin()->size(), 6);
  QCOMPARE(forwardDecl->uses().begin()->at(0), SimpleRange(3, 19, 3, 26));
  QCOMPARE(forwardDecl->uses().begin()->at(1), SimpleRange(4, 12, 4, 19));
  QCOMPARE(forwardDecl->uses().begin()->at(2), SimpleRange(4, 22, 4, 29));
  QCOMPARE(forwardDecl->uses().begin()->at(3), SimpleRange(5, 12, 5, 19));
  QCOMPARE(forwardDecl->uses().begin()->at(4), SimpleRange(5, 22, 5, 29));
  QCOMPARE(forwardDecl->uses().begin()->at(5), SimpleRange(6, 19, 6, 26));

  release(top);
}

void TestDUChain::testTemplateForwardDeclaration()
{
  QByteArray method("class B{}; template<class T = B>class Test; Test<B> t; template<class T = B>class Test {}; ");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 4);

  QVERIFY(top->localDeclarations()[1]->internalContext());

  QVERIFY(dynamic_cast<ForwardDeclaration*>(top->localDeclarations()[1]));

  CppClassType::Ptr type1 = top->localDeclarations()[1]->type<CppClassType>();
  CppClassType::Ptr type2 = top->localDeclarations()[2]->type<CppClassType>();
  CppClassType::Ptr type3 = top->localDeclarations()[3]->type<CppClassType>();

  QVERIFY(type1);
  QVERIFY(type2);
  QVERIFY(type3);

  TemplateDeclaration* temp1Decl = dynamic_cast<TemplateDeclaration*>(type1->declaration(top));
  QVERIFY(temp1Decl);
  TemplateDeclaration* temp2Decl = dynamic_cast<TemplateDeclaration*>(type2->declaration(top));
  QVERIFY(temp2Decl);
  TemplateDeclaration* temp3Decl = dynamic_cast<TemplateDeclaration*>(type3->declaration(top));
  QVERIFY(temp3Decl);

  TemplateDeclaration* temp1DeclResolved = dynamic_cast<TemplateDeclaration*>(type1->declaration(top));
  QVERIFY(temp1DeclResolved);
  TemplateDeclaration* temp2DeclResolved = dynamic_cast<TemplateDeclaration*>(type2->declaration(top));
  QVERIFY(temp2DeclResolved);

  QVERIFY(dynamic_cast<Declaration*>(temp1DeclResolved)->type<CppClassType>());
  QVERIFY(dynamic_cast<Declaration*>(temp2DeclResolved)->type<CppClassType>());

  QCOMPARE(temp2Decl->instantiatedFrom(), temp1Decl);
  QCOMPARE(temp2DeclResolved->instantiatedFrom(), temp1DeclResolved);

  QVERIFY(type2->declaration(0)->abstractType().unsafeData());
  QVERIFY(type2->declaration(0)->type<CppClassType>().unsafeData());
  QCOMPARE(temp2DeclResolved->instantiatedFrom(), temp3Decl);

  QualifiedIdentifier t("Test<>");
  kDebug() << "searching" << t;

  Declaration* decl = findDeclaration(top, QualifiedIdentifier("Test<>"));
  QVERIFY(decl);
  QCOMPARE(decl->abstractType()->toString(), QString("Test< B >"));

  release(top);
}

void TestDUChain::testTemplateForwardDeclaration2()
{
  QByteArray method("class B{}; template<class T = B, class Q = B, class R = B>class Test; Test<B> t; template<class T, class Q, class R>class Test { T a; Q b; R c;}; ");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 4);

  QVERIFY(top->localDeclarations()[1]->internalContext());

  QVERIFY(dynamic_cast<ForwardDeclaration*>(top->localDeclarations()[1]));

  CppClassType::Ptr type1 = top->localDeclarations()[1]->type<CppClassType>();
  CppClassType::Ptr type2 = top->localDeclarations()[2]->type<CppClassType>();
  CppClassType::Ptr type3 = top->localDeclarations()[3]->type<CppClassType>();

  QVERIFY(type1);
  QVERIFY(type2);
  QVERIFY(type3);

  QCOMPARE(type1->declaration(0)->abstractType()->indexed(), type3->indexed());

  TemplateDeclaration* temp1Decl = dynamic_cast<TemplateDeclaration*>(type1->declaration(top));
  QVERIFY(temp1Decl);
  TemplateDeclaration* temp2Decl = dynamic_cast<TemplateDeclaration*>(type2->declaration(top));
  QVERIFY(temp2Decl);
  Declaration* temp2DeclForward = dynamic_cast<Declaration*>(type2->declaration(top));
  QVERIFY(temp2DeclForward);
  TemplateDeclaration* temp3Decl = dynamic_cast<TemplateDeclaration*>(type3->declaration(top));
  QVERIFY(temp3Decl);
  QCOMPARE(temp2Decl->instantiatedFrom(), temp1Decl);
  QCOMPARE(dynamic_cast<TemplateDeclaration*>(temp2DeclForward)->instantiatedFrom(), temp3Decl);

  Declaration* decl = findDeclaration(top, QualifiedIdentifier("Test<>"));
  QVERIFY(decl);
  QVERIFY(decl->internalContext());
  Declaration* aDecl = findDeclaration(decl->logicalInternalContext(0), QualifiedIdentifier("a"));
  QVERIFY(aDecl);
  Declaration* bDecl = findDeclaration(decl->logicalInternalContext(0), QualifiedIdentifier("b"));
  QVERIFY(bDecl);
  Declaration* cDecl = findDeclaration(decl->logicalInternalContext(0), QualifiedIdentifier("c"));
  QVERIFY(cDecl);

  release(top);
}

void TestDUChain::testUses()
{
  QByteArray method(
    "class GenericArgument\n"
    "{\n"
    "  public:\n"
    "    GenericArgument() {}\n"
    "    GenericArgument( const char*, const void* ) {}\n"
    "};\n"
    "\n"
    "template <class T>\n"
    "class Argument : public GenericArgument\n"
    "{\n"
    "  public:\n"
    "    Argument( const char* name, const T &value )\n"
    "      : GenericArgument( name, static_cast<const void*>( &value ) )\n"
    "    {}\n"
    "};\n"
    "\n"
    "class A\n"
    "{\n"
    "  public:\n"
    "    A()\n"
    "    {\n"
    "      A::invokeMethod( this, \"hello\", Argument<bool>( \"test\", true ) );\n"
    "    }\n"
    "\n"
    "    void invokeMethod( A*, const char*, int foo = 0, const GenericArgument &arg = GenericArgument() ) {}\n"
    "    void invokeMethod( A*, const char*, const GenericArgument& ) {}\n"
    "};\n"
  );

  TopDUContext* top = parse(method, DumpAll);

  DUChainWriteLocker lock;

  /**
   * FIXME: somehow this test fails sometimes when
   *        run together with the rest of the tests in this suite.
   *        running it alone works though...
   *        Apparently the AST is completely different and I have no clue why...
   */
  if (top->childContexts().size() == 2) {
    // expect that error...
    QEXPECT_FAIL("", "the AST gets completely FUBAR when this test is run together with the others, running it alone works like a charm though...", Abort);
  }
  QCOMPARE(top->childContexts().size(), 4);
  QCOMPARE(top->childContexts().at(0)->type(), DUContext::Class);
  QCOMPARE(top->childContexts().at(1)->type(), DUContext::Template);
  QCOMPARE(top->childContexts().at(2)->type(), DUContext::Class);
  QCOMPARE(top->childContexts().at(3)->type(), DUContext::Class);

  QList<Declaration*> decls = top->findDeclarations(QualifiedIdentifier("A::invokeMethod"));
  QCOMPARE(decls.count(), 2);

  // only declaration 2 is used, check this now
  QCOMPARE(decls.at( 0 )->uses().count(), 0);
  QCOMPARE(decls.at( 1 )->uses().count(), 1);

  release(top);
}

void TestDUChain::testConst()
{
  {
    QByteArray method("class A{}; const char* a; const char& b; char* const & c; char* const d; const A e; const A* f; A* const g;");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().size(), 8);
    PointerType::Ptr a = top->localDeclarations()[1]->type<PointerType>();
    QVERIFY(a);
    QVERIFY(!a->modifiers() & AbstractType::ConstModifier);
    IntegralType::Ptr a2 = a->baseType().cast<IntegralType>();
    QVERIFY(a2);
    QVERIFY(a2->modifiers() & AbstractType::ConstModifier);

    ReferenceType::Ptr b = top->localDeclarations()[2]->type<ReferenceType>();
    QVERIFY(b);
    QVERIFY(!b->modifiers() & AbstractType::ConstModifier);
    IntegralType::Ptr b2 = b->baseType().cast<IntegralType>();
    QVERIFY(b2);
    QVERIFY(b2->modifiers() & AbstractType::ConstModifier);

    ReferenceType::Ptr c = top->localDeclarations()[3]->type<ReferenceType>();
    QVERIFY(c);
    QVERIFY(!c->modifiers() & AbstractType::ConstModifier);
    PointerType::Ptr c2 = c->baseType().cast<PointerType>();
    QVERIFY(c2);
    QVERIFY(c2->modifiers() & AbstractType::ConstModifier);
    IntegralType::Ptr c3 = c2->baseType().cast<IntegralType>();
    QVERIFY(c3);
    QVERIFY(!c3->modifiers() & AbstractType::ConstModifier);

    PointerType::Ptr d = top->localDeclarations()[4]->type<PointerType>();
    QVERIFY(d);
    QVERIFY(d->modifiers() & AbstractType::ConstModifier);
    IntegralType::Ptr d2 = d->baseType().cast<IntegralType>();
    QVERIFY(d2);
    QVERIFY(!d2->modifiers() & AbstractType::ConstModifier);

    CppClassType::Ptr e = top->localDeclarations()[5]->type<CppClassType>();
    QVERIFY(e);
    QVERIFY(e->modifiers() & AbstractType::ConstModifier);

    PointerType::Ptr f = top->localDeclarations()[6]->type<PointerType>();
    QVERIFY(f);
    QVERIFY(!f->modifiers() & AbstractType::ConstModifier);
    CppClassType::Ptr f2 = f->baseType().cast<CppClassType>();
    QVERIFY(f2);
    QVERIFY(f2->modifiers() & AbstractType::ConstModifier);

    PointerType::Ptr g = top->localDeclarations()[7]->type<PointerType>();
    QVERIFY(g);
    QVERIFY(g->modifiers() & AbstractType::ConstModifier);
    CppClassType::Ptr g2 = g->baseType().cast<CppClassType>();
    QVERIFY(g2);
    QVERIFY(!g2->modifiers() & AbstractType::ConstModifier);
    release(top);
  }
  {
    QByteArray method("class A; template<class T> class B; B<const A*> ca;B<A* const> cb;");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().size(), 4);
    QCOMPARE(top->localDeclarations()[2]->abstractType()->toString().trimmed(), QString("B< const A* >"));
    QCOMPARE(top->localDeclarations()[3]->abstractType()->toString().trimmed(), QString("B< A* const >"));
    release(top);
  }
  {
    QByteArray method("class A; A* a = const_cast<A*>(bla);");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().size(), 2);
    release(top);
  }
  {
    QByteArray method("class C;const C& c;");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().size(), 2);
    QVERIFY(top->localDeclarations()[1]->type<ReferenceType>());
    QVERIFY(top->localDeclarations()[1]->type<ReferenceType>()->baseType());
    QVERIFY(top->localDeclarations()[1]->type<ReferenceType>()->baseType()->modifiers() & AbstractType::ConstModifier);
    kDebug() << top->localDeclarations()[1]->type<ReferenceType>()->toString();
    release(top);
  }}

void TestDUChain::testDeclarationId()
{
  QByteArray method("template<class T> class C { template<class T2> class C2{}; }; ");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(top->inSymbolTable());
  QCOMPARE(top->localDeclarations().count(), 1);
  QVERIFY(top->localDeclarations()[0]->internalContext());
  QVERIFY(top->localDeclarations()[0]->inSymbolTable());
  QCOMPARE(top->localDeclarations()[0]->internalContext()->localDeclarations().count(), 1);

  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  QVERIFY(top->childContexts()[1]->localDeclarations()[0]->inSymbolTable());
  //kDebug() << "pointer of C2:" << top->childContexts()[1]->localDeclarations()[0];
  Declaration* decl = findDeclaration(top, QualifiedIdentifier("C<int>::C2<float>"));
  QVERIFY(decl);
  kDebug() << decl->toString();
  kDebug() << decl->qualifiedIdentifier().toString();
  DeclarationId id = decl->id();
  QVERIFY(top->localDeclarations()[0]->internalContext());
//   kDebug() << "id:" << id.m_direct << id.m_specialization << "indirect:" << id.indirect.m_identifier.index << id.indirect.m_additionalIdentity << "direct:" << *((uint*)(&id.direct)) << *(((uint*)(&id.direct))+1);
  Declaration* declAgain = id.getDeclaration(top);
  QVERIFY(!id.isDirect());
  QVERIFY(declAgain);
  QCOMPARE(declAgain->qualifiedIdentifier().toString(), decl->qualifiedIdentifier().toString());
  QCOMPARE(declAgain, decl);
  //kDebug() << declAgain->qualifiedIdentifier().toString();
  //kDebug() << declAgain->toString();
  QCOMPARE(declAgain, decl);

  release(top);
}

void TestDUChain::testFileParse()
{
  //QSKIP("Unwanted", SkipSingle);

  //QFile file("/opt/kde4/src/kdevelop/languages/cpp/duchain/tests/files/membervariable.cpp");
  QFile file("/opt/kde4/src/kdevelop/languages/csharp/parser/csharp_parser.cpp");
  //QFile file("/opt/kde4/src/kdevelop/plugins/outputviews/makewidget.cpp");
  //QFile file("/opt/kde4/src/kdelibs/kate/part/katecompletionmodel.h");

  //QFile file("/opt/kde4/src/kdevelop/lib/kdevbackgroundparser.cpp");
  //QFile file("/opt/qt-copy/src/gui/kernel/qwidget.cpp");
  QVERIFY( file.open( QIODevice::ReadOnly ) );

  QByteArray fileData = file.readAll();
  file.close();
  QString contents = QString::fromUtf8( fileData.constData() );
  rpp::Preprocessor preprocessor;
  rpp::pp pp(&preprocessor);

  QByteArray preprocessed = stringFromContents(pp.processFile("anonymous", fileData));

  TopDUContext* top = parse(preprocessed, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  release(top);
}

typedef BasicSetRepository::Index Index;

void TestDUChain::testStringSets() {

  const unsigned int setCount = 11;
  const unsigned int choiceCount = 40;
  const unsigned int itemCount = 120;

  BasicSetRepository rep("test repository");

//  kDebug() << "Start repository-layout: \n" << rep.dumpDotGraph();

  clock_t repositoryClockTime = 0; //Time spent on repository-operations
  clock_t genericClockTime = 0; //Time spend on equivalent operations with generic sets

  clock_t repositoryIntersectionClockTime = 0; //Time spent on repository-operations
  clock_t genericIntersectionClockTime = 0; //Time spend on equivalent operations with generic sets
  clock_t qsetIntersectionClockTime = 0; //Time spend on equivalent operations with generic sets

  clock_t repositoryUnionClockTime = 0; //Time spent on repository-operations
  clock_t genericUnionClockTime = 0; //Time spend on equivalent operations with generic sets

  clock_t repositoryDifferenceClockTime = 0; //Time spent on repository-operations
  clock_t genericDifferenceClockTime = 0; //Time spend on equivalent operations with generic sets

  Set sets[setCount];
  std::set<Index> realSets[setCount];
  for(unsigned int a = 0; a < setCount; a++)
  {
    std::set<Index> chosenIndices;
    unsigned int thisCount = rand() % choiceCount;
    if(thisCount == 0)
      thisCount = 1;

    for(unsigned int b = 0; b < thisCount; b++)
    {
      Index choose = (rand() % itemCount) + 1;
      while(chosenIndices.find(choose) != chosenIndices.end()) {
        choose = (rand() % itemCount) + 1;
      }

      clock_t c = clock();
      chosenIndices.insert(chosenIndices.end(), choose);
      genericClockTime += clock() - c;
    }

    clock_t c = clock();
    sets[a] = rep.createSet(chosenIndices);
    repositoryClockTime += clock() - c;

    realSets[a] = chosenIndices;

    std::set<Index> tempSet = sets[a].stdSet();

    if(tempSet != realSets[a]) {
      QString dbg = "created set: ";
      for(std::set<Index>::const_iterator it = realSets[a].begin(); it != realSets[a].end(); ++it)
        dbg += QString("%1 ").arg(*it);
      kDebug() << dbg;

      dbg = "repo.   set: ";
      for(std::set<Index>::const_iterator it = tempSet.begin(); it != tempSet.end(); ++it)
        dbg += QString("%1 ").arg(*it);
      kDebug() << dbg;

      kDebug() << "DOT-Graph:\n\n" << sets[a].dumpDotGraph() << "\n\n";
    }
    QVERIFY(tempSet == realSets[a]);
  }

  for(int cycle = 0; cycle < 100; ++cycle) {
      if(cycle % 10 == 0)
         kDebug() << "cycle" << cycle;

    for(unsigned int a = 0; a < setCount; a++) {
      for(unsigned int b = 0; b < setCount; b++) {
        /// ----- SUBTRACTION/DIFFERENCE
        std::set<Index> _realDifference;
        clock_t c = clock();
        std::set_difference(realSets[a].begin(), realSets[a].end(), realSets[b].begin(), realSets[b].end(), std::insert_iterator<std::set<Index> >(_realDifference, _realDifference.begin()));
        genericDifferenceClockTime += clock() - c;

        c = clock();
        Set _difference = sets[a] - sets[b];
        repositoryDifferenceClockTime += clock() - c;

        if(_difference.stdSet() != _realDifference)
        {
          {
            kDebug() << "SET a:";
            QString dbg = "";
            for(std::set<Index>::const_iterator it = realSets[a].begin(); it != realSets[a].end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            kDebug() << "DOT-Graph:\n\n" << sets[a].dumpDotGraph() << "\n\n";
          }
          {
            kDebug() << "SET b:";
            QString dbg = "";
            for(std::set<Index>::const_iterator it = realSets[b].begin(); it != realSets[b].end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            kDebug() << "DOT-Graph:\n\n" << sets[b].dumpDotGraph() << "\n\n";
          }

          {
            std::set<Index> tempSet = _difference.stdSet();

            kDebug() << "SET difference:";
            QString dbg = "real    set: ";
            for(std::set<Index>::const_iterator it = _realDifference.begin(); it != _realDifference.end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            dbg = "repo.   set: ";
            for(std::set<Index>::const_iterator it = tempSet.begin(); it != tempSet.end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            kDebug() << "DOT-Graph:\n\n" << _difference.dumpDotGraph() << "\n\n";
          }
        }
        QVERIFY(_difference.stdSet() == _realDifference);


        /// ------ UNION

        std::set<Index> _realUnion;
        c = clock();
        std::set_union(realSets[a].begin(), realSets[a].end(), realSets[b].begin(), realSets[b].end(), std::insert_iterator<std::set<Index> >(_realUnion, _realUnion.begin()));
        genericUnionClockTime += clock() - c;

        c = clock();
        Set _union = sets[a] + sets[b];
        repositoryUnionClockTime += clock() - c;

        if(_union.stdSet() != _realUnion)
        {
          {
            kDebug() << "SET a:";
            QString dbg = "";
            for(std::set<Index>::const_iterator it = realSets[a].begin(); it != realSets[a].end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            kDebug() << "DOT-Graph:\n\n" << sets[a].dumpDotGraph() << "\n\n";
          }
          {
            kDebug() << "SET b:";
            QString dbg = "";
            for(std::set<Index>::const_iterator it = realSets[b].begin(); it != realSets[b].end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            kDebug() << "DOT-Graph:\n\n" << sets[b].dumpDotGraph() << "\n\n";
          }

          {
            std::set<Index> tempSet = _union.stdSet();

            kDebug() << "SET union:";
            QString dbg = "real    set: ";
            for(std::set<Index>::const_iterator it = _realUnion.begin(); it != _realUnion.end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            dbg = "repo.   set: ";
            for(std::set<Index>::const_iterator it = tempSet.begin(); it != tempSet.end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            kDebug() << "DOT-Graph:\n\n" << _union.dumpDotGraph() << "\n\n";
          }
        }
        QVERIFY(_union.stdSet() == _realUnion);

        std::set<Index> _realIntersection;

        /// -------- INTERSECTION
        c = clock();
        std::set_intersection(realSets[a].begin(), realSets[a].end(), realSets[b].begin(), realSets[b].end(), std::insert_iterator<std::set<Index> >(_realIntersection, _realIntersection.begin()));
        genericIntersectionClockTime += clock() - c;

        //Just for fun: Test how fast QSet intersections are
        QSet<Index> first, second;
        for(std::set<Index>::const_iterator it = realSets[a].begin(); it != realSets[a].end(); ++it)
          first.insert(*it);
        for(std::set<Index>::const_iterator it = realSets[b].begin(); it != realSets[b].end(); ++it)
          second.insert(*it);
        c = clock();
        QSet<Index> i = first.intersect(second);
        qsetIntersectionClockTime += clock() - c;

        c = clock();
        Set _intersection = sets[a] & sets[b];
        repositoryIntersectionClockTime += clock() - c;


        if(_intersection.stdSet() != _realIntersection)
        {
          {
            kDebug() << "SET a:";
            QString dbg = "";
            for(std::set<Index>::const_iterator it = realSets[a].begin(); it != realSets[a].end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            kDebug() << "DOT-Graph:\n\n" << sets[a].dumpDotGraph() << "\n\n";
          }
          {
            kDebug() << "SET b:";
            QString dbg = "";
            for(std::set<Index>::const_iterator it = realSets[b].begin(); it != realSets[b].end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            kDebug() << "DOT-Graph:\n\n" << sets[b].dumpDotGraph() << "\n\n";
          }

          {
            std::set<Index> tempSet = _intersection.stdSet();

            kDebug() << "SET intersection:";
            QString dbg = "real    set: ";
            for(std::set<Index>::const_iterator it = _realIntersection.begin(); it != _realIntersection.end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            dbg = "repo.   set: ";
            for(std::set<Index>::const_iterator it = tempSet.begin(); it != tempSet.end(); ++it)
              dbg += QString("%1 ").arg(*it);
            kDebug() << dbg;

            kDebug() << "DOT-Graph:\n\n" << _intersection.dumpDotGraph() << "\n\n";
          }
        }
        QVERIFY(_intersection.stdSet() == _realIntersection);
      }
    }
#ifdef DEBUG_STRINGREPOSITORY
    kDebug() << "cycle " << cycle;
  kDebug() << "Clock-cycles needed for set-building: repository-set: " << repositoryClockTime << " generic-set: " << genericClockTime;
  kDebug() << "Clock-cycles needed for intersection: repository-sets: " << repositoryIntersectionClockTime << " generic-set: " << genericIntersectionClockTime << " QSet: " << qsetIntersectionClockTime;
  kDebug() << "Clock-cycles needed for union: repository-sets: " << repositoryUnionClockTime << " generic-set: " << genericUnionClockTime;
  kDebug() << "Clock-cycles needed for difference: repository-sets: " << repositoryDifferenceClockTime << " generic-set: " << genericDifferenceClockTime;
#endif
  }
}

void TestDUChain::testSymbolTableValid() {
  DUChainReadLocker lock(DUChain::lock());
  PersistentSymbolTable::self().selfAnalysis();
}

void TestDUChain::testIndexedStrings() {

  int testCount  = 600000;

  QHash<QString, IndexedString> knownIndices;
  int a = 0;
  for(a = 0; a < testCount; ++a) {
    QString testString;
    int length = rand() % 10;
    for(int b = 0; b < length; ++b)
      testString.append((char)(rand() % 6) + 'a');
    QByteArray array = testString.toUtf8();
    //kDebug() << "checking with" << testString;
    //kDebug() << "checking" << a;
    IndexedString indexed(array.constData(), array.size(), IndexedString::hashString(array.constData(), array.size()));

    QCOMPARE(indexed.str(), testString);
    if(knownIndices.contains(testString)) {
      QCOMPARE(indexed.index(), knownIndices[testString].index());
    } else {
      knownIndices[testString] = indexed;
    }

    if(a % (testCount/10) == 0)
      kDebug() << a << "of" << testCount;
  }
  kDebug() << a << "successful tests";
}

TopDUContext* TestDUChain::parse(const QByteArray& unit, DumpAreas dump, TopDUContext* update, bool keepAst)
{
  if (dump)
    kDebug(9007) << "==== Beginning new test case...:" << endl << unit;

  //If the AST flag is set, then the parse session needs to be owned by a shared pointer
  ParseSession::Ptr session(new ParseSession());

  rpp::Preprocessor preprocessor;
  rpp::pp pp(&preprocessor);

  session->setContentsAndGenerateLocationTable(pp.processFile("anonymous", unit));

  Parser parser(&control);
  TranslationUnitAST* ast = parser.parse(session.data());
  ast->session = session.data();

  if (dump & DumpAST) {
    kDebug(9007) << "===== AST:";
    cppDumper.dump(ast, session.data());
  }

  static int testNumber = 0;
  IndexedString url(QString("/internal/%1").arg(testNumber++));

  DeclarationBuilder definitionBuilder(session.data());
  Cpp::EnvironmentFilePointer file( new Cpp::EnvironmentFile( url, 0 ) );
  
  //HACK Should actually use DUChain::updateContextForUrl
  kDebug() << "update->features & TopDUContext::AST: " << (update ? update->features() & TopDUContext::AST : 0);
  if(keepAst)
  {
    definitionBuilder.setMapAst(true);
    update->setAst( IAstContainer::Ptr( session.data() ) );
  }
  TopDUContext* top = definitionBuilder.buildDeclarations(file, ast, 0, ReferencedTopDUContext(update));
  if(update) {
    Q_ASSERT(top == update);
  }

  UseBuilder useBuilder(session.data());
  useBuilder.buildUses(ast);

  if (dump & DumpDUChain) {
    kDebug(9007) << "===== DUChain:";

    DUChainWriteLocker lock(DUChain::lock());
    dumper.dump(top);
  }

  if (dump & DumpType) {
    kDebug(9007) << "===== Types:";
    DUChainWriteLocker lock(DUChain::lock());
    DumpTypes dt;
    foreach (const AbstractType::Ptr& type, definitionBuilder.topTypes())
      dt.dump(type.unsafeData());
  }

  if (dump)
    kDebug(9007) << "===== Finished test case.";

  return top;
}

#include "test_duchain.moc"
