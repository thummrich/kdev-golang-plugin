add_subdirectory(dvcs/tests)
add_subdirectory(dvcs/test2)
add_subdirectory(tests)

add_definitions(-DKDE_DEFAULT_DEBUG_AREA=9509)

set(kdevplatformvcs_UIS
    widgets/vcscommitdialog.ui
    widgets/vcseventwidget.ui
    widgets/vcsdiffwidget.ui
    dvcs/ui/cvsgenericoutputview.ui
    dvcs/ui/cvsmainview.ui
    dvcs/ui/importmetadatawidget.ui
    dvcs/ui/logview.ui
    dvcs/ui/branchmanager.ui
)

set(kdevplatformvcs_LIB_SRCS
    vcsjob.cpp
    vcsrevision.cpp
    vcsannotation.cpp
    vcspluginhelper.cpp
    vcslocation.cpp
    vcsdiff.cpp
    vcsevent.cpp
    vcsstatusinfo.cpp
    widgets/vcsimportmetadatawidget.cpp
    widgets/vcseventwidget.cpp
    widgets/vcsdiffwidget.cpp
    widgets/vcscommitdialog.cpp
    widgets/vcsdiffpatchsources.cpp
    models/vcsannotationmodel.cpp
    models/vcseventmodel.cpp
    models/vcsitemeventmodel.cpp
    dvcs/dvcsjob.cpp
    dvcs/dvcsplugin.cpp
    dvcs/ui/dvcsmainview.cpp
    dvcs/ui/dvcsgenericoutputview.cpp
    dvcs/ui/importdialog.cpp
    dvcs/ui/importmetadatawidget.cpp
    dvcs/ui/logview.cpp
    dvcs/ui/branchmanager.cpp
    dvcs/ui/revhistory/commitView.cpp
    dvcs/ui/revhistory/commitlogmodel.cpp
)

kde4_add_ui_files(kdevplatformvcs_LIB_SRCS ${kdevplatformvcs_UIS})
kde4_add_library(kdevplatformvcs SHARED ${kdevplatformvcs_LIB_SRCS})
target_link_libraries(kdevplatformvcs 
    ${KDE4_KIO_LIBS}
    ${KDE4_KPARTS_LIBS}
    kdevplatforminterfaces 
    kdevplatformutil
    kdevplatformproject
    kdevplatformlanguage
    kdevplatformoutputview
)
# add kdevplatform* when they're exported targets
target_link_libraries(kdevplatformvcs LINK_INTERFACE_LIBRARIES 
        ${KDE4_KIO_LIBS} 
        ${KDE4_KPARTS_LIBS})
set_target_properties(kdevplatformvcs PROPERTIES VERSION ${KDEVPLATFORM_LIB_VERSION} SOVERSION ${KDEVPLATFORM_LIB_SOVERSION})
install(TARGETS kdevplatformvcs EXPORT KDevPlatformTargets ${INSTALL_TARGETS_DEFAULT_ARGS} )

install(FILES
    vcsexport.h
    vcsjob.h
    vcsrevision.h
    vcsannotation.h
    vcsdiff.h
    vcspluginhelper.h
    vcsevent.h
    vcsstatusinfo.h
    vcslocation.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/vcs COMPONENT Devel
)

install(FILES
    widgets/vcsimportmetadatawidget.h
    widgets/vcsannotationwidget.h
    widgets/vcseventwidget.h
    widgets/vcsdiffwidget.h
    widgets/vcscommitdialog.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/vcs/widgets COMPONENT Devel
)


install(FILES
    models/vcsannotationmodel.h
    models/vcseventmodel.h
    models/vcsitemeventmodel.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/vcs/models COMPONENT Devel
)

install(FILES
    interfaces/ibasicversioncontrol.h
    interfaces/icentralizedversioncontrol.h
    interfaces/idistributedversioncontrol.h
    interfaces/ibranchingversioncontrol.h
    interfaces/ibrowsableversioncontrol.h
    interfaces/irepositoryversioncontrol.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/vcs/interfaces COMPONENT Devel
)

install(FILES
    dvcs/dvcsjob.h
    dvcs/dvcsplugin.h
    dvcs/dvcsevent.h
    DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/vcs/dvcs COMPONENT Devel
)
