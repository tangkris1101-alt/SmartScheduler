# 3rdparty

第三方库（源码内嵌，随主仓库提交）。

- **QXlsx**（`QXlsx/`，v1.5.0，MIT）：Excel .xlsx 读写，用于 Excel 批量导入与课表导出。
  顶层 CMake `add_subdirectory(3rdparty/QXlsx/QXlsx)`，链接目标 `QXlsx::QXlsx`（静态）。
- 排课引擎与领域模型不依赖第三方库。
