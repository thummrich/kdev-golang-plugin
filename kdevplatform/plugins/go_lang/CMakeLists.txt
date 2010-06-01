project(golang)
add_definitions( -DKDE_DEFAULT_DEBUG_AREA=9532 )

set(kdevgolang_PART_SRCS
    golangplugin.cpp
)

kde4_add_plugin(kdevgolang ${kdevgolang_PART_SRCS})
target_link_libraries(kdevgolang kdevplatforminterfaces kdevplatformproject ${KDE4_KDEUI_LIBS})

install(TARGETS kdevgolang DESTINATION ${PLUGIN_INSTALL_DIR})


########### install files ###############

install(FILES kdevgolang.desktop DESTINATION ${SERVICES_INSTALL_DIR})
#install(FILES kdevopenwithui.rc DESTINATION ${DATA_INSTALL_DIR}/kdevopenwith)
