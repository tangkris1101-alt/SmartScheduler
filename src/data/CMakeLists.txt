# sched_data：SQLite 存储 + Excel 导入导出静态库（依赖 sched_core + Qt Sql + QXlsx，不依赖 GUI）
add_library(sched_data STATIC
    ExcelIo.cpp
    ExcelIo.h
    ProjectStore.cpp
    ProjectStore.h
)

target_include_directories(sched_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(sched_data PUBLIC sched_core Qt6::Sql QXlsx::QXlsx)
