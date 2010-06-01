/* This file is part of KDevelop
    Copyright 2006 Hamish Rodda<rodda@kde.org>
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

#include "test_cppcodecompletion.h"

#include <QtTest/QtTest>
#include <typeinfo>

#include <language/duchain/duchain.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/topducontext.h>
#include <language/duchain/forwarddeclaration.h>
#include <language/duchain/declaration.h>
#include <language/duchain/aliasdeclaration.h>
#include <language/editor/documentrange.h>
#include <language/duchain/classfunctiondeclaration.h>
#include "declarationbuilder.h"
#include "usebuilder.h"
#include "cppeditorintegrator.h"
#include "dumptypes.h"
#include "environmentmanager.h"

#include "tokens.h"
#include "parsesession.h"

#include "rpp/preprocessor.h"
#include "rpp/pp-engine.h"
#include "rpp/pp-environment.h"
#include "expressionvisitor.h"
#include "expressionparser.h"
#include "codecompletion/context.h"
#include "codecompletion/helpers.h"
#include "codecompletion/item.h"
#include "cpppreprocessenvironment.h"
#include <language/duchain/classdeclaration.h>
#include "cppduchain/missingdeclarationproblem.h"
#include "cppduchain/missingdeclarationassistant.h"
#include <qstandarditemmodel.h>
#include <language/duchain/functiondefinition.h>

#include <language/codecompletion/codecompletiontesthelper.h>
#include <language/duchain/persistentsymboltable.h>
#include <cpputils.h>

#include <tests/autotestshell.h>
#include <tests/testcore.h>

using namespace KTextEditor;

using namespace KDevelop;

QTEST_MAIN(TestCppCodeCompletion)

QString testFile1 = "class Erna; struct Honk { int a,b; enum Enum { Number1, Number2 }; Erna& erna; operator int() {}; }; struct Pointer { Honk* operator ->() const {}; Honk& operator * () {}; }; Honk globalHonk; Honk honky; \n#define HONK Honk\n";

QString testFile2 = "struct Honk { int a,b; enum Enum { Number1, Number2 }; Erna& erna; operator int() {}; }; struct Erna { Erna( const Honk& honk ) {} }; struct Heinz : public Erna {}; Erna globalErna; Heinz globalHeinz; int globalInt; Heinz globalFunction(const Heinz& h ) {}; Erna globalFunction( const Erna& erna); Honk globalFunction( const Honk&, const Heinz& h ) {}; int globalFunction(int ) {}; HONK globalMacroHonk; struct GlobalClass { Heinz function(const Heinz& h ) {}; Erna function( const Erna& erna);  }; GlobalClass globalClass;\n#undef HONK\n";

QString testFile3 = "struct A {}; struct B : public A {};";

QString testFile4 = "void test1() {}; class TestClass() { TestClass() {} };";

typedef CodeCompletionItemTester<Cpp::CodeCompletionContext> CompletionItemTester;


#define TEST_FILE_PARSE_ONLY if (testFileParseOnly) QSKIP("Skip", SkipSingle);
TestCppCodeCompletion::TestCppCodeCompletion()
{
  testFileParseOnly = false;
}

void TestCppCodeCompletion::initTestCase()
{
  KDevelop::AutoTestShell::init();
  TestCore* core = new KDevelop::TestCore();
  core->initialize(KDevelop::Core::NoUi);
  Cpp::EnvironmentManager::init();

  DUChain::self()->disablePersistentStorage();
  typeInt = AbstractType::Ptr(new IntegralType(IntegralType::TypeInt));

  addInclude( "testFile1.h", testFile1 );
  addInclude( "testFile2.h", testFile2 );
  addInclude( "testFile3.h", testFile3 );
}

void TestCppCodeCompletion::cleanupTestCase()
{
}

Declaration* TestCppCodeCompletion::findDeclaration(DUContext* context, const Identifier& id, const SimpleCursor& position)
{
  QList<Declaration*> ret = context->findDeclarations(id, position);
  if (ret.count())
    return ret.first();
  return 0;
}

Declaration* TestCppCodeCompletion::findDeclaration(DUContext* context, const QualifiedIdentifier& id, const SimpleCursor& position)
{
  QList<Declaration*> ret = context->findDeclarations(id, position);
  if (ret.count())
    return ret.first();
  return 0;
}

void TestCppCodeCompletion::testNoMemberAccess() {
  QByteArray test = "class MyClass{ public:\n int myint; };\n\n";
  
  TopDUContext* context = parse(test, DumpNone);
  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(context->childContexts().count(), 1);
  
  CompletionItemTester testCase(context, "void "); //NoMemberAccess with non-empty valid-type expression
  QVERIFY(testCase.completionContext->isValid());
  QCOMPARE(testCase.names, QStringList()); //Valid, but should not offer any completions in this case
  
  CompletionItemTester testCase1(context, "asdf "); //NoMemberAccess with non-empty invalid-type expression
  QVERIFY(!testCase1.completionContext->isValid());
  
  CompletionItemTester testCase2(context, " "); //NoMemberAccess with empty expression
  QVERIFY(testCase2.completionContext->isValid());
  QCOMPARE(testCase.names, QStringList()); //Theoretically should have "MyClass", but global completions aren't included
  
  release(context);
}

void TestCppCodeCompletion::testFunctionImplementation() {
  //__hidden1 and _Hidden2 should not be visible in the code-completion, as their identifiers are reserved to C++ implementations and standard libraries.
  addInclude("myclass.h", "namespace mynamespace { class myclass { void students(); }; }; class __hidden1; int _Hidden2; ");
  QByteArray test = "#include \"myclass.h\"\nnamespace mynamespace { }";
  
  TopDUContext* context = parse(test, DumpNone);
  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(context->childContexts().count(), 1);
  
  CompletionItemTester testCase(context->childContexts()[0]);
  QVERIFY(testCase.completionContext->isValid());
  QCOMPARE(testCase.names, QStringList() << "mynamespace" << "myclass");
  
  //TODO: If it ever becomes possible to test implementationhelpers, here it should be done
  release(context);
}

void TestCppCodeCompletion::testAliasDeclarationAccessPolicy() {
  QByteArray test = "namespace Base { int One; int Two; int Three };\
  class List { public: using Base::One; protected: using Base::Two; private: using Base::Three; }; int main(List a) {}";
  
  TopDUContext* context = parse(test, DumpNone);
  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(context->childContexts().count(), 4);
  
  CompletionItemTester testCase(context->childContexts()[3], "a.");
  QVERIFY(testCase.completionContext->isValid());
  QCOMPARE(testCase.names, QStringList() << "One");
  
  AliasDeclaration* aliasDeclOne = dynamic_cast<AliasDeclaration*>(context->childContexts()[1]->localDeclarations()[0]);
  AliasDeclaration* aliasDeclTwo = dynamic_cast<AliasDeclaration*>(context->childContexts()[1]->localDeclarations()[1]);
  AliasDeclaration* aliasDeclThree = dynamic_cast<AliasDeclaration*>(context->childContexts()[1]->localDeclarations()[2]);
  QVERIFY(aliasDeclOne && aliasDeclTwo && aliasDeclThree);
  QVERIFY(aliasDeclOne->accessPolicy() == KDevelop::Declaration::Public);
  QVERIFY(aliasDeclTwo->accessPolicy() == KDevelop::Declaration::Protected);
  QVERIFY(aliasDeclThree->accessPolicy() == KDevelop::Declaration::Private);
  
  release(context);
}

void TestCppCodeCompletion::testKeywords() {
  QByteArray test = "struct Values { int Value1; int Value2; struct Sub { int SubValue; }; }; int test(int a) {}";

  TopDUContext* context = parse(test, DumpNone);
  DUChainWriteLocker lock(DUChain::lock());
  QCOMPARE(context->childContexts().count(), 3);
  
  CompletionItemTester testCase(context->childContexts()[2], "switch (a) { case ");
  QVERIFY(testCase.completionContext->isValid());
  QCOMPARE(testCase.names, QStringList() << "a" << "Values" << "test");
  CompletionItemTester testCase2(context->childContexts()[2], "switch (a) { case Values.");
  QVERIFY(testCase2.completionContext->isValid());
  QCOMPARE(testCase2.names, QStringList() << "Value1" << "Value2");
  
  CompletionItemTester testReturn(context->childContexts()[2], "return ");
  QVERIFY(testReturn.completionContext->isValid());
  QCOMPARE(testReturn.names, QStringList()  << "return int" << "a" << "Values" << "test");
  CompletionItemTester testReturn2(context->childContexts()[2], "return Values.");
  QVERIFY(testReturn2.completionContext->isValid());
  QCOMPARE(testReturn2.names, QStringList() << "return int" << "Value1" << "Value2");
  
  CompletionItemTester testNew(context->childContexts()[2], "Values b = new ");
  QVERIFY(testNew.completionContext->isValid());
  QVERIFY(testNew.names.contains("Values"));
  CompletionItemTester testNew2(context->childContexts()[2], "Values::Sub b = new Values::");
  QVERIFY(testNew2.completionContext->isValid());
  QCOMPARE(testNew2.names, QStringList() << "Value1" << "Value2" << "Sub" ); //A little odd to see Value1 & 2 here
  
  CompletionItemTester testElse(context->childContexts()[2], "if (a) {} else ");
  QVERIFY(testElse.completionContext->isValid());
  QCOMPARE(testElse.names, QStringList() << "a" << "Values" << "test");
  CompletionItemTester testElse2(context->childContexts()[2], "if (a) {} else Values::");
  QVERIFY(testElse2.completionContext->isValid());
  QCOMPARE(testElse2.names, QStringList() << "Value1" << "Value2" << "Sub" );
#if 0  
  CompletionItemTester testThrow(context->childContexts()[2], "throw ");
  QVERIFY(testThrow.completionContext->isValid());
  QCOMPARE(testThrow.names, QStringList() << "a" << "Values" << "test");
  CompletionItemTester testThrow2(context->childContexts()[2], "thow Values::");
  QVERIFY(testThrow2.completionContext->isValid());
  QCOMPARE(testThrow2.names, QStringList() << "Value1" << "Value2" << "Sub" );
#endif
  CompletionItemTester testDelete(context->childContexts()[2], "delete ");
  QVERIFY(testDelete.completionContext->isValid());
  QCOMPARE(testDelete.names, QStringList() << "a" << "Values" << "test");
  CompletionItemTester testDelete2(context->childContexts()[2], "delete Values::");
  QVERIFY(testDelete2.completionContext->isValid());
  QCOMPARE(testDelete2.names, QStringList() << "Value1" << "Value2" << "Sub" );
  
  //TODO: emit, Q_EMIT;
  
  release(context);
}

void TestCppCodeCompletion::testArgumentMatching() {
  {
    QByteArray test = "struct A{ int m;}; void test(int q) { A a;  }";

    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->childContexts().count(), 3);
    CompletionItemTester tester(context->childContexts()[2], "test(a.");
    QVERIFY(tester.completionContext->parentContext());
    QCOMPARE(tester.completionContext->parentContext()->knownArgumentTypes().count(), 0);
    QCOMPARE(tester.completionContext->parentContext()->functionName(), QString("test"));
    bool abort = false;
    QCOMPARE(tester.completionContext->parentContext()->completionItems(abort).size(), 1);
    release(context);
  }
  {
    QByteArray test = "#define A(x) #x\n void test(char* a, char* b, int c) { } ";

    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->childContexts().count(), 2);
    CompletionItemTester tester(context->childContexts()[1], "test(\"hello\", A(a),");
    QVERIFY(tester.completionContext->parentContext());
    QCOMPARE(tester.completionContext->parentContext()->knownArgumentTypes().count(), 2);
    QVERIFY(tester.completionContext->parentContext()->knownArgumentTypes()[0].type.abstractType());
    QVERIFY(tester.completionContext->parentContext()->knownArgumentTypes()[1].type.abstractType());
    QCOMPARE(tester.completionContext->parentContext()->knownArgumentTypes()[0].type.abstractType()->toString(), QString("const char*"));
    QCOMPARE(tester.completionContext->parentContext()->knownArgumentTypes()[1].type.abstractType()->toString(), QString("const char*"));
    release(context);
  }
}

void TestCppCodeCompletion::testSubClassVisibility() {
  {
    QByteArray test = "typedef struct { int am; } A; void test() { A b; } ";

    TopDUContext* context = parse( test, DumpAll /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->childContexts().count(), 3);
    QCOMPARE(context->childContexts()[0]->localDeclarations().count(), 1);
    QCOMPARE(context->childContexts()[0]->localDeclarations()[0]->kind(), Declaration::Instance);
    QCOMPARE(CompletionItemTester(context->childContexts()[2], "b.").names, QStringList() << "am");

    release(context);
  }
  {
    QByteArray test = "struct A { int am; struct B { int bm; }; }; void test() { A::B b; } ";

    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->childContexts().count(), 3);
    QCOMPARE(CompletionItemTester(context->childContexts()[2], "b.").names, QStringList() << "bm");

    release(context);
  }
  {
    QByteArray test = "struct A { int am; struct B; }; struct A::B {int bm; }; void test() { A::B b; } ";

    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->childContexts().count(), 4);
    QCOMPARE(CompletionItemTester(context->childContexts()[3], "b.").names, QStringList() << "bm");

    release(context);
  }
}

void TestCppCodeCompletion::testMacrosInCodeCompletion()
{
  QByteArray test = "#define test foo\n #define test2 fee\n struct A {int mem;}; void fun() { A foo; A* fee; }";
  TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());
  
  QCOMPARE(context->childContexts().size(), 3);
  
  QCOMPARE(CompletionItemTester(context->childContexts()[2], "foo.", QString(), SimpleCursor(2, 0)).names, QStringList() << "mem");
  QCOMPARE(CompletionItemTester(context->childContexts()[2], "test.", QString(), SimpleCursor(2, 0)).names, QStringList() << "mem");
  
  QCOMPARE(CompletionItemTester(context->childContexts()[2], "fee->", QString(), SimpleCursor(2, 0)).names, QStringList() << "mem");
  QCOMPARE(CompletionItemTester(context->childContexts()[2], "test2->", QString(), SimpleCursor(2, 0)).names, QStringList() << "mem");
  
  
  release(context);
}

void TestCppCodeCompletion::testConstructorCompletion() {
  {
    QByteArray test = "class A {}; class Class : public A { Class(); int m_1; float m_2; char m_3; static int m_4; }; ";

    //74
    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    
    {
      kDebug() << "TEST 2";
      CompletionItemTester tester(context, "class Class { Class(); int m_1; float m_2; char m_3; }; Class::Class(int m1, float m2, char m3) : m_1(1), m_2(m2), ");
      
      //At first, only the members should be shown
      kDebug() << tester.names;
      QCOMPARE(tester.names, QStringList() << "m_3"); //m_1 should not be shown, because it is already initialized
      ///@todo Make sure that the item also inserts parens
    }

    {
      CompletionItemTester tester(context, "Class::Class(int m1, float m2, char m3) : ");
      
      //At first, only the members should be shown
      kDebug() << tester.names;
      QCOMPARE(tester.names, QStringList() << "A" <<  "m_1" << "m_2" << "m_3");
      ///@todo Make sure that the item also inserts parens
    }

    {
      kDebug() << "TEST 3";
      CompletionItemTester tester(context, "Class::Class(int m1, float m2, char m3) : m_1(");
      
      //At first, only the members should be shown
      QVERIFY(tester.names.size());
      QVERIFY(tester.completionContext->parentContext()); //There must be a type-hinting context
      QVERIFY(tester.completionContext->parentContext()->parentContext()); //There must be a type-hinting context
      QVERIFY(!tester.completionContext->isConstructorInitialization());
      QVERIFY(!tester.completionContext->parentContext()->isConstructorInitialization());
      QVERIFY(tester.completionContext->parentContext()->memberAccessOperation() == Cpp::CodeCompletionContext::FunctionCallAccess);
      QVERIFY(tester.completionContext->parentContext()->parentContext()->isConstructorInitialization());
    }
    
    release(context);
  }
}

void TestCppCodeCompletion::testSignalSlotCompletion() {
    // By processing qobjectdefs.h, we make sure that the qt-specific macros are defined in the duchain through overriding (see setuphelpers.cpp)
    addInclude("/qobjectdefs.h", "#define signals\n#define slots\n#define Q_SIGNALS\n#define Q_SLOTS\n#define Q_PRIVATE_SLOT\n#define SIGNAL\n#define SLOT\n int n;\n");
  
    addInclude("QObject.h", "#include \"/qobjectdefs.h\"\n class QObject { void connect(QObject* from, const char* signal, QObject* to, const char* slot); void connect(QObject* from, const char* signal, const char* slot); };");
    
    QByteArray test("#include \"QObject.h\"\n class TE; class A : public QObject { public slots: void slot1(); void slot2(TE*); signals: void signal1(TE*, char);void signal2(); public: void test() { } private: Q_PRIVATE_SLOT( d, void slot3(TE*) )  };");

    TopDUContext* context = parse( test, DumpAll );
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->childContexts().count(), 1);
    QCOMPARE(context->childContexts()[0]->childContexts().count(), 8);
    CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, ");
    QCOMPARE(CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, ").names.toSet(), (QStringList() << "connect" << "signal1" << "signal2").toSet());
    QCOMPARE(CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, SIGNAL(").names.toSet(), (QStringList() << "connect" << "signal1" << "signal2").toSet());
    kDebug() << "ITEMS:" << CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, SIGNAL(signal2()), this, SLOT(").names;
    QCOMPARE(CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, SIGNAL(signal2()), this, ").names.toSet(), (QStringList() << "connect" << "signal1" << "signal2" << "slot1" << "slot2" << "slot3" << "Connect to A::signal2 ()").toSet());
    QCOMPARE(CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, SIGNAL(signal2()), this, SIGNAL(").names.toSet(), (QStringList() << "connect" << "signal1" << "signal2" << "Connect to A::signal2 ()").toSet());
    QCOMPARE(CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, SIGNAL(signal2()), this, SLOT(").names.toSet(), (QStringList() << "connect" << "slot1" << "slot2" << "slot3" << "Connect to A::signal2 ()" << "signal2").toSet());

    QVERIFY(((QStringList() << "connect" << "signal1" << "signal2" << "slot1" << "slot2" << "slot3" << "Connect to A::signal2 ()").toSet() - CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, SIGNAL(signal2()), ").names.toSet()).isEmpty());
    QVERIFY(((QStringList() << "connect" << "signal1" << "signal2" << "Connect to A::signal2 ()").toSet() - CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, SIGNAL(signal2()), SIGNAL(").names.toSet()).isEmpty());
    QVERIFY(((QStringList() << "connect" << "slot1" << "slot2" << "slot3"<< "Connect to A::signal2 ()").toSet() - CompletionItemTester(context->childContexts()[0]->childContexts()[5], "connect( this, SIGNAL(signal2()), SLOT(").names.toSet()).isEmpty());
    
    Declaration* decl = context->childContexts().last()->findDeclarations(Identifier("slot3")).first();
    QVERIFY(decl);
    QVERIFY(dynamic_cast<ClassFunctionDeclaration*>(decl));
    QVERIFY(dynamic_cast<ClassFunctionDeclaration*>(decl)->accessPolicy() == Declaration::Private);
    QVERIFY(dynamic_cast<ClassFunctionDeclaration*>(decl)->isSlot());
    release(context);
}


void TestCppCodeCompletion::testAssistant() {
  {
    QByteArray test = "#define A hallo(3) = 1\n void test() { A; bla = 5; }";

    TopDUContext* context = parse( test, DumpAll );
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->problems().count(), 1);
    release(context);
  }
  {
    QByteArray test = "int n; class C { C() : member(n) {} }; }";

    TopDUContext* context = parse( test, DumpAll );
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->problems().count(), 1);
    {
      KSharedPtr<Cpp::MissingDeclarationProblem> mdp( dynamic_cast<Cpp::MissingDeclarationProblem*>(context->problems()[0].data()) );
      QVERIFY(mdp);
      kDebug() << "problem:" << mdp->description();
      QCOMPARE(mdp->type->containerContext.data(), context->childContexts()[0]);
      QCOMPARE(mdp->type->identifier().toString(), QString("member"));
      QVERIFY(mdp->type->assigned.type.isValid());
      QCOMPARE(TypeUtils::removeConstants(mdp->type->assigned.type.abstractType(), context)->toString(), QString("int"));
    }
    release(context);
  }
  {
    QByteArray test = "class C {}; void test() {C c; c.value = 5; int i = c.value2; i = c.value3(); }";

    TopDUContext* context = parse( test, DumpAll );
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->problems().count(), 3);
    {
      KSharedPtr<Cpp::MissingDeclarationProblem> mdp( dynamic_cast<Cpp::MissingDeclarationProblem*>(context->problems()[0].data()) );
      QVERIFY(mdp);
      kDebug() << "problem:" << mdp->description();
      QCOMPARE(mdp->type->containerContext.data(), context->childContexts()[0]);
      QCOMPARE(mdp->type->identifier().toString(), QString("value"));
      QVERIFY(mdp->type->assigned.type.isValid());
      QCOMPARE(TypeUtils::removeConstants(mdp->type->assigned.type.abstractType(), context)->toString(), QString("int"));
      QCOMPARE(context->childContexts().count(), 3);
    }
    {
      ///@todo Make this work as well
/*      KSharedPtr<Cpp::MissingDeclarationProblem> mdp( dynamic_cast<Cpp::MissingDeclarationProblem*>(context->problems()[1].data()) );
      QVERIFY(mdp);
      kDebug() << "problem:" << mdp->description();
      QCOMPARE(mdp->type->containerContext.data(), context->childContexts()[0]);
      QCOMPARE(mdp->type->identifier().toString(), QString("value2"));
      QVERIFY(!mdp->type->assigned.type.isValid());
      QVERIFY(mdp->type->convertedTo.type.isValid());
      QCOMPARE(TypeUtils::removeConstants(mdp->type->convertedTo.type.abstractType())->toString(), QString("int"));
      QCOMPARE(context->childContexts().count(), 3);*/
    }
    {
      KSharedPtr<Cpp::MissingDeclarationProblem> mdp( dynamic_cast<Cpp::MissingDeclarationProblem*>(context->problems()[2].data()) );
      QVERIFY(mdp);
      kDebug() << "problem:" << mdp->description();
      QCOMPARE(mdp->type->containerContext.data(), context->childContexts()[0]);
      QCOMPARE(mdp->type->identifier().toString(), QString("value3"));
      QVERIFY(!mdp->type->assigned.type.isValid());
      QVERIFY(mdp->type->convertedTo.type.isValid());
      QCOMPARE(TypeUtils::removeConstants(mdp->type->convertedTo.type.abstractType(), context)->toString(), QString("int"));
      QCOMPARE(context->childContexts().count(), 3);
    }
    release(context);
  }
  {
    QByteArray test = "class C {}; void test() {C c; int valueName; c.functionName(valueName); }";

    TopDUContext* context = parse( test, DumpAll );
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->problems().count(), 1);
    {
      KSharedPtr<Cpp::MissingDeclarationProblem> mdp( dynamic_cast<Cpp::MissingDeclarationProblem*>(context->problems()[0].data()) );
      QVERIFY(mdp);
      kDebug() << "problem:" << mdp->description();
      QCOMPARE(mdp->type->containerContext.data(), context->childContexts()[0]);
      QCOMPARE(mdp->type->identifier().toString(), QString("functionName"));
      QVERIFY(!mdp->type->assigned.type.isValid());
      QCOMPARE(mdp->type->arguments.count(), 1);
      ///@todo Use the value-names of values given to the function
//       QCOMPARE(mdp->type->arguments[0].second, QString("valueName"));
      QCOMPARE(context->childContexts().count(), 3);
    }
    release(context);
  }  
}

