add_definitions( -DKDE_DEFAULT_DEBUG_AREA=9516 )

########### next target ###############

set(kdevfilemanager_PART_SRCS
    kdevfilemanagerplugin.cpp
    filemanager.cpp
)

kde4_add_plugin(kdevfilemanager ${kdevfilemanager_PART_SRCS})
target_link_libraries(kdevfilemanager ${KDE4_KIO_LIBS} ${KDE4_KFILE_LIBS} ${KDE4_KTEXTEDITOR_LIBS} kdevplatforminterfaces)

install(TARGETS kdevfilemanager DESTINATION ${PLUGIN_INSTALL_DIR} )


########### install files ###############

install( FILES kdevfilemanager.desktop DESTINATION ${SERVICES_INSTALL_DIR} )
install( FILES kdevfilemanager.rc DESTINATION ${DATA_INSTALL_DIR}/kdevfilemanager )


