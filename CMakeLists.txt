cmake_minimum_required(VERSION 3.22)
project(SoundDetectivePlugins VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Fetch JUCE once — shared by all Sound Detective plugins
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.7
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugins
# ---------------------------------------------------------------------------
add_subdirectory(Plugins/Dust1200)
add_subdirectory(Plugins/Backtrace)