void TestCppCodeCompletion::testImportTypedef() {
  {
    QByteArray test = "class Class { }; typedef Class Klass; class C : public Class { };";

    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->childContexts().count(), 2);
    QCOMPARE(context->childContexts()[1]->importedParentContexts().count(), 1);
    QCOMPARE(context->childContexts()[1]->importedParentContexts()[0].context(context->topContext()), context->childContexts()[0]);
  }
  {
    QByteArray test = "class A { public: int m; }; template<class Base> class C : public Base { };";

    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    Declaration* CDecl = findDeclaration(context, QualifiedIdentifier("C<A>"));
    QVERIFY(CDecl);
    QVERIFY(CDecl->internalContext());
    QVector<DUContext::Import> imports = CDecl->internalContext()->importedParentContexts();
    QCOMPARE(imports.size(), 2);
    QVERIFY(imports[0].context(context));
    QVERIFY(imports[1].context(context));
    QCOMPARE(imports[0].context(context)->type(), DUContext::Template);
    QCOMPARE(imports[1].context(context)->type(), DUContext::Class);
    QCOMPARE(imports[1].context(context)->scopeIdentifier(true), QualifiedIdentifier("A"));
  }
  {
    QByteArray test = "class A { public: int m; }; template<class Base> class C : public Base { }; typedef C<A> TheBase; class B : public TheBase { }; class E : public B{ };";

    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    Declaration* typeDef = findDeclaration(context, QualifiedIdentifier("TheBase"));
    QVERIFY(typeDef);
    QVERIFY(typeDef->isTypeAlias());
    QVERIFY(typeDef->type<KDevelop::TypeAliasType>());
    
    Declaration* BDecl = findDeclaration(context, QualifiedIdentifier("B"));
    QVERIFY(BDecl);
    QCOMPARE(BDecl->internalContext()->importedParentContexts().size(), 1);
    QVERIFY(BDecl->internalContext()->importedParentContexts()[0].context(context));
  }
  
}

