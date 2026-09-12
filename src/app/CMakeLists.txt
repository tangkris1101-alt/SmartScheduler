# sched_app：QML 桥接层静态库（纯 Qt Core，不依赖 Qt Quick，便于单元测试）
add_library(sched_app STATIC
    AppController.cpp
    AppController.h
    CombinedModel.cpp
    CombinedModel.h
    GroupModel.cpp
    GroupModel.h
    PlanModels.cpp
    PlanModels.h
    RosterModels.cpp
    RosterModels.h
    Theme.cpp
    Theme.h
)

target_include_directories(sched_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sched_app PUBLIC sched_core sched_data Qt6::Core Qt6::Gui)
