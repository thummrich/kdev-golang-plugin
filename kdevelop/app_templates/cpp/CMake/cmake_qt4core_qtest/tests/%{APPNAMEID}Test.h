#ifndef %{APPNAMEID}TEST_H
#define %{APPNAMEID}TEST_H

#include <QtCore/QObject>

class %{APPNAMEID}Test : public QObject
{
Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    void someTest();
};

#endif // %{APPNAME}TEST_H
