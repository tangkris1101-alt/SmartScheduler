#include "PrintController.h"

#include "AppController.h"
#include "ExcelIo.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QPrinter>
#include <QPrintDialog>
#include <QStandardPaths>
#include <QUrl>

namespace {

// 打印渲染选项
struct PrintOptions {
    bool landscape = true;
    int tablesPerPage = 1;
    int fontSize = 10;
    bool showTeachers = true;
    bool lunchLine = true;
    bool showLock = true;
    QString headerNote;

    static PrintOptions fromMap(const QVariantMap &m)
    {
        PrintOptions o;
        o.landscape = m.value(QStringLiteral("landscape"), true).toBool();
        o.tablesPerPage = qBound(1, m.value(QStringLiteral("tablesPerPage"), 1).toInt(), 2);
        o.fontSize = qBound(7, m.value(QStringLiteral("fontSize"), 10).toInt(), 18);
        o.showTeachers = m.value(QStringLiteral("showTeachers"), true).toBool();
        o.lunchLine = m.value(QStringLiteral("lunchLine"), true).toBool();
        o.showLock = m.value(QStringLiteral("showLock"), true).toBool();
        o.headerNote = m.value(QStringLiteral("headerNote")).toString();
        return o;
    }
};

const QStringList kDayNames = {QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
                               QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"),
                               QStringLiteral("周日")};

QString cellTextOf(const sched::School &s, qint64 classId, int slot, const PrintOptions &opt,
                   bool withTeacher)
{
    QStringList lines;
    for (const auto &a : s.schedule.assignments) {
        if (a.classId != classId || a.slotIndex != slot)
            continue;
        const sched::Course *co = s.findCourse(a.courseId);
        QString text = co ? (co->shortName.isEmpty() ? co->name : co->shortName) : QStringLiteral("?");
        // 预排锁标
        if (opt.showLock) {
            for (const auto &p : s.preAssignments) {
                if (p.classId == classId && p.slotIndex == slot) {
                    text = QStringLiteral("[预]") + text;
                    break;
                }
            }
        }
        if (a.half == 1)
            text += QStringLiteral("(单)");
        else if (a.half == 2)
            text += QStringLiteral("(双)");
        if (withTeacher) {
            QStringList names;
            for (const auto &e : s.planEntries) {
                if (e.classId == classId && e.courseId == a.courseId) {
                    for (qint64 tid : e.teacherIds) {
                        if (const sched::Teacher *t = s.findTeacher(tid))
                            names.append(t->name);
                    }
                    break;
                }
            }
            if (!names.isEmpty())
                text += QStringLiteral("\n") + names.join(QStringLiteral("、"));
        }
        lines.append(text);
    }
    return lines.join(QStringLiteral(" / "));
}

QString teacherCellText(const sched::School &s, qint64 teacherId, int slot)
{
    QStringList lines;
    for (const auto &occ : s.teacherOccupancy(teacherId, slot)) {
        const sched::ClassInfo *c = s.findClass(occ.classId);
        const sched::Course *co = s.findCourse(occ.courseId);
        QString text = QStringLiteral("%1 %2").arg(c ? c->name : QStringLiteral("活动"),
                                                   co ? co->name : QStringLiteral("?"));
        if (occ.half == 1)
            text += QStringLiteral("(单)");
        else if (occ.half == 2)
            text += QStringLiteral("(双)");
        lines.append(text);
    }
    return lines.join(QStringLiteral("\n"));
}

QString roomCellText(const sched::School &s, qint64 roomId, int slot)
{
    QStringList lines;
    for (const auto &a : s.schedule.assignments) {
        if (a.slotIndex != slot)
            continue;
        for (const auto &e : s.planEntries) {
            if (e.classId == a.classId && e.courseId == a.courseId && e.classroomId == roomId) {
                const sched::ClassInfo *c = s.findClass(a.classId);
                const sched::Course *co = s.findCourse(a.courseId);
                lines.append(QStringLiteral("%1 %2").arg(c ? c->name : QStringLiteral("?"),
                                                         co ? co->name : QStringLiteral("?")));
                break;
            }
        }
    }
    return lines.join(QStringLiteral("\n"));
}

// 页眉：校名 + 标题 + 说明
void paintHeader(QPainter &p, const QRect &area, const sched::School &s, const QString &title,
                 const PrintOptions &opt)
{
    QFont f = p.font();
    f.setPointSize(opt.fontSize + 6);
    f.setBold(true);
    p.setFont(f);
    const QRect titleRect(area.left(), area.top(), area.width(), area.height() * 2 / 3);
    p.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("%1 %2").arg(s.name, title));
    f.setPointSize(opt.fontSize - 1);
    f.setBold(false);
    p.setFont(f);
    if (!opt.headerNote.isEmpty()) {
        p.drawText(QRect(area.left(), area.top() + titleRect.height(), area.width(),
                         area.height() - titleRect.height()),
                   Qt::AlignHCenter | Qt::AlignVCenter, opt.headerNote);
    }
}