void TestCppCodeCompletion::testPrivateVariableCompletion() {
  TEST_FILE_PARSE_ONLY
  QByteArray test = "class C {void test() {}; int i; };";

  DUContext* context = parse( test, DumpAll /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(context->type() != DUContext::Helper);
  QCOMPARE(context->childContexts().count(), 1);
  DUContext* CContext = context->childContexts()[0];
  QCOMPARE(CContext->type(), DUContext::Class);
  QCOMPARE(CContext->childContexts().count(), 2);
  QCOMPARE(CContext->localDeclarations().count(), 2);
  DUContext* testContext = CContext->childContexts()[1];
  QCOMPARE(testContext->type(), DUContext::Other );
  QVERIFY(testContext->owner());
  QCOMPARE(testContext->localScopeIdentifier(), QualifiedIdentifier("test"));
  lock.unlock();
  
  CompletionItemTester tester(testContext);
  kDebug() << "names:" << tester.names;
  QCOMPARE(tester.names.toSet(), (QStringList() << "C" << "i" << "test" << "this").toSet());

  lock.lock();
  release(context);
}

void TestCppCodeCompletion::testCompletionPrefix() {
  TEST_FILE_PARSE_ONLY
  {
    QByteArray method("struct Test {int m;}; Test t;Test* t2;void test() {}");
    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().size(), 3);
    //Make sure the completion behind "for <" does not only show types
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";for(int a = 0; a <  t2->").names.contains("m"));
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";for(int a = 0; a <  ").names.contains("t2"));
    //Make sure that only types are shown as template parameters
    QVERIFY(!CompletionItemTester(top->childContexts()[2], "Test<").names.contains("t2"));
    
    QCOMPARE(CompletionItemTester(top->childContexts()[2], "if((t).").names, QStringList() << "m");
    QCOMPARE(CompletionItemTester(top->childContexts()[2], "Test t(&t2->").names, QStringList() << "m");

    QCOMPARE(CompletionItemTester(top->childContexts()[2], "Test(\"(\").").names, QStringList() << "m");
    
    QCOMPARE(CompletionItemTester(top->childContexts()[2], "Test(\" \\\" quotedText( \\\" \").").names, QStringList() << "m");
    
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i = ").completionContext->parentContext());
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i ( ").completionContext->parentContext());
    bool abort = false;
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i = ").completionContext->parentContext()->completionItems(abort).size());
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i ( ").completionContext->parentContext()->completionItems(abort).size());
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i = ").completionContext->parentContext()->completionItems(abort)[0]->typeForArgumentMatching().size());
    QVERIFY(CompletionItemTester(top->childContexts()[2], ";int i ( ").completionContext->parentContext()->completionItems(abort)[0]->typeForArgumentMatching().size());
    
    
    release(top);
  }
}

void TestCppCodeCompletion::testStringProblem() {
  TEST_FILE_PARSE_ONLY
  {
    QByteArray method("void test() {int i;};");
    TopDUContext* top = parse(method, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 2);
    CompletionItemTester tester(top->childContexts()[1],QString("bla url('\\\"');"));
    
    QCOMPARE(tester.names.toSet(), (QStringList() << "i" << "test").toSet());;
    release(top);
  }
  {
    QByteArray method("void test() {int i;};");
    TopDUContext* top = parse(method, DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 2);
    CompletionItemTester tester(top->childContexts()[1],QString("bla url(\"http://wwww.bla.de/\");"));
    
    QCOMPARE(tester.names.toSet(), (QStringList() << "i" << "test").toSet());;
    release(top);
  }
}

void TestCppCodeCompletion::testInheritanceVisibility() {
  TEST_FILE_PARSE_ONLY
  QByteArray method("class A { public: class AMyClass {}; }; class B : protected A { public: class BMyClass {}; }; class C : private B{ public: class CMyClass {}; }; class D : public C { class DMyClass{}; }; ");
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 4);

  QCOMPARE(top->childContexts()[1]->type(), DUContext::Class);
  QVERIFY(top->childContexts()[1]->owner());
  QVERIFY(Cpp::localClassFromCodeContext(top->childContexts()[1]));
  //From within B, MyClass is visible, because of the protected inheritance
  QCOMPARE(top->childContexts()[1]->localDeclarations().size(), 1);
  QVERIFY(!Cpp::isAccessible(top, dynamic_cast<ClassMemberDeclaration*>(top->childContexts()[0]->localDeclarations()[0]), top, top->childContexts()[1]));
  QCOMPARE(CompletionItemTester(top->childContexts()[1], "A::").names, QStringList() << "AMyClass");
  QCOMPARE(CompletionItemTester(top->childContexts()[1]).names.toSet(), QSet<QString>() << "BMyClass" << "AMyClass" << "A" << "B" );
  QCOMPARE(CompletionItemTester(top, "A::").names, QStringList() << "AMyClass");
  kDebug() << "list:" << CompletionItemTester(top, "B::").names << CompletionItemTester(top, "A::").names.size();
  QCOMPARE(CompletionItemTester(top, "B::").names, QStringList() << "BMyClass");
  QCOMPARE(CompletionItemTester(top->childContexts()[2]).names.toSet(), QSet<QString>() << "CMyClass" << "BMyClass" << "AMyClass" << "C" << "B" << "A");
  QCOMPARE(CompletionItemTester(top, "C::").names.toSet(), QSet<QString>() << "CMyClass");
  QCOMPARE(CompletionItemTester(top->childContexts()[3]).names.toSet(), QSet<QString>() << "DMyClass" << "CMyClass" << "D" << "C" << "B" << "A");
  QCOMPARE(CompletionItemTester(top, "D::").names.toSet(), QSet<QString>() << "CMyClass" ); //DMyClass is private
}



void TestCppCodeCompletion::testConstVisibility() {
  TEST_FILE_PARSE_ONLY
  QByteArray method("struct test { void f(); void e() const; }; int main() { const test a; } void test::e() const { }");
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 5);

  kDebug() << "list:" << CompletionItemTester(top->childContexts()[2], "a.").names << CompletionItemTester(top->childContexts()[2], "a.").names.size();
  QCOMPARE(CompletionItemTester(top->childContexts()[2], "a.").names.toSet(), QSet<QString>() << "e");
  kDebug() << "list:" << CompletionItemTester(top->childContexts()[4], "").names << CompletionItemTester(top->childContexts()[4], "").names.size();
  QCOMPARE(CompletionItemTester(top->childContexts()[4], "").names.toSet(), QSet<QString>() << "e" << "test" << "main" << "this");
}

void TestCppCodeCompletion::testFriendVisibility() {
  TEST_FILE_PARSE_ONLY
  QByteArray method("class A { class PrivateClass {}; friend class B; }; class B{};");
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->childContexts().count(), 2);

  //No type within A, so there should be no items
  QCOMPARE(CompletionItemTester(top->childContexts()[1], "A::").names, QStringList() << "PrivateClass");
}

void TestCppCodeCompletion::testLocalUsingNamespace() {
  TEST_FILE_PARSE_ONLY
  {
    QByteArray method("namespace Fuu { int test0(); }; namespace Foo { using namespace Fuu; int test() {} } void Bar() { using namespace Foo; int b = test(); }");
    TopDUContext* top = parse(method, DumpAll);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 4);
    QCOMPARE(top->childContexts()[1]->localDeclarations().size(), 2);
    QCOMPARE(top->childContexts()[3]->localDeclarations().size(), 2);
    QVERIFY(top->childContexts()[1]->localDeclarations()[1]->uses().size());
    QVERIFY(top->childContexts()[3]->findLocalDeclarations(KDevelop::globalImportIdentifier(), KDevelop::SimpleCursor::invalid(), 0, KDevelop::AbstractType::Ptr(), KDevelop::DUContext::NoFiltering).size());
  //   QVERIFY(top->childContexts()[2]->findDeclarations(KDevelop::globalImportIdentifier).size());
    
    QVERIFY(CompletionItemTester(top->childContexts()[3]).names.contains("test"));
    QVERIFY(CompletionItemTester(top->childContexts()[3]).names.contains("test0"));
//     QVERIFY(CompletionItemTester(top->childContexts()[3], "Foo::").names.contains("test0"));
    release(top);
  }
}

void TestCppCodeCompletion::testTemplateFunction() {
    QByteArray method("template<class A> void test(A i); void t() { }");
    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->childContexts().count(), 4);
    
    bool abort = false;
    {
      CompletionItemTester tester1(top->childContexts()[3], "test<");
      QVERIFY(tester1.completionContext->parentContext());
      CompletionItemTester tester2 = tester1.parent();
      QCOMPARE(tester2.items.size(), 1);
      Cpp::NormalDeclarationCompletionItem* item = dynamic_cast<Cpp::NormalDeclarationCompletionItem*>(tester2.items[0].data());
      QVERIFY(item);
      QVERIFY(item->completingTemplateParameters());
    }
    {
      kDebug() << "second test";
      CompletionItemTester tester1(top->childContexts()[3], "test<int>(");
      QVERIFY(tester1.completionContext->parentContext());
      CompletionItemTester tester2 = tester1.parent();
      QCOMPARE(tester2.items.size(), 1);
      Cpp::NormalDeclarationCompletionItem* item = dynamic_cast<Cpp::NormalDeclarationCompletionItem*>(tester2.items[0].data());
      QVERIFY(item);
      QVERIFY(!item->completingTemplateParameters());
      QVERIFY(item->typeForArgumentMatching().size() == 1);
      QVERIFY(item->typeForArgumentMatching()[0].type<IntegralType>());
    }
    
    release(top);
}

void TestCppCodeCompletion::testTemplateArguments() {
    QByteArray method("template<class T> struct I; typedef I<int> II; template<class T> struct Test { T t; }; ");
    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->childContexts().count(), 3);
    
    QVERIFY(findDeclaration(top, QualifiedIdentifier("II")));
    
    Declaration* decl = findDeclaration(top, QualifiedIdentifier("Test<II>::t"));
    QVERIFY(decl);
    QVERIFY(decl->abstractType());
    QVERIFY(decl->type<TypeAliasType>());
    
    //Since II is not template-dependent, the type should have stayed a TypeAliasType
    QCOMPARE(Identifier(decl->abstractType()->toString()), Identifier("II"));
    
    release(top);
}

void TestCppCodeCompletion::testCompletionBehindTypedeffedConstructor() {
    QByteArray method("template<class T> struct A { A(T); int m; }; typedef A<int> TInt; void test() {}");
    TopDUContext* top = parse(method, DumpAll);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 4);
    QCOMPARE(top->childContexts()[1]->localDeclarations().size(), 2);

    //Member completion
    // NOTE: constructor A is not listed, as you can't call the constructor in this way
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "A<int>().").names.toSet(), (QStringList() << QString("m")).toSet());
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "TInt().").names.toSet(), (QStringList() << QString("m")).toSet());
    
    //Argument-hints
    kDebug() << CompletionItemTester(top->childContexts()[3], "TInt(").parent().names;
    QVERIFY(CompletionItemTester(top->childContexts()[3], "TInt(").parent().names.contains("A"));
    QVERIFY(CompletionItemTester(top->childContexts()[3], "TInt ti(").parent().names.contains("A"));
    
    release(top);
}

void TestCppCodeCompletion::testCompletionInExternalClassDefinition() {
    {
      QByteArray method("class A { class Q; class B; }; class A::B {class C;}; class A::B::C{void test(); }; void A::B::test() {}; void A::B::C::test() {}");
      TopDUContext* top = parse(method, DumpAll);

      DUChainWriteLocker lock(DUChain::lock());
      QCOMPARE(top->childContexts().count(), 7);
      QCOMPARE(top->childContexts()[1]->childContexts().count(), 1);
      QVERIFY(CompletionItemTester(top->childContexts()[1]->childContexts()[0]).names.contains("Q"));
      QVERIFY(CompletionItemTester(top->childContexts()[2]->childContexts()[0]).names.contains("Q"));
      QVERIFY(CompletionItemTester(top->childContexts()[3]).names.contains("Q"));
      QVERIFY(CompletionItemTester(top->childContexts()[5]).names.contains("Q"));
      release(top);
    }
}

