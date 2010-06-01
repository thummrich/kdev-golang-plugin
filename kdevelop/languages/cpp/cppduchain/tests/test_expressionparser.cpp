/* This file is part of KDevelop
    Copyright 2007 David Nolden <david.nolden.kdevelop@art-master.de>

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

#include "test_expressionparser.h"

#include <QtTest/QtTest>

#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/topducontext.h>
#include "typeutils.h"
#include "declarationbuilder.h"
#include "usebuilder.h"
#include <language/duchain/declaration.h>
#include <language/editor/documentrange.h>
#include "cppeditorintegrator.h"
#include "dumptypes.h"
#include "environmentmanager.h"
#include "templatedeclaration.h"
#include "cppducontext.h"
#include <rpp/chartools.h>


#include "tokens.h"
#include "parsesession.h"

#include "rpp/preprocessor.h"
#include "expressionvisitor.h"
#include "expressionparser.h"
#include <language/duchain/classfunctiondeclaration.h>

#include <tests/autotestshell.h>
#include <tests/testcore.h>

#include <typeinfo>

using namespace KTextEditor;
using namespace KDevelop;
using namespace TypeUtils;
using namespace Cpp;

class MyExpressionVisitor : public Cpp::ExpressionVisitor {
  public:
  MyExpressionVisitor( ParseSession* session ) : ExpressionVisitor(session)  {
  }
  protected:
  virtual void expressionType( AST* node, const AbstractType::Ptr& type, Instance /*instance */) {
    Cpp::DumpChain d;
    kDebug(9007) << "expression-result for";
    DUChainReadLocker lock( DUChain::lock() );
    d.dump( node, session() );
    kDebug(9007) << "is:" << type->toString();
  }
};

QTEST_MAIN(TestExpressionParser)

char* debugString( const QString& str ) {
  char* ret = new char[str.length()+1];
  QByteArray b = str.toAscii();
  strcpy( ret, b.data() );
  return ret;
}

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
    QString s = QString("Type: %1").arg(type ? type->toString() : QString("<null>"));
    return qstrdup(s.toLatin1().constData());
  }
}

#define TEST_FILE_PARSE_ONLY if (testFileParseOnly) QSKIP("Skip", SkipSingle);
TestExpressionParser::TestExpressionParser()
{
  testFileParseOnly = false;
}

void TestExpressionParser::initTestCase()
{
  AutoTestShell::init();
  TestCore* core = new TestCore();
  core->initialize(KDevelop::Core::NoUi);
  EnvironmentManager::init();

  DUChain::self()->disablePersistentStorage();
}

void TestExpressionParser::cleanupTestCase()
{
}

Declaration* TestExpressionParser::findDeclaration(DUContext* context, const Identifier& id, const SimpleCursor& position)
{
  QList<Declaration*> ret = context->findDeclarations(id, position);
  if (ret.count())
    return ret.first();
  return 0;
}

Declaration* TestExpressionParser::findDeclaration(DUContext* context, const QualifiedIdentifier& id, const SimpleCursor& position)
{
  QList<Declaration*> ret = context->findDeclarations(id, position);
  if (ret.count())
    return ret.first();
  return 0;
}

void TestExpressionParser::testIntegralType() {
  DUContext* top = parse(" ", DumpNone);
  Cpp::ExpressionParser parser(true,true);

  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateType("const char*", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    
    AbstractType::Ptr aType(result.type.abstractType());
    PointerType::Ptr ptrType = aType.cast<PointerType>();
    QVERIFY(ptrType);
    QVERIFY(!(ptrType->modifiers() & PointerType::ConstModifier));
    
    AbstractType::Ptr aBaseType = ptrType->baseType();
    QVERIFY(aBaseType);
    IntegralType::Ptr iBaseType = aBaseType.cast<IntegralType>();
    QVERIFY(iBaseType);
    QCOMPARE(iBaseType->dataType(), (uint)IntegralType::TypeChar);

    QVERIFY(iBaseType->modifiers() & IntegralType::ConstModifier);
  }
  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateType("'x'", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    
    AbstractType::Ptr aType(result.type.abstractType());
    ConstantIntegralType::Ptr ciType = aType.cast<ConstantIntegralType>();
    QVERIFY(ciType);
    QCOMPARE(ciType->dataType(), (uint)ConstantIntegralType::TypeChar);
    QCOMPARE(ciType->value<char>(), 'x');
  }
  {
      Cpp::ExpressionEvaluationResult result = parser.evaluateType("5", KDevelop::DUContextPointer(top));
      QVERIFY(result.isValid());
      
      AbstractType::Ptr aType(result.type.abstractType());
      ConstantIntegralType::Ptr ciType = aType.cast<ConstantIntegralType>();
      QVERIFY(ciType);
      QCOMPARE(ciType->dataType(), (uint)ConstantIntegralType::TypeInt);
      QCOMPARE(ciType->value<int>(), 5);
  }
  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateType("true", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    
    AbstractType::Ptr aType(result.type.abstractType());
    ConstantIntegralType::Ptr ciType = aType.cast<ConstantIntegralType>();
    QVERIFY(ciType);
    QCOMPARE(ciType->dataType(), (uint)ConstantIntegralType::TypeBoolean);
    QCOMPARE(ciType->value<bool>(), true);
  }
  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateType("'\\n'", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    
    AbstractType::Ptr aType(result.type.abstractType());
    ConstantIntegralType::Ptr ciType = aType.cast<ConstantIntegralType>();
    QVERIFY(ciType);
    QCOMPARE(ciType->dataType(), (uint)ConstantIntegralType::TypeChar);
    QCOMPARE(ciType->value<char>(), '\n');
  }
}

