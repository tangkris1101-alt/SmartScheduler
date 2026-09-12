#include "Theme.h"

#include <QSettings>

Theme::Theme(QObject *parent)
    : QObject(parent)
    , m_dark(QSettings().value(QStringLiteral("ui/darkMode"), false).toBool())
{
}

void Theme::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    QSettings().setValue(QStringLiteral("ui/darkMode"), dark);
    Q_EMIT themeChanged();
}
