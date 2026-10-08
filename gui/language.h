#pragma once

#include <QLocale>
#include <QStringList>

class QTranslator;

// The translator must live as long as the application uses its translations.
QString installGuiLanguage(QTranslator& translator, const QString& preference,
    const QStringList& systemLanguages = QLocale::system().uiLanguages());