// 通用周课表网格（天×节次）
void paintWeekGrid(QPainter &p, const QRect &area, const sched::School &s, const PrintOptions &opt,
                   const std::function<QString(int slot)> &cellAt)
{
    const auto &tc = s.timeConfig;
    const int days = tc.daysPerWeek;
    const int periods = tc.periodsPerDay;

    const int headH = area.height() / (periods + 3);
    const int headW = area.width() / (days + 3);
    const int rowH = (area.height() - headH) / periods;
    const int colW = (area.width() - headW) / days;

    QFont f = p.font();
    f.setPointSize(opt.fontSize);
    p.setFont(f);

    // 表头
    p.drawText(QRect(area.left(), area.top(), headW, headH),
               Qt::AlignCenter, QStringLiteral("节次"));
    for (int d = 0; d < days; ++d)
        p.drawText(QRect(area.left() + headW + d * colW, area.top(), colW, headH), Qt::AlignCenter,
                   kDayNames[d]);

    const QPen thin(Qt::gray, 1);
    const QPen thick(Qt::black, 2);
    for (int pr = 0; pr < periods; ++pr) {
        const int y = area.top() + headH + pr * rowH;
        p.drawText(QRect(area.left(), y, headW, rowH), Qt::AlignCenter,
                   QStringLiteral("第%1节").arg(pr + 1));
        for (int d = 0; d < days; ++d) {
            const QRect cell(area.left() + headW + d * colW, y, colW, rowH);
            p.setPen(thin);
            p.drawRect(cell);
            const QString text = cellAt(tc.slotIndex(d, pr));
            if (!text.isEmpty())
                p.drawText(cell.adjusted(2, 2, -2, -2), Qt::AlignCenter | Qt::TextWordWrap, text);
        }
    }
    // 外框与午休分隔
    p.setPen(thick);
    p.drawRect(QRect(area.left(), area.top(), headW + days * colW, headH + periods * rowH));
    if (opt.lunchLine && tc.morningPeriods > 0 && tc.morningPeriods < periods) {
        const int y = area.top() + headH + tc.morningPeriods * rowH;
        p.drawLine(area.left(), y, area.left() + headW + days * colW, y);
    }
    p.setPen(thin);
    for (int d = 0; d <= days; ++d)
        p.drawLine(area.left() + headW + d * colW, area.top(), area.left() + headW + d * colW,
                   area.top() + headH + periods * rowH);
    p.drawLine(area.left(), area.top() + headH, area.left() + headW + days * colW,
               area.top() + headH);
}

// 单个对象（班级/老师/教室）的一整张周课表
void paintEntityTable(QPainter &p, const QRect &page, const sched::School &s, const QString &scope,
                      qint64 id, const PrintOptions &opt)
{
    const int margin = page.width() / 24;
    const QRect area = page.adjusted(margin, margin, -margin, -margin);
    const int headerH = area.height() / 12;
    QString title, name;
    std::function<QString(int)> cellAt;
    if (scope == QLatin1StringView("class")) {
        const sched::ClassInfo *c = s.findClass(id);
        name = c ? c->name : QStringLiteral("?");
        title = QStringLiteral("班级课表 · %1").arg(name);
        cellAt = [&](int slot) { return cellTextOf(s, id, slot, opt, opt.showTeachers); };
    } else if (scope == QLatin1StringView("teacher")) {
        const sched::Teacher *t = s.findTeacher(id);
        name = t ? t->name : QStringLiteral("?");
        title = QStringLiteral("老师任课表 · %1").arg(name);
        cellAt = [&](int slot) { return teacherCellText(s, id, slot); };
    } else {
        const sched::Classroom *r = s.findClassroom(id);
        name = r ? r->name : QStringLiteral("?");
        title = QStringLiteral("教室使用表 · %1").arg(name);
        cellAt = [&](int slot) { return roomCellText(s, id, slot); };
    }
    paintHeader(p, QRect(area.left(), area.top(), area.width(), headerH), s, title, opt);
    paintWeekGrid(p, QRect(area.left(), area.top() + headerH, area.width(), area.height() - headerH),
                  s, opt, cellAt);
}

