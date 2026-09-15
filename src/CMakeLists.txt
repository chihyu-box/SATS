add_library(sats STATIC
    logic/pe_array_logic.cpp
    logic/scaler_logic.cpp
    logic/skew_buf_logic.cpp
    logic/spad_logic.cpp
    module/exec_controller.cpp
    module/load_controller.cpp
    module/memory_arbiter.cpp
    module/pe_array_module.cpp
    module/scaler_module.cpp
    module/skew_buf_module.cpp
    module/spad_module.cpp
    module/store_controller.cpp
    module/top_module.cpp
)

target_include_directories(sats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sats PUBLIC SystemC::systemc DRAMSys::DRAMSys)
target_compile_definitions(sats PUBLIC SC_INCLUDE_DYNAMIC_PROCESSES)

