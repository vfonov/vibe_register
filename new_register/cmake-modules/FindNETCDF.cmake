# FindNETCDF.cmake — find NetCDF C library
#
# Sets:
#   NETCDF_INCLUDE_DIR  — path to netcdf.h
#   NETCDF_LIBRARY      — path to libnetcdf
#   NETCDF_FOUND        — TRUE if both are found
#
# Respects CMAKE_PREFIX_PATH: pass -DCMAKE_PREFIX_PATH=/opt/minc/1.9.18
# to search <prefix>/include and <prefix>/lib automatically.

find_path(NETCDF_INCLUDE_DIR netcdf.h
    HINTS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES include
    PATHS /usr/include /usr/local/include /usr/local/bic/include
)

find_library(NETCDF_LIBRARY NAMES netcdf
    HINTS ${CMAKE_PREFIX_PATH}
    PATH_SUFFIXES lib lib64 lib/x86_64-linux-gnu
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib /usr/local/bic/lib
)

if(NETCDF_INCLUDE_DIR AND NETCDF_LIBRARY)
    set(NETCDF_FOUND TRUE)
endif()

if(NETCDF_FOUND)
    if(NOT NETCDF_FIND_QUIETLY)
        message(STATUS "Found NetCDF headers: ${NETCDF_INCLUDE_DIR}")
        message(STATUS "Found NetCDF library: ${NETCDF_LIBRARY}")
    endif()
else()
    if(NETCDF_FIND_REQUIRED)
        message(FATAL_ERROR "Could not find NetCDF")
    endif()
endif()
