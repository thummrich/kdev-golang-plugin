project(grepview)
add_definitions( -DKDE_DEFAULT_DEBUG_AREA=9001 )


########### next target ###############

set(kdevgrepview_PART_SRCS
    grepviewplugin.cpp
    grepdialog.cpp
    grepoutputmodel.cpp
    grepoutputdelegate.cpp
    grepjob.cpp
)

set(kdevgrepview_PART_UI
    grepwidget.ui
)

kde4_add_ui_files(kdevgrepview_PART_SRCS ${kdevgrepview_PART_UI})
kde4_add_plugin(kdevgrepview ${kdevgrepview_PART_SRCS})
target_link_libraries(kdevgrepview
    ${KDE4_KDECORE_LIBS}
    ${KDE4_KDEUI_LIBS}
    ${KDE4_KPARTS_LIBS}
    ${KDE4_KTEXTEDITOR_LIBS}
    ${KDEVPLATFORM_INTERFACES_LIBRARIES}
    ${KDEVPLATFORM_OUTPUTVIEW_LIBRARIES}
    ${KDEVPLATFORM_PROJECT_LIBRARIES}
    ${KDEVPLATFORM_UTIL_LIBRARIES}
    ${KDEVPLATFORM_LANGUAGE_LIBRARIES}
)

install(TARGETS kdevgrepview DESTINATION ${PLUGIN_INSTALL_DIR} )


########### install files ###############

install( FILES kdevgrepview.desktop DESTINATION ${SERVICES_INSTALL_DIR} )
install( FILES kdevgrepview.rc DESTINATION ${DATA_INSTALL_DIR}/kdevgrepview )
