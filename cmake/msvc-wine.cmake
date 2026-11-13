set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Compiler response files bypass the Wine wrapper's argument translation.
unset(CMAKE_NINJA_FORCE_RESPONSE_FILE CACHE)
unset(CMAKE_NINJA_FORCE_RESPONSE_FILE)

find_program(CMAKE_CXX_COMPILER NAMES cl REQUIRED)
find_program(CMAKE_LINKER NAMES link REQUIRED)
find_program(CMAKE_AR NAMES lib REQUIRED)
find_program(CMAKE_RC_COMPILER NAMES rc REQUIRED)
find_program(CMAKE_MT NAMES mt REQUIRED)
# Keep msvc-wine's manifest-update exit-code translation during link steps.
set(CMAKE_MT "${CMAKE_CURRENT_LIST_DIR}/msvc-mt.sh")

find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)
# msvc-link.py packs long librarian commands and bypasses the FIFO wait on errors.
set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK
    "\"${Python3_EXECUTABLE}\" \"${CMAKE_CURRENT_LIST_DIR}/msvc-link.py\"")
