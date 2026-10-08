#include "language.h"

#include <QApplication>
#include <QTranslator>

QString installGuiLanguage(QTranslator& translator, const QString& preference,
    const QStringList& systemLanguages)
{
    const QStringList supported{"en", "zh_CN", "ar", "ru", "es", "tr", "ja", "ko", "pt", "uk", "id", "ms"};
    QString language = supported.contains(preference) ? preference : QStringLiteral("en");
    QLocale selectedLocale(language);
    if (!supported.contains(preference))
        for (const auto& name : systemLanguages)
        {
            const QLocale locale(name);
            const auto code = locale.name().section('_', 0, 0);
            if (locale.language() == QLocale::Chinese && locale.script() == QLocale::SimplifiedHanScript)
                language = "zh_CN";
            else if (supported.contains(code))
                language = code;
            else
                continue;
            selectedLocale = locale;
            break;
        }

    qApp->removeTranslator(&translator);
    if (translator.load(":/i18n/firominer_" + language + ".qm"))
        qApp->installTranslator(&translator);
    else
    {
        qWarning("Could not load the GUI translation; using English.");
        language = "en";
        selectedLocale = QLocale(language);
        if (translator.load(":/i18n/firominer_en.qm"))
            qApp->installTranslator(&translator);
    }
    QLocale::setDefault(selectedLocale);
    QApplication::setLayoutDirection(selectedLocale.textDirection());
    return language;
}
