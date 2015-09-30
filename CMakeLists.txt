project(timestamp-output
    "CXX"
    )


# settings


cmake_minimum_required(VERSION
    "2.8"
    )

set(CMAKE_EXPORT_COMPILE_COMMANDS
    ON
    )


# target


add_executable(${PROJECT_NAME}
    "timestamp-output.cpp"
    )

target_compile_options(${PROJECT_NAME}
    PUBLIC
    "-std=c++11"
    )

install(
    TARGETS ${PROJECT_NAME}
    DESTINATION "bin"
    )

# package

set(CPACK_PACKAGE_VERSION_MAJOR
    "1"
    )
set(CPACK_PACKAGE_VERSION_MINOR
    "0"
    )
set(CPACK_PACKAGE_VERSION_PATCH
    "0"
    )

set(CPACK_PACKAGE_CONTACT
    "Chris Pick <chris@chrispick.com>"
    )

set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Prepend timestamps to every line of output."
    )

set(CPACK_GENERATOR
    "DEB"
    )

include(CPack)
