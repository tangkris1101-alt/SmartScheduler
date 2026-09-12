#pragma once

#include <QColor>
#include <QObject>

// 主题令牌：亮/暗两套色板 + 圆角/间距/字号阶梯，QML 中以 Theme.xxx 访问。
// dark 属性切换深色模式并经 QSettings 持久化。
class Theme : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY themeChanged)

    // 色板
    Q_PROPERTY(QColor primary READ primary NOTIFY themeChanged)
    Q_PROPERTY(QColor primaryHover READ primaryHover NOTIFY themeChanged)
    Q_PROPERTY(QColor primaryPressed READ primaryPressed NOTIFY themeChanged)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY themeChanged)
    Q_PROPERTY(QColor background READ background NOTIFY themeChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY themeChanged)
    Q_PROPERTY(QColor sidebar READ sidebar NOTIFY themeChanged)
    Q_PROPERTY(QColor border READ border NOTIFY themeChanged)
    Q_PROPERTY(QColor shadow READ shadow NOTIFY themeChanged)
    Q_PROPERTY(QColor text READ text NOTIFY themeChanged)
    Q_PROPERTY(QColor textSecondary READ textSecondary NOTIFY themeChanged)
    Q_PROPERTY(QColor textDisabled READ textDisabled NOTIFY themeChanged)
    Q_PROPERTY(QColor success READ success NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
    Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged)
    Q_PROPERTY(QColor overlayHover READ overlayHover NOTIFY themeChanged)
    Q_PROPERTY(QColor overlayPress READ overlayPress NOTIFY themeChanged)
    Q_PROPERTY(QColor selectedBg READ selectedBg NOTIFY themeChanged)

    // 圆角 / 间距 / 字号 / 尺寸（与明暗无关的常量令牌）
    Q_PROPERTY(int radiusS READ radiusS CONSTANT)
    Q_PROPERTY(int radiusM READ radiusM CONSTANT)
    Q_PROPERTY(int radiusL READ radiusL CONSTANT)
    Q_PROPERTY(int spacingXS READ spacingXS CONSTANT)
    Q_PROPERTY(int spacingS READ spacingS CONSTANT)
    Q_PROPERTY(int spacingM READ spacingM CONSTANT)
    Q_PROPERTY(int spacingL READ spacingL CONSTANT)
    Q_PROPERTY(int spacingXL READ spacingXL CONSTANT)
    Q_PROPERTY(int fontCaption READ fontCaption CONSTANT)
    Q_PROPERTY(int fontBody READ fontBody CONSTANT)
    Q_PROPERTY(int fontSubtitle READ fontSubtitle CONSTANT)
    Q_PROPERTY(int fontTitle READ fontTitle CONSTANT)
    Q_PROPERTY(int fontBig READ fontBig CONSTANT)
    Q_PROPERTY(int headerHeight READ headerHeight CONSTANT)
    Q_PROPERTY(int navWidth READ navWidth CONSTANT)

public:
    explicit Theme(QObject *parent = nullptr);

    bool dark() const { return m_dark; }
    void setDark(bool dark);

    QColor primary() const { return m_dark ? QColor("#3b82f6") : QColor("#2563eb"); }
    QColor primaryHover() const { return m_dark ? QColor("#60a5fa") : QColor("#3b82f6"); }
    QColor primaryPressed() const { return m_dark ? QColor("#2563eb") : QColor("#1e40af"); }
    QColor onPrimary() const { return QColor("#ffffff"); }
    QColor background() const { return m_dark ? QColor("#14161a") : QColor("#f3f4f6"); }
    QColor surface() const { return m_dark ? QColor("#1e2126") : QColor("#ffffff"); }
    QColor surfaceAlt() const { return m_dark ? QColor("#262a31") : QColor("#f6f7f9"); }
    QColor sidebar() const { return m_dark ? QColor("#191c21") : QColor("#fafbfc"); }
    QColor border() const { return m_dark ? QColor("#33373f") : QColor("#e2e5ea"); }
    QColor shadow() const { return m_dark ? QColor(0, 0, 0, 100) : QColor(15, 23, 42, 20); }
    QColor text() const { return m_dark ? QColor("#e5e7eb") : QColor("#1f2937"); }
    QColor textSecondary() const { return m_dark ? QColor("#9aa0a8") : QColor("#6b7280"); }
    QColor textDisabled() const { return m_dark ? QColor("#5f6368") : QColor("#9ca3af"); }
    QColor success() const { return m_dark ? QColor("#22c55e") : QColor("#16a34a"); }
    QColor warning() const { return m_dark ? QColor("#f59e0b") : QColor("#d97706"); }
    QColor danger() const { return m_dark ? QColor("#ef4444") : QColor("#dc2626"); }
    QColor overlayHover() const { return m_dark ? QColor(255, 255, 255, 15) : QColor(15, 23, 42, 13); }
    QColor overlayPress() const { return m_dark ? QColor(255, 255, 255, 26) : QColor(15, 23, 42, 23); }
    QColor selectedBg() const { return m_dark ? QColor(59, 130, 246, 46) : QColor(37, 99, 235, 26); }

    int radiusS() const { return 4; }
    int radiusM() const { return 8; }
    int radiusL() const { return 14; }
    int spacingXS() const { return 4; }
    int spacingS() const { return 8; }
    int spacingM() const { return 12; }
    int spacingL() const { return 16; }
    int spacingXL() const { return 24; }
    int fontCaption() const { return 12; }
    int fontBody() const { return 14; }
    int fontSubtitle() const { return 16; }
    int fontTitle() const { return 20; }
    int fontBig() const { return 26; }
    int headerHeight() const { return 56; }
    int navWidth() const { return 216; }

Q_SIGNALS:
    void themeChanged();

private:
    bool m_dark = false;
};
