
#First remove the cpp language part default area, then set our own
remove_definitions( -DKDE_DEFAULT_DEBUG_AREA=9007 )
add_definitions( -DKDE_DEFAULT_DEBUG_AREA=9040 )

add_subdirectory(rpp)
add_subdirectory(tests)
#add_subdirectory(headergen)

########### next target ###############

set(kdevcppparser_LIB_SRCS
    ast.cpp
    default_visitor.cpp
    parentvisitor.cpp
    memorypool.cpp
    name_compiler.cpp
    visitor.cpp
    class_compiler.cpp
    control.cpp
    lexer.cpp
    parser.cpp
    listnode.cpp
    tokens.cpp
    dumptree.cpp
    parsesession.cpp
    type_compiler.cpp
    commentparser.cpp
    commentformatter.cpp
    codegenerator.cpp
)

# Note: This library doesn't follow API/ABI/BC rules and shouldn't have a SOVERSION
#       Its only purpose is to support the plugin without needing to add all source files
#       to the plugin target
kde4_add_library(kdev4cppparser SHARED ${kdevcppparser_LIB_SRCS})
target_link_libraries(kdev4cppparser ${KDEVPLATFORM_LANGUAGE_LIBRARIES} ${KDE4_KDECORE_LIBS} ${KDE4_KTEXTEDITOR_LIBS} kdev4cpprpp )
install(TARGETS kdev4cppparser ${INSTALL_TARGETS_DEFAULT_ARGS} )
