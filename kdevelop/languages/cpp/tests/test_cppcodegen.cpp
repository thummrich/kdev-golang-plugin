/*
   This file is part of KDevelop
   Copyright 2009 Ramon Zarazua <killerfox512+kde@gmail.com>
   Copyright 2009 David Nolden <david.nolden.kdevelop@art-master.de>
   
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

#include "test_cppcodegen.h"

#include "parsesession.h"
#include "ast.h"
#include "astutilities.h"
#include "codegen/cppnewclass.h"
//#include "codegen/makeimplementationprivate.h"

#include <language/backgroundparser/backgroundparser.h>
#include <language/codegen/coderepresentation.h>
#include <language/codegen/documentchangeset.h>
#include <language/duchain/duchain.h>  
#include <language/duchain/duchainlock.h>

#include <interfaces/ilanguagecontroller.h>

#include <QtTest/QTest>
#include <shell/shellextension.h>
#include <tests/autotestshell.h>
#include <tests/testcore.h>
#include <qtest_kde.h>
#include <language/codecompletion/codecompletiontesthelper.h>
#include <dumpchain.h>
#include <language/duchain/dumpchain.h>
#include <language/duchain/parsingenvironment.h>
#include <language/duchain/codemodel.h>
#include <typeinfo>
#include <language/duchain/classdeclaration.h>
#include <cppducontext.h>
#include <interfaces/iassistant.h>

QTEST_KDEMAIN(TestCppCodegen, GUI )

using namespace KDevelop;

void TestCppCodegen::initTestCase()
{
  //Initialize KDevelop components
  AutoTestShell::init();
  Core::initialize(0, KDevelop::Core::NoUi);
  
  Core::self()->languageController()->backgroundParser()->setDelay(1);
  
  CodeRepresentation::setDiskChangesForbidden(true);
}

void TestCppCodegen::cleanupTestCase()
{
  Core::self()->cleanup();
}

void dumpAST(InsertIntoDUChain& code)
{
    DUChainReadLocker lock;
    
    Q_ASSERT(!code->parsingEnvironmentFile()->isProxyContext());
    
    ParseSession::Ptr session = ParseSession::Ptr::dynamicCast<IAstContainer>(code->ast());
    Q_ASSERT(session);
    Cpp::DumpChain dump;
    dump.dump(session->topAstNode(), session.data());
}

void dumpDUChain(InsertIntoDUChain& code)
{
    DUChainReadLocker lock;
    
    KDevelop::DumpChain dump;
    dump.dump(code.topContext());
}


void TestCppCodegen::testAssistants()
{
  {
    //A missing-declaration assistant should be added, that creates a declaration "Honk val = ..."
    InsertIntoDUChain code("test_assistants.cpp", "enum Honk { Hank };\nvoid test() {\n val = Hank;\n }\n");
    kWarning() << "********************* Parsing step 1";
    code.parse(TopDUContext::AllDeclarationsContextsUsesAndAST);
    
    DUChainReadLocker lock;

    QCOMPARE(code->problems().size(), 2);
    //There is one problem from the include-path resolver as it couldn't resolve include paths for the artificial code
    //The second problem is the missing-declaration assistant problem
    QVERIFY(code->problems()[1]->solutionAssistant());
    QCOMPARE(code->problems()[1]->solutionAssistant()->actions().size(), 1);
    code->problems()[1]->solutionAssistant()->actions()[0]->execute();
    
    //Make sure the assistant has inserted the correct solution
    kDebug() << code.m_insertedCode.text();
    QVERIFY(code.m_insertedCode.text().contains("Honk val = Hank;"));
  }  
}

void TestCppCodegen::testSimplifiedUpdating()
{
  {
    InsertIntoDUChain code1("duchaintest_1.h", "struct A { struct Member2; struct Member1; };");
    InsertIntoDUChain code3("duchaintest_3.h", "#include <duchaintest_1.h>\n struct C : public A { Member1 m1; Member2 m2; };");
    kWarning() << "********************* Parsing step 1";
    code3.parse(TopDUContext::AllDeclarationsContextsUsesAndAST);
    
    DUChainReadLocker lock;

    QCOMPARE(code3->localDeclarations().size(), 1);
    QCOMPARE(code3->childContexts().size(), 1);
    QCOMPARE(code3->childContexts()[0]->localDeclarations().size(), 2);
    QVERIFY(code3->childContexts()[0]->localDeclarations()[0]->abstractType().cast<StructureType>());
    QVERIFY(code3->childContexts()[0]->localDeclarations()[1]->abstractType().cast<StructureType>());
    QCOMPARE(code3->childContexts()[0]->importedParentContexts().size(), 1);
    QCOMPARE(code1->childContexts().size(), 1);
  }  
  {
    InsertIntoDUChain code1("duchaintest_1.h", "struct A { struct Member1; };");
    InsertIntoDUChain code2("duchaintest_2.h", "template<class T> struct B : public T{ struct Member2; };");
    InsertIntoDUChain code3("duchaintest_3.h", "#include <duchaintest_2.h>\n #include <duchaintest_1.h>\n struct C : public B<A> { Member1 m1; Member2 m2; };");
    kWarning() << "********************* Parsing step 1";
    code3.parse(TopDUContext::AllDeclarationsContextsUsesAndAST);
    
    DUChainReadLocker lock;

    QCOMPARE(code3->localDeclarations().size(), 1);
    QCOMPARE(code3->childContexts().size(), 1);
    QCOMPARE(code3->childContexts()[0]->localDeclarations().size(), 2);
    QVERIFY(code3->childContexts()[0]->localDeclarations()[0]->abstractType().cast<StructureType>());
    QVERIFY(code3->childContexts()[0]->localDeclarations()[1]->abstractType().cast<StructureType>());
    QCOMPARE(code3->childContexts()[0]->importedParentContexts().size(), 1);
    QCOMPARE(code1->childContexts().size(), 1);
    
    QCOMPARE(code2->localDeclarations().size(), 1);
    ClassDeclaration* BClass = dynamic_cast<ClassDeclaration*>(code2->localDeclarations()[0]);
    QVERIFY(BClass);
    QCOMPARE(BClass->baseClassesSize(), 1u);
    QCOMPARE(BClass->baseClasses()[0].baseClass.abstractType()->toString(), QString("T"));
    
    QCOMPARE(code3->childContexts()[0]->importedParentContexts().size(), 1);
    
    DUContext* BAContext = code3->childContexts()[0]->importedParentContexts()[0].context(code3.topContext());
    QVERIFY(BAContext);
    QVERIFY(!BAContext->inSymbolTable());
    
    //2 contexts are imported: The template-context and the parent-class context
    QCOMPARE(BAContext->importedParentContexts().size(), 2);
    QCOMPARE(BAContext->importedParentContexts()[1].context(code3.topContext()), code1->childContexts()[0]);
    ClassDeclaration* classDecl = dynamic_cast<ClassDeclaration*>(BAContext->owner());
    QVERIFY(classDecl);
    QCOMPARE(classDecl->baseClassesSize(), 1u);
    QCOMPARE(classDecl->baseClasses()[0].baseClass.index(), code1->localDeclarations()[0]->indexedType().index());
    
    lock.unlock();
    kWarning() << "********************* Parsing step 2";
    code3.parse(TopDUContext::AllDeclarationsContextsUsesAndAST | TopDUContext::ForceUpdateRecursive, true);
    
    lock.lock();
    QCOMPARE(code3->localDeclarations().size(), 1);
    QCOMPARE(code3->childContexts().size(), 1);
    QCOMPARE(code3->childContexts()[0]->localDeclarations().size(), 2);
    QCOMPARE(code1->childContexts().size(), 1);

    QVERIFY(code3->childContexts()[0]->localDeclarations()[0]->abstractType().cast<StructureType>());
    QVERIFY(code3->childContexts()[0]->localDeclarations()[1]->abstractType().cast<StructureType>());
    
    //BClass should have been updated, not deleted
    QVERIFY(BClass == dynamic_cast<ClassDeclaration*>(code2->localDeclarations()[0]));
    QCOMPARE(BClass->baseClassesSize(), 1u);
    QCOMPARE(BClass->baseClasses()[0].baseClass.abstractType()->toString(), QString("T"));

    //The template-instantiation context "B<A>" should have been deleted
    DUContext* BAContext2 = code3->childContexts()[0]->importedParentContexts()[0].context(code3.topContext());
//     kDebug() << "BAContexts" << BAContext << BAContext2;
//     QVERIFY(BAContext != BAContext2);
    QCOMPARE(BAContext2->importedParentContexts().size(), 2);
    QCOMPARE(BAContext2->importedParentContexts()[1].context(code3.topContext()), code1->childContexts()[0]);
    
    classDecl = dynamic_cast<ClassDeclaration*>(BAContext2->owner());
    QVERIFY(classDecl);
    QCOMPARE(classDecl->baseClassesSize(), 1u);
    QCOMPARE(classDecl->baseClasses()[0].baseClass.index(), code1->localDeclarations()[0]->indexedType().index());
  }
  {
    InsertIntoDUChain code1("duchaintest_1.h", "struct A { struct Member1; };");
    InsertIntoDUChain code2("duchaintest_2.h", "template<class T> struct B : public T{ struct Member2; };");
    InsertIntoDUChain code3("duchaintest_3.h", "#include <duchaintest_2.h>\n #include <duchaintest_1.h>\n typedef B<A> Parent; struct C : public Parent { Member1 m1; Member2 m2; };");
    
    code3.parse(TopDUContext::AllDeclarationsContextsUsesAndAST);
    
    DUChainReadLocker lock;

    QCOMPARE(code3->localDeclarations().size(), 2);
    QCOMPARE(code3->childContexts().size(), 1);
    QCOMPARE(code3->childContexts()[0]->localDeclarations().size(), 2);
    QVERIFY(code3->childContexts()[0]->localDeclarations()[0]->abstractType().cast<StructureType>());
    QVERIFY(code3->childContexts()[0]->localDeclarations()[1]->abstractType().cast<StructureType>());
    
    lock.unlock();
    code3.parse(TopDUContext::AllDeclarationsContextsUsesAndAST | TopDUContext::ForceUpdateRecursive, true);
    
    lock.lock();
    QCOMPARE(code3->localDeclarations().size(), 2);
    QCOMPARE(code3->childContexts().size(), 1);
    QCOMPARE(code3->childContexts()[0]->localDeclarations().size(), 2);
    QVERIFY(code3->childContexts()[0]->localDeclarations()[0]->abstractType().cast<StructureType>());
    QVERIFY(code3->childContexts()[0]->localDeclarations()[1]->abstractType().cast<StructureType>());
  }
  
  {
    QString text = "class C { class D d; };";
    
    InsertIntoDUChain code("testsimplified.cpp", text);
    
    code.parse(TopDUContext::AllDeclarationsContextsUsesAndAST);
    
    DUChainReadLocker lock;

    dumpAST(code);
    dumpDUChain(code);
    //The forward-declaration of 'D' is forwarded into the top-context
    QCOMPARE(code->localDeclarations().size(), 2);
    Declaration* classDecl = code->localDeclarations()[0];
    QCOMPARE(code->childContexts().size(), 1);
    QCOMPARE(code->childContexts()[0]->localDeclarations().size(), 1);
    QVERIFY(code->childContexts()[0]->localDeclarations()[0]->abstractType().cast<StructureType>());
    QCOMPARE(code->childContexts()[0]->localDeclarations()[0]->abstractType()->toString(), QString("D"));
    
    lock.unlock();
    
    code.parse(TopDUContext::AllDeclarationsContextsUsesAndAST | TopDUContext::ForceUpdate, true);
    lock.lock();
    
    dumpDUChain(code);
    
    QCOMPARE(code->localDeclarations().size(), 2);
    
    //Verify that an update has happened, rather than recreating everything
    QCOMPARE(code->localDeclarations()[0], classDecl);
    
    QCOMPARE(code->childContexts().size(), 1);
    QCOMPARE(code->childContexts()[0]->localDeclarations().size(), 1);
    QVERIFY(code->childContexts()[0]->localDeclarations()[0]->abstractType().cast<StructureType>());
    QCOMPARE(code->childContexts()[0]->localDeclarations()[0]->abstractType()->toString(), QString("D"));
  }
  
  {
    QString text = "class C {int test(); int mem; }; void test(int a); int i;";
    
    InsertIntoDUChain code("testsimplified.cpp", text);
    
    code.parse(TopDUContext::SimplifiedVisibleDeclarationsAndContexts | TopDUContext::AST);
    
    DUChainReadLocker lock;

    dumpAST(code);
    dumpDUChain(code);
    
    QCOMPARE(code->localDeclarations().size(), 3);
    QCOMPARE(code->childContexts().size(), 1);
    QCOMPARE(code->childContexts()[0]->localDeclarations().size(), 2);
    QVERIFY(!code->childContexts()[0]->localDeclarations()[0]->abstractType());
    QVERIFY(!code->childContexts()[0]->localDeclarations()[1]->abstractType());
    //In simplified parsing mode, the type should not have been built
    QVERIFY(!code->localDeclarations()[0]->abstractType());
    QVERIFY(code->localDeclarations()[0]->kind() == Declaration::Type);
    QVERIFY(!code->localDeclarations()[1]->abstractType());
    QVERIFY(code->localDeclarations()[1]->kind() == Declaration::Instance);
    QVERIFY(!code->localDeclarations()[2]->abstractType());
    QVERIFY(code->localDeclarations()[2]->kind() == Declaration::Instance);
    
    {
      uint count;
      const KDevelop::CodeModelItem* items;
      
      KDevelop::CodeModel::self().items(code->url(), count, items);
      for(uint a = 0; a < count; ++a) {
        if(items[a].id == code->localDeclarations()[0]->qualifiedIdentifier()) {
          QVERIFY(items[a].kind & KDevelop::CodeModelItem::Class);
        }
      }
    }
  }
  
  {
    InsertIntoDUChain codeA("A.h", "#ifndef A_H\n #define A_H\n class A{}; \n#endif");
    InsertIntoDUChain codeB("B.h", "#include <A.h>\n class B{};");
    
    codeB.parse(TopDUContext::SimplifiedVisibleDeclarationsAndContexts | TopDUContext::AST | TopDUContext::Recursive);
    
    DUChainReadLocker lock;

    //This is not only for debug-output, but also verifies that the AST is there as requesed
    dumpAST(codeA);

    QCOMPARE(codeB->importedParentContexts().size(), 1);
    QCOMPARE(codeA->localDeclarations().size(), 1);
    QVERIFY(codeA->parsingEnvironmentFile()->featuresSatisfied((TopDUContext::Features)(TopDUContext::SimplifiedVisibleDeclarationsAndContexts | TopDUContext::AST | TopDUContext::Recursive)));
    QVERIFY(codeB->parsingEnvironmentFile()->featuresSatisfied((TopDUContext::Features)(TopDUContext::SimplifiedVisibleDeclarationsAndContexts | TopDUContext::AST | TopDUContext::Recursive)));
    
    lock.unlock();

    //Update with more features
    codeB.parse(TopDUContext::AllDeclarationsContextsUsesAndAST | TopDUContext::Recursive, true);
    
    lock.lock();
    QCOMPARE(codeB->importedParentContexts().size(), 1);
    QCOMPARE(codeA->localDeclarations().size(), 1);
    QVERIFY(codeA->parsingEnvironmentFile()->featuresSatisfied((TopDUContext::Features)(TopDUContext::AllDeclarationsContextsUsesAndAST | TopDUContext::Recursive)));
    QVERIFY(codeB->parsingEnvironmentFile()->featuresSatisfied((TopDUContext::Features)(TopDUContext::AllDeclarationsContextsUsesAndAST | TopDUContext::Recursive)));
  }
  {
    ///Test whether "empty" files work
    InsertIntoDUChain codeA("Q.h", "");
    InsertIntoDUChain codeB("B.h", "#include <Q.h>\n class B{};");
    
    codeB.parse(TopDUContext::SimplifiedVisibleDeclarationsAndContexts | TopDUContext::AST | TopDUContext::Recursive);
    
    DUChainReadLocker lock;

    QCOMPARE(codeB->importedParentContexts().size(), 1);
    QVERIFY(codeA->localDeclarations().isEmpty());
    QVERIFY(!codeA->parsingEnvironmentFile()->isProxyContext());
  }
  {
    ///Test the 'ignoring' of header-guards
    InsertIntoDUChain codeA("A.h", "#ifndef A_H\n #define A_H\n class A{};\n #ifdef HONK\n class Honk {};\n #endif\n \n#endif \n");
    InsertIntoDUChain codeB("B.h", "#define A_H\n \n #include <A.h>\n class B{};");
    
    QVERIFY(!codeA.tryGet());
    
    codeB.parse(TopDUContext::SimplifiedVisibleDeclarationsAndContexts | TopDUContext::AST | TopDUContext::Recursive);
    
    DUChainReadLocker lock;

    //This is not only for debug-output, but also verifies that the AST is there as requesed
    dumpAST(codeA);
    
    QCOMPARE(codeA->localDeclarations().size(), 1);
    QCOMPARE(codeB->importedParentContexts().size(), 1);
    
    lock.unlock();
    
    codeB.parse(TopDUContext::SimplifiedVisibleDeclarationsAndContexts | TopDUContext::AST | TopDUContext::ForceUpdateRecursive | TopDUContext::Recursive);
    
    lock.lock();
    
    QCOMPARE(codeA->localDeclarations().size(), 1);
    QCOMPARE(codeB->importedParentContexts().size(), 1);
  }
}

void TestCppCodegen::testAstDuChainMapping()
{
  {
    InsertIntoDUChain code("ClassA.h", "class ClassA { public: ClassA(); private: int i;  float f, j;\
                                                        struct ContainedStruct { int i; ClassA * p;  } structVar; };");
    code.parse(TopDUContext::AllDeclarationsContextsUsesAndAST);

    DUChainReadLocker lock;
    
    //----ClassA.h----
    ParseSession::Ptr session = ParseSession::Ptr::dynamicCast<IAstContainer>(code->ast());
    QVERIFY(session);
    TranslationUnitAST * ast = session->topAstNode();
    QVERIFY(ast);
    QVERIFY(ast->declarations);
    QCOMPARE(ast->declarations->count(), 1);
    
    QVERIFY(AstUtils::childNode<SimpleDeclarationAST>(ast, 0));
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(ast, 0)->type_specifier,
              session->astNodeFromDeclaration(code->localDeclarations()[0]));
    //ClassA

    ClassSpecifierAST * classAst = AstUtils::node_cast<ClassSpecifierAST>
                                  (AstUtils::childNode<SimpleDeclarationAST>(ast, 0)->type_specifier);
    QVERIFY(classAst);
    
    DUContext * cont = code->localDeclarations()[0]->internalContext();
    QVERIFY(cont);
    
    QCOMPARE(classAst->member_specs->count(), 6);
    QCOMPARE(cont->localDeclarations().size(), 6);
    
    //ClassA()
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(classAst, 1),
             session->astNodeFromDeclaration(cont->localDeclarations()[0]));
    //int i         
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(classAst, 3),
             session->astNodeFromDeclaration(cont->localDeclarations()[1]));
    //float f         
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(classAst, 4),
             session->astNodeFromDeclaration(cont->localDeclarations()[2]));
    //float j, part of the same declaration as above        
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(classAst, 4),
             session->astNodeFromDeclaration(cont->localDeclarations()[3]));
    //struct ContainedStruct
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(classAst, 5)->type_specifier,
             session->astNodeFromDeclaration(cont->localDeclarations()[4]));
    //ContainedStruct structVar, part of the same declaration as above         
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(classAst, 5),
             session->astNodeFromDeclaration(cont->localDeclarations()[5]));
  }

  {
    //----ClassA.cpp----
    
    InsertIntoDUChain codeH("ClassA.h", "class ClassA { public: ClassA(); private: int i;  float f, j;\
                                                        struct ContainedStruct { int i; ClassA * p;  } structVar; };");
    
    InsertIntoDUChain code("ClassA.cpp", "#include<ClassA.h> \n ClassA::ClassA() : i(0), j(0.0) {structVar.i = 0; ContainedStruct testStruct; }");
    code.parse(TopDUContext::AllDeclarationsContextsUsesAndAST);

    DUChainReadLocker lock;
    
    ParseSession::Ptr session = ParseSession::Ptr::dynamicCast<IAstContainer>(code->ast());
    QVERIFY(session);
    TranslationUnitAST * ast = session->topAstNode();
    QVERIFY(ast);
    QVERIFY(ast->declarations);
    QCOMPARE(ast->declarations->count(), 1);
    
    QVERIFY(AstUtils::childNode<FunctionDefinitionAST>(ast, 0));
    QCOMPARE(AstUtils::childNode<FunctionDefinitionAST>(ast, 0),
              session->astNodeFromDeclaration(KDevelop::DeclarationPointer(code->localDeclarations()[0])));
    QCOMPARE(code->localDeclarations()[0]->context()->importedParentContexts().size(), 1);
    QVERIFY(code->localDeclarations()[0]->context()->importedParentContexts()[0].context(code.m_topContext) != code.m_topContext);
  }
  
  
  {
    InsertIntoDUChain code("AbstractClass.h", "class AbstractClass { public: virtual ~AbstractClass();\
                                                       virtual void pureVirtual() = 0; virtual const int constPure(const int &) const = 0; \
                                                       virtual void regularVirtual(); virtual const int constVirtual(const int &) const; int data; };");
    code.parse(TopDUContext::AllDeclarationsContextsUsesAndAST);

    DUChainReadLocker lock;
    //----AbstractClass.h----
    ParseSession::Ptr session = ParseSession::Ptr::dynamicCast<IAstContainer>(code->ast());
    QVERIFY(session);
    TranslationUnitAST * ast = session->topAstNode();
    QVERIFY(ast);
    QVERIFY(ast->declarations);
    QCOMPARE(ast->declarations->count(), 1);
    
    QVERIFY(AstUtils::childNode<SimpleDeclarationAST>(ast, 0));
    QCOMPARE(session->astNodeFromDeclaration(code->localDeclarations()[0]),
            AstUtils::childNode<SimpleDeclarationAST>(ast, 0)->type_specifier);
    //AbstractClass
    ClassSpecifierAST * classAst = AstUtils::node_cast<ClassSpecifierAST>
                                  (AstUtils::childNode<SimpleDeclarationAST>(ast, 0)->type_specifier);
    QVERIFY(classAst);
    
    DUContext * cont = code->localDeclarations()[0]->internalContext();
    QVERIFY(cont);
    
    //~AbstractClass()
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(classAst, 1),
             session->astNodeFromDeclaration(cont->localDeclarations()[0]));
    
    //pureVirtual()
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(classAst, 2),
             session->astNodeFromDeclaration(cont->localDeclarations()[1]));
    
    //constPure()
    SimpleDeclarationAST * func = AstUtils::childNode<SimpleDeclarationAST>(classAst, 3);
    QCOMPARE(func,
             session->astNodeFromDeclaration(cont->localDeclarations()[2]));
    QVERIFY(AstUtils::childInitDeclarator(func, 0));
    QVERIFY(AstUtils::parameterAtIndex(AstUtils::childInitDeclarator(func, 0)->declarator, 0));
    QVERIFY(cont->localDeclarations()[2]->internalContext()->localDeclarations()[0]);
    QVERIFY(AstUtils::parameterAtIndex(AstUtils::childInitDeclarator(func, 0)->declarator, 0));
    QCOMPARE(AstUtils::parameterAtIndex(AstUtils::childInitDeclarator(func, 0)->declarator, 0),
             session->astNodeFromDeclaration(cont->localDeclarations()[2]->internalContext()->localDeclarations()[0]));
    
    //regularVirtual()
    QCOMPARE(AstUtils::childNode<SimpleDeclarationAST>(classAst, 4),
             session->astNodeFromDeclaration(cont->localDeclarations()[3]));
    
    //constPure()
    func = AstUtils::childNode<SimpleDeclarationAST>(classAst, 5);
    QCOMPARE(func,
             session->astNodeFromDeclaration(cont->localDeclarations()[4]));
    QVERIFY(AstUtils::childInitDeclarator(func, 0));
    QVERIFY(AstUtils::parameterAtIndex(AstUtils::childInitDeclarator(func, 0)->declarator, 0));
    QVERIFY(cont->localDeclarations()[4]->internalContext()->localDeclarations()[0]);
    QVERIFY(AstUtils::parameterAtIndex(AstUtils::childInitDeclarator(func, 0)->declarator, 0));
    QCOMPARE(AstUtils::parameterAtIndex(AstUtils::childInitDeclarator(func, 0)->declarator, 0),
             session->astNodeFromDeclaration(cont->localDeclarations()[4]->internalContext()->localDeclarations()[0]));
  }
}

void TestCppCodegen::testClassGeneration()
{
  #if 0
  TopDUContext * top = 0;
  GET_CONTEXT("AbstractClass.h", top);
  
  
  CppNewClass newClass;
  newClass.identifier("GeneratedClass");
  newClass.addBaseClass("AbstractClass");
  newClass.setHeaderUrl(CodeRepresentation::artificialUrl("GeneratedClass.h"));
  newClass.setImplementationUrl(CodeRepresentation::artificialUrl("GeneratedClass.cpp"));
  
  DocumentChangeSet changes;/* = newClass.generate();
  changes.applyAllToTemp();
  QCOMPARE(changes.tempNamesForAll().size(), 2);
  parseFile(changes.tempNamesForAll()[0].second);
  parseFile(changes.tempNamesForAll()[1].second);*/
  #endif
}