void TestExpressionParser::testTemplatesSimple() {
  QByteArray method("template<class T> T test(const T& t) {}; template<class T, class T2> class A { }; class B{}; class C{}; typedef A<B,C> B;");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  Declaration* defClassA = top->localDeclarations()[1];
  QCOMPARE(defClassA->identifier(), Identifier("A"));
  QVERIFY(defClassA->type<CppClassType>());

  Declaration* defClassB = top->localDeclarations()[2];
  QCOMPARE(defClassB->identifier(), Identifier("B"));
  QVERIFY(defClassB->type<CppClassType>());

  Declaration* defClassC = top->localDeclarations()[3];
  QCOMPARE(defClassC->identifier(), Identifier("C"));
  QVERIFY(defClassC->type<CppClassType>());

  DUContext* classA = defClassA->logicalInternalContext(0);
  QVERIFY(classA->parentContext());
  QCOMPARE(classA->importedParentContexts().count(), 1); //The template-parameter context is imported
  QCOMPARE(classA->localScopeIdentifier(), QualifiedIdentifier("A"));

  DUContext* classB = defClassB->logicalInternalContext(0);
  QVERIFY(classB->parentContext());
  QCOMPARE(classB->importedParentContexts().count(), 0);
  QCOMPARE(classB->localScopeIdentifier(), QualifiedIdentifier("B"));

  DUContext* classC = defClassC->logicalInternalContext(0);
  QVERIFY(classC->parentContext());
  QCOMPARE(classC->importedParentContexts().count(), 0);
  QCOMPARE(classC->localScopeIdentifier(), QualifiedIdentifier("C"));

  release(top);
}

void TestExpressionParser::testTemplates() {
  QByteArray method("class A{}; template<class T, int val> struct Container {typedef Container<T> SelfType; enum{ Value = val }; T member; T operator*() const {}; }; Container<A> c;");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  Declaration* defClassA = top->localDeclarations()[0];
  QCOMPARE(defClassA->identifier(), Identifier("A"));
  QVERIFY(defClassA->type<CppClassType>());

  Declaration* container = top->localDeclarations()[1];
  QCOMPARE(container->identifier(), Identifier("Container"));
  QVERIFY(container->type<CppClassType>());

  Cpp::TemplateDeclaration* templateDecl = dynamic_cast<Cpp::TemplateDeclaration*>(container);
  QVERIFY(templateDecl);


  Cpp::ExpressionParser parser;

  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateExpression("c.member", KDevelop::DUContextPointer(top));

    QVERIFY(result.isValid());
    QVERIFY(result.type.type<CppClassType>());

    QVERIFY(result.allDeclarationsSize());
    AbstractType::Ptr t(result.type.abstractType());
    QVERIFY(dynamic_cast<IdentifiedType*>(t.unsafeData()));
    QCOMPARE(dynamic_cast<IdentifiedType*>(t.unsafeData())->declaration(0), defClassA);
  }

  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateExpression("*c", KDevelop::DUContextPointer(top));

    QVERIFY(result.isValid());
    QVERIFY(result.type.type<CppClassType>());

    QVERIFY(result.allDeclarationsSize());
    AbstractType::Ptr t(result.type.abstractType());
    QVERIFY(dynamic_cast<IdentifiedType*>(t.unsafeData()));
    QCOMPARE(dynamic_cast<IdentifiedType*>(t.unsafeData())->declaration(0), defClassA);
  }

  release(top);
}

