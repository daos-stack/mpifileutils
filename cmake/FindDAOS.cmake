# - Try to find daos libs 
# Once done this will define
#  daos_FOUND - System has libdaos
#  daos_INCLUDE_DIRS - daos.h
#  daos_LIBRARIES - libdaos
#  DUNS_LIBRARIES - libduns 
#  DFS_LIBRARIES - libdfs 
#  COMMON_LIBRARIES - libdaos_common

FIND_PATH(WITH_DAOS_PREFIX
    NAMES include/daos.h
)

FIND_LIBRARY(DAOS_LIBRARIES
    NAMES daos
    HINTS ${WITH_DAOS_PREFIX}/lib
)

FIND_LIBRARY(DUNS_LIBRARIES
    NAMES duns
    HINTS ${WITH_DAOS_PREFIX}/lib
)

FIND_LIBRARY(DFS_LIBRARIES
    NAMES dfs
    HINTS ${WITH_DAOS_PREFIX}/lib
)

FIND_LIBRARY(COMMON_LIBRARIES
    NAMES daos_common
    HINTS ${WITH_DAOS_PREFIX}/lib
)

FIND_PATH(DAOS_INCLUDE_DIRS
    NAMES daos.h
    HINTS ${WITH_DAOS_PREFIX}/include
)

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(DAOS DEFAULT_MSG
    DAOS_LIBRARIES
    DUNS_LIBRARIES
    DFS_LIBRARIES
    COMMON_LIBRARIES
    DAOS_INCLUDE_DIRS
)

# Hide these vars from ccmake GUI
MARK_AS_ADVANCED(
	DAOS_LIBRARIES
	DUNS_LIBRARIES
        DFS_LIBRARIES
        COMMON_LIBRARIES
	DAOS_INCLUDE_DIRS
)
