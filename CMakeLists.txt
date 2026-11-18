cmake_minimum_required(VERSION 3.24)
project(GhostShift LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Aether SDK: cmake --install <engine build> --prefix <dir>, then -DCMAKE_PREFIX_PATH=<dir>.
find_package(Aether CONFIG REQUIRED)

add_library(ghostshift_server_core STATIC
    server/src/dedicated_server.cpp server/src/server_config.cpp src/gs/cleaning.cpp src/gs/haunting.cpp src/gs/trap.cpp src/gs/contract.cpp src/gs/caretaker.cpp src/gs/scene_setup.cpp)
target_include_directories(ghostshift_server_core PUBLIC server/include src)
target_link_libraries(ghostshift_server_core PUBLIC Aether::Net Aether::Save Aether::Engine)

add_executable(ghostshift_cleaning_test tests/cleaning_test.cpp)
target_link_libraries(ghostshift_cleaning_test PRIVATE ghostshift_server_core)

add_executable(ghostshift_netcond_test tests/netcond_test.cpp)
target_include_directories(ghostshift_netcond_test PRIVATE tests)
target_link_libraries(ghostshift_netcond_test PRIVATE ghostshift_server_core)

add_executable(ghostshift_session_test tests/session_test.cpp)
target_include_directories(ghostshift_session_test PRIVATE tests)
target_link_libraries(ghostshift_session_test PRIVATE ghostshift_server_core)

add_executable(ghostshift_haunting_test tests/haunting_test.cpp)
target_link_libraries(ghostshift_haunting_test PRIVATE ghostshift_server_core)

add_executable(ghostshift_trap_test tests/trap_test.cpp)
target_link_libraries(ghostshift_trap_test PRIVATE ghostshift_server_core)

add_executable(ghostshift_contract_test tests/contract_test.cpp)
target_link_libraries(ghostshift_contract_test PRIVATE ghostshift_server_core)

add_executable(ghostshift_caretaker_test tests/caretaker_test.cpp)
target_link_libraries(ghostshift_caretaker_test PRIVATE ghostshift_server_core)

# The assembled game server: loads the cooked game headless and runs the Ghost Shift systems (needs the SDK's Aether::Game).
add_executable(ghostshift_server server/main.cpp)
target_compile_definitions(ghostshift_server PRIVATE AETHER_SERVER_PHYSICS=0)
target_link_libraries(ghostshift_server PRIVATE ghostshift_server_core Aether::Game)

add_executable(ghostshift_playtest tests/playtest_client.cpp)
target_link_libraries(ghostshift_playtest PRIVATE ghostshift_server_core)

add_executable(ghostshift_scene_setup_test tests/scene_setup_test.cpp)
target_link_libraries(ghostshift_scene_setup_test PRIVATE ghostshift_server_core)