void TestExpressionParser::testArray() {
  QByteArray method("struct Bla { int val; } blaArray[5+3];");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  Cpp::ExpressionParser parser;

  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateExpression("Bla", KDevelop::DUContextPointer(top));

    QVERIFY(result.isValid());
    QVERIFY(result.type.type<CppClassType>());

    QCOMPARE(result.toString(), QString("Bla"));
  }

  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateExpression("blaArray", KDevelop::DUContextPointer(top));

    QVERIFY(result.isValid());
    QVERIFY(result.isInstance);
    kDebug() << result.toString();
    QVERIFY(result.type.type<ArrayType>());
    QCOMPARE(result.type.type<ArrayType>()->dimension(), 8);
  }

  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateExpression("blaArray[5].val", KDevelop::DUContextPointer(top));

    QVERIFY(result.isValid());
    AbstractType::Ptr t(result.type.abstractType());
    QVERIFY(dynamic_cast<KDevelop::IntegralType*>(t.unsafeData()));
  }

  release(top);
}

void TestExpressionParser::testDynamicArray() {
  QByteArray method("struct Bla { int val; } blaArray[] = { {5} };");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  Cpp::ExpressionParser parser;

  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateExpression("Bla", KDevelop::DUContextPointer(top));

    QVERIFY(result.isValid());
    QVERIFY(result.type.type<CppClassType>());

    QCOMPARE(result.toString(), QString("Bla"));
  }

  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateExpression("blaArray", KDevelop::DUContextPointer(top));

    QVERIFY(result.isValid());
    QVERIFY(result.isInstance);
    kDebug() << result.toString();
    QVERIFY(result.type.type<ArrayType>());
  }

  {
    Cpp::ExpressionEvaluationResult result = parser.evaluateExpression("blaArray[5].val", KDevelop::DUContextPointer(top));

    QVERIFY(result.isValid());
    QVERIFY(result.type.type<KDevelop::IntegralType>());
  }

  release(top);
}

void TestExpressionParser::testTemplates2()
{
  QByteArray method("class C { class A; template<class T> T test() {} }; class B { class A; void test() {C c; } }; ");

  DUContext* top = parse(method, DumpAll);

  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->childContexts()[1]->childContexts().size(), 2);
  
  {
    Cpp::ExpressionParser parser(false, true);;
    Cpp::ExpressionEvaluationResult result = parser.evaluateExpression("c.test<A>()", KDevelop::DUContextPointer(top->childContexts()[1]->childContexts()[1]));

    QVERIFY(result.isValid());
    QVERIFY(result.isInstance);
    kDebug() << result.toString();
    QCOMPARE(result.type.abstractType()->toString(), QString("B::A"));
  }
  release(top);
}

