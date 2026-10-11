add_executable(levelcad_core_tests test_document.cpp)
target_link_libraries(levelcad_core_tests PRIVATE LevelCAD::Core)
add_test(NAME core_document COMMAND levelcad_core_tests)
add_executable(levelcad_connectivity_tests test_connectivity.cpp)
target_link_libraries(levelcad_connectivity_tests PRIVATE LevelCAD::Core)
add_test(NAME electrical_connectivity COMMAND levelcad_connectivity_tests)
if(LEVELCAD_BUILD_DESKTOP)
  add_executable(levelcad_import_tests test_project_import.cpp
    ../application/desktop/src/ProjectImport.cpp
    ../application/desktop/src/SymbolLibrary.cpp)
  target_include_directories(levelcad_import_tests PRIVATE ../application/desktop/include)
  target_link_libraries(levelcad_import_tests PRIVATE LevelCAD::Core Qt6::Widgets)
  add_test(NAME project_import COMMAND levelcad_import_tests)
  add_executable(levelcad_desktop_tests test_desktop.cpp
    ../application/desktop/resources/branding.qrc
    ../application/desktop/src/Launcher.cpp
    ../application/desktop/src/DocumentIO.cpp
    ../application/desktop/src/SchematicView.cpp
    ../application/desktop/src/MainWindow.cpp
    ../application/desktop/src/SymbolLibrary.cpp
    ../application/desktop/src/ProjectImport.cpp
    ../application/desktop/src/BoardEditor.cpp
    ../application/desktop/src/ModelView.cpp
    ../application/desktop/src/MeshLibrary.cpp
    ../application/desktop/src/OfficialLibrary.cpp
    ../application/desktop/src/Production.cpp
    ../application/desktop/src/AiAssistant.cpp
    ../application/desktop/src/CadCommands.cpp
    ../application/desktop/src/PluginEngine.cpp
    ../libraries/models3d/sparkfun/models.qrc
    ../application/desktop/src/Language.cpp
    ../application/desktop/src/ExportDialog.cpp
    ../application/desktop/src/Collaboration.cpp ../application/desktop/src/BoardIO.cpp
    ../resources/translations/translations.qrc
    ../libraries/kicad/symbols.qrc)
  set_target_properties(levelcad_desktop_tests PROPERTIES AUTORCC ON)
  target_include_directories(levelcad_desktop_tests PRIVATE ../application/desktop/include)
  target_link_libraries(levelcad_desktop_tests PRIVATE LevelCAD::Core Qt6::Widgets Qt6::Network Qt6::Concurrent)
  add_test(NAME desktop_editor COMMAND levelcad_desktop_tests)
  get_target_property(LEVELCAD_QMAKE Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(LEVELCAD_QT_BIN "${LEVELCAD_QMAKE}" DIRECTORY)
  set_tests_properties(desktop_editor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PATH=${LEVELCAD_QT_BIN}\;$ENV{PATH}")
  set_tests_properties(project_import PROPERTIES ENVIRONMENT "PATH=${LEVELCAD_QT_BIN}\;$ENV{PATH}")
endif()






if(LEVELCAD_BUILD_DESKTOP)
  add_executable(levelcad_collaboration_tests test_collaboration.cpp ../application/desktop/src/Collaboration.cpp ../application/desktop/src/BoardIO.cpp ../application/desktop/src/DocumentIO.cpp)
  target_include_directories(levelcad_collaboration_tests PRIVATE ../application/desktop/include)
  target_link_libraries(levelcad_collaboration_tests PRIVATE LevelCAD::Core Qt6::Core Qt6::Network)
  add_test(NAME collaboration COMMAND levelcad_collaboration_tests)
  set_tests_properties(collaboration PROPERTIES ENVIRONMENT "PATH=${LEVELCAD_QT_BIN}\;$ENV{PATH}")
endif()

add_executable(levelcad_pcb_tests test_pcb.cpp)
target_link_libraries(levelcad_pcb_tests PRIVATE LevelCAD::Core)
add_test(NAME pcb_layout COMMAND levelcad_pcb_tests)
if(LEVELCAD_BUILD_DESKTOP)
  add_executable(levelcad_production_tests test_production.cpp ../application/desktop/src/Production.cpp ../application/desktop/src/BoardIO.cpp ../application/desktop/src/DocumentIO.cpp)
  target_include_directories(levelcad_production_tests PRIVATE ../application/desktop/include)
  target_link_libraries(levelcad_production_tests PRIVATE LevelCAD::Core Qt6::Core)
  add_test(NAME production COMMAND levelcad_production_tests)
  set_tests_properties(production PROPERTIES ENVIRONMENT "PATH=${LEVELCAD_QT_BIN}\;$ENV{PATH}")
endif()
