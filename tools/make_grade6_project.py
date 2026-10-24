#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""按《六个年级排课报告》的要求生成 SmartScheduler 工程（.spk，SQLite）。

用法：python tools/make_grade6_project.py [输出路径]
      ONLY_GRADE=高三 python tools/make_grade6_project.py out.spk   # 只生成一个年级（排查用）

生成后可用应用内「自动排课」直接排：
      SmartScheduler.exe --smoke=solve --project=<spk> --runs=4

报告里能落成数据的硬性要求：
  · 六个年级的班数：初一 15 / 初二 15 / 初三 13 / 高一 16 / 高二 16 / 高三 17（共 92 班）
  · 每周课时总量：570 / 570 / 520 / 624 / 622 / 663（逐班 38/38/40/39/39(含 2 个 38)/39）
  · 固定格：自习、校本、劳动、教研、第 9 节体育活动、班会 —— 原位固定、不挂老师
  · 该连堂的连上：初一/初二 45 链、初三 39、高一 32、高二 48、高三 51
  · 走班同时段：高三 3 组；高二 1/2/3 班通用培优
  · 合班上课：高三体育一位老师带两个班
  · 共用格（单双周）：初二 综合实践/心理、高一 通用/心理、高二 音乐/美术
  · 老师不排时段：初三 3 位、高二 2 位；非文化科不排第 1 节；周一第 1 节班主任会
  · 跨年级带课：付裕、张嘉洇、邱建龙
