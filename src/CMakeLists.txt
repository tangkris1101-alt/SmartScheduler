add_subdirectory(core)
add_subdirectory(data)
add_subdirectory(app)

qt_add_executable(SmartScheduler
    app/main.cpp
)

qt_add_qml_module(SmartScheduler
    URI SmartScheduler
    VERSION 1.0
    QML_FILES
        ui/Main.qml
        ui/AppButton.qml
        ui/AppCard.qml
        ui/AppComboBox.qml
        ui/AppConfirmDialog.qml
        ui/AppEmptyState.qml
        ui/AppStepper.qml
        ui/AppSwitch.qml
        ui/AppTabs.qml
        ui/AppTextField.qml
        ui/PlaceholderPage.qml
        ui/RosterCell.qml
        ui/TimetableGrid.qml
        ui/NewProjectDialog.qml
        ui/HomePage.qml
        ui/RosterPage.qml
        ui/GroupsPage.qml
        ui/CourseFeaturesPage.qml
        ui/PlanEditorPage.qml
        ui/LimitsPage.qml
        ui/SchedulerPage.qml
        ui/TimetablePage.qml
)

target_link_libraries(SmartScheduler PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::PrintSupport
    sched_core
    sched_data
    sched_engine
    sched_app
)

set_target_properties(SmartScheduler PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