// 总课表（行=班级，列=节次），rowsPerPage 分页
void paintMasterPage(QPainter &p, const QRect &page, const sched::School &s, int firstRow,
                     int rowsPerPage, const PrintOptions &opt)
{
    const int margin = page.width() / 24;
    const QRect area = page.adjusted(margin, margin, -margin, -margin);
    const int headerH = area.height() / 12;
    paintHeader(p, QRect(area.left(), area.top(), area.width(), headerH), s,
                QStringLiteral("总课表"), opt);

    const auto &tc = s.timeConfig;
    const int slots = tc.slotCount();
    const QRect gridArea(area.left(), area.top() + headerH, area.width(), area.height() - headerH);
    const int nameW = gridArea.width() / 12;
    const int headH = gridArea.height() / (rowsPerPage + 4);
    const int colW = (gridArea.width() - nameW) / slots;
    const int rowH = (gridArea.height() - headH) / rowsPerPage;

    QFont f = p.font();
    f.setPointSize(std::max(6, opt.fontSize - 2));
    p.setFont(f);

    // 表头：节次
    p.drawText(QRect(gridArea.left(), gridArea.top(), nameW, headH), Qt::AlignCenter,
               QStringLiteral("班级"));
    for (int sl = 0; sl < slots; ++sl) {
        p.drawText(QRect(gridArea.left() + nameW + sl * colW, gridArea.top(), colW, headH),
                   Qt::AlignCenter,
                   QStringLiteral("%1.%2").arg(kDayNames[sl / tc.periodsPerDay].mid(1))
                       .arg(sl % tc.periodsPerDay + 1));
    }
    p.setPen(QPen(Qt::gray, 1));
    const int lastRow = std::min(firstRow + rowsPerPage, int(s.classes.size()));
    for (int r = firstRow; r < lastRow; ++r) {
        const int y = gridArea.top() + headH + (r - firstRow) * rowH;
        const auto &c = s.classes[r];
        p.drawText(QRect(gridArea.left(), y, nameW, rowH), Qt::AlignCenter, c.name);
        for (int sl = 0; sl < slots; ++sl) {
            const QRect cell(gridArea.left() + nameW + sl * colW, y, colW, rowH);
            p.drawRect(cell);
            QString text;
            for (const auto &a : s.schedule.assignments) {
                if (a.classId == c.id && a.slotIndex == sl) {
                    const sched::Course *co = s.findCourse(a.courseId);
                    text = co ? (co->shortName.isEmpty() ? co->name : co->shortName)
                              : QStringLiteral("?");
                    if (a.half == 1)
                        text += QStringLiteral("单");
                    else if (a.half == 2)
                        text += QStringLiteral("双");
                    break;
                }
            }
            if (!text.isEmpty())
                p.drawText(cell, Qt::AlignCenter, text);
        }
    }
    // 外框
    p.setPen(QPen(Qt::black, 2));
    p.drawRect(QRect(gridArea.left(), gridArea.top(), nameW + slots * colW,
                     headH + (lastRow - firstRow) * rowH));
    p.setPen(QPen(Qt::gray, 1));
}

QList<qint64> resolveIds(const sched::School &s, const QString &scope, const QList<qint64> &ids)
{
    if (!ids.isEmpty())
        return ids;
    QList<qint64> out;
    if (scope == QLatin1StringView("class"))
        for (const auto &c : s.classes)
            out.append(c.id);
    else if (scope == QLatin1StringView("teacher"))
        for (const auto &t : s.teachers)
            out.append(t.id);
    else if (scope == QLatin1StringView("room"))
        for (const auto &r : s.classrooms)
            out.append(r.id);
    return out;
}

} // namespace

PrintController::PrintController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
}