"""
import os, sqlite3, sys

DAYS, PERIODS, MORNING, WEEKS = 5, 9, 5, 20
SCHEMA_VERSION = 5

def slot(day, period):
    """day/period 均 1 起始 → slot 序号"""
    return (day - 1) * PERIODS + (period - 1)

# ---------------------------------------------------------------- 课程
COURSES = [
    # name, is_major, no_load
    ("语文", 1, 0), ("数学", 1, 0), ("英语", 1, 0),
    ("物理", 0, 0), ("化学", 0, 0), ("生物", 0, 0),
    ("历史", 0, 0), ("地理", 0, 0), ("政治", 0, 0),
    ("体育", 0, 0), ("音乐", 0, 0), ("美术", 0, 0),
    ("信息", 0, 0), ("通用", 0, 0), ("心理", 0, 0), ("综合实践", 0, 0),
    ("班会", 0, 0),
    # 以下为固定格：不挂老师、不计课时
    ("自习", 0, 1), ("校本", 0, 1), ("劳动", 0, 1), ("教研", 0, 1), ("体育活动", 0, 1),
]
COURSE_ID = {name: i + 1 for i, (name, _, _) in enumerate(COURSES)}

# ---------------------------------------------------------------- 年级配置
# hours: 学科周课时；fixed: (课程, 日, 节, half)；chains: 每周连堂的课
GRADES = {
    "初一": dict(classes=15, hours={
        "语文": 7, "数学": 6, "英语": 7, "政治": 2, "历史": 2, "地理": 2, "生物": 2,
        "体育": 3, "音乐": 1, "美术": 1, "信息": 1, "综合实践": 1, "班会": 1,
    }, fixed=[("班会", 1, 8, 0), ("自习", 3, 9, 0), ("校本", 5, 9, 0)],
        chains=["语文", "英语", "生物"]),

    "初二": dict(classes=15, hours={
        "语文": 7, "数学": 6, "英语": 7, "政治": 2, "历史": 2, "地理": 2, "生物": 2,
        "体育": 3, "音乐": 1, "美术": 1, "信息": 1, "综合实践": 0.5, "心理": 0.5, "班会": 1,
    }, fixed=[("班会", 1, 8, 0), ("自习", 3, 9, 0), ("校本", 5, 9, 0),
              ],
        chains=["语文", "英语", "生物"]),

    "初三": dict(classes=13, hours={
        "语文": 7, "数学": 7, "英语": 7, "物理": 3, "化学": 3, "政治": 2, "历史": 2,
        "体育": 3, "音乐": 1, "美术": 1, "信息": 1, "综合实践": 1, "班会": 1,
    }, fixed=[("班会", 1, 8, 0), ("自习", 4, 9, 0)],
        chains=["语文", "英语", "物理"]),

    "高一": dict(classes=16, hours={
        "语文": 5, "数学": 5, "英语": 5, "物理": 3, "化学": 3, "生物": 2, "历史": 1,
        "地理": 1, "政治": 1, "体育": 2, "音乐": 1, "美术": 1, "信息": 1,
        "通用": 0.5, "心理": 0.5, "班会": 1,
    }, fixed=[("班会", 2, 7, 0), ("教研", 2, 9, 0), ("自习", 3, 9, 0),
              ("劳动", 4, 9, 0), ("校本", 5, 9, 0), ("体育活动", 1, 9, 0),
              ("体育活动", 3, 8, 0)],
        chains=["语文", "英语"]),

    "高二": dict(classes=16, hours={
        "语文": 5, "数学": 5, "英语": 5, "物理": 2, "化学": 3, "生物": 2, "历史": 2,
        "地理": 2, "政治": 2, "体育": 2, "音乐": 0.5, "美术": 0.5, "通用": 1, "班会": 1,
    }, fixed=[("班会", 1, 8, 0), ("校本", 2, 8, 0), ("教研", 3, 9, 0),

              ("体育活动", 1, 9, 0), ("体育活动", 2, 9, 0),
              ("体育活动", 4, 9, 0), ("体育活动", 5, 9, 0)],
        chains=["语文", "英语", "化学"]),

    "高三": dict(classes=17, hours={
        "语文": 6, "数学": 6, "英语": 6, "物理": 4, "化学": 3, "生物": 2,
        "历史": 1, "地理": 1, "政治": 1, "体育": 2, "班会": 1,
    }, fixed=[("班会", 1, 8, 0), ("自习", 3, 8, 0),
              ("体育活动", 1, 9, 0), ("体育活动", 2, 9, 0), ("体育活动", 3, 9, 0),
              ("体育活动", 4, 9, 0), ("体育活动", 5, 9, 0)],
        chains=["语文", "数学", "英语"]),
}

# 报告里点名的老师：跨年级 / 特殊不排时段
NAMED = {
    "付裕":   dict(assign=[("高一", "音乐", 13, 16), ("高二", "音乐", 1, 8)]),
    "张嘉洇": dict(assign=[("初一", "美术", 1, 2), ("初二", "美术", 1, 4)]),
    "邱建龙": dict(assign=[("初二", "信息", 1, 5), ("高一", "信息", 1, 5)]),
    "刁燕娴": dict(assign=[("初三", "数学", 1, 4)], forbid_periods=[(2, 1), (2, 2), (3, 1)]),
    "曹媛媛": dict(assign=[("初三", "语文", 1, 4)], forbid_periods=[(1, 1), (1, 2), (4, 1), (4, 2)]),
    "陈伟炼": dict(assign=[("初三", "英语", 1, 4)], forbid_periods=[(5, 1), (5, 2), (3, 2)]),
    "陈会作": dict(assign=[("高二", "物理", 1, 3)], forbid_periods=[(2, 3)]),
    "陈小阳": dict(assign=[("高二", "历史", 1, 3)], forbid_periods=[(4, 7)]),
}


def build(only=None):
    grades = {k: v for k, v in GRADES.items() if (only is None or k == only)}
    classes, teachers, plan, pre, nosched, teach_act = [], [], [], [], [], []
    combined, sync = [], []
    class_id = 0
    grade_of = {}          # class_id -> grade name
    class_index = {}       # (grade, 班号) -> class_id
    teacher_id = {}
    teacher_list = []

    def add_teacher(name, note=""):
        if name in teacher_id:
            return teacher_id[name]
        teacher_id[name] = len(teacher_list) + 1
        teacher_list.append((name, note))
        return teacher_id[name]

    # ---- 班级
    for gi, (gname, cfg) in enumerate(grades.items(), start=1):
        for n in range(1, cfg["classes"] + 1):
            class_id += 1
            classes.append((class_id, f"{gname}{n}班", f"{gname}{n}班", f"{gname}{n}班", gi))
            grade_of[class_id] = gname
            class_index[(gname, n)] = class_id

    # ---- 老师分工（同学科按班分段；点名的老师优先占用）
    assigned = {}          # (grade, subject, class_no) -> teacher name
    for tname, spec in NAMED.items():
        for (g, subj, a, b) in spec.get("assign", []):
            for n in range(a, b + 1):
                assigned[(g, subj, n)] = tname

    # 每位老师带几个班：按科目周课时折算，目标人均 ~14 节/周（接近真实学校）
    block = {"语文": 2, "数学": 2, "英语": 2, "物理": 4, "化学": 3, "生物": 6,
             "历史": 6, "地理": 6, "政治": 6, "体育": 3, "音乐": 4, "美术": 4,
             "信息": 4, "通用": 4, "心理": 4, "综合实践": 4}
    for gname, cfg in grades.items():
        ncls = cfg["classes"]
        subjects = [s for s in cfg["hours"] if s not in ("班会",)]
        for subj in subjects:
            step = block.get(subj, 3)
            n = 1
            while n <= ncls:
                if (gname, subj, n) in assigned:
                    n += 1
                    continue
                end = min(ncls, n + step - 1)
                # 跳过已被点名的班，把连续未分配的班凑成一位老师
                members = [k for k in range(n, end + 1) if (gname, subj, k) not in assigned]
                if not members:
                    n = end + 1
                    continue
                tname = f"{gname}{subj}{members[0]}"
                for k in members:
                    assigned[(gname, subj, k)] = tname
                n = end + 1

    # ---- 建老师（点名老师先建，保证 id 稳定）
    for tname in NAMED:
        add_teacher(tname)
    for (g, subj, n), tname in assigned.items():
        add_teacher(tname)
    # 班主任：每班一位（报告里「15 位班主任」即一班一位），班会与周一第 1 节的班主任会都归他
    for (gname, cfg) in grades.items():
        for n in range(1, cfg["classes"] + 1):
            add_teacher(f"{gname}{n}班班主任")

    # ---- 教学计划
    entry_id = 0
    for (gname, cfg) in grades.items():
        ncls = cfg["classes"]
        for n in range(1, ncls + 1):
            cid = class_index[(gname, n)]
            # 高二 1、2 班多一格自习：物理各减 1 节，保证 39 节总量
            hours = dict(cfg["hours"])
            if gname == "高二" and n <= 2:
                hours["物理"] = hours.get("物理", 2) - 1
            for subj, h in hours.items():
                if subj == "班会":
                    tname = f"{gname}{n}班班主任"
                else:
                    tname = assigned[(gname, subj, n)]
                entry_id += 1
                chained = 1 if subj in cfg["chains"] else 0
                plan.append(dict(id=entry_id, class_id=cid, course_id=COURSE_ID[subj],
                                 weekly=h, chained=chained, chain_len=2,
                                 no_load=0, teacher=teacher_id[tname]))
            # 固定格（不挂老师、不计课时）：同一门课的多格合并成一条计划，
            # 避免教学计划里出现多行「体育活动」（预排仍按每一格各建一条）
            fixed_count = {}
            for (subj, day, period, half) in cfg["fixed"]:
                if subj in hours:      # 已作为学科计入（如班会/通用/心理/音乐/美术/综合实践）
                    continue
                fixed_count[subj] = fixed_count.get(subj, 0) + 1
            for subj, cnt in fixed_count.items():
                entry_id += 1
                plan.append(dict(id=entry_id, class_id=cid, course_id=COURSE_ID[subj],
                                 weekly=cnt, chained=0, chain_len=2, no_load=1, teacher=None))
            # 高二 1、2 班多一格自习（报告里「自习 2 格」）；没有计划条目的话，
            # 预排会生成一条临时会话，质量报告里「排上的节数」就会多出来
            if gname == "高二" and n <= 2:
                entry_id += 1
                plan.append(dict(id=entry_id, class_id=cid, course_id=COURSE_ID["自习"],
                                 weekly=1, chained=0, chain_len=2, no_load=1, teacher=None))

    # ---- 预排（固定格）
    pid = 0
    used_by_class = {}          # class_id -> {slot}
    for (gname, cfg) in grades.items():
        for n in range(1, cfg["classes"] + 1):
            cid = class_index[(gname, n)]
            for (subj, day, period, half) in cfg["fixed"]:
                pid += 1
                sl = slot(day, period)
                pre.append((pid, cid, COURSE_ID[subj], sl, half))
                used_by_class.setdefault(cid, set()).add(sl)

    # ---- 单双周共用格（每班 1 格）：两门课各占半周，钉在同一格；
    #      同一格不能让同一位老师带两个班，所以逐班挑「该班两位老师都没用过」的时段
    HALF_PAIRS = {"初二": ("综合实践", "心理"), "高一": ("通用", "心理"), "高二": ("音乐", "美术")}
    teacher_slots = {}          # teacher name -> {slot}
    prefer = [(d, p) for pp in (8, 7, 9, 6) for d in range(1, DAYS + 1) for p in (pp,)]
    for gname, (sa, sb) in HALF_PAIRS.items():
        if gname not in grades:
            continue
        cfg = grades[gname]
        for n in range(1, cfg["classes"] + 1):
            cid = class_index[(gname, n)]
            ta = assigned[(gname, sa, n)]
            tb = assigned[(gname, sb, n)]
            busy = used_by_class.setdefault(cid, set())
            chosen = None
            for (day, period) in prefer:
                sl = slot(day, period)
                if sl in busy:
                    continue
                if sl in teacher_slots.get(ta, ()) or sl in teacher_slots.get(tb, ()):
                    continue
                chosen = sl
                break
            if chosen is None:
                raise SystemExit(f"没有可用时段：{gname}{n}班 {sa}/{sb}")
            busy.add(chosen)
            teacher_slots.setdefault(ta, set()).add(chosen)
            teacher_slots.setdefault(tb, set()).add(chosen)
            pid += 1
            pre.append((pid, cid, COURSE_ID[sa], chosen, 1))
            pid += 1
            pre.append((pid, cid, COURSE_ID[sb], chosen, 2))
    # 高二 1、2 班的自习（周五第 8 节）
    for n in ((1, 2) if "高二" in grades else ()):
        pid += 1
        pre.append((pid, class_index[("高二", n)], COURSE_ID["自习"], slot(5, 8), 0))

    # ---- 老师不排时段
    nc = 0
    for tname, spec in NAMED.items():
        if "forbid_periods" not in spec:
            continue
        nc += 1
        for (day, period) in spec["forbid_periods"]:
            nosched.append((nc, 2, teacher_id[tname], slot(day, period)))
    # 非文化科不排第 1 节（高一/高二/高三）—— 报告里「非文化科尽量不排第 1 节」已做到 0 节
    # 体育不设「第 1 节」硬禁：它是每周 3 节的重课，禁掉第 1 节会把可行域压死（报告里这条是「尽量」）
    for subj in ("音乐", "美术", "信息", "通用", "心理", "综合实践"):
        nc += 1
        for day in range(1, DAYS + 1):
            nosched.append((nc, 1, COURSE_ID[subj], slot(day, 1)))

    # ---- 班主任会（周一第 1 节，初一 & 高三 班主任不上课）
    aid = 0
    for gname in ("初一", "高三"):
        if gname not in grades:
            continue
        cfg = grades[gname]
        for n in range(1, cfg["classes"] + 1):
            aid += 1
            teach_act.append((aid, teacher_id[f"{gname}{n}班班主任"], slot(1, 1), -1))

    # ---- 走班（同时段）：高三 3 组
    gid = 0
    sync_groups = []
    def add_sync(name, members):
        nonlocal gid
        gid += 1
        sync_groups.append((gid, name, members))
    if "高三" in grades:
        # 走班：同时段组 + 把各成员的第一节钉在同一格（引擎对同步组的搜索较弱，
        # 用预排把「同一节」这件事直接定死，课表效果与走班一致）
        for name, members, (day, period) in [
                ("高三走班 A", [("高三", 16, "生物"), ("高三", 14, "政治"), ("高三", 1, "地理")], (2, 6)),
                ("高三走班 B", [("高三", 15, "化学"), ("高三", 7, "生物")], (2, 7)),
                ("高三走班 C", [("高三", 3, "地理"), ("高三", 4, "生物")], (4, 6))]:
            add_sync(name, members)
            for (g, n, subj) in members:
                pid += 1
                pre.append((pid, class_index[(g, n)], COURSE_ID[subj], slot(day, period), 0))

    # ---- 合班：高三体育两班一起（一位老师）、高二 1/2/3 班通用培优
    cg = 0
    combined_groups = []
    pairs = [(1, 2), (3, 4), (5, 6), (7, 8), (9, 10), (11, 12), (13, 14), (15, 16), (17, None)]
    for (a, b) in (pairs if "高三" in grades else []):
        members = [("高三", a, "体育")]
        if b:
            members.append(("高三", b, "体育"))
        cg += 1
        combined_groups.append((cg, f"高三体育 {a}{'/' + str(b) if b else ''}", members))
    if "高二" in grades:
        cg += 1
        combined_groups.append((cg, "高二通用培优",
                                [("高二", 1, "通用"), ("高二", 2, "通用"), ("高二", 3, "通用")]))

    return dict(classes=classes, teachers=teacher_list, plan=plan, pre=pre,
                nosched=nosched, teach_act=teach_act, sync=sync_groups,
                combined=combined_groups, class_index=class_index)


def write_spk(data, path):
    if os.path.exists(path):
        os.remove(path)
    db = sqlite3.connect(path)
    cur = db.cursor()
    cur.executescript("""
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE classes(id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '', long_name TEXT NOT NULL DEFAULT '',
 short_name TEXT NOT NULL DEFAULT '', code TEXT NOT NULL DEFAULT '', priority INTEGER NOT NULL DEFAULT 0,
 remark TEXT NOT NULL DEFAULT '', grade_id INTEGER NOT NULL DEFAULT -1);
