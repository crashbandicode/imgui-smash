# Preload with cmake -C before the project is configured.
# The reviewed CMake 3.31.6 link.txt expanded the default CXX archive rule to:
#   aarch64-none-elf-gcc-ar qc <archive> <objects>
#   aarch64-none-elf-gcc-ranlib <archive>
# qcD and ranlib -D are that rule plus GNU deterministic mode.
# CACHE FORCE keeps the rule if the language module only fills an empty value.
# The authorized clean build's link.txt is the check that the toolchain accepted it.
# <CMAKE_AR> and <CMAKE_RANLIB> still come from the devkit toolchain.

set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcD <TARGET> <LINK_FLAGS> <OBJECTS>" CACHE STRING "deterministic CXX static archive" FORCE)
set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> qD <TARGET> <LINK_FLAGS> <OBJECTS>" CACHE STRING "deterministic CXX archive append" FORCE)
set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -D <TARGET>" CACHE STRING "deterministic CXX ranlib" FORCE)