void TestCppCodeCompletion::testTemplateMemberAccess() {
  {
    QByteArray method("template<class T> struct I; template<class T> class Test { public: typedef I<T> It; }; template<class T> struct I { }; Test<int>::It test;");
    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->localDeclarations().count(), 4);
    AbstractType::Ptr type = TypeUtils::unAliasedType(top->localDeclarations()[3]->abstractType());
    QVERIFY(type);
    IdentifiedType* identified = dynamic_cast<IdentifiedType*>(type.unsafeData());
    QVERIFY(identified);
    QVERIFY(!identified->declarationId().isDirect());
    QString specializationString = IndexedInstantiationInformation(identified->declarationId().specialization()).information().toString();
    QCOMPARE(specializationString, QString("<int>"));
    QCOMPARE(top->localDeclarations()[3]->abstractType()->toString().remove(' '), QString("Test<int>::It"));
    QCOMPARE(TypeUtils::unAliasedType(top->localDeclarations()[3]->abstractType())->toString().remove(' '), QString("I<int>"));
    
    lock.unlock();
    parse(method, DumpNone, 0, KUrl(), top);
    lock.lock();

    QCOMPARE(top->localDeclarations().count(), 4);
    QVERIFY(top->localDeclarations()[3]->abstractType());
    QCOMPARE(TypeUtils::unAliasedType(top->localDeclarations()[3]->abstractType())->toString().remove(' '), QString("I<int>"));
    
    release(top);
  }
  {
    QByteArray method("template<class T> class Test { public: T member; typedef T Data; enum { Value = 3 }; }; typedef Test<int> IntTest; void test() { IntTest tv; int i = Test<int>::Value; }");
    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "Test<int>::").names.toSet(), QSet<QString>() << "Data" << "Value" << "member");

    lock.unlock();
    parse(method, DumpNone, 0, KUrl(), top);
    lock.lock();

    QCOMPARE(top->childContexts().count(), 4);
    QCOMPARE(top->childContexts()[3]->type(), DUContext::Other);
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "IntTest::").names.toSet(), QSet<QString>() << "Data" << "Value" << "member");
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "Test<int>::").names.toSet(), QSet<QString>() << "Data" << "Value" << "member");
    QCOMPARE(CompletionItemTester(top->childContexts()[3], "tv.").names.toSet(), QSet<QString>() << "member");
    release(top);
  }
}

void TestCppCodeCompletion::testNamespaceCompletion() {
  
  QByteArray method("namespace A { class m; namespace Q {}; }; namespace A { class n; int q; }");
  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QCOMPARE(top->localDeclarations().count(), 2);
  QCOMPARE(top->childContexts().count(), 2);
  QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("A"));
  QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("A"));
  QCOMPARE(top->localDeclarations()[0]->kind(), Declaration::Namespace);
  QCOMPARE(top->localDeclarations()[1]->kind(), Declaration::Namespace);
  QVERIFY(!top->localDeclarations()[0]->abstractType());
  QVERIFY(!top->localDeclarations()[1]->abstractType());
  QCOMPARE(top->localDeclarations()[0]->internalContext(), top->childContexts()[0]);
  QCOMPARE(top->localDeclarations()[1]->internalContext(), top->childContexts()[1]);
  
  QCOMPARE(CompletionItemTester(top).names, QStringList() << "A");

  QCOMPARE(CompletionItemTester(top->childContexts()[1], "A::").names.toSet(), QSet<QString>() << "m" << "n" << "Q");
  QCOMPARE(CompletionItemTester(top).itemData("A", KTextEditor::CodeCompletionModel::Prefix).toString(), QString("namespace"));
  release(top);
}


void TestCppCodeCompletion::testIndirectImports()
{
  {
    addInclude("testIndirectImportsHeader1.h", "class C {};");
    addInclude("testIndirectImportsHeader2.h", "template<class T> class D : public T {};");
    
    QByteArray method("#include \"testIndirectImportsHeader2.h\"\n#include \"testIndirectImportsHeader1.h\"\n typedef D<C> Base; class MyClass : public C, public Base {}; ");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->importedParentContexts().size(), 2);
    QCOMPARE(top->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->importedParentContexts().count(), 2);
    QVERIFY(!top->childContexts()[0]->importedParentContexts()[0].isDirect()); //Should not be direct, since it crosses a header
    QVERIFY(!top->childContexts()[0]->importedParentContexts()[0].indirectDeclarationId().isDirect()); //Should not be direct, since it crosses a header
    QVERIFY(!top->childContexts()[0]->importedParentContexts()[1].isDirect()); //Should not be direct, since it crosses a header
    QVERIFY(!top->childContexts()[0]->importedParentContexts()[1].indirectDeclarationId().isDirect()); //Should not be direct, since it crosses a header
    DUContext* import1 = top->childContexts()[0]->importedParentContexts()[1].context(top);
    QVERIFY(import1);
    QCOMPARE(import1->importedParentContexts().count(), 2); //The template-context is also imported
    QVERIFY(!import1->importedParentContexts()[1].isDirect()); //Should not be direct, since it crosses a header
    QVERIFY(!import1->importedParentContexts()[1].indirectDeclarationId().isDirect()); //Should not be direct, since it crosses a header

    DUContext* import2 = import1->importedParentContexts()[0].context(top);
    QVERIFY(import2);
    QCOMPARE(import2->importedParentContexts().count(), 0);

    release(top);
  }
}

void TestCppCodeCompletion::testSameNamespace() {
  {
    addInclude("testSameNamespaceClassHeader.h", "namespace A {\n class B\n {\n \n};\n \n}");
    
    QByteArray method("#include \"testSameNamespaceClassHeader.h\"\n namespace A {\n namespace AA {\n};\n };\n");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top->importedParentContexts().size(), 1);
    QVERIFY(!top->parentContext());
    QCOMPARE(top->childContexts().count(), 1);
    QCOMPARE(top->childContexts()[0]->childContexts().count(), 1);
    {
      kDebug() << CompletionItemTester(top->childContexts()[0]).names;
      QCOMPARE(CompletionItemTester(top->childContexts()[0]).names.toSet(), QSet<QString>() << "B" << "A" << "AA");
      QCOMPARE(CompletionItemTester(top->childContexts()[0]->childContexts()[0]).names.toSet(), QSet<QString>() << "B" << "A" << "AA");
    }
    
    release(top);
  }

  {
    //                 0         1         2         3         4         5         6         7
    //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
    QByteArray method("namespace A { class C { }; void test2(); } namespace A { void test() { } class C {};}");

    TopDUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QVERIFY(!top->parentContext());
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->childContexts().count(), 3);
    QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 2);
    FunctionDefinition* funDef = dynamic_cast<KDevelop::FunctionDefinition*>(top->childContexts()[1]->localDeclarations()[0]);
    QVERIFY(!funDef->hasDeclaration());

  //   lock.unlock();
    {
      kDebug() << CompletionItemTester(top->childContexts()[1]->childContexts()[2]).names;
      QCOMPARE(CompletionItemTester(top->childContexts()[1]->childContexts()[2]).names.toSet(), QSet<QString>() << "C" << "A");
      QCOMPARE(CompletionItemTester(top->childContexts()[1]).names.toSet(), QSet<QString>() << "C" << "A");
      QCOMPARE(CompletionItemTester(top->childContexts()[1]->childContexts()[1]).names.toSet(), QSet<QString>() << "C" << "test2" << "test" << "A");
    }
    
    release(top);
  }
}

void TestCppCodeCompletion::testUnnamedNamespace() {
  TEST_FILE_PARSE_ONLY

  //                 0         1         2         3         4         5         6         7
  //                 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
  QByteArray method("namespace {int a;} namespace { int b; }; void test() {a = 3;}");

  TopDUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(!top->parentContext());
  QCOMPARE(top->childContexts().count(), 4);
  QCOMPARE(top->localDeclarations().count(), 3);
  kDebug() << top->localDeclarations()[0]->range().textRange();
  QCOMPARE(top->localDeclarations()[0]->range().textRange(), KTextEditor::Range(0, 10, 0, 10));
  QVERIFY(findDeclaration(top, QualifiedIdentifier("a")));
  QVERIFY(findDeclaration(top, QualifiedIdentifier("b")));
  QVERIFY(findDeclaration(top, QualifiedIdentifier("a"))->uses().size());
  PersistentSymbolTable::FilteredDeclarationIterator decls = KDevelop::PersistentSymbolTable::self().getFilteredDeclarations(QualifiedIdentifier(Cpp::unnamedNamespaceIdentifier().identifier()), top->recursiveImportIndices());
  QVERIFY(decls);
  QCOMPARE(top->findLocalDeclarations(Cpp::unnamedNamespaceIdentifier().identifier()).size(), 2);
  QCOMPARE(top->findDeclarations(QualifiedIdentifier(Cpp::unnamedNamespaceIdentifier().identifier())).size(), 2);

//   lock.unlock();
  {
    Cpp::CodeCompletionContext::Ptr cptr( new  Cpp::CodeCompletionContext(DUContextPointer(top), "; ", QString(), top->range().end) );
    bool abort = false;
    typedef KSharedPtr <KDevelop::CompletionTreeItem > Item;
    
    QList <Item > items = cptr->completionItems(abort);
    foreach(Item i, items) {
      Cpp::NormalDeclarationCompletionItem* decItem  = dynamic_cast<Cpp::NormalDeclarationCompletionItem*>(i.data());
      QVERIFY(decItem);
      kDebug() << decItem->declaration()->toString();
      kDebug() << i->data(fakeModel().index(0, KTextEditor::CodeCompletionModel::Name), Qt::DisplayRole, 0).toString();
    }
    
    //Have been filtered out, because only types are shown from the global scope
    QCOMPARE(items.count(), 0); //C, test, and i
  }
  {
    Cpp::CodeCompletionContext::Ptr cptr( new  Cpp::CodeCompletionContext(DUContextPointer(top->childContexts()[3]), "; ", QString(), top->range().end) );
    bool abort = false;
    typedef KSharedPtr <KDevelop::CompletionTreeItem > Item;
    
    QList <Item > items = cptr->completionItems(abort);
    foreach(Item i, items) {
      Cpp::NormalDeclarationCompletionItem* decItem  = dynamic_cast<Cpp::NormalDeclarationCompletionItem*>(i.data());
      QVERIFY(decItem);
      kDebug() << decItem->declaration()->toString();
      kDebug() << i->data(fakeModel().index(0, KTextEditor::CodeCompletionModel::Name), Qt::DisplayRole, 0).toString();
    }
    
    QCOMPARE(items.count(), 3); //b, a, and test
  }
  
//   lock.lock();
  release(top);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void TestCppCodeCompletion::testCompletionContext() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "#include \"testFile1.h\"\n";
  test += "#include \"testFile2.h\"\n";
  test += "void test() { }";

  DUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(context->childContexts().count());
  DUContext* testContext = context->childContexts()[0];
  QCOMPARE( testContext->type(), DUContext::Function );

  lock.unlock();
  {
    ///Test whether a recursive function-call context is created correctly
    Cpp::CodeCompletionContext::Ptr cptr( new  Cpp::CodeCompletionContext(DUContextPointer(DUContextPointer(context)), "; globalFunction(globalFunction(globalHonk, ", QString(), SimpleCursor::invalid() ) );
    Cpp::CodeCompletionContext& c(*cptr);
    QVERIFY( c.isValid() );
    QVERIFY( c.memberAccessOperation() == Cpp::CodeCompletionContext::NoMemberAccess );
    QVERIFY( !c.memberAccessContainer().isValid() );

    //globalHonk is of type Honk. Check whether all matching functions were rated correctly by the overload-resolution.
    //The preferred parent-function in the list should be "Honk globalFunction( const Honk&, const Heinz& h )", because the first argument maches globalHonk
    Cpp::CodeCompletionContext* function = c.parentContext();
    QVERIFY(function);
    QVERIFY(function->memberAccessOperation() == Cpp::CodeCompletionContext::FunctionCallAccess);
    QVERIFY(!function->functions().isEmpty());

    lock.lock();
    for( Cpp::CodeCompletionContext::FunctionList::const_iterator it = function->functions().begin(); it != function->functions().end(); ++it )
      kDebug(9007) << (*it).function.declaration()->toString() << ((*it).function.isViable() ? QString("(viable)") : QString("(not viable)")) ;
    lock.unlock();

    QCOMPARE(function->functions().size(), 4);
    QVERIFY(function->functions()[0].function.isViable());
    //Because Honk has a conversion-function to int, globalFunction(int) is yet viable(although it can take only 1 parameter)
    QVERIFY(function->functions()[1].function.isViable());
    //Because Erna has a constructor that takes "const Honk&", globalFunction(Erna) is yet viable(although it can take only 1 parameter)
    QVERIFY(function->functions()[2].function.isViable());
    //Because a value of type Honk is given, 2 globalFunction's are not viable
    QVERIFY(!function->functions()[3].function.isViable());


    function = function->parentContext();
    QVERIFY(function);
    QVERIFY(function->memberAccessOperation() == Cpp::CodeCompletionContext::FunctionCallAccess);
    QVERIFY(!function->functions().isEmpty());
    QVERIFY(!function->parentContext());
    QVERIFY(function->functions().size() == 4);
    //Because no arguments were given, all functions are viable
    QVERIFY(function->functions()[0].function.isViable());
    QVERIFY(function->functions()[1].function.isViable());
    QVERIFY(function->functions()[2].function.isViable());
    QVERIFY(function->functions()[3].function.isViable());
  }

  {
    ///The context is a function, and there is no prefix-expression, so it should be normal completion.
    DUContextPointer contPtr(context);
    Cpp::CodeCompletionContext c(contPtr, "{", QString(), SimpleCursor::invalid() );
    QVERIFY( c.isValid() );
    QVERIFY( c.memberAccessOperation() == Cpp::CodeCompletionContext::NoMemberAccess );
    QVERIFY( !c.memberAccessContainer().isValid() );
  }

  lock.lock();
  release(context);
}


