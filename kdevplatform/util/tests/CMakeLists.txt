set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )

set(embeddedfreetreetest_SRCS test_embeddedfreetree.cpp)

kde4_add_unit_test(embeddedfreetreetest ${embeddedfreetreetest_SRCS})
target_link_libraries(embeddedfreetreetest ${KDE4_KDECORE_LIBS} ${KDE4_TEXTEDITOR_LIBS} ${QT_QTTEST_LIBRARY} kdevplatformlanguage)

set(kdevvarlengtharray_SRC test_kdevvarlengtharray.cpp)
kde4_add_unit_test(kdevvarlengtharraytest ${kdevvarlengtharray_SRC})
target_link_libraries(kdevvarlengtharraytest ${KDE4_KDECORE_LIBS} ${QT_QTTEST_LIBRARY})
