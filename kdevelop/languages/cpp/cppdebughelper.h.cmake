#cmakedefine DEBUG_UI_LOCKUP 
#cmakedefine DEBUG 

//When this is enabled, the include-path-resolver will always be issued,
//and the returned include-path compared to the one returned by the build-manager.
//Set it to 1 to debug build-managers.
#cmakedefine DEBUG_INCLUDE_PATHS 1

#cmakedefine DEBUG_LEXERCACHE
#cmakedefine DEBUG_UPDATE_MATCHING
#cmakedefine DEBUG_UPDATE_RANGES

#ifdef DEBUG
#define ifDebug(x) x
#else
#define ifDebug(x)
#endif

