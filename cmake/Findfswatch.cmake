# Findfswatch
# -----------
#
# Try to find the fswatch file system watcher library
#
# Defined variables once found:
#
# ::
#
#   fswatch_FOUND - System has fswatch
#   fswatch_INCLUDE_DIRS - The fswatch include directory
#   fswatch_LIBRARIES - The libraries needed to use fswatch

find_path(
    fswatch_INCLUDE_DIRS
    NAMES libfswatch/c++/monitor.hpp
)

mark_as_advanced(fswatch_INCLUDE_DIRS)

find_library(
    fswatch_LIBRARIES
    NAMES fswatch
)

mark_as_advanced(fswatch_LIBRARY)

find_package_handle_standard_args(
    fswatch
    FOUND_VAR fswatch_FOUND
    REQUIRED_VARS fswatch_INCLUDE_DIRS fswatch_LIBRARIES
)