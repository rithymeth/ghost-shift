cmake_minimum_required(VERSION 3.24)
project(GhostShift LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Aether SDK: cmake --install <engine build> --prefix <dir>, then -DCMAKE_PREFIX_PATH=<dir>.
find_package(Aether CONFIG REQUIRED)

add_library(ghostshift_server_core STATIC
    server/src/dedicated_server.cpp server/src/server_config.cpp src/gs/cleaning.cpp)
target_include_directories(ghostshift_server_core PUBLIC server/include src)
target_link_libraries(ghostshift_server_core PUBLIC Aether::Net Aether::Engine)

add_executable(ghostshift_cleaning_test tests/cleaning_test.cpp)
target_link_libraries(ghostshift_cleaning_test PRIVATE ghostshift_server_core)

add_executable(ghostshift_netcond_test tests/netcond_test.cpp)
target_include_directories(ghostshift_netcond_test PRIVATE tests)
target_link_libraries(ghostshift_netcond_test PRIVATE ghostshift_server_core)