CREATE TABLE courses(id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '', long_name TEXT NOT NULL DEFAULT '',
 short_name TEXT NOT NULL DEFAULT '', code TEXT NOT NULL DEFAULT '', priority INTEGER NOT NULL DEFAULT 0,
 remark TEXT NOT NULL DEFAULT '', group_id INTEGER NOT NULL DEFAULT -1, is_major INTEGER NOT NULL DEFAULT 0,
 is_science INTEGER NOT NULL DEFAULT 0, even_spread INTEGER NOT NULL DEFAULT 0,
 progress_align_count INTEGER NOT NULL DEFAULT 2, am_pm_policy INTEGER NOT NULL DEFAULT 0);
CREATE TABLE teachers(id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '', long_name TEXT NOT NULL DEFAULT '',
 short_name TEXT NOT NULL DEFAULT '', code TEXT NOT NULL DEFAULT '', priority INTEGER NOT NULL DEFAULT 0,
 remark TEXT NOT NULL DEFAULT '', group_id INTEGER NOT NULL DEFAULT -1,
 max_periods_per_day INTEGER NOT NULL DEFAULT -1, mutual_exclusion_group INTEGER NOT NULL DEFAULT -1);
CREATE TABLE classrooms(id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '', long_name TEXT NOT NULL DEFAULT '',
 short_name TEXT NOT NULL DEFAULT '', code TEXT NOT NULL DEFAULT '', priority INTEGER NOT NULL DEFAULT 0,
 remark TEXT NOT NULL DEFAULT '', capacity INTEGER NOT NULL DEFAULT 1);
