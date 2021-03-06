set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )
include_directories(
    ${KDevelop_SOURCE_DIR}/languages/cpp
    ${KDevelop_SOURCE_DIR}/languages/cpp/parser
    ${KDevelop_BINARY_DIR}/languages/cpp
)

########### next target ###############

set(expressionparsertest_SRCS test_expressionparser.cpp)


kde4_add_unit_test(cppexpressionparsertest ${expressionparsertest_SRCS})
target_link_libraries(cppexpressionparsertest ${KDE4_KDECORE_LIBS} ${KDE4_KTEXTEDITOR_LIBRARY} ${QT_QTTEST_LIBRARY}
    kdev4cppparser kdev4cpprpp ${KDEVPLATFORM_LANGUAGE_LIBRARIES} kdev4cppduchain ${KDEVPLATFORM_TESTS_LIBRARIES}  )


include_directories(
    ${KDevelop_SOURCE_DIR}/languages/cpp/parser
    ${KDevelop_SOURCE_DIR}/languages/cpp
)

########### next target ###############

set(duchaintest_SRCS test_duchain.cpp)


kde4_add_unit_test(duchaintest ${duchaintest_SRCS})
target_link_libraries(duchaintest ${KDE4_KDECORE_LIBS} ${KDE4_KTEXTEDITOR_LIBRARY} ${QT_QTTEST_LIBRARY}
    kdev4cppparser kdev4cpprpp kdev4cppduchain ${KDEVPLATFORM_LANGUAGE_LIBRARIES} ${KDEVPLATFORM_TESTS_LIBRARIES})

########### next target ###############

set(itemrepositorytest_SRCS test_itemrepository.cpp)


kde4_add_unit_test(itemrepositorytest ${itemrepositorytest_SRCS})
target_link_libraries(itemrepositorytest ${KDE4_KDECORE_LIBS} ${KDE4_KTEXTEDITOR_LIBRARY} ${QT_QTTEST_LIBRARY} ${KDEVPLATFORM_LANGUAGE_LIBRARIES} ${KDEVPLATFORM_TESTS_LIBRARIES})
