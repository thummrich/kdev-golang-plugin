
add_definitions( -DKDE_DEFAULT_DEBUG_AREA=9507 )
set( outputviewinterfaces_LIB_SRCS
    outputmodel.cpp
    ioutputviewmodel.cpp
    outputjob.cpp
)
kde4_add_library( kdevplatformoutputview SHARED ${outputviewinterfaces_LIB_SRCS} )
target_link_libraries(kdevplatformoutputview 
        ${QT_QTCORE_LIBRARY} 
        ${KDE4_KDEUI_LIBS} 
        kdevplatforminterfaces  )
# Need to add kdevplatforminterfaces when its exported as target
target_link_libraries(kdevplatformoutputview LINK_INTERFACE_LIBRARIES 
        ${QT_QTCORE_LIBRARY} 
        ${KDE4_KDEUI_LIBS})
set_target_properties(kdevplatformoutputview PROPERTIES VERSION ${KDEVPLATFORM_LIB_VERSION} SOVERSION ${KDEVPLATFORM_LIB_SOVERSION})
install(TARGETS kdevplatformoutputview EXPORT KDevPlatformTargets ${INSTALL_TARGETS_DEFAULT_ARGS} )

 ########### install files ###############
 install( FILES
          ioutputview.h
          outputmodel.h
          ioutputviewmodel.h
          outputviewexport.h
          outputjob.h
          DESTINATION ${INCLUDE_INSTALL_DIR}/kdevplatform/outputview COMPONENT Devel)
