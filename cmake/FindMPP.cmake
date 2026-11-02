find_path(MPP_INCLUDE_DIR NAMES rk_mpi.h PATH_SUFFIXES rockchip)
find_library(MPP_LIBRARY NAMES rockchip_mpp)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MPP REQUIRED_VARS MPP_INCLUDE_DIR MPP_LIBRARY)

if(MPP_FOUND AND NOT TARGET MPP::MPP)
    add_library(MPP::MPP UNKNOWN IMPORTED)
    set_target_properties(MPP::MPP PROPERTIES
        IMPORTED_LOCATION "${MPP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MPP_INCLUDE_DIR}"
    )
endif()
