project(cpp)
kde4_no_enable_final(cpp)
include(CppBuildConfig.cmake)

include_directories(
    ${KDevelop_SOURCE_DIR}/languages/cpp/parser
    ${KDevelop_SOURCE_DIR}/languages/cpp/cppduchain
)

add_definitions( -DKDE_DEFAULT_DEBUG_AREA=9007 )

if(CMAKE_COMPILER_IS_GNUCXX)
  # TODO: Remove when LTS for g++ < 4.3 has ended.
  #       See also: languages/cpp/parser/parser.h
  macro_ensure_version("4.3.0" "${_gcc_version}" GCC_IS_NEWER_THAN_4_3)
  if (GCC_IS_NEWER_THAN_4_3)
    message(STATUS "Enabling c++0x support for unordered map")
    add_definitions( -std=c++0x ) # For unordered_map
  else(GCC_IS_NEWER_THAN_4_3)
    add_definitions( -DGXX_LT_4_3 )
  endif (GCC_IS_NEWER_THAN_4_3)
endif(CMAKE_COMPILER_IS_GNUCXX)

add_subdirectory(parser)
add_subdirectory(cppduchain)
add_subdirectory(tests)
########### next target ###############

set(kdevcpplanguagesupport_PART_SRCS
    cpplanguagesupport.cpp
    includepathcomputer.cpp
    cppparsejob.cpp
    preprocessjob.cpp
    cpphighlighting.cpp
    cpputils.cpp
    stringhelpers.cpp
    includepathresolver.cpp
    setuphelpers.cpp
    quickopen.cpp
    
    codecompletion/model.cpp
    codecompletion/worker.cpp
    codecompletion/context.cpp
    codecompletion/item.cpp
    codecompletion/helpers.cpp
    codecompletion/missingincludeitem.cpp
    codecompletion/implementationhelperitem.cpp
    codecompletion/missingincludemodel.cpp

    codegen/cppnewclass.cpp
    codegen/simplerefactoring.cpp
    codegen/progressdialogs.cpp
    codegen/codeassistant.cpp
    codegen/signatureassistant.cpp
    codegen/unresolvedincludeassistant.cpp
#     codegen/makeimplementationprivate.cpp
)

set(kdevcpplanguagesupport_LIB_UI
    codegen/ui/custom_include_paths.ui
#     codegen/ui/privateimplementation.ui
)

kde4_add_ui_files(kdevcpplanguagesupport_PART_SRCS ${kdevcpplanguagesupport_LIB_UI})

option(BUILD_kdev_includepathresolver "Build the includepath resolver debugging tool" OFF)
if(BUILD_kdev_includepathresolver)
  add_executable(kdev_includepathresolver includepathresolver.cpp)
  set_target_properties( kdev_includepathresolver PROPERTIES COMPILE_FLAGS -DTEST )
  target_link_libraries( kdev_includepathresolver
  ${KDEVPLATFORM_INTERFACES_LIBRARIES}  ${KDEVPLATFORM_PROJECT_LIBRARIES}
  ${QT_QTCORE_LIBRARY} ${QT_QTGUI_LIBRARY} ${KDE4_KDECORE_LIBS} ${KDEVPLATFORM_LANGUAGE_LIBRARIES} )
  install(TARGETS kdev_includepathresolver ${INSTALL_TARGETS_DEFAULT_ARGS} )
endif(BUILD_kdev_includepathresolver)

kde4_add_plugin(kdevcpplanguagesupport ${kdevcpplanguagesupport_PART_SRCS})
target_link_libraries(kdevcpplanguagesupport
    kdev4cpprpp
    kdev4cppduchain
    kdev4cppparser
    ${QT_QTDESIGNER_LIBRARY}
    ${KDEVPLATFORM_INTERFACES_LIBRARIES}
    ${KDEVPLATFORM_PROJECT_LIBRARIES}
    ${KDE4_THREADWEAVER_LIBRARIES}
    ${KDEVPLATFORM_LANGUAGE_LIBRARIES}
    ${KDE4_KDECORE_LIBS}
    ${KDE4_KTEXTEDITOR_LIBS}
)

install(TARGETS kdevcpplanguagesupport DESTINATION ${PLUGIN_INSTALL_DIR})


########### install files ###############

install(FILES kdevcppsupport.desktop DESTINATION ${SERVICES_INSTALL_DIR})
install(FILES kdevcppsupport.rc DESTINATION ${DATA_INSTALL_DIR}/kdevcppsupport)
