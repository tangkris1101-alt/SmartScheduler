# sched_core：核心领域模型静态库（仅依赖 Qt Core，不依赖 GUI）
add_library(sched_core STATIC
    model/ClassInfo.h
    model/Classroom.h
    model/CombinedGroup.h
    model/Constraints.h
    model/Course.h
    model/Group.h
    model/NamedEntity.h
    model/PlanEntry.h
    model/Schedule.cpp
    model/Schedule.h
    model/School.cpp
    model/School.h
    model/Teacher.h
    model/TimeConfig.h
)

target_include_directories(sched_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 领域模型中使用名为 slots 的成员（NoScheduleConstraint::slots），与 Qt 关键字宏冲突，故禁用之
target_compile_definitions(sched_core PUBLIC QT_NO_KEYWORDS)

target_link_libraries(sched_core PUBLIC Qt6::Core)
