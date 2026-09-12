add_subdirectory(core)
add_subdirectory(data)

qt_add_executable(SmartScheduler
    app/main.cpp
)

qt_add_qml_module(SmartScheduler
    URI SmartScheduler
    VERSION 1.0
    QML_FILES
        ui/Main.qml
)

target_link_libraries(SmartScheduler PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::PrintSupport
    sched_core
    sched_data
)

set_target_properties(SmartScheduler PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
