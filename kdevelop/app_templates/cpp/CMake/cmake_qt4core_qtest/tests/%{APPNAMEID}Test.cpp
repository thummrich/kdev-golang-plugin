#include "%{APPNAMEID}Test.h"
#include <QtTest/QTest>

void %{APPNAMEID}Test::initTestCase()
{}

void %{APPNAMEID}Test::init()
{}

void %{APPNAMEID}Test::cleanup()
{}

void %{APPNAMEID}Test::cleanupTestCase()
{}

void %{APPNAMEID}Test::someTest()
{
    QCOMPARE(1,2);
}


QTEST_MAIN(%{APPNAMEID}Test)
#include "%{APPNAMEID}Test.moc"
