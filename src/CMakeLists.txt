include(FetchContent)

include(Common.cmake)

add_executable(r5bsp
    main.cc
    rpak.cc
    decomp.cc
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.3.4
)

FetchContent_GetProperties(glfw)

if (NOT glfw_POPULATED)
    FetchContent_Populate(glfw)

    # Just configure GLFW only
    set(GLFW_BUILD_EXAMPLES     OFF CACHE BOOL "Build Examples" FORCE)
    set(GLFW_BUILD_TESTS        OFF CACHE BOOL "Build tests" FORCE)
    set(GLFW_BUILD_DOCS         OFF CACHE BOOL "Build docs" FORCE)
    set(GLFW_INSTALL            OFF CACHE BOOL "Configure an install" FORCE)

    # This excludes glfw from being rebuilt when ALL_BUILD is built
    # it will only be built when a target is built that has a dependency on glfw
    add_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR} EXCLUDE_FROM_ALL)

    # Set the target's folders
    set_target_properties(glfw PROPERTIES FOLDER ${PROJECT_NAME}/thirdparty)
endif()

target_include_directories(r5bsp PUBLIC ${glfw_SOURCE_DIR}/include)
target_link_libraries(r5bsp glfw)

add_dependencies(r5bsp glfw)

add_subdirectory(imgui)
add_subdirectory(glad)

target_include_directories(r5bsp PUBLIC glm)