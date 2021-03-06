set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )

include_directories(
    ${KDevelop_SOURCE_DIR}/languages/cpp
    ${KDevelop_BINARY_DIR}/languages/cpp
)

########### next target ###############

add_definitions(-DTEST_COMPLETION)

set(cppcodecompletiontest_SRCS
  test_cppcodecompletion.cpp
  
  ../codecompletion/context.cpp
  ../stringhelpers.cpp
  ../codecompletion/item.cpp
  ../codecompletion/helpers.cpp
  ../codecompletion/implementationhelperitem.cpp
  ../cpphighlighting.cpp
  ../codecompletion/model.cpp
  ../quickopen.cpp
  ../codegen/simplerefactoring.cpp
  ../includepathresolver.cpp
  ../setuphelpers.cpp
  ../codegen/cppnewclass.cpp
  ../codegen/progressdialogs.cpp
  ../codegen/unresolvedincludeassistant.cpp
  ../cpputils.cpp
  ../codecompletion/worker.cpp
  ../includepathcomputer.cpp
  ../codecompletion/missingincludemodel.cpp
  ../codecompletion/missingincludeitem.cpp
)

set(cppcodegentest_SRCS
  test_cppcodegen.cpp
  
  ../codecompletion/context.cpp
  ../stringhelpers.cpp
  ../codecompletion/item.cpp
  ../codecompletion/helpers.cpp
  ../codecompletion/implementationhelperitem.cpp
  ../cpphighlighting.cpp
  ../codecompletion/model.cpp
  ../quickopen.cpp
  ../codegen/simplerefactoring.cpp
  ../includepathresolver.cpp
  ../setuphelpers.cpp
  ../codegen/cppnewclass.cpp
  ../codegen/progressdialogs.cpp
  ../codegen/unresolvedincludeassistant.cpp
  ../cpputils.cpp
  ../codecompletion/worker.cpp
  ../includepathcomputer.cpp
  ../codecompletion/missingincludemodel.cpp
  ../codecompletion/missingincludeitem.cpp
)

add_definitions(-DBUILD_TESTS)

kde4_add_unit_test(cppcodecompletiontest ${cppcodecompletiontest_SRCS})
target_link_libraries(cppcodecompletiontest ${QT_QTTEST_LIBRARY}
    kdev4cppduchain
    kdev4cpprpp
    kdev4cppparser
    ${QT_QTDESIGNER_LIBRARY}
    ${KDEVPLATFORM_INTERFACES_LIBRARIES}
    ${KDEVPLATFORM_PROJECT_LIBRARIES}
    ${KDE4_THREADWEAVER_LIBRARIES}
    ${KDEVPLATFORM_LANGUAGE_LIBRARIES}
    ${KDEVPLATFORM_TESTS_LIBRARIES}
    ${KDE4_KDECORE_LIBS}
    ${KDE4_KTEXTEDITOR_LIBS}
)

kde4_add_unit_test(cppcodegentest ${cppcodegentest_SRCS})
target_link_libraries(cppcodegentest
    kdev4cppduchain
    kdev4cpprpp
    kdev4cppparser
    ${QT_QTTEST_LIBRARY}
    ${QT_QTCORE_LIBRARY}
    ${KDEVPLATFORM_LANGUAGE_LIBRARIES}
    ${KDEVPLATFORM_PROJECT_LIBRARIES}
    ${KDEVPLATFORM_TESTS_LIBRARIES}
)