namespace {

// 统一渲染入口：对每个“页面”回调一次（调用方负责 newPage）
template<typename Device>
bool renderTo(Device &device, const sched::School &s, const QString &scope,
              const QList<qint64> &idsIn, const PrintOptions &opt, int w, int h, QString *error)
{
    QPainter painter(&device);
    if (!painter.isActive()) {
        if (error)
            *error = QStringLiteral("无法初始化绘制设备");
        return false;
    }
    const QRect page(0, 0, w, h);
    painter.fillRect(page, Qt::white);
    painter.setPen(Qt::black);

    const QList<qint64> ids = resolveIds(s, scope, idsIn);
    bool first = true;
    auto newPage = [&]() -> bool {
        if (first) {
            first = false;
            return true;
        }
        return device.newPage();
    };

    if (scope == QLatin1StringView("master")) {
        const QRect area = page.adjusted(w / 24, h / 24, -w / 24, -h / 24);
        const int headerH = area.height() / 12;
        const int headH = (area.height() - headerH) / 8;
        const int rowH = headH;
        const int rowsPerPage = std::max(1, (area.height() - headerH - headH) / rowH);
        for (int firstRow = 0; firstRow < s.classes.size(); firstRow += rowsPerPage) {
            if (!newPage())
                break;
            if (!first)
                painter.fillRect(page, Qt::white);
            paintMasterPage(painter, page, s, firstRow, rowsPerPage, opt);
        }
        return true;
    }

    if (ids.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有选择任何输出对象");
        return false;
    }
    for (int i = 0; i < ids.size(); i += opt.tablesPerPage) {
        if (!newPage())
            break;
        painter.fillRect(page, Qt::white);
        if (opt.tablesPerPage == 2 && i + 1 < ids.size()) {
            const QRect topHalf(0, 0, w, h / 2);
            const QRect bottomHalf(0, h / 2, w, h / 2);
            paintEntityTable(painter, topHalf, s, scope, ids[i], opt);
            paintEntityTable(painter, bottomHalf, s, scope, ids[i + 1], opt);
        } else {
            paintEntityTable(painter, page, s, scope, ids[i], opt);
        }
    }
    return true;
}

QSize pagePixels(const PrintOptions &opt)
{
    // A4 @ 150dpi
    return opt.landscape ? QSize(1754, 1240) : QSize(1240, 1754);
}

} // namespace