void TestCppCodegen::testPrivateImplementation()
{
#if 0
  TopDUContext * top = 0;
  GET_CONTEXT("ClassA.h", top);
  QVERIFY(top);
  
  MakeImplementationPrivate generator;
  DocumentRange range;
  
  {
    DUChainReadLocker lock(DUChain::lock());
    generator.autoGenerate(top->localDeclarations()[0]->internalContext(), &range);
  }
  generator.setStructureName("ClassAPrivate");
  generator.setPointerName("d");
  
  QVERIFY2(generator.execute(), generator.errorText().toAscii());
  generator.documentChangeSet().setReplacementPolicy(DocumentChangeSet::StopOnFailedChange);
  
  //Formatting plugin does not like source code in a single line
  generator.documentChangeSet().setFormatPolicy(DocumentChangeSet::NoAutoFormat);
  DocumentChangeSet::ChangeResult result = generator.documentChangeSet().applyAllToTemp();
  QVERIFY2(result, result.m_failureReason.toAscii());
  kDebug() << "tempName: " << generator.documentChangeSet().tempNameForFile(IndexedString(CodeRepresentation::artificialUrl("ClassA.h"))).str();
  kDebug() << "Generated Text:" << createCodeRepresentation(generator.documentChangeSet().tempNameForFile(IndexedString(CodeRepresentation::artificialUrl("ClassA.h"))))->text();
  kDebug() << "GeneratedText:" << createCodeRepresentation(generator.documentChangeSet().tempNameForFile(IndexedString(CodeRepresentation::artificialUrl("ClassA.cpp"))))->text();
  
  QVERIFY(generator.documentChangeSet().tempNameForFile(IndexedString(CodeRepresentation::artificialUrl("ClassA.h"))) != IndexedString(CodeRepresentation::artificialUrl("ClassA.h")));
  QVERIFY(generator.documentChangeSet().tempNameForFile(IndexedString(CodeRepresentation::artificialUrl("ClassA.cpp"))) != IndexedString(CodeRepresentation::artificialUrl("ClassA.cpp")));
  
  IndexedString tempName1 = generator.documentChangeSet().tempNameForFile(IndexedString(CodeRepresentation::artificialUrl("ClassA.h")));
  IndexedString tempName2 = generator.documentChangeSet().tempNameForFile(IndexedString(CodeRepresentation::artificialUrl("ClassA.cpp")));
  parseFile(tempName1);
  parseFile(tempName2);
  
  TopDUContext * newHeader = m_contexts[tempName1].data();
  TopDUContext * newImplementation = m_contexts[tempName2].data();
  
  QVERIFY(newHeader);
  QVERIFY(newImplementation);
  
  DUChainReadLocker lock(DUChain::lock());
  kDebug() << "HeaderProblems: ";
  foreach(ProblemPointer p, newHeader->problems())
    kDebug() << p->description();
  kDebug() << "Implementation Problems: ";
  foreach(ProblemPointer p, newImplementation->problems())
    kDebug() << p->description();
  
  //There is a problem from include path resolver, we care about semantic problems
  QCOMPARE(newHeader->problems().size(), 1);
  QCOMPARE(newImplementation->problems().size(), 1);
#endif
}

#include "test_cppcodegen.moc"
