# Due to the use of system() and some unix-style paths this test will only run
# under Linux. (Maybe this can be fixed later)
#
# Moreover, I'm not sure if there is a cvs commandline client for windows
# (need to check this out ...)

if (UNIX)

# Running the test only makes sense if the cvs command line client
# is present. So check for it before adding the test ...
FIND_PROGRAM(CVS NAMES cvs
	PATHS
	/bin
	/usr/bin
	/usr/local/bin
)

if (CVS)

	include_directories( ${KDE4_INCLUDES}
		${KDevPlatform_SOURCE_DIR}
        ${KDevPlatform_SOURCE_DIR}/util
        ${KDevPlatform_SOURCE_DIR}/interfaces
		${KDevPlatform_SOURCE_DIR}/vcs
		${KDevPlatform_SOURCE_DIR}/plugins/cvs
	)
    set( EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR} )
	set(cvstest_SRCS cvstest.cpp
        ../cvsjob.cpp
        ../cvsproxy.cpp
        ../cvsannotatejob.cpp
        ../cvslogjob.cpp
        ../cvsstatusjob.cpp
        ../cvsdiffjob.cpp
    )
	kde4_add_unit_test(kdevcvs-test ${cvstest_SRCS})
	target_link_libraries(kdevcvs-test
		${QT_QTTEST_LIBRARY}
		${KDE4_KDECORE_LIBS}
        kdevplatformutil
        kdevplatformvcs
	)

endif (CVS)

endif (UNIX)

