project(documentview)
#add_subdirectory(settings)

########### next target ###############

set(kdevdocumentview_PART_SRCS
    kdevdocumentview.cpp
    kdevdocumentviewdelegate.cpp
    kdevdocumentviewplugin.cpp
    kdevdocumentmodel.cpp
    kdevdocumentselection.cpp
)


kde4_add_plugin(kdevdocumentview ${kdevdocumentview_PART_SRCS})
target_link_libraries(kdevdocumentview ${KDE4_KDECORE_LIBS} ${KDEVPLATFORM_INTERFACES_LIBRARIES} ${KDE4_KTEXTEDITOR_LIBS})

install(TARGETS kdevdocumentview DESTINATION ${PLUGIN_INSTALL_DIR} )


########### install files ###############

install( FILES kdevdocumentview.desktop DESTINATION ${SERVICES_INSTALL_DIR} )
install( FILES kdevdocumentview.rc DESTINATION ${DATA_INSTALL_DIR}/kdevdocumentview )

