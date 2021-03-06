

add_definitions( -DKDE_DEFAULT_DEBUG_AREA=9503 )
set(kdevplatformproject_LIB_SRCS
    projectmodel.cpp
    projectconfigskeleton.cpp
    importprojectjob.cpp
    builderjob.cpp
    projectbuildsetmodel.cpp
    projectitemlineedit.cpp
    helper.cpp
    interfaces/iprojectbuilder.cpp
    interfaces/iprojectfilemanager.cpp
    interfaces/ibuildsystemmanager.cpp
)


kde4_add_library(kdevplatformproject SHARED ${kdevplatformproject_LIB_SRCS})
target_link_libraries(kdevplatformproject 
        ${QT_QTDESIGNER_LIBRARY} 
        kdevplatforminterfaces
        kdevplatformutil
	kdevplatformlanguage
        ${KDE4_KIO_LIBS})
target_link_libraries(kdevplatformproject LINK_INTERFACE_LIBRARIES 
        ${QT_QTGUI_LIBRARY} 
        ${KDE4_KDECORE_LIBS} 
        ${KDE4_KUTIL_LIBRARY})
set_target_properties(kdevplatformproject PROPERTIES VERSION ${KDEVPLATFORM_LIB_VERSION} SOVERSION ${KDEVPLATFORM_LIB_SOVERSION})
install(TARGETS kdevplatformproject EXPORT KDevPlatformTargets ${INSTALL_TARGETS_DEFAULT_ARGS} )

########### install files ###############

install(FILES
    interfaces/iprojectbuilder.h
    interfaces/iprojectfilemanager.h
    interfaces/ibuildsystemmanager.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/project/interfaces COMPONENT Devel
 )

install(FILES
    importprojectjob.h
    projectconfigskeleton.h
    projectmodel.h
    projectkcmodule.h
    projectexport.h
    projectitemlineedit.h
    projectbuildsetmodel.h
    builderjob.h
    helper.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/project COMPONENT Devel
 )