CREATE TABLE entity_groups(id INTEGER PRIMARY KEY, type INTEGER NOT NULL DEFAULT 0, name TEXT NOT NULL DEFAULT '',
 mask_days INTEGER NOT NULL DEFAULT -1, mask_periods INTEGER NOT NULL DEFAULT -1);
CREATE TABLE group_members(group_id INTEGER NOT NULL, member_id INTEGER NOT NULL, PRIMARY KEY(group_id, member_id));
CREATE TABLE plan_entries(id INTEGER PRIMARY KEY, class_id INTEGER NOT NULL, course_id INTEGER NOT NULL,
 weekly_periods REAL NOT NULL DEFAULT 0, chained_count INTEGER NOT NULL DEFAULT 0,
 chain_length INTEGER NOT NULL DEFAULT 2, morning_periods INTEGER NOT NULL DEFAULT -1,
 early_periods INTEGER NOT NULL DEFAULT -1, is_combined INTEGER NOT NULL DEFAULT 0,
 classroom_id INTEGER NOT NULL DEFAULT -1, start_week INTEGER NOT NULL DEFAULT 1,
 end_week INTEGER NOT NULL DEFAULT 0, sync_course_id INTEGER NOT NULL DEFAULT -1,
 progress_group INTEGER NOT NULL DEFAULT -1, no_load INTEGER NOT NULL DEFAULT 0);
