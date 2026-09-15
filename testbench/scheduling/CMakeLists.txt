add_executable(scheduling main.cpp)
target_link_libraries(scheduling PRIVATE sats)
target_include_directories(scheduling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../helper)