void TestCppCodeCompletion::testTypeConversion() {
  TEST_FILE_PARSE_ONLY

  QByteArray test = "#include \"testFile1.h\"\n";
  test += "#include \"testFile2.h\"\n";
  test += "#include \"testFile3.h\"\n";
  test += "int n;\n";
  test += "void test() { }\n";

  DUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  DUContext* testContext = context->childContexts()[0];
  QCOMPARE( testContext->type(), DUContext::Function );

  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("Heinz") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("Erna") ));
  Declaration* decl = findDeclaration( testContext, QualifiedIdentifier("Erna") );
  QVERIFY(decl);
  QVERIFY(decl->logicalInternalContext(context->topContext()));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("Honk") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("A") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("B") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("test") ));
  QVERIFY(findDeclaration( testContext, QualifiedIdentifier("n") ));
  AbstractType::Ptr Heinz = findDeclaration( testContext, QualifiedIdentifier("Heinz") )->abstractType();
  AbstractType::Ptr Erna = findDeclaration( testContext, QualifiedIdentifier("Erna") )->abstractType();
  AbstractType::Ptr Honk = findDeclaration( testContext, QualifiedIdentifier("Honk") )->abstractType();
  AbstractType::Ptr A = findDeclaration( testContext, QualifiedIdentifier("A") )->abstractType();
  AbstractType::Ptr B = findDeclaration( testContext, QualifiedIdentifier("B") )->abstractType();
  AbstractType::Ptr n = findDeclaration( testContext, QualifiedIdentifier("n") )->abstractType();

  QVERIFY(n);

  {
    FunctionType::Ptr test = findDeclaration( testContext, QualifiedIdentifier("test") )->type<FunctionType>();
    QVERIFY(test);

    Cpp::TypeConversion conv(context->topContext());
    QVERIFY(!conv.implicitConversion(test->returnType()->indexed(), Heinz->indexed(), false));
    QVERIFY(!conv.implicitConversion(Heinz->indexed(), test->returnType()->indexed(), false));
    QVERIFY(!conv.implicitConversion(test->returnType()->indexed(), n->indexed(), false));
    QVERIFY(!conv.implicitConversion(n->indexed(), test->returnType()->indexed(), false));
  }
  //lock.unlock();
  {
    ///Test whether a recursive function-call context is created correctly
    Cpp::TypeConversion conv(context->topContext());
    QVERIFY( !conv.implicitConversion(Honk->indexed(), Heinz->indexed()) );
    QVERIFY( conv.implicitConversion(Honk->indexed(), typeInt->indexed()) ); //Honk has operator int()
    QVERIFY( conv.implicitConversion(Honk->indexed(), Erna->indexed()) ); //Erna has constructor that takes Honk
    QVERIFY( !conv.implicitConversion(Erna->indexed(), Heinz->indexed()) );

    ///@todo reenable once base-classes are parsed correctly
    //QVERIFY( conv.implicitConversion(B, A) ); //B is based on A, so there is an implicit copy-constructor that creates A from B
    //QVERIFY( conv.implicitConversion(Heinz, Erna) ); //Heinz is based on Erna, so there is an implicit copy-constructor that creates Erna from Heinz

  }

  //lock.lock();
  release(context);
}

KDevelop::IndexedType toReference(IndexedType t) {
  
  ReferenceType::Ptr refType( new ReferenceType);
  refType->setBaseType(t.abstractType());
  return refType->indexed();
}

KDevelop::IndexedType toPointer(IndexedType t) {
  
  PointerType::Ptr refType( new PointerType);
  refType->setBaseType(t.abstractType());
  return refType->indexed();
}

