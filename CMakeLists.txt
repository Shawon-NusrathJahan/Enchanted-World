
cmake_minimum_required(VERSION 3.20)

project(EnchantedGarden)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)

add_executable(EnchantedGarden
    src/main.cpp
    src/mountain.cpp
    src/village.cpp
    src/garden.cpp
    src/animation.cpp
    src/theme.cpp
    src/ui.cpp
    src/scenes.cpp
    src/gardensim.cpp
    src/hospitalsim.cpp
    src/hospital.cpp
)

target_include_directories(EnchantedGarden PRIVATE
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(EnchantedGarden PRIVATE
    glfw
    GLEW::GLEW
    OpenGL::GL
)

# Optional: headless test of the garden simulation (no window).  cmake -DBUILD_TESTS=ON
option(BUILD_TESTS "Build the garden simulation test" OFF)
if(BUILD_TESTS)
    add_executable(garden_sim_test
        tests/garden_sim_test.cpp src/gardensim.cpp src/theme.cpp src/ui.cpp)
    target_include_directories(garden_sim_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(garden_sim_test PRIVATE GLEW::GLEW OpenGL::GL)

    add_executable(hospital_sim_test tests/hospital_sim_test.cpp src/hospitalsim.cpp)
    target_include_directories(hospital_sim_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
endif()
