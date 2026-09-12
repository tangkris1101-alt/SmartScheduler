#pragma once

namespace sched {

// 作息时间结构：描述一周几天、每天几节等全局时间参数
struct TimeConfig {
    int daysPerWeek = 5;    // 每周上课天数
    int periodsPerDay = 7;  // 每天节数
    int morningPeriods = 4; // 其中上午节数（其余为下午）
    int weeksPerTerm = 20;  // 学期周数

    // 一周总节次（slot）数
    int slotCount() const { return daysPerWeek * periodsPerDay; }

    // slot 序号换算：day、period 均为 0 起始
    int slotIndex(int day, int period) const { return day * periodsPerDay + period; }
    int slotDay(int index) const { return index / periodsPerDay; }
    int slotPeriod(int index) const { return index % periodsPerDay; }

    bool isValidSlot(int index) const { return index >= 0 && index < slotCount(); }

    bool operator==(const TimeConfig &) const = default;
};

} // namespace sched