bool PrintController::exportPdf(const QString &pathOrUrl, const QString &scope,
                                const QList<qint64> &ids, const QVariantMap &options)
{
    QString path = pathOrUrl;
    if (path.startsWith(QLatin1StringView("file:")))
        path = QUrl(path).toLocalFile();
    const PrintOptions opt = PrintOptions::fromMap(options);
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageOrientation(opt.landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    pdf.setResolution(150);
    const QSize px = pagePixels(opt);
    if (!renderTo(pdf, *m_app->school(), scope, ids, opt, px.width(), px.height(), &m_lastError))
        return false;
    m_lastError.clear();
    return true;
}

bool PrintController::printNative(const QString &scope, const QList<qint64> &ids,
                                  const QVariantMap &options)
{
    const PrintOptions opt = PrintOptions::fromMap(options);
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageOrientation(opt.landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    QPrintDialog dlg(&printer);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    const QRect r = printer.pageRect(QPrinter::DevicePixel).toRect();
    return renderTo(printer, *m_app->school(), scope, ids, opt, r.width(), r.height(),
                    &m_lastError);
}

bool PrintController::exportPngs(const QString &dirOrUrl, const QString &scope,
                                 const QList<qint64> &ids, const QVariantMap &options)
{
    QString dir = dirOrUrl;
    if (dir.startsWith(QLatin1StringView("file:")))
        dir = QUrl(dir).toLocalFile();
    QDir().mkpath(dir); // 输出目录可能不存在
    const PrintOptions opt = PrintOptions::fromMap(options);
    const QSize px = pagePixels(opt);
    const QRect page(0, 0, px.width(), px.height());
    const auto &s = *m_app->school();
    const QList<qint64> resolved = resolveIds(s, scope, ids);

    // 每页独立 QImage + 局部 QPainter，避免 painter 与被替换的图像错位
    int pageNo = 0;
    auto renderPage = [&](const std::function<void(QPainter &)> &fn) -> bool {
        QImage img(px, QImage::Format_ARGB32);
        img.fill(Qt::white);
        {
            QPainter p(&img);
            if (!p.isActive())
                return false;
            p.setPen(Qt::black);
            fn(p);
        }
        pageNo++;
        return img.save(QStringLiteral("%1/page%2.png").arg(dir).arg(pageNo));
    };

    if (scope == QLatin1StringView("master")) {
        const QRect area = page.adjusted(px.width() / 24, px.height() / 24, -px.width() / 24,
                                         -px.height() / 24);
        const int headerH = area.height() / 12;
        const int headH = (area.height() - headerH) / 8;
        const int rowsPerPage = std::max(1, (area.height() - headerH - headH) / headH);
        for (int firstRow = 0; firstRow < s.classes.size(); firstRow += rowsPerPage) {
            if (!renderPage([&](QPainter &p) { paintMasterPage(p, page, s, firstRow, rowsPerPage, opt); }))
                return false;
        }
        return pageNo > 0;
    }

    if (resolved.isEmpty()) {
        m_lastError = QStringLiteral("没有选择任何输出对象");
        return false;
    }
    for (int i = 0; i < resolved.size(); i += opt.tablesPerPage) {
        if (opt.tablesPerPage == 2 && i + 1 < resolved.size()) {
            const qint64 a = resolved[i], b = resolved[i + 1];
            if (!renderPage([&](QPainter &p) {
                    paintEntityTable(p, QRect(0, 0, px.width(), px.height() / 2), s, scope, a, opt);
                    paintEntityTable(p, QRect(0, px.height() / 2, px.width(), px.height() / 2), s,
                                     scope, b, opt);
                }))
                return false;
        } else {
            const qint64 id = resolved[i];
            if (!renderPage([&](QPainter &p) { paintEntityTable(p, page, s, scope, id, opt); }))
                return false;
        }
    }
    return pageNo > 0;
}

bool PrintController::exportExcel(const QString &pathOrUrl, const QString &scope,
                                  const QList<qint64> &ids)
{
    QString path = pathOrUrl;
    if (path.startsWith(QLatin1StringView("file:")))
        path = QUrl(path).toLocalFile();
    if (!path.endsWith(QLatin1StringView(".xlsx"), Qt::CaseInsensitive))
        path += QStringLiteral(".xlsx");
    const auto &s = *m_app->school();
    QVector<qint64> classIds, teacherIds;
    bool master = false;
    if (scope == QLatin1StringView("master")) {
        master = true;
    } else if (scope == QLatin1StringView("class")) {
        classIds = resolveIds(s, scope, ids).toVector();
    } else if (scope == QLatin1StringView("teacher")) {
        teacherIds = resolveIds(s, scope, ids).toVector();
    } else {
        m_lastError = QStringLiteral("Excel 导出暂不支持教室表");
        return false;
    }
    return sched::ExcelExporter::exportTimetables(path, s, classIds, teacherIds, master,
                                                  &m_lastError);
}

QString PrintController::renderPreview(const QString &scope, const QList<qint64> &ids,
                                       const QVariantMap &options)
{
    const PrintOptions opt = PrintOptions::fromMap(options);
    const QSize px = pagePixels(opt);
    QImage image(px, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    if (!painter.isActive())
        return {};
    const QRect page(0, 0, px.width(), px.height());
    painter.setPen(Qt::black);
    const auto &s = *m_app->school();
    const QList<qint64> resolved = resolveIds(s, scope, ids);
    if (scope == QLatin1StringView("master")) {
        const QRect area = page.adjusted(px.width() / 24, px.height() / 24, -px.width() / 24,
                                         -px.height() / 24);
        const int headerH = area.height() / 12;
        const int headH = (area.height() - headerH) / 8;
        const int rowsPerPage = std::max(1, (area.height() - headerH - headH) / headH);
        paintMasterPage(painter, page, s, 0, rowsPerPage, opt);
    } else if (!resolved.isEmpty()) {
        paintEntityTable(painter, page, s, scope, resolved.first(), opt);
    } else {
        painter.end();
        return {};
    }
    painter.end();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = QStringLiteral("%1/sched_preview.png").arg(dir);
    if (!image.save(path))
        return {};
    return QStringLiteral("file:///%1?v=%2").arg(path).arg(QDateTime::currentMSecsSinceEpoch());
}