CREATE TABLE plan_entry_teachers(entry_id INTEGER NOT NULL, teacher_id INTEGER NOT NULL, PRIMARY KEY(entry_id, teacher_id));
CREATE TABLE combined_groups(id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '');
CREATE TABLE time_sync_groups(id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '');
CREATE TABLE time_sync_group_members(group_id INTEGER NOT NULL, class_id INTEGER NOT NULL, course_id INTEGER NOT NULL);
CREATE TABLE combined_group_members(group_id INTEGER NOT NULL, class_id INTEGER NOT NULL, course_id INTEGER NOT NULL);
CREATE TABLE no_schedule(constraint_id INTEGER NOT NULL, entity_type INTEGER NOT NULL,
 entity_id INTEGER NOT NULL, slot INTEGER NOT NULL, PRIMARY KEY(constraint_id, slot));
CREATE TABLE preassign(id INTEGER PRIMARY KEY, class_id INTEGER NOT NULL, course_id INTEGER NOT NULL,
 slot_index INTEGER NOT NULL, half INTEGER NOT NULL DEFAULT 0);
CREATE TABLE teacher_activities(id INTEGER PRIMARY KEY, teacher_id INTEGER NOT NULL,
 slot_index INTEGER NOT NULL, activity_course_id INTEGER NOT NULL DEFAULT -1);
