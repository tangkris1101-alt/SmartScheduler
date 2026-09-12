# sched_engine：自动排课引擎静态库（依赖 sched_core，QObject API 供 QML 桥接）
add_library(sched_engine STATIC
    EngineCore.cpp
    EngineCore.h
    EngineTypes.h
    SchedulerEngine.cpp
    SchedulerEngine.h
)

target_include_directories(sched_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sched_engine PUBLIC sched_core)
