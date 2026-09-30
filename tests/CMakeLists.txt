# 单元测试（Qt Test），由顶层在 BUILD_TESTING 时引入，注册进 CTest。

add_executable(tst_model tst_model.cpp)
target_link_libraries(tst_model PRIVATE sched_core Qt6::Test)
add_test(NAME tst_model COMMAND tst_model)

add_executable(tst_store tst_store.cpp)
target_link_libraries(tst_store PRIVATE sched_core sched_data Qt6::Sql Qt6::Test)
add_test(NAME tst_store COMMAND tst_store)

add_executable(tst_engine tst_engine.cpp)
target_link_libraries(tst_engine PRIVATE sched_core sched_engine Qt6::Test)
add_test(NAME tst_engine COMMAND tst_engine)

add_executable(tst_app tst_app.cpp)
target_link_libraries(tst_app PRIVATE sched_core sched_data sched_app Qt6::Test)
add_test(NAME tst_app COMMAND tst_app)

add_executable(tst_excel tst_excel.cpp)
target_link_libraries(tst_excel PRIVATE sched_core sched_data Qt6::Test)
add_test(NAME tst_excel COMMAND tst_excel)

add_executable(tst_metrics tst_metrics.cpp)
target_link_libraries(tst_metrics PRIVATE sched_core Qt6::Test)
add_test(NAME tst_metrics COMMAND tst_metrics)