void TestCppCodeCompletion::testTypeConversion2() {
  {
    QByteArray test = "class A {}; class B {public: explicit B(const A&); explicit B(const int&){}; private: operator A() const {}; }; class C : public B{private: C(B) {}; };";
    TopDUContext* context = parse( test, DumpAll /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->localDeclarations().size(), 3);
    QVERIFY(context->localDeclarations()[0]->internalContext());
    QCOMPARE(context->localDeclarations()[1]->internalContext()->localDeclarations().count(), 3);
    ClassFunctionDeclaration* classFun = dynamic_cast<ClassFunctionDeclaration*>(context->localDeclarations()[1]->internalContext()->localDeclarations()[0]);
    QVERIFY(classFun);
    QVERIFY(classFun->isExplicit());
    classFun = dynamic_cast<ClassFunctionDeclaration*>(context->localDeclarations()[1]->internalContext()->localDeclarations()[1]);
    QVERIFY(classFun);
    QVERIFY(classFun->isExplicit());
    
    
    Cpp::TypeConversion conv(context);
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[2]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[2]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[2]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[0]->indexedType(), context->localDeclarations()[1]->indexedType()) );

    QVERIFY( !conv.implicitConversion(toReference(context->localDeclarations()[2]->indexedType()), toReference(context->localDeclarations()[0]->indexedType()) ));
    QVERIFY( conv.implicitConversion(toReference(context->localDeclarations()[2]->indexedType()), toReference(context->localDeclarations()[1]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toReference(context->localDeclarations()[1]->indexedType()), toReference(context->localDeclarations()[2]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toReference(context->localDeclarations()[1]->indexedType()), toReference(context->localDeclarations()[0]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toReference(context->localDeclarations()[0]->indexedType()), toReference(context->localDeclarations()[1]->indexedType()) ));

    QVERIFY( !conv.implicitConversion(toPointer(context->localDeclarations()[2]->indexedType()), toPointer(context->localDeclarations()[0]->indexedType()) ));
    QVERIFY( conv.implicitConversion(toPointer(context->localDeclarations()[2]->indexedType()), toPointer(context->localDeclarations()[1]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toPointer(context->localDeclarations()[1]->indexedType()), toPointer(context->localDeclarations()[2]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toPointer(context->localDeclarations()[1]->indexedType()), toPointer(context->localDeclarations()[0]->indexedType()) ));
    QVERIFY( !conv.implicitConversion(toPointer(context->localDeclarations()[0]->indexedType()), toPointer(context->localDeclarations()[1]->indexedType()) ));
    
    release(context);
  }
  {
    QByteArray test = "const char** b; char** c; char** const d; char* const * e; char f; const char q; ";
    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->localDeclarations().size(), 6);
    Cpp::TypeConversion conv(context);
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[0]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    PointerType::Ptr fromPointer = context->localDeclarations()[1]->indexedType().type< PointerType>();
    QVERIFY(fromPointer);
    QVERIFY( !(fromPointer->modifiers() & AbstractType::ConstModifier));
    QVERIFY( conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[2]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[0]->indexedType(), context->localDeclarations()[2]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[2]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[2]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[3]->indexedType(), context->localDeclarations()[0]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[3]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[0]->indexedType(), context->localDeclarations()[3]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[1]->indexedType(), context->localDeclarations()[3]->indexedType()) );
    QVERIFY( !conv.implicitConversion(context->localDeclarations()[3]->indexedType(), context->localDeclarations()[1]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[4]->indexedType(), context->localDeclarations()[5]->indexedType()) );
    QVERIFY( conv.implicitConversion(context->localDeclarations()[5]->indexedType(), context->localDeclarations()[4]->indexedType()) );
    
    release(context);
  }
  
  {
    QByteArray test = "class A {}; class C {}; enum M { Em }; template<class T> class B{ public:B(T t); }; ";
    TopDUContext* context = parse( test, DumpNone /*DumpDUChain | DumpAST */);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(context->localDeclarations().size(), 4);
    QCOMPARE(context->childContexts().size(), 5);
    Cpp::TypeConversion conv(context);
    Declaration* decl = findDeclaration(context, QualifiedIdentifier("B<A>"));
    QVERIFY(decl);
    kDebug() << decl->toString();
    
    QVERIFY( conv.implicitConversion(context->localDeclarations()[0]->indexedType(), decl->indexedType()) );
    
    decl = findDeclaration(context, QualifiedIdentifier("B<M>"));
    QVERIFY(decl);
    kDebug() << decl->toString();
    QCOMPARE(context->childContexts()[2]->localDeclarations().size(), 1);
    QVERIFY( conv.implicitConversion(context->childContexts()[2]->localDeclarations()[0]->indexedType(), decl->indexedType()) );    
    
    release(context);
  }  
}

void TestCppCodeCompletion::testInclude() {
  TEST_FILE_PARSE_ONLY

  addInclude("file1.h", "#include \"testFile1.h\"\n#include \"testFile2.h\"\n");


  QByteArray test = "#include \"file1.h\"  \n  struct Cont { operator int() {}; }; void test( int c = 5 ) { this->test( Cont(), 1, 5.5, 6); }; HONK undefinedHonk;";
  DUContext* c = parse( test, DumpNone /*DumpDUChain | DumpAST */);
  DUChainWriteLocker lock(DUChain::lock());

  QVERIFY(c->topContext()->usingImportsCache());
  
  Declaration* decl = findDeclaration(c, QualifiedIdentifier("globalHeinz"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Heinz"));

  decl = findDeclaration(c, QualifiedIdentifier("globalErna"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Erna"));

  decl = findDeclaration(c, QualifiedIdentifier("globalInt"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("int"));

  decl = findDeclaration(c, QualifiedIdentifier("Honk"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Honk"));

  decl = findDeclaration(c, QualifiedIdentifier("honky"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Honk"));

  decl = findDeclaration(c, QualifiedIdentifier("globalHonk"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Honk"));

  decl = findDeclaration(c, QualifiedIdentifier("globalMacroHonk"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  QCOMPARE(decl->abstractType()->toString(), QString("Honk"));

  ///HONK was #undef'ed in testFile2, so this must be unresolved.
  decl = findDeclaration(c, QualifiedIdentifier("undefinedHonk"));
  QVERIFY(decl);
  QVERIFY(decl->abstractType().cast<DelayedType>());


  Cpp::ExpressionParser parser;

  ///The following test tests the expression-parser, but it is here because the other tests depend on it
  lock.unlock();
  Cpp::ExpressionEvaluationResult result = parser.evaluateExpression( "globalHonk.erna", DUContextPointer( c ) );
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.abstractType()->toString(), QString("Erna&"));


  ///Test overload-resolution
  lock.unlock();
  result = parser.evaluateExpression( "globalClass.function(globalHeinz)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.abstractType()->toString(), QString("Heinz"));

  lock.unlock();
  result = parser.evaluateExpression( "globalClass.function(globalHonk.erna)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.abstractType()->toString(), QString("Erna"));

  //No matching function for type Honk. Since the expression-parser is not set to "strict", it returns any found function with the right name.
  lock.unlock();
  result = parser.evaluateExpression( "globalClass.function(globalHonk)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  //QCOMPARE(result.type.abstractType()->toString(), QString("Heinz"));


  lock.unlock();
  result = parser.evaluateExpression( "globalFunction(globalHeinz)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.abstractType()->toString(), QString("Heinz"));
  lock.unlock();

  result = parser.evaluateExpression( "globalFunction(globalHonk.erna)", DUContextPointer(c));
  lock.lock();

  QVERIFY(result.isValid());
  QVERIFY(result.isInstance);
  QVERIFY(result.type);
  QCOMPARE(result.type.abstractType()->toString(), QString("Erna"));

  release(c);
}

void TestCppCodeCompletion::testUpdateChain() {
  TEST_FILE_PARSE_ONLY

{
    QByteArray text("#define Q_FOREACH(variable, container) for (QForeachContainer<__typeof__(container)> _container_(container); !_container_.brk && _container_.i != _container_.e; __extension__ ({ ++_container_.brk; ++_container_.i; })) for (variable = *_container_.i;; __extension__ ({--_container_.brk; break;})) \nvoid test() { Q_FOREACH(int a, b) { int i; } }");
    TopDUContext* top = parse( text, DumpAll );

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->childContexts()[1]->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->childContexts()[1]->childContexts()[1]->localDeclarations().count(), 1);
    IndexedDeclaration decl(top->childContexts()[1]->childContexts()[1]->childContexts()[1]->localDeclarations()[0]);
    QVERIFY(decl.data());
    QCOMPARE(decl.data()->identifier().toString(), QString("i"));
    
    parse(text, DumpNone, 0, KUrl(), top);
    QVERIFY(decl.data()); //Make sure the declaration has been updated, and not deleted
    
    release(top);
}
}

void TestCppCodeCompletion::testHeaderSections() {
  TEST_FILE_PARSE_ONLY
  /**
   * Make sure that the ends of header-sections are recognized correctly
   * */

  addInclude( "someHeader.h", "\n" );
  addInclude( "otherHeader.h", "\n" );

  IncludeFileList includes;

  HashedString turl("ths.h");

  QCOMPARE(preprocess(turl, "#include \"someHeader.h\"\nHello", includes, 0, true), QString("\n"));
  QCOMPARE(includes.count(), 1);
  includes.clear();

  QCOMPARE(preprocess(turl, "#include \"someHeader.h\"\nHello", includes, 0, false), QString("\nHello"));
  QCOMPARE(includes.count(), 1);
  includes.clear();

  QCOMPARE(preprocess(turl, "#include \"someHeader.h\"\n#include \"otherHeader.h\"\nHello", includes, 0, false), QString("\n\nHello"));
  QCOMPARE(includes.count(), 2);
  includes.clear();

  QCOMPARE(preprocess(turl, "#include \"someHeader.h\"\n#include \"otherHeader.h\"\nHello", includes, 0, true), QString("\n\n"));
  QCOMPARE(includes.count(), 2);
  includes.clear();

  QCOMPARE(preprocess(turl, "#ifndef GUARD\n#define GUARD\n#include \"someHeader.h\"\nHello\n#endif", includes, 0, true), QString("\n\n\n"));
  QCOMPARE(includes.count(), 1);
  includes.clear();

  QCOMPARE(preprocess(turl, "#ifndef GUARD\n#define GUARD\n#include \"someHeader.h\"\nHello\n#endif", includes, 0, false), QString("\n\n\nHello\n"));
  QCOMPARE(includes.count(), 1);
  includes.clear();
}

void TestCppCodeCompletion::testForwardDeclaration()
{
  addInclude( "testdeclaration.h", "class Test{ };" );
  QByteArray method("#include \"testdeclaration.h\"\n class Test; ");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());


  Declaration* decl = findDeclaration(top, Identifier("Test"), top->range().end);
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  AbstractType::Ptr t(decl->abstractType());
  QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));
  QVERIFY(!decl->isForwardDeclaration());

  release(top);
}

void TestCppCodeCompletion::testUsesThroughMacros() {
  {
    QByteArray method("int x;\n#define TEST(X) X\ny = TEST(x);");

    DUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->count(), 1);
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->at(0).start.column, 9);
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->at(0).end.column, 10);
  }
  {
    ///2 uses of x, that go through the macro TEST(..), and effectively are in line 2 column 5.
    QByteArray method("int x;\n#define TEST(X) void test() { int z = X; int q = X; }\nTEST(x)");

    kDebug() << method;
    DUContext* top = parse(method, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->localDeclarations()[0]->uses().count(), 1);
    //Since uses() returns unique uses, and both uses of x in TEST(x) have the same range,
    //only one use is returned.
    QCOMPARE(top->localDeclarations()[0]->uses().begin()->count(), 1);

    SimpleRange range1(top->localDeclarations()[0]->uses().begin()->at(0));
    QCOMPARE(range1.start.line, 2);
    QCOMPARE(range1.end.line, 2);
    QCOMPARE(range1.start.column, 5);
    QCOMPARE(range1.end.column, 6);
  }
}

void TestCppCodeCompletion::testAcrossHeaderReferences()
{
  addInclude( "acrossheader1.h", "class Test{ };" );
  addInclude( "acrossheader2.h", "Test t;" );
  QByteArray method("#include \"acrossheader1.h\"\n#include \"acrossheader2.h\"\n");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());


  Declaration* decl = findDeclaration(top, Identifier("t"), top->range().end);
  QVERIFY(decl);
  QVERIFY(decl->abstractType());
  AbstractType::Ptr t(decl->abstractType());
  QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));

  release(top);
}

void TestCppCodeCompletion::testAcrossHeaderTemplateResolution() {
  addInclude("acrossheaderresolution1.h", "class C {}; namespace std { template<class T> class A {  }; }");
  addInclude("acrossheaderresolution2.h", "namespace std { template<class T> class B { typedef A<T> Type; }; }");
  
  QByteArray method("#include \"acrossheaderresolution1.h\"\n#include \"acrossheaderresolution2.h\"\n std::B<C>::Type t;");
  
  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());
  
  Declaration* decl = findDeclaration(top, QualifiedIdentifier("t"), top->range().end);
  QVERIFY(decl);
  QCOMPARE(QualifiedIdentifier(TypeUtils::unAliasedType(decl->abstractType())->toString()), QualifiedIdentifier("std::A<C>"));
  
  release(top);
}

void TestCppCodeCompletion::testAcrossHeaderTemplateReferences()
{
  addInclude( "acrossheader1.h", "class Dummy { }; template<class Q> class Test{ };" );
  addInclude( "acrossheader2.h", "template<class B, class B2 = Test<B> > class Test2 : public Test<B>{ Test<B> bm; };" );
  QByteArray method("#include \"acrossheader1.h\"\n#include \"acrossheader2.h\"\n ");

  DUContext* top = parse(method, DumpNone);

  DUChainWriteLocker lock(DUChain::lock());


  {
    kDebug() << "top is" << top;
    Declaration* decl = findDeclaration(top, QualifiedIdentifier("Dummy"), top->range().end);
    QVERIFY(decl);
    QVERIFY(decl->abstractType());
    AbstractType::Ptr t(decl->abstractType());
    QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));
    QCOMPARE(decl->abstractType()->toString(), QString("Dummy"));
  }
  {
    Declaration* decl = findDeclaration(top, QualifiedIdentifier("Test2<Dummy>::B2"), top->range().end);
    QVERIFY(decl);
    QVERIFY(decl->abstractType());
    AbstractType::Ptr t(decl->abstractType());
    QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));
    QCOMPARE(decl->abstractType()->toString(), QString("Test< Dummy >"));
  }
  {
    Declaration* decl = findDeclaration(top, QualifiedIdentifier("Test2<Dummy>::bm"), top->range().end);
    QVERIFY(decl);
    QVERIFY(decl->abstractType());
    AbstractType::Ptr t(decl->abstractType());
    QVERIFY(dynamic_cast<const IdentifiedType*>(t.unsafeData()));
    QCOMPARE(decl->abstractType()->toString(), QString("Test< Dummy >"));
  }
  {
    ClassDeclaration* decl = dynamic_cast<ClassDeclaration*>(findDeclaration(top, QualifiedIdentifier("Test2<Dummy>"), top->range().end));
    QVERIFY(decl);
    QVERIFY(decl->abstractType());
    CppClassType::Ptr classType = decl->abstractType().cast<CppClassType>();
    QVERIFY(classType);
    QCOMPARE(decl->baseClassesSize(), 1u);
    QVERIFY(decl->baseClasses()[0].baseClass);
    CppClassType::Ptr parentClassType = decl->baseClasses()[0].baseClass.type<CppClassType>();
    QVERIFY(parentClassType);
    QCOMPARE(parentClassType->toString(), QString("Test< Dummy >"));
  }

  release(top);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void TestCppCodeCompletion::release(DUContext* top)
{
  //EditorIntegrator::releaseTopRange(top->textRangePtr());
  if(dynamic_cast<TopDUContext*>(top))
    DUChain::self()->removeDocumentChain(static_cast<TopDUContext*>(top));
  //delete top;
}

void TestCppCodeCompletion::addInclude( const QString& identity, const QString& text ) {
  fakeIncludes[identity] = text;
}

//Only for debugging
QString print(const Cpp::ReferenceCountedStringSet& set) {
  QString ret;
  bool first = true;
  Cpp::ReferenceCountedStringSet::Iterator it(set.iterator());
  while(it) {
    if(!first)
      ret += ", ";
    first = false;

    ret += (*it).str();
    ++it;
  }
  return ret;
}

//Only for debugging
QStringList toStringList( const Cpp::ReferenceCountedStringSet& set ) {
  QStringList ret;
  Cpp::ReferenceCountedStringSet::Iterator it(set.iterator());
  while(it) {
    ret << (*it).str();
    ++it;
  }
  ret.sort();
  return ret;
}

QStringList splitSorted(const QString& str) {
  QStringList ret = str.split("\n");
  ret.sort();
  return ret;
}

void TestCppCodeCompletion::testEmptyMacroArguments() {
  QString test("#define merge(prefix, suffix) prefix ## suffix\n void merge(test1, ) () { } void merge(, test2) () { }");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 2);
  QCOMPARE(ctx->localDeclarations()[0]->identifier().toString(), QString("test1"));
  QCOMPARE(ctx->localDeclarations()[1]->identifier().toString(), QString("test2"));
}

void TestCppCodeCompletion::testMacroExpansionRanges() {
{
  QString test("#define TEST(X) int allabamma; \nTEST(C)\n");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 1);
  kDebug() << ctx->localDeclarations()[0]->range().textRange();
  //kDebug() << ctx->localDeclarations()[1]->range().textRange();
  QCOMPARE(ctx->localDeclarations()[0]->range().textRange(), KTextEditor::Range(1, 7, 1, 7)); //Because the macro TEST was expanded out of its physical range, the Declaration is collapsed.
//  QCOMPARE(ctx->localDeclarations()[1]->range().textRange(), KTextEditor::Range(1, 10, 1, 11));
  //kDebug() << "Range:" << ctx->localDeclarations()[0]->range().textRange();
}
{
  QString test("#define A(X) bbbbbb\nint A(0);\n");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 1);
  kDebug() << ctx->localDeclarations()[0]->range().textRange();
  QCOMPARE(ctx->localDeclarations()[0]->range().textRange(), KTextEditor::Range(1, 8, 1, 8)); //Because the macro TEST was expanded out of its physical range, the Declaration is collapsed.
}
{
  QString test("#define TEST namespace NS{int a;int b;int c;int d;int q;} class A{}; \nTEST; int a; int b; int c; int d;int e;int f;int g;int h;\n");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 10);
  QCOMPARE(ctx->localDeclarations()[1]->range().textRange(), KTextEditor::Range(1, 4, 1, 4)); //Because the macro TEST was expanded out of its physical range, the Declaration is collapsed.
  QCOMPARE(ctx->localDeclarations()[2]->range().textRange(), KTextEditor::Range(1, 10, 1, 11));
}
{
  //The range of the merged declaration name should be trimmed to the end of the macro invocation
  QString test("#define TEST(X) class X ## Class {};\nTEST(Hallo)\n");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QCOMPARE(ctx->localDeclarations().count(), 1);
  kDebug() << ctx->localDeclarations()[0]->range().textRange();
  QCOMPARE(ctx->localDeclarations()[0]->range().textRange(), KTextEditor::Range(1, 5, 1, 11));
}
}

void TestCppCodeCompletion::testTimeMacro()
{
  QString test("const char* str = __TIME__;");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QVERIFY(ctx->problems().isEmpty());
  QCOMPARE(ctx->localDeclarations().count(), 1);
}

void TestCppCodeCompletion::testDateMacro()
{
  QString test("const char* str = __DATE__;");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QVERIFY(ctx->problems().isEmpty());
  QCOMPARE(ctx->localDeclarations().count(), 1);
}

void TestCppCodeCompletion::testFileMacro()
{
  QString test("const char* str = __FILE__;");
  DUChainWriteLocker l(DUChain::lock());
  TopDUContext* ctx = parse(test.toUtf8());
  QVERIFY(ctx->problems().isEmpty());
  QCOMPARE(ctx->localDeclarations().count(), 1);
}