CREATE TABLE schedule_assignments(class_id INTEGER NOT NULL, slot_index INTEGER NOT NULL,
 half INTEGER NOT NULL DEFAULT 0, course_id INTEGER NOT NULL, PRIMARY KEY(class_id, slot_index, half));
""")
    cur.executemany("INSERT INTO meta(key,value) VALUES(?,?)", [
        ("schema_version", str(SCHEMA_VERSION)),
        ("school_name", "六个年级（报告要求）"),
        ("days_per_week", str(DAYS)),
        ("periods_per_day", str(PERIODS)),
        ("morning_periods", str(MORNING)),
        ("weeks_per_term", str(WEEKS)),
    ])
    cur.executemany(
        "INSERT INTO classes(id,name,long_name,short_name,grade_id) VALUES(?,?,?,?,?)",
        data["classes"])
    for i, (name, is_major, _no_load) in enumerate(COURSES, start=1):
        cur.execute("INSERT INTO courses(id,name,long_name,short_name,is_major) VALUES(?,?,?,?,?)",
                    (i, name, name, name, is_major))
    for i, (name, note) in enumerate(data["teachers"], start=1):
        cur.execute("INSERT INTO teachers(id,name,long_name,short_name,remark) VALUES(?,?,?,?,?)",
                    (i, name, name, name, note))
    for e in data["plan"]:
        cur.execute("""INSERT INTO plan_entries(id,class_id,course_id,weekly_periods,chained_count,
                       chain_length,no_load) VALUES(?,?,?,?,?,?,?)""",
                    (e["id"], e["class_id"], e["course_id"], e["weekly"], e["chained"],
                     e["chain_len"], e["no_load"]))
        if e["teacher"]:
            cur.execute("INSERT INTO plan_entry_teachers(entry_id,teacher_id) VALUES(?,?)",
                        (e["id"], e["teacher"]))
    cur.executemany("INSERT INTO preassign(id,class_id,course_id,slot_index,half) VALUES(?,?,?,?,?)",
                    data["pre"])
    cur.executemany("INSERT INTO no_schedule(constraint_id,entity_type,entity_id,slot) VALUES(?,?,?,?)",
                    data["nosched"])
    cur.executemany("INSERT INTO teacher_activities(id,teacher_id,slot_index,activity_course_id) VALUES(?,?,?,?)",
                    data["teach_act"])
    for (gid, name, members) in data["sync"]:
        cur.execute("INSERT INTO time_sync_groups(id,name) VALUES(?,?)", (gid, name))
        for (g, n, subj) in members:
            cur.execute("INSERT INTO time_sync_group_members(group_id,class_id,course_id) VALUES(?,?,?)",
                        (gid, data["class_index"][(g, n)], COURSE_ID[subj]))
    for (gid, name, members) in data["combined"]:
        cur.execute("INSERT INTO combined_groups(id,name) VALUES(?,?)", (gid, name))
        for (g, n, subj) in members:
            cur.execute("INSERT INTO combined_group_members(group_id,class_id,course_id) VALUES(?,?,?)",
                        (gid, data["class_index"][(g, n)], COURSE_ID[subj]))
    db.commit()
    db.close()


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\Administrator\Desktop\六个年级排课.spk"
    import os as _os
    d = build(_os.environ.get("ONLY_GRADE") or None)
    write_spk(d, out)
    print("written", out)
    # 自检统计
    from collections import Counter
    per_class = Counter()
    for e in d["plan"]:
        per_class[e["class_id"]] += e["weekly"]
    totals = Counter()
    for cid, h in per_class.items():
        g = [g for g in GRADES for k in [1] if True]
    # 按年级汇总
    grade_total = Counter()
    for cid, h in per_class.items():
        # class_id → grade
        pass
    print("classes", len(d["classes"]), "teachers", len(d["teachers"]),
          "entries", len(d["plan"]), "pre", len(d["pre"]),
          "nosched", len(d["nosched"]), "acts", len(d["teach_act"]),
          "sync", len(d["sync"]), "combined", len(d["combined"]))