void TestExpressionParser::testSmartPointer() {
  QByteArray method("template<class T> struct SmartPointer { T* operator ->() const {}; template<class Target> SmartPointer<Target> cast() {}; T& operator*() {};  } ; class B{int i;}; class C{}; SmartPointer<B> bPointer;");
  //QByteArray method("template<class T> struct SmartPointer { template<class Target> void cast() {}; } ; ");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  Cpp::ExpressionParser parser;

  QVERIFY(top->localDeclarations()[0]->internalContext());

  AbstractType::Ptr t(top->localDeclarations()[3]->abstractType());
  IdentifiedType* idType = dynamic_cast<IdentifiedType*>(t.unsafeData());
  QVERIFY(idType);
  QCOMPARE(idType->declaration(0)->context(), KDevelop::DUContextPointer(top).data());
  QVERIFY(idType->declaration(0)->internalContext() != top->localDeclarations()[0]->internalContext());

  Declaration* baseDecl = top->localDeclarations()[0];
  Declaration* specialDecl = idType->declaration(0);
  TemplateDeclaration* baseTemplateDecl = dynamic_cast<TemplateDeclaration*>(baseDecl);
  TemplateDeclaration* specialTemplateDecl = dynamic_cast<TemplateDeclaration*>(specialDecl);

  QVERIFY(baseTemplateDecl);
  QVERIFY(specialTemplateDecl);
  QVERIFY(specialTemplateDecl->isInstantiatedFrom(baseTemplateDecl));

  QVERIFY(specialDecl->internalContext() != baseDecl->internalContext());
  QCOMPARE(specialDecl->internalContext()->importedParentContexts().count(), 1); //Only the template-contexts are imported
  QCOMPARE(baseDecl->internalContext()->importedParentContexts().count(), 1);

  DUContext* specialTemplateContext = specialDecl->internalContext()->importedParentContexts().first().context(0);
  DUContext* baseTemplateContext = baseDecl->internalContext()->importedParentContexts().first().context(0);
  QVERIFY(specialTemplateContext != baseTemplateContext);
  QCOMPARE(specialTemplateContext->type(), DUContext::Template);
  QCOMPARE(baseTemplateContext->type(), DUContext::Template);
  kDebug(9007) << typeid(baseTemplateContext).name();
  kDebug(9007) << typeid(specialTemplateContext).name();
/*  CppDUContext<DUContext>* baseTemplateCtx = dynamic_cast<CppDUContext<DUContext>*>(baseTemplateContext);
  CppDUContext<DUContext>* specialTemplateCtx = dynamic_cast<CppDUContext<DUContext>*>(specialTemplateContext);
  QVERIFY(baseTemplateCtx);
  QVERIFY(specialTemplateCtx);
  QCOMPARE(specialTemplateCtx->instantiatedFrom(), baseTemplateContext);*/

  QCOMPARE(specialTemplateContext->localDeclarations().count(), 1);
  QCOMPARE(baseTemplateContext->localDeclarations().count(), 1);
  QVERIFY(specialTemplateContext->localDeclarations()[0] != baseTemplateContext->localDeclarations()[0]);

  kDebug(9007) << top->localDeclarations()[3]->abstractType()->toString();
  Cpp::ExpressionEvaluationResult result = parser.evaluateExpression( "*bPointer", KDevelop::DUContextPointer(top));
  QVERIFY(result.isInstance);
  QCOMPARE(result.type.abstractType()->toString(), QString("B&"));

  result = parser.evaluateExpression( "bPointer->i", KDevelop::DUContextPointer(top));
  QVERIFY(result.isInstance);
  QCOMPARE(result.type.abstractType()->toString(), QString("int"));

/*  result = parser.evaluateExpression( "bPointer->cast<B>()->i", KDevelop::DUContextPointer(top));
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QCOMPARE(result.type.abstractType()->toString(), QString("int"));*/

  release(top);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void TestExpressionParser::testSimpleExpression() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "struct Cont { int& a; Cont* operator -> () {}; double operator*(); }; Cont c; Cont* d = &c; void test() { c.a = 5; d->a = 5; (*d).a = 5; c.a(5, 1, c); c.b<Fulli>(); }";
  DUContext* c = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  DUContext* testContext = c->childContexts()[1];
  QCOMPARE( testContext->type(), DUContext::Function );

  //Make sure the declaration of "c" is found correctly
  Declaration* d = findDeclaration( testContext, QualifiedIdentifier("c") );
  QVERIFY(d);
  AbstractType::Ptr t(d->abstractType());
  QVERIFY( dynamic_cast<IdentifiedType*>( t.unsafeData() ) );
  QVERIFY( dynamic_cast<IdentifiedType*>( t.unsafeData() )->qualifiedIdentifier().toString() == "Cont" );

  kDebug(9007) << "test-Context:" << testContext;
  lock.unlock();

  Cpp::ExpressionParser parser;
  Cpp::ExpressionEvaluationResult result;

  result = parser.evaluateType( "const Cont", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("const Cont"));
  QVERIFY(!result.isInstance);
  lock.unlock();

  result = parser.evaluateExpression( "Cont", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont"));
  QVERIFY(!result.isInstance);
  lock.unlock();

  result = parser.evaluateExpression( "c.a", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  lock.unlock();

  result = parser.evaluateExpression( "d", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont*"));
  QCOMPARE(result.type.type<PointerType>()->baseType()->toString(), QString("Cont"));
  lock.unlock();

  //Test pointer-referencing
  result = parser.evaluateExpression( "&c.a", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("int*"));
  QVERIFY(result.isInstance);
  lock.unlock();

  //Test pointer-referencing and dereferencing
  result = parser.evaluateExpression( "*(&c.a)", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("int"));
  kDebug() << result.toString();
  QVERIFY(result.isInstance);
  lock.unlock();

  //Test overloaded "operator*"
  result = parser.evaluateExpression( "*c", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("double"));
  QVERIFY(result.isInstance);
  lock.unlock();

  //Test overloaded "operator->"
  result = parser.evaluateExpression( "c->a", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("int&"));
  QVERIFY(result.isInstance);
  lock.unlock();

  //Test normal pointer-access + assign expression
  result = parser.evaluateExpression( "d->a = 5", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("int&"));
  QVERIFY(result.isInstance);
  lock.unlock();

  //Test double * (one real, one overloaded)
  result = parser.evaluateExpression( "**d", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("double"));
  QVERIFY(result.isInstance);
  lock.unlock();

  //Test double &
  result = parser.evaluateExpression( "&d", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont**"));
  QVERIFY(result.isInstance);
  lock.unlock();

  //Test type-expression
  result = parser.evaluateExpression( "Cont", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(!result.isInstance);
  QVERIFY(result.type.abstractType());
  lock.unlock();

  //Test conditional expression
  result = parser.evaluateExpression( "d ? c.a : c.a", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("int&"));
  QVERIFY(result.isInstance);
  lock.unlock();

  result = parser.evaluateExpression( "\"hello\"", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString().trimmed(), QString("const char*").trimmed());
  QVERIFY(result.isInstance);
  lock.unlock();

  result = parser.evaluateExpression( "sizeof(Cont)", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("int"));
  QVERIFY(result.isInstance);
  lock.unlock();

  result = parser.evaluateExpression( "new Cont()", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont*"));
  QVERIFY(result.isInstance);
  lock.unlock();

  result = parser.evaluateExpression( "5", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("5"));
  QVERIFY(result.isInstance);
  QVERIFY(result.type.type<ConstantIntegralType>());
  lock.unlock();
  QVERIFY(!TypeUtils::isNullType(result.type.abstractType()));

  result = parser.evaluateExpression( "5.5", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("5.5"));
  QVERIFY(result.isInstance);
  QVERIFY(result.type.type<ConstantIntegralType>());
  lock.unlock();
  QVERIFY(!TypeUtils::isNullType(result.type.abstractType()));

  result = parser.evaluateExpression( "5.5 + 1.5", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("7"));
  QVERIFY(result.isInstance);
  QVERIFY(result.type.type<ConstantIntegralType>());
  lock.unlock();
  QVERIFY(!TypeUtils::isNullType(result.type.abstractType()));

  result = parser.evaluateExpression( "3 + 0.5", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("3.5"));
  QVERIFY(result.isInstance);
  QVERIFY(result.type.type<ConstantIntegralType>());
  lock.unlock();
  QVERIFY(!TypeUtils::isNullType(result.type.abstractType()));

  result = parser.evaluateExpression( "3u + 0.5", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("3.5"));
  QVERIFY(result.isInstance);
  QVERIFY(result.type.type<ConstantIntegralType>());
  lock.unlock();
  QVERIFY(!TypeUtils::isNullType(result.type.abstractType()));

  result = parser.evaluateExpression( "0", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QCOMPARE(result.type.abstractType()->toString(), QString("0"));
  QVERIFY(result.isInstance);
  QVERIFY(result.type.type<ConstantIntegralType>());
  lock.unlock();
  QVERIFY(TypeUtils::isNullType(result.type.abstractType()));

  lock.lock();
  release(c);
}

void TestExpressionParser::testThis() {
  TEST_FILE_PARSE_ONLY

  QByteArray text("class A{ void test() { } void test2() const { }; void extTest(); }; void A::extTest() {}");
  DUContext* top = parse( text, DumpNone);//DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 3);
  QCOMPARE(top->childContexts()[0]->type(), DUContext::Class);
  QCOMPARE(top->childContexts()[0]->childContexts().count(), 5);
  DUContext* testContext = top->childContexts()[0]->childContexts()[1];
  QCOMPARE(testContext->type(), DUContext::Other);
  QVERIFY(testContext->owner());

  DUContext* test2Context = top->childContexts()[0]->childContexts()[3];
  QCOMPARE(test2Context->type(), DUContext::Other);
  QVERIFY(test2Context->owner());

  Cpp::ExpressionParser parser;
  Cpp::ExpressionEvaluationResult result1 = parser.evaluateExpression( "this", KDevelop::DUContextPointer(testContext));
  QVERIFY(result1.isValid());
  QVERIFY(result1.type);
  QVERIFY(result1.isInstance);
  QCOMPARE(result1.type.abstractType()->toString(), QString("A*"));


  Cpp::ExpressionEvaluationResult result2 = parser.evaluateExpression( "this", KDevelop::DUContextPointer(test2Context));
  QVERIFY(result2.isValid());
  QVERIFY(result2.type);
  QVERIFY(result2.isInstance);
  QCOMPARE(result2.type.abstractType()->toString().trimmed(), QString("A* const"));

  DUContext* extTestCtx = top->childContexts()[2];
  QCOMPARE(extTestCtx->type(), DUContext::Other);
  QVERIFY(extTestCtx->owner());

  Cpp::ExpressionEvaluationResult result3 = parser.evaluateExpression( "this", KDevelop::DUContextPointer(extTestCtx));
  QVERIFY(result3.isValid());
  QVERIFY(result3.type);
  QVERIFY(result3.isInstance);
  QCOMPARE(result3.type.abstractType()->toString(), QString("A*"));

  release(top);
}

void TestExpressionParser::testBaseClasses() {
  TEST_FILE_PARSE_ONLY
}

void TestExpressionParser::testTypeConversion2() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "const int& i; int q; unsigned int qq; int* ii; int* const iii;";
  DUContext* c = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(c->localDeclarations().count(), 5);
  
  TypeConversion tc(c->topContext());
  QVERIFY(tc.implicitConversion(c->localDeclarations()[1]->indexedType(), c->localDeclarations()[0]->indexedType()));
  QVERIFY(tc.implicitConversion(c->localDeclarations()[2]->indexedType(), c->localDeclarations()[0]->indexedType()));
  QVERIFY(tc.implicitConversion(c->localDeclarations()[1]->indexedType(), c->localDeclarations()[0]->indexedType(), false));
  QVERIFY(tc.implicitConversion(c->localDeclarations()[2]->indexedType(), c->localDeclarations()[0]->indexedType(), false));

  //Only the pointed-to part must be const-compatible
  QVERIFY(tc.implicitConversion(c->localDeclarations()[4]->indexedType(), c->localDeclarations()[3]->indexedType()));
  
  //QVERIFY(0);
  //lock.lock();
  release(c);
}

void TestExpressionParser::testTypeConversionWithTypedefs() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "const int i; typedef int q; typedef unsigned int qq; int* ii; int* const iii;";
  DUContext* c = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(c->localDeclarations().count(), 5);
  
  TypeConversion tc(c->topContext());
  QVERIFY(tc.implicitConversion(c->localDeclarations()[1]->indexedType(), c->localDeclarations()[0]->indexedType()));
  QVERIFY(tc.implicitConversion(c->localDeclarations()[2]->indexedType(), c->localDeclarations()[0]->indexedType()));
  QVERIFY(tc.implicitConversion(c->localDeclarations()[1]->indexedType(), c->localDeclarations()[2]->indexedType()));
  QVERIFY(tc.implicitConversion(c->localDeclarations()[2]->indexedType(), c->localDeclarations()[1]->indexedType()));
  QVERIFY(tc.implicitConversion(c->localDeclarations()[1]->indexedType(), c->localDeclarations()[0]->indexedType(), false));
  QVERIFY(tc.implicitConversion(c->localDeclarations()[2]->indexedType(), c->localDeclarations()[0]->indexedType(), false));

  //Only the pointed-to part must be const-compatible
  QVERIFY(tc.implicitConversion(c->localDeclarations()[4]->indexedType(), c->localDeclarations()[3]->indexedType()));
  
  //QVERIFY(0);
  //lock.lock();
  release(c);
}

void TestExpressionParser::testTypeConversion() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "struct Cont { operator int() {}; }; void test( int c = 5 ) { this->test( Cont(), 1, 5.5, 6); }";
  DUContext* c = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  DUContext* testContext = c->childContexts()[1];
  QCOMPARE( testContext->type(), DUContext::Function );

  DUContext* contContext = c->childContexts()[0];
  Declaration* decl = contContext->localDeclarations()[0];

  QCOMPARE(decl->identifier(), Identifier("operator{...cast...}"));
  KDevelop::FunctionType::Ptr function = decl->type<KDevelop::FunctionType>();
  QCOMPARE(function->returnType()->toString(), QString("int"));

  Declaration* testDecl = c->localDeclarations()[1];
  AbstractFunctionDeclaration* functionDecl = dynamic_cast<AbstractFunctionDeclaration*>(testDecl);
  QVERIFY(functionDecl);

  QVERIFY(functionDecl->defaultParametersSize() == 1);
  QCOMPARE(functionDecl->defaultParameters()[0].str(), QString("5"));

  //QVERIFY(0);
  //lock.lock();
  release(c);
}

void TestExpressionParser::testEnum() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "enum Honk { Hank, Hunk }; void test() {}";
  DUContext* c = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(c->childContexts().size(), 3);
  
  lock.unlock();
  
  Cpp::ExpressionParser parser;

  //Reenable this once the type-parsing system etc. is fixed

  Cpp::ExpressionEvaluationResult result = parser.evaluateExpression( "Hank", KDevelop::DUContextPointer(c));
  lock.lock();
  
  kDebug() << typeid(*result.type.abstractType()).name();
  
  QVERIFY(result.type.type<EnumeratorType>());
  
  //QVERIFY(0);
  //lock.lock();
  release(c);
}

void TestExpressionParser::testCasts() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "struct Cont2 {}; struct Cont { int& a; Cont* operator -> () {}; double operator*(); }; Cont c; Cont* d = &c; void test() { c.a = 5; d->a = 5; (*d).a = 5; c.a(5, 1, c); c(); c.a = dynamic_cast<const Cont2*>(d); }";
  DUContext* c = parse( test, DumpNone );
  DUChainWriteLocker lock(DUChain::lock());

  DUContext* testContext = c->childContexts()[2];
  QCOMPARE( testContext->type(), DUContext::Function );

  //Make sure the declaration of "c" is found correctly
  Declaration* d = findDeclaration( testContext, QualifiedIdentifier("c") );
  QVERIFY(d);
  AbstractType::Ptr t(d->abstractType());
  QVERIFY( dynamic_cast<IdentifiedType*>( t.unsafeData() ) );
  QVERIFY( dynamic_cast<IdentifiedType*>( t.unsafeData() )->qualifiedIdentifier().toString() == "Cont" );

  lock.unlock();

  Cpp::ExpressionParser parser;

  //Reenable this once the type-parsing system etc. is fixed

  Cpp::ExpressionEvaluationResult result = parser.evaluateExpression( "(Cont2*)(d)", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont2*"));
  lock.unlock();

  result = parser.evaluateExpression( "dynamic_cast<Cont2*>(d)", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont2*"));
  lock.unlock();

  result = parser.evaluateExpression( "static_cast<Cont2*>(d)", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont2*"));
  lock.unlock();

  result = parser.evaluateExpression( "reinterpret_cast<Cont2*>(d)", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont2*"));
  lock.unlock();

  result = parser.evaluateExpression( "new Cont2*", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont2**"));
  lock.unlock();

  result = parser.evaluateExpression( "new Cont2", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont2*"));
  lock.unlock();

  result = parser.evaluateExpression( "new Cont2[5]", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont2*"));
  lock.unlock();

  result = parser.evaluateExpression( "(int*)new Cont2[5]", KDevelop::DUContextPointer(testContext));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("int*"));
  lock.unlock();

  lock.lock();
  release(c);
}

void TestExpressionParser::testOperators() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "struct Cont2 {int operator[] {}; operator()() {};}; struct Cont3{}; struct Cont { Cont3 operator[](int i) {} Cont3 operator()() {} Cont3 operator+(const Cont3& c3 ) {} }; Cont c; Cont2 operator+( const Cont& c, const Cont& c2){} Cont3 c3;";
  DUContext* ctx = parse( test, DumpNone );
  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(ctx->childContexts().count(), 5);
  QCOMPARE(ctx->localDeclarations().count(), 6);

  Declaration* cont2Dec = ctx->localDeclarations()[0];
  Declaration* cont3Dec = ctx->localDeclarations()[1];
  Declaration* contDec = ctx->localDeclarations()[2];
  Declaration* cDec = ctx->localDeclarations()[3];
  Declaration* plusDec = ctx->localDeclarations()[4];

  QVERIFY(cont2Dec->internalContext());
  QVERIFY(cont3Dec->internalContext());
  QVERIFY(contDec->internalContext());
  QVERIFY(!cDec->internalContext());
  QVERIFY(plusDec->internalContext());

  QVERIFY(findDeclaration(ctx, QualifiedIdentifier("Cont::operator()")));
  QVERIFY(findDeclaration(ctx, QualifiedIdentifier("Cont::operator()"))->type<KDevelop::FunctionType>());

  CppClassType::Ptr cont2 = cont2Dec->type<CppClassType>();
  CppClassType::Ptr cont3 = cont3Dec->type<CppClassType>();
  CppClassType::Ptr cont = contDec->type<CppClassType>();
  QVERIFY(cont2);
  QVERIFY(cont3);
  QVERIFY(cont);

  lock.unlock();

  Cpp::ExpressionParser parser;

  //Reenable this once the type-parsing system etc. is fixed

  Cpp::ExpressionEvaluationResult result = parser.evaluateExpression( "c+c", KDevelop::DUContextPointer(ctx));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont2"));
  lock.unlock();

  result = parser.evaluateExpression( "c+c3", KDevelop::DUContextPointer(ctx));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont3"));
  lock.unlock();

  result = parser.evaluateExpression( "c()", KDevelop::DUContextPointer(ctx));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont3"));
  lock.unlock();

  result = parser.evaluateExpression( "c[5]", KDevelop::DUContextPointer(ctx));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont3"));
  lock.unlock();

  //A simple test: Constructing a type should always result in the type, no matter whether there is a constructor.
  result = parser.evaluateExpression( "Cont(5)", KDevelop::DUContextPointer(ctx));
  lock.lock();
  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type.abstractType());
  QCOMPARE(result.type.abstractType()->toString(), QString("Cont"));
  lock.unlock();

  lock.lock();
  release(ctx);
}

void TestExpressionParser::testTemplateFunctions() {
  {
    QByteArray method("template<class T> T test(T t) {}");

    DUContext* top = parse(method, DumpAll);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    
    QCOMPARE(top->usesCount(), 1);
    
    Cpp::ExpressionParser parser(false, true);

    Cpp::ExpressionEvaluationResult result;

    result = parser.evaluateExpression( "test<int>()", KDevelop::DUContextPointer(top));
 
    QVERIFY(result.isValid());
    QVERIFY(result.type.type<IntegralType>());
    
    result = parser.evaluateExpression( "test(5)", KDevelop::DUContextPointer(top));
 
    QVERIFY(result.isValid());
    QVERIFY(result.type.type<IntegralType>());
    
    release(top);
  }
  {
    QByteArray method("template<class T> class A { template<class T> T a(const T& q) {}; }; class C{}; template<class T> T a(T& q) {}; template<class T> T b(A<T> q) {}; template<class T> T c(const A<T*&>& q) {}; A<int>* aClass;");

    DUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 6);
    Declaration* d = findDeclaration(top, QualifiedIdentifier("a<A>"));
    QVERIFY(d);
    KDevelop::FunctionType::Ptr cppFunction = d->type<KDevelop::FunctionType>();
    QVERIFY(cppFunction);
    QCOMPARE(cppFunction->arguments().count(), 1);
    QCOMPARE(cppFunction->returnType()->indexed(), top->localDeclarations()[0]->indexedType());
    QCOMPARE(cppFunction->arguments()[0]->toString(), QString("A&"));

    Cpp::ExpressionParser parser(false, true);

    Cpp::ExpressionEvaluationResult result;

    result = parser.evaluateExpression( "a(C())", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    QCOMPARE(result.type.abstractType()->indexed(), top->localDeclarations()[1]->abstractType()->indexed());

    result = parser.evaluateExpression( "b<C>(A<C>())", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    QCOMPARE(result.type.abstractType()->indexed(), top->localDeclarations()[1]->abstractType()->indexed());

    result = parser.evaluateExpression( "b(A<C>())", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    kDebug() << result.toString();
  /*  IdentifiedType* identified = dynamic_cast<IdentifiedType*>(result.type.abstractType().data());
    Q_ASSERT(identified);*/
    //kDebug() << "identified:" << identified->qualifiedIdentifier();
    QCOMPARE(result.type.abstractType()->indexed(), top->localDeclarations()[1]->abstractType()->indexed());

    result = parser.evaluateExpression( "a<A>(A())", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    QCOMPARE(result.type.abstractType()->indexed(), top->localDeclarations()[0]->abstractType()->indexed());

    result = parser.evaluateExpression( "A<C>()", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());

    result = parser.evaluateExpression( "aClass->a<A>(A())", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    QCOMPARE(result.type.abstractType()->indexed(), top->localDeclarations()[0]->abstractType()->indexed());

    ///@todo find out why this doesn't work
//     result = parser.evaluateExpression( "aClass->a(A())", KDevelop::DUContextPointer(top));
//     QVERIFY(result.isValid());
//     QCOMPARE(result.type.abstractType()->indexed(), top->localDeclarations()[0]->abstractType()->indexed());
//     kDebug() << result.type.abstractType()->toString();

    result = parser.evaluateExpression( "a(A())", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    QCOMPARE(result.type.abstractType()->indexed(), top->localDeclarations()[0]->abstractType()->indexed());

    //This test will succeed again once we have a working type repository!
    result = parser.evaluateExpression( "c(A<C*&>())", KDevelop::DUContextPointer(top));
    QVERIFY(result.isValid());
    kDebug() << result.toString();
    QCOMPARE(result.type.abstractType()->indexed(), top->localDeclarations()[1]->abstractType()->indexed());
    release(top);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void TestExpressionParser::release(DUContext* top)
{
  //KDevelop::EditorIntegrator::releaseTopRange(top->textRangePtr());
  if(dynamic_cast<TopDUContext*>(top))
    DUChain::self()->removeDocumentChain(static_cast<TopDUContext*>(top));
  //delete top;
}

DUContext* TestExpressionParser::parse(const QByteArray& unit, DumpAreas dump)
{
  if (dump)
    kDebug(9007) << "==== Beginning new test case...:" << endl << unit;

  ParseSession* session = new ParseSession();
  session->setContentsAndGenerateLocationTable(tokenizeFromByteArray(unit));

  Parser parser(&control);
  TranslationUnitAST* ast = parser.parse(session);
  ast->session = session;

  if (dump & DumpAST) {
    kDebug(9007) << "===== AST:";
    cppDumper.dump(ast, session);
  }

  static int testNumber = 0;
  IndexedString url(QString("file:///internal/%1").arg(testNumber++));

  DeclarationBuilder definitionBuilder(session);

  Cpp::EnvironmentFilePointer file( new Cpp::EnvironmentFile( url, 0 ) );
  TopDUContext* top = definitionBuilder.buildDeclarations(file, ast);

  UseBuilder useBuilder(session);
  useBuilder.buildUses(ast);

  if (dump & DumpDUChain) {
    kDebug(9007) << "===== DUChain:";

    DUChainWriteLocker lock(DUChain::lock());
    dumper.dump(top);
  }

  if (dump & DumpType) {
    kDebug(9007) << "===== Types:";
    DumpTypes dt;
    DUChainWriteLocker lock(DUChain::lock());
    foreach (const AbstractType::Ptr& type, definitionBuilder.topTypes())
      dt.dump(type.unsafeData());
  }

  if (dump)
    kDebug(9007) << "===== Finished test case.";

  delete session;

  return top;
}

#include "test_expressionparser.moc"