void TestCppCodeCompletion::testNaiveMatching() {
  return;
    Cpp::EnvironmentManager::self()->setMatchingLevel(Cpp::EnvironmentManager::Naive);
    {
      addInclude("recursive_test_1.h", "#include \"recursive_test_2.h\"\nint i1;\n");
      addInclude("recursive_test_2.h", "#include \"recursive_test_1.h\"\nint i2;\n");
      
      TopDUContext* test1 = parse(QByteArray("#include \"recursive_test_1.h\"\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      QCOMPARE(test1->recursiveImportIndices().count(), 3u);
      QCOMPARE(test1->importedParentContexts().count(), 1);
      QCOMPARE(test1->importedParentContexts()[0].indexedContext().context()->importedParentContexts().count(), 1);
      QCOMPARE(test1->importedParentContexts()[0].indexedContext().context()->importedParentContexts()[0].indexedContext().context()->importedParentContexts().count(), 1);
      QCOMPARE(test1->importedParentContexts()[0].indexedContext().context()->importedParentContexts()[0].indexedContext().context()->importedParentContexts()[0].indexedContext().context()->importedParentContexts().count(), 1);
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }

}

void TestCppCodeCompletion::testHeaderGuards() {
    {
      TopDUContext* test1 = parse(QByteArray("#ifndef GUARD\n#define GUARD\nint x = 5; \n#endif\n#define BLA\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }
    {
      TopDUContext* test1 = parse(QByteArray("#ifndef GUARD\n#define GUARD\nint x = 5;\n#ifndef GUARD\n#define GUARD\n#endif\n#if defined(TEST)\n int q = 4;#endif\n#endif\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QCOMPARE(envFile1->headerGuard().str(), QString("GUARD"));
      release(test1);
    }
    {
      TopDUContext* test1 = parse(QByteArray("int x;\n#ifndef GUARD\n#define GUARD\nint x = 5; \n#endif\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }
    {
      TopDUContext* test1 = parse(QByteArray("#define X\n#ifndef GUARD\n#define GUARD\nint x = 5; \n#endif\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }
    {
      TopDUContext* test1 = parse(QByteArray("#ifndef GUARD\n#define GUARD\nint x = 5; \n#endif\nint o;\n"), DumpNone);
      DUChainWriteLocker l(DUChain::lock());
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      QVERIFY(envFile1);
      QVERIFY(envFile1->headerGuard().isEmpty());
      release(test1);
    }
}

void TestCppCodeCompletion::testEnvironmentMatching() {
    {
      CppPreprocessEnvironment::setRecordOnlyImportantString(false);
      
      addInclude("deep2.h", "#ifdef WANT_DEEP\nint x;\n#undef WANT_DEEP\n#endif\n");
      addInclude("deep1.h", "#define WANT_DEEP\n#include \"deep2.h\"\n");
      TopDUContext* test1 = parse(QByteArray("#include \"deep1.h\""), DumpNone);
      Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
      DUChainWriteLocker lock(DUChain::lock());
      QVERIFY(envFile1);
      QCOMPARE(envFile1->definedMacroNames().set().count(), 0u);
      QCOMPARE(envFile1->definedMacros().set().count(), 0u);
      QCOMPARE(envFile1->usedMacros().set().count(), 0u);
    }

    addInclude("h1.h", "#ifndef H1_H  \n#define H1_H  \n  class H1 {};\n #else \n class H1_Already_Defined {}; \n#endif");
    addInclude("h1_user.h", "#ifndef H1_USER \n#define H1_USER \n#include \"h1.h\" \nclass H1User {}; \n#endif\n");

    {
      TopDUContext* test1 = parse(QByteArray("#include \"h1.h\" \n#include \"h1_user.h\"\n\nclass Honk {};"), DumpNone);
        //We test here, whether the environment-manager re-combines h1_user.h so it actually contains a definition of class H1.
        //In the version parsed in test1, H1_H was already defined, so the h1.h parsed into h1_user.h was parsed to contain H1_Already_Defined.
        TopDUContext* test2 = parse(QByteArray("#include \"h1_user.h\"\n"), DumpNone);
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(test1->parsingEnvironmentFile());
        QVERIFY(test2->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile1 = dynamic_cast<Cpp::EnvironmentFile*>(test1->parsingEnvironmentFile().data());
        Cpp::EnvironmentFile* envFile2 = dynamic_cast<Cpp::EnvironmentFile*>(test2->parsingEnvironmentFile().data());
        QVERIFY(envFile1);
        QVERIFY(envFile2);

        QCOMPARE(envFile1->usedMacros().set().count(), 0u);
        QCOMPARE(envFile2->usedMacros().set().count(), 0u);
        QVERIFY(findDeclaration( test1, Identifier("H1") ));

      QCOMPARE( envFile1->contentStartLine(), 3 );
    }

    { //Test shadowing of strings through #undefs
      addInclude("stringset_test1.h", "String1 s1;\n#undef String2\n String2 s2;");
      addInclude("stringset_test2.h", "String1 s1;\n#undef String2\n String2 s2;");

      {
        TopDUContext* top = parse(QByteArray("#include \"stringset_test1.h\"\n"), DumpNone);
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(top->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile = dynamic_cast<Cpp::EnvironmentFile*>(top->parsingEnvironmentFile().data());
        QVERIFY(envFile);
        kDebug() << "url" << envFile->url().str();
        QCOMPARE(envFile->usedMacros().set().count(), 0u);
        QCOMPARE(toStringList(envFile->strings()), splitSorted("String1\ns1\ns2")); //The #undef protects String2, so it cannot be affected from outside
      }
      {
        TopDUContext* top = parse(QByteArray("#define String1\n#include \"stringset_test1.h\"\nString2 String1;"), DumpNone); //Both String1 and String2 are shadowed. String1 by the macro, and String2 by the #undef in stringset_test1.h
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(top->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile = dynamic_cast<Cpp::EnvironmentFile*>(top->parsingEnvironmentFile().data());
        QVERIFY(envFile);
        //String1 is shadowed by the macro-definition, so it is not a string that can be affected from outside.
        QCOMPARE(toStringList(envFile->strings()), splitSorted("s1\ns2"));
        QCOMPARE(toStringList(envFile->usedMacroNames()), QStringList()); //No macros from outside were used

        QCOMPARE(envFile->definedMacros().set().count(), 1u);
        QCOMPARE(envFile->usedMacros().set().count(), 0u);

        QCOMPARE(top->importedParentContexts().count(), 1);
        TopDUContext* top2 = dynamic_cast<TopDUContext*>(top->importedParentContexts()[0].context(0));
        QVERIFY(top2);
        Cpp::EnvironmentFile* envFile2 = dynamic_cast<Cpp::EnvironmentFile*>(top2->parsingEnvironmentFile().data());
        QVERIFY(envFile2);

        QCOMPARE(envFile2->definedMacros().set().count(), 0u);

        QCOMPARE(toStringList(envFile2->usedMacroNames()), QStringList("String1")); //stringset_test1.h used the Macro String1 from outside
        QCOMPARE(toStringList(envFile2->strings()), splitSorted("String1\ns1\ns2"));
      }
      {
        TopDUContext* top = parse(QByteArray("#define String1\n#undef String1\n#include \"stringset_test1.h\""), DumpNone);
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(top->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile = dynamic_cast<Cpp::EnvironmentFile*>(top->parsingEnvironmentFile().data());
        QVERIFY(envFile);
        QCOMPARE(envFile->definedMacros().set().count(), 0u);
        QCOMPARE(envFile->usedMacros().set().count(), 0u);
        //String1 is shadowed by the macro-definition, so it is not a string that can be affected from outside.
        kDebug() << toStringList(envFile->strings()) << splitSorted("s1\ns2");
        QCOMPARE(toStringList(envFile->strings()), splitSorted("s1\ns2"));
        QCOMPARE(toStringList(envFile->usedMacroNames()), QStringList()); //No macros from outside were used

        QCOMPARE(top->importedParentContexts().count(), 1);
        TopDUContext* top2 = dynamic_cast<TopDUContext*>(top->importedParentContexts()[0].context(0));
        QVERIFY(top2);
        Cpp::EnvironmentFile* envFile2 = dynamic_cast<Cpp::EnvironmentFile*>(top2->parsingEnvironmentFile().data());
        QVERIFY(envFile2);
        QCOMPARE(envFile2->definedMacros().set().count(), 0u);

        QCOMPARE(toStringList(envFile2->usedMacroNames()), QStringList()); //stringset_test1.h used the Macro String1 from outside. However it is an undef-macro, so it does not appear in usedMacroNames() and usedMacros()
        QCOMPARE(envFile2->usedMacros().set().count(), (unsigned int)0);
        QCOMPARE(toStringList(envFile2->strings()), splitSorted("String1\ns1\ns2"));
      }
      {
        addInclude("usingtest1.h", "#define HONK\nMACRO m\n#undef HONK2\n");

        TopDUContext* top = parse(QByteArray("#define MACRO meh\nint MACRO;\n#include \"usingtest1.h\"\n"), DumpNone);
        DUChainWriteLocker lock(DUChain::lock());
        QVERIFY(top->parsingEnvironmentFile());
        Cpp::EnvironmentFile* envFile = dynamic_cast<Cpp::EnvironmentFile*>(top->parsingEnvironmentFile().data());
        QVERIFY(envFile);
        QCOMPARE(envFile->definedMacros().set().count(), 2u);
        QCOMPARE(envFile->unDefinedMacroNames().set().count(), 1u);
        QCOMPARE(envFile->usedMacros().set().count(), 0u);
        QCOMPARE(envFile->usedMacroNames().set().count(), 0u);

        kDebug() << toStringList(envFile->strings()) ;
        QCOMPARE(envFile->strings().count(), 3u); //meh, m, int

        QCOMPARE(top->importedParentContexts().count(), 1);
        TopDUContext* top2 = dynamic_cast<TopDUContext*>(top->importedParentContexts()[0].context(0));
        QVERIFY(top2);
        Cpp::EnvironmentFile* envFile2 = dynamic_cast<Cpp::EnvironmentFile*>(top2->parsingEnvironmentFile().data());
        QVERIFY(envFile2);
        QCOMPARE(envFile2->definedMacros().set().count(), 1u);
        QCOMPARE(envFile2->unDefinedMacroNames().set().count(), 1u);
        QCOMPARE(envFile2->usedMacros().set().count(), 1u);
        QCOMPARE(envFile2->usedMacroNames().set().count(), 1u);
        kDebug() << toStringList(envFile2->strings()) ;
        QCOMPARE(envFile2->strings().count(), 3u); //meh(from macro), MACRO, m
      }
    }

/*    addInclude( "envmatch_header1.h", "#include \"envmatch_header2.h\"\n class SomeName; #define SomeName SomeAlternativeName" );
    addInclude( "envmatch_header2.h", "#ifndef SOMEDEF\n #define SOMEDEF\n#endif\n" );
    QByteArray method1("#include \"envmatch_header1.h\"");
    QByteArray method2("#include \"envmatch_header1.h\"");
    QByteArray method3("#include \"envmatch_header1.h\"\n#include \"envmatch_header1.h\"");

    DUContext* top1 = parse(method1, DumpNone);
    DUContext* top2 = parse(method1, DumpNone);
    DUContext* top3 = parse(method1, DumpNone);

    DUChainWriteLocker lock(DUChain::lock());

    QCOMPARE(top1->importedParentContexts().count(), 1);
    QCOMPARE(top2->importedParentContexts().count(), 1);
//     QCOMPARE(top3->importedParentContexts().count(), 2);

    QCOMPARE(top1->importedParentContexts()[0], top2->importedParentContexts()[1]);*/
}

void TestCppCodeCompletion::testPreprocessor() {
  TEST_FILE_PARSE_ONLY
  
  IncludeFileList includes;
  
  {
    QString a = "#define Q(c) c; char* q = #c; \n Q(int i;\n char* c = \"a\";)\n";
    QString preprocessed = preprocess(HashedString(), a, includes);  
    kDebug() << "preprocessed:" << preprocessed;
    QVERIFY(preprocessed.contains("\"int i;\\n char* c = \\\"a\\\";")); //The newline must have been escaped correctly, and the string as well
    TopDUContext* top = parse(a.toLocal8Bit(), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    kDebug() << top->localDeclarations()[0]->identifier().toString();
    QCOMPARE(top->localDeclarations().count(), 3);
    QCOMPARE(top->localDeclarations()[0]->range().start.line, 1);
    QCOMPARE(top->localDeclarations()[1]->range().start.line, 2);
    QCOMPARE(top->localDeclarations()[2]->range().start.line, 2);
  }
  {
    QString a = "#define Q(c) c ## ULL \n void test() {int i = Q(0x5);}";
    QString preprocessed = preprocess(HashedString(), a, includes);  
    kDebug() << "preprocessed:" << preprocessed;
    TopDUContext* top = parse(a.toLocal8Bit(), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 2);
    QCOMPARE(top->childContexts()[1]->localDeclarations().count(), 1);
  }
  {
    QString a = "#define MA(x) T<x> a\n #define MB(x) T<x>\n #define MC(X) int\n #define MD(X) c\n template <typename P1> struct A {}; template <typename P2> struct T {}; int main(int argc, char ** argv) { MA(A<int>); A<MB(int)> b; MC(a)MD(b); MC(a)d; }";
    QString preprocessed = preprocess(HashedString(), a, includes);  
    kDebug() << "preprocessed:" << preprocessed;
    TopDUContext* top = parse(a.toUtf8(), DumpAll);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 6);
    QCOMPARE(top->childContexts()[5]->localDeclarations().count(), 4);
  }
    #ifdef TEST_MACRO_EXPANSION_ORDER
    //Not working yet
  {//No macro-expansion should happen on the first layer of a macro-call
  QString preprocessed = preprocess(HashedString(), "#define VAL_KIND A \n#define DO_CAT_I(a, b) a ## b \n#define DO_CAT(a, b) DO_CAT_I(a, b) \nint DO_CAT(Value_, VAL_KIND); \nint DO_CAT_I(Value_, VAL_KIND);\n int VAL_KIND;\nint DO_CAT(VAL_KIND, _Value);\nint DO_CAT(VAL_KIND, _Value);\n", includes);
  kDebug() << preprocessed;
    TopDUContext* top = parse(preprocessed.toUtf8(), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 5);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("Value_A"));
    QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("Value_VAL_KIND"));
    QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("A"));
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("A_Value"));
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("VAL_KIND_Value"));
  }
  #endif
  {//Test macro redirection
    QString test = preprocess(HashedString(), "#define M1(X) X ## _m1 \n#define M2(X) M ## X\n#define M3 M2\n#define M4 M3 \nM4(1)(hallo)", includes);
    kDebug() << test;
    QCOMPARE(test.trimmed(), QString("hallo_m1"));
  }
  {//Test replacement of merged preprocessor function calls
    TopDUContext* top = parse(QByteArray("#define MACRO_1(X) X ## _fromMacro1 \n#define A(pred, n) MACRO_ ## n(pred) \n#define D(X,Y) A(X ## Y, 1) \nint D(a,ba);"), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("aba_fromMacro1"));
  }
  {//Test merging
    TopDUContext* top = parse(QByteArray("#define D(X,Y) X ## Y \nint D(a,ba);"), DumpNone);
    IncludeFileList includes;
    kDebug() << preprocess(HashedString("somefile"), "#define D(X,Y) X ## Y \nint D(a,ba);", includes);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("aba"));
  }
  {
    TopDUContext* top = parse(QByteArray("#define MERGE(a, b) a ## b \n#define MERGE_WITH_PARENS(par) MERGE ## par \nint MERGE_WITH_PARENS((int, B));"), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("intB"));
  }
  {//Test simple #if
    TopDUContext* top = parse(QByteArray("#define X\n#if defined(X)\nint xDefined;\n#endif\n#if !defined(X)\nint xNotDefined;\n#endif\n#if (!defined(X))\nint xNotDefined2;\n#endif"), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("xDefined"));
  }
  {//Test simple #if
    TopDUContext* top = parse(QByteArray("#if defined(X)\nint xDefined;\n#endif\n#if !defined(X)\nint xNotDefined;\n#endif\n#if (!defined(X))\nint xNotDefined2;\n#endif"), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QVERIFY(top->localDeclarations().count() >= 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("xNotDefined"));
    QCOMPARE(top->localDeclarations().count(), 2);
    QCOMPARE(top->localDeclarations()[1]->identifier(), Identifier("xNotDefined2"));
  }
  {//Test multi-line definitions
    TopDUContext* top = parse(QByteArray("#define X \\\nint i;\\\nint o;\nX;\n"), DumpNone);
    IncludeFileList includes;
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 2);
  }
  {//Test multi-line definitions
    TopDUContext* top = parse(QByteArray("#define THROUGH_DEFINE(X) X\nclass B {\nclass C{\n};\nC* cPcPcPcPcPcPcPcPcP;\n};\nB* bP;\nvoid test() {\nTHROUGH_DEFINE(bP->cPcPcPcPcPcPcPcPcP);\n}\n"), DumpNone);
    IncludeFileList includes;
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->childContexts().count(), 3);
    QCOMPARE(top->childContexts()[0]->localDeclarations().count(), 2);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().size(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->count(), 1);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->at(0).start.column, 19);
    QCOMPARE(top->childContexts()[0]->localDeclarations()[1]->uses().begin()->at(0).end.column, 37);
  }
  {//Test merging
    TopDUContext* top = parse(QByteArray("#define D(X,Y) X ## Y \nint D(a,ba);"), DumpNone);
    IncludeFileList includes;
    kDebug() << preprocess(HashedString("somefile"), "#define D(X,Y) X ## Y \nint D(a,ba);", includes);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("aba"));
  }
  {//Test merging
    TopDUContext* top = parse(QByteArray("#define A(x) int x;\n#define B(name) A(bo ## name)\nB(Hallo)"), DumpNone);
    IncludeFileList includes;
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().count(), 1);
    QCOMPARE(top->localDeclarations()[0]->identifier(), Identifier("boHallo"));
  }
}

void TestCppCodeCompletion::testArgumentList()
{
  QMap<QByteArray, QString> codeToArgList;
  codeToArgList.insert("void foo(int arg[]){}", "(int arg[])");
  codeToArgList.insert("void foo(int arg[1]){}", "(int arg[1])");
  codeToArgList.insert("void foo(int arg[][1]){}", "(int arg[][1])");
  codeToArgList.insert("void foo(int arg[1][1]){}", "(int arg[1][1])");
  codeToArgList.insert("void foo(int arg[][1][1]){}", "(int arg[][1][1])");
  QMap< QByteArray, QString >::const_iterator it = codeToArgList.constBegin();
  while (it != codeToArgList.constEnd()){
    qDebug() << "input function is:" << it.key();
    TopDUContext* top = parse(it.key(), DumpNone);
    DUChainWriteLocker lock(DUChain::lock());
    QCOMPARE(top->localDeclarations().size(), 1);

    CompletionItemTester complCtx(top, "");
    Cpp::NormalDeclarationCompletionItem item(DeclarationPointer(top->localDeclarations().first()), KSharedPtr<KDevelop::CodeCompletionContext>(complCtx.completionContext.data()));
    QString ret;
    Cpp::createArgumentList(item, ret, 0);
    QCOMPARE(ret, it.value());

    release(top);

    ++it;
  }
}

void TestCppCodeCompletion::testStaticMethods()
{
  QByteArray code("struct A {\n"
                  "  public: static void myPublicStatic() {}\n"
                  "  protected: static void myProtectedStatic() {}\n"
                  "  private: static void myPrivateStatic() {}\n"
                  "  public: void myPublicNonStatic() {}\n"
                  "  protected: void myProtectedNonStatic() {}\n"
                  "  private: void myPrivateNonStatic() {}\n"
                  "};\n"
                  "A a; int main() { return 0;}");
  TopDUContext* top = parse(code, DumpNone);
  DUChainWriteLocker lock(DUChain::lock());
  QVERIFY(top->problems().isEmpty());
  QCOMPARE(top->childContexts().size(), 3);
  {
    // in body of main
    CompletionItemTester complCtx(top->childContexts().last(), "a.");
    QVERIFY(complCtx.completionContext->isValid());
    QCOMPARE(complCtx.names, QStringList() << "myPublicStatic" << "myPublicNonStatic");
  }
  {
    // in body of main
    CompletionItemTester complCtx(top->childContexts().last(), "A::");
    QVERIFY(complCtx.completionContext->isValid());
    QEXPECT_FAIL("", "non-static functions don't get filtered. comment in context.cpp: ///@todo what NOT to show on static member choose? Actually we cannot hide all non-static functions, because of function-pointers", Continue);
    QCOMPARE(complCtx.names, QStringList() << "myPublicStatic"); // fails and gets skipped
    // this is a fallback to verify the current behavior
    QCOMPARE(complCtx.names, QStringList() << "myPublicStatic" << "myPublicNonStatic");
  }
  {
    // in body of myPrivate
    CompletionItemTester complCtx(top->childContexts().first()->childContexts().last(), "this->");
    QVERIFY(complCtx.completionContext->isValid());
    QCOMPARE(complCtx.names, QStringList() << "myPublicStatic" << "myProtectedStatic" << "myPrivateStatic" << "myPublicNonStatic" << "myProtectedNonStatic" << "myPrivateNonStatic");
  }
  {
    // in body of myPrivate
    CompletionItemTester complCtx(top->childContexts().first()->childContexts().last(), "A::");
    QVERIFY(complCtx.completionContext->isValid());
    QEXPECT_FAIL("", "non-static functions don't get filtered. comment in context.cpp: ///@todo what NOT to show on static member choose? Actually we cannot hide all non-static functions, because of function-pointers", Continue);
    QCOMPARE(complCtx.names, QStringList() << "myPublicStatic" << "myProtectedStatic" << "myPrivateStatic"); // fails and gets skipped
    // this is a fallback to verify the current behavior
    QCOMPARE(complCtx.names, QStringList() << "myPublicStatic" << "myProtectedStatic" << "myPrivateStatic" << "myPublicNonStatic" << "myProtectedNonStatic" << "myPrivateNonStatic");
  }
  release(top);
}


class TestPreprocessor : public rpp::Preprocessor
{
public:

  TestCppCodeCompletion* cc;
  IncludeFileList& included;
  rpp::pp* pp;
  bool stopAfterHeaders;
  Cpp::EnvironmentFilePointer environmentFile;

  TestPreprocessor( TestCppCodeCompletion* _cc, IncludeFileList& _included, bool _stopAfterHeaders ) : cc(_cc), included(_included), pp(0), stopAfterHeaders(_stopAfterHeaders) {
  }

  rpp::Stream* sourceNeeded(QString& fileName, rpp::Preprocessor::IncludeType type, int sourceLine, bool skipCurrentPath)
  {
    QMap<QString,QString>::const_iterator it = cc->fakeIncludes.constFind(fileName);
    if( it != cc->fakeIncludes.constEnd() || !pp ) {
      kDebug(9007) << "parsing included file \"" << fileName << "\"";
      included << LineContextPair( dynamic_cast<TopDUContext*>(cc->parse( (*it).toUtf8(), TestCppCodeCompletion::DumpNone, pp, KUrl(it.key()))), sourceLine );
    } else {
      kDebug(9007) << "could not find include-file \"" << fileName << "\"";
    }
    return 0;
  }

  void setPp( rpp::pp* _pp ) {
    pp = _pp;
  }

  virtual void headerSectionEnded(rpp::Stream& stream) {
    if( environmentFile )
      environmentFile->setContentStartLine( stream.originalInputPosition().line );
    if(stopAfterHeaders)
      stream.toEnd();
  }
  
  virtual void foundHeaderGuard(rpp::Stream& stream, KDevelop::IndexedString guardName) {
    environmentFile->setHeaderGuard(guardName);
  }
};

QString TestCppCodeCompletion::preprocess( const HashedString& url, const QString& text, IncludeFileList& included, rpp::pp* parent, bool stopAfterHeaders, KSharedPtr<Cpp::EnvironmentFile>* paramEnvironmentFile, rpp::LocationTable** returnLocationTable, PreprocessedContents* targetContents ) {
  TestPreprocessor ppc( this, included, stopAfterHeaders );


    rpp::pp preprocessor(&ppc);
    ppc.setPp( &preprocessor );

    KSharedPtr<Cpp::EnvironmentFile> environmentFile;
    if( paramEnvironmentFile && *paramEnvironmentFile )
      environmentFile = *paramEnvironmentFile;
    else
      environmentFile = Cpp::EnvironmentFilePointer( new Cpp::EnvironmentFile( IndexedString(url.str()), 0 ) );

  ppc.environmentFile = environmentFile;

    if( paramEnvironmentFile )
      *paramEnvironmentFile = environmentFile;

    CppPreprocessEnvironment* currentEnvironment = new CppPreprocessEnvironment( &preprocessor, environmentFile );
    preprocessor.setEnvironment( currentEnvironment );
    currentEnvironment->setEnvironmentFile( environmentFile );

    if( parent )
      preprocessor.environment()->swapMacros(parent->environment());
    else
      currentEnvironment->merge(CppUtils::standardMacros());

    PreprocessedContents contents = preprocessor.processFile(url.str(), text.toUtf8());
    if(targetContents)
      *targetContents = contents;

    QString result = QString::fromUtf8(stringFromContents(contents));

    if (returnLocationTable)
      *returnLocationTable = preprocessor.environment()->takeLocationTable();

    currentEnvironment->finishEnvironment();

    if( parent ) {
      preprocessor.environment()->swapMacros(parent->environment());
      static_cast<CppPreprocessEnvironment*>(parent->environment())->environmentFile()->merge(*environmentFile);
    }

    return result;
}

TopDUContext* TestCppCodeCompletion::parse(const QByteArray& unit, DumpAreas dump, rpp::pp* parent, KUrl _identity, TopDUContext* update)
{
  if (dump)
    kDebug(9007) << "==== Beginning new test case...:" << endl << unit;

  ParseSession* session = new ParseSession();
   ;

  static int testNumber = 0;
  HashedString url(QString("file:///internal/%1").arg(testNumber++));
  if( !_identity.isEmpty() )
      url = _identity.pathOrUrl();

   IncludeFileList included;
   QList<DUContext*> temporaryIncluded;

  rpp::LocationTable* locationTable;

  Cpp::EnvironmentFilePointer file;

  PreprocessedContents contents;

  preprocess( url, QString::fromUtf8(unit), included, parent, false, &file, &locationTable, &contents ).toUtf8();

  session->setContents( contents, locationTable );

    if( parent ) {
      //Temporarily insert all files parsed previously by the parent, so forward-declarations can be resolved etc.
      TestPreprocessor* testPreproc = dynamic_cast<TestPreprocessor*>(parent->preprocessor());
      if( testPreproc ) {
        foreach( LineContextPair include, testPreproc->included ) {
          if( !containsContext( included, include.context ) ) {
            included.push_front( include );
            temporaryIncluded << include.context;
          }
        }
      } else {
        kDebug(9007) << "PROBLEM";
      }
    }

  Parser parser(&control);
  TranslationUnitAST* ast = parser.parse(session);
  ast->session = session;

  if (dump & DumpAST) {
    kDebug(9007) << "===== AST:";
    cppDumper.dump(ast, session);
  }

  DeclarationBuilder definitionBuilder(session);

  TopDUContext* top = definitionBuilder.buildDeclarations(file, ast, &included, ReferencedTopDUContext(update));

  UseBuilder useBuilder(session);
  useBuilder.buildUses(ast);
  foreach(KDevelop::ProblemPointer problem, useBuilder.problems()) {
    DUChainWriteLocker lock(DUChain::lock());
    top->addProblem(problem);
  }

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

  if( parent ) {
    //Remove temporarily inserted files parsed previously by the parent
    DUChainWriteLocker lock(DUChain::lock());
    TestPreprocessor* testPreproc = dynamic_cast<TestPreprocessor*>(parent->preprocessor());
    if( testPreproc ) {
      foreach( DUContext* context, temporaryIncluded )
        top->removeImportedParentContext( context );
    } else {
      kDebug(9007) << "PROBLEM";
    }
  }


  if (dump)
    kDebug(9007) << "===== Finished test case.";

  delete session;

  return top;
}

#include "test_cppcodecompletion.moc"
