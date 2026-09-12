# 智排课系统 SmartScheduler

基于 **原生 Qt 6 (C++20 + QML / Qt Quick)** 的现代化中小学自动排课系统。
以经典软件「自明排课系统 9.0」为功能蓝本完全重写，覆盖其全部排课能力，并弥补其在兼容性、数据开放性、批量录入、算法质量与界面体验上的不足。

## 功能概览

- **全流程排课**：名单管理 → 教学计划 → 限制条件 → 自动排课 → 手工调整 → 打印/导出
- **完整约束模型**：合班课、单双周课、连课、进度一致（教案对齐）、场地/教室容量、互斥老师、日最多课、不排时间、固定预排、老师活动时间
- **智能引擎**：约束传播 + 局部搜索（min-conflicts），硬约束零违反保证、软约束量化评分、best-of-N 自动取最优、随机种子可复现
- **现代体验**：Fluent 风格 UI、深色模式、拖拽调课（实时冲突高亮）、撤销/重做、新手向导
- **开放数据**：SQLite 项目文件（`.spk`）、Excel 批量导入/课表导出、PDF/PNG 输出、JSON 备份

## 技术栈

| 层 | 选型 |
|---|---|
| 语言 / GUI | C++20 · Qt 6.8 LTS · Qt Quick (QML) + Quick Controls 2 |
| 构建 | CMake ≥ 3.24 + Ninja，MinGW 13 64-bit |
| 存储 | SQLite（Qt SQL） |
| Excel | QXlsx（vendor 于 `3rdparty/`） |
| 打印 / PDF | Qt PrintSupport · QPdfWriter |
| 测试 / CI | Qt Test · GitHub Actions |

## 构建

> ⚠️ Qt 的 MinGW 工具链（qmlimportscanner / qmlcachegen）**不支持非 ASCII 路径**。
> 请保证项目完整路径全为英文。本机位置：`C:\Users\Administrator\Desktop\SmartScheduler`

```bat
:: 1. 注入本机工具链环境（C:\Qt，由 aqtinstall 安装）
scripts\env.bat

:: 2. 配置 → 构建 → 测试 → 运行
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.1/mingw_64
cmake --build build
ctest --test-dir build --output-on-failure
build\SmartScheduler.exe
```

## 目录结构

```
src/core     领域模型 + 约束系统 + 排课引擎（纯 C++，无 GUI 依赖，可单测）
src/data     SQLite 存储 · Excel/JSON 导入导出
src/app      程序入口 · QML 桥接 ViewModel
src/ui       QML 界面 · 主题 · 组件
tests        Qt Test 单元测试
docs         功能对照（老软件说明书 → 新实现映射）
3rdparty     第三方库（QXlsx）
```

## 路线图

- **v1.0**：行政班排课全流程 + Excel/PDF 输出
- **v1.5**：课表模板可视化设计器（对标老软件"增强打印"）
- **v2.0**：局域网多用户协同 · 新高考走班选课排课
