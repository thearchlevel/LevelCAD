add_library(levelcad_core STATIC
    core/src/Document.cpp
    connectivity/src/Connectivity.cpp
    pcb/src/Board.cpp
    ../exporters/spice/src/SpiceExporter.cpp
)
target_include_directories(levelcad_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/connectivity/include
    ${CMAKE_CURRENT_SOURCE_DIR}/pcb/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../exporters/spice/include
)
target_compile_features(levelcad_core PUBLIC cxx_std_20)
add_library(LevelCAD::Core ALIAS levelcad_core)

