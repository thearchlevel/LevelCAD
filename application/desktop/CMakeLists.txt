add_executable(LevelCAD
    src/main.cpp
    resources/branding.qrc
    src/MainWindow.cpp
    src/SchematicView.cpp
    src/DocumentIO.cpp
    src/SymbolLibrary.cpp
    src/ProjectImport.cpp
    src/BoardEditor.cpp
    src/ModelView.cpp
    src/MeshLibrary.cpp
    ../../libraries/models3d/sparkfun/models.qrc
    src/Language.cpp
    src/ExportDialog.cpp
    src/Collaboration.cpp src/BoardIO.cpp
    ../../resources/translations/translations.qrc
    ../../libraries/kicad/symbols.qrc
)
target_include_directories(LevelCAD PRIVATE include)
target_link_libraries(LevelCAD PRIVATE LevelCAD::Core Qt6::Widgets Qt6::Network)
set_target_properties(LevelCAD PROPERTIES WIN32_EXECUTABLE TRUE MACOSX_BUNDLE TRUE)

set_target_properties(LevelCAD PROPERTIES AUTORCC ON)





add_executable(LevelCADServer ../server/main.cpp src/Collaboration.cpp src/BoardIO.cpp src/DocumentIO.cpp)
target_include_directories(LevelCADServer PRIVATE include)
target_link_libraries(LevelCADServer PRIVATE LevelCAD::Core Qt6::Core Qt6::Network)

if(WIN32)
  enable_language(RC)
  set(LEVELCAD_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/img/LevelCAD.ico")
  configure_file(resources/LevelCAD.rc.in "${CMAKE_CURRENT_BINARY_DIR}/LevelCAD.rc" @ONLY)
  target_sources(LevelCAD PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LevelCAD.rc")
  target_sources(LevelCADServer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/LevelCAD.rc")
endif()
