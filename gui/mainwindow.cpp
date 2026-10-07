#include "mainwindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QHeaderView>
#include <QHostInfo>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QStyleOption>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTransform>
#include <QtMath>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

//! Colors for one appearance. The branded themes share the Firo Core wallet's tokens:
//! one warm neutral ramp tinted toward the wine, teal for healthy states, gold for pending
//! ones and red for errors only. The native theme maps the same roles onto the platform palette.
struct GuiTheme
{
    bool branded = true;
    QColor bg, panel, panelSoft, border, ink, inkSoft, inkFaint, wine, wineDeep, wineTint, wineText;
    QColor teal, tealTint, tealText, error, errorTint, gold, goldTint, hover, fieldBorder;
    QColor heroStart, heroEnd, chartLine;
};

namespace
{
enum class Tone { Neutral, Positive, Warning, Danger };
enum class Glyph { Overview, Setup, Gpu, Activity, Settings, Help, Play, Stop, Copy, Temperature, Fan, Power };

// The Firo symbol from firominer.svg, in its 520-unit square.
const char* const firoSymbol[] = {
    "M155.6,370.7c5.9,0,11.2-3.2,14-8.4l37.3-70.6h-57.5c-8.7,0-15.8-7.1-15.8-15.8v-31.6c0-8.7,7.1-15.8,15.8-15.8h90.9"
    "l70.6-133.9c2.7-5.2,8.1-8.4,14-8.4h118.8C397.5,37.4,332.3,7,260,7C120.3,7,7,120.3,7,260c0,39.7,9.2,77.3,25.5,110.7H155.6z",
    "M364.4,149.3c-5.9,0-11.2,3.2-14,8.4l-37.3,70.6h57.5c8.7,0,15.8,7.1,15.8,15.8v31.6c0,8.7-7.1,15.8-15.8,15.8h-90.9"
    "l-70.6,133.9c-2.7,5.2-8.1,8.4-14,8.4H76.4C122.5,482.6,187.7,513,260,513c139.7,0,253-113.3,253-253c0-39.7-9.2-77.3-25.5-110.7H364.4z"};

bool highContrastEnabled()
{
    bool highContrast = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    highContrast = QGuiApplication::styleHints()->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast;
#elif defined(Q_OS_WIN)
    HIGHCONTRAST settings{};
    settings.cbSize = sizeof(settings);
    highContrast = SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof(settings), &settings, 0) &&
        (settings.dwFlags & HCF_HIGHCONTRASTON);
#endif
    return highContrast;
}

GuiTheme brandTheme(bool dark)
{
    GuiTheme theme;
    auto set = [dark](QColor& color, const char* light, const char* darkColor) { color = QColor(dark ? darkColor : light); };
    set(theme.bg, "#F5F3F4", "#0F0C10");
    set(theme.panel, "#FFFFFF", "#18141A");
    set(theme.panelSoft, "#F8F6F7", "#211B23");
    set(theme.border, "#E8E3E6", "#2E2730");
    set(theme.ink, "#1A1216", "#F5F0F3");
    set(theme.inkSoft, "#554B51", "#C2B8BF");
    set(theme.inkFaint, "#776C73", "#958A92");
    set(theme.wine, "#9B1C2E", "#C8304F");
    set(theme.wineDeep, "#7E1726", "#A62742");
    set(theme.wineTint, "#FBEEF0", "#24E84868");
    set(theme.wineText, "#9B1C2E", "#F27A93");
    set(theme.teal, "#1E7D6F", "#4CC2AD");
    set(theme.tealTint, "#E5F4F0", "#244CC2AD");
    set(theme.tealText, "#176A5E", "#4CC2AD");
    set(theme.error, "#CC2F26", "#FF7B6E");
    set(theme.errorTint, "#FDECEA", "#24FF7B6E");
    set(theme.gold, "#96560C", "#EBB15E");
    set(theme.goldTint, "#FCF1E1", "#24EBB15E");
    set(theme.hover, "#F0ECEE", "#2A232C");
    set(theme.fieldBorder, "#CFC6CB", "#463C48");
    set(theme.heroStart, "#9B1C2E", "#86182A");
    set(theme.heroEnd, "#5E0F1D", "#3F0A15");
    theme.chartLine = theme.wineText;
    return theme;
}

// System and high-contrast appearances keep the platform's colors.
GuiTheme nativeTheme(const QPalette& palette)
{
    GuiTheme theme;
    theme.branded = false;
    theme.bg = palette.color(QPalette::Window);
    theme.panel = palette.color(QPalette::Base);
    theme.panelSoft = theme.hover = palette.color(QPalette::AlternateBase);
    theme.border = theme.fieldBorder = palette.color(QPalette::Mid);
    theme.ink = theme.inkSoft = theme.inkFaint = palette.color(QPalette::WindowText);
    theme.error = theme.gold = theme.ink;
    theme.wine = theme.wineDeep = theme.wineText = theme.chartLine = palette.color(QPalette::Highlight);
    theme.teal = theme.tealText = theme.wine;
    theme.wineTint = theme.tealTint = theme.errorTint = theme.goldTint = Qt::transparent;
    theme.heroStart = theme.heroEnd = theme.panel;
    return theme;
}

QString css(const QColor& color)
{
    return color.alpha() == 255 ? color.name() :
        QString("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

QString withFamilies(QString sheet);

QString brandedSheet(const GuiTheme& theme)
{
    QString sheet = R"(
        QMainWindow, QWidget#workspace, QScrollArea, QScrollArea > QWidget > QWidget, QDialog, QMessageBox { background: $BG; }
        QWidget { color: $INK; }
        QLabel { background: transparent; }
        QLabel[role="muted"] { color: $INK_SOFT; }
        QLabel[role="faint"], QLabel[role="caption"] { color: $INK_FAINT; }
        QLabel[role="title"] { font-family: "$DISPLAY"; font-size: 32px; font-weight: 700; }
        QLabel[role="brand"] { font-family: "$DISPLAY"; font-size: 23px; font-weight: 700; }
        QLabel[role="section"] { font-size: 17px; font-weight: 700; }
        QLabel[role="strong"] { font-weight: 700; }
        QLabel[role="pill"] { font-size: 13px; font-weight: 700; }
        QLabel[role="code"] { background: $PANEL_SOFT; border: 1px solid $BORDER; border-radius: 10px; padding: 10px 12px; }
        QLabel[role="badge"] { background: $WINE_TINT; border-radius: 10px; }
        QLabel[role="gpuRate"] { font-family: "$DISPLAY"; font-size: 30px; font-weight: 700; }
        QLabel[role="unit"] { font-family: "$DISPLAY_LIGHT"; font-size: 17px; font-weight: 300; color: $INK_FAINT; }
        QLabel#notice { border-radius: 10px; padding: 12px 14px; }
        QLabel#notice[tone="danger"] { background: $ERROR_TINT; color: $ERROR; }
        QLabel#notice[tone="warning"] { background: $GOLD_TINT; color: $GOLD; }
        QFrame#panel, QFrame#gpuCard, QFrame#gpuEmpty { background: $PANEL; border: 1px solid $BORDER; border-radius: 14px; }
        QFrame#gpuDivider { background: $BORDER; border: none; }
        QFrame#sidebar { background: $PANEL; border: none; border-right: 1px solid $BORDER; }
        QFrame#hero { border: none; border-radius: 14px;
            background: qlineargradient(x1: 0, y1: 0, x2: 1, y2: 1, stop: 0 $HERO_START, stop: 1 $HERO_END); }
        QFrame#hero QLabel { color: #FFFFFF; }
        QFrame#hero QLabel[role="heroSoft"], QFrame#hero QLabel[role="caption"] { color: rgba(255, 255, 255, 199); }
        QFrame#hero QLabel[role="heroValue"] { font-family: "$DISPLAY"; font-size: 64px; font-weight: 700; }
        QFrame#hero QLabel[role="heroUnit"] { font-family: "$DISPLAY_LIGHT"; font-size: 28px; font-weight: 300; color: rgba(255, 255, 255, 158); }
        QFrame#hero QLabel[role="heroTitle"] { font-family: "$DISPLAY"; font-size: 40px; font-weight: 700; }
        QFrame#hero QLabel[role="statValue"] { font-family: "$DISPLAY"; font-size: 28px; font-weight: 700; }
        QFrame#hero QWidget#heroStat { border: none; border-left: 1px solid rgba(255, 255, 255, 71); }
        QListWidget#navigation { background: transparent; border: none; outline: none; font-weight: 700; }
        QListWidget#navigation::item { height: 44px; border: none; border-radius: 10px; padding-left: 12px; margin-bottom: 4px; color: $INK_SOFT; }
        QListWidget#navigation[compact="true"]::item { padding-left: 14px; }
        QListWidget#navigation::item:hover { background: $HOVER; color: $INK; }
        QListWidget#navigation::item:selected { background: transparent; color: $WINE_TEXT; }
        QListWidget#navigation::item:selected:hover { background: $HOVER; }
        QPushButton { background: $PANEL; color: $INK; border: 1px solid $FIELD_BORDER; border-radius: 10px; padding: 10px 16px; font-weight: 700; }
        QPushButton:hover { background: $HOVER; }
        QPushButton:focus { border: 2px solid $WINE_TEXT; padding: 9px 15px; }
        QPushButton:disabled { background: $PANEL_SOFT; border-color: $BORDER; color: $INK_FAINT; }
        QPushButton[role="primary"] { background: $WINE; border: 1px solid $WINE; color: #FFFFFF; }
        QPushButton[role="primary"]:hover { background: $WINE_DEEP; border-color: $WINE_DEEP; }
        QPushButton[role="primary"]:focus { border: 2px solid $INK; padding: 9px 15px; }
        QPushButton[role="primary"]:disabled { background: $HOVER; border-color: $HOVER; color: $INK_FAINT; }
        QPushButton[role="link"] { background: transparent; border: none; color: $WINE_TEXT; padding: 4px 0; text-align: left; }
        QPushButton[role="link"]:hover, QPushButton[role="link"]:focus { text-decoration: underline; }
        QPushButton[role="icon"] { background: transparent; border: none; border-radius: 8px; padding: 6px; }
        QPushButton[role="icon"]:hover { background: $HOVER; }
        QPushButton[role="icon"]:disabled { background: transparent; }
        QPushButton[role="icon"]:focus { border: 2px solid $WINE_TEXT; padding: 4px; }
        QPushButton[role="sidebar"] { background: transparent; border: none; border-radius: 10px; color: $INK_SOFT; padding: 0 12px; min-height: 44px; text-align: left; }
        QPushButton[role="sidebar"][compact="true"] { padding: 0 14px; }
        QPushButton[role="sidebar"]:hover { background: $HOVER; color: $INK; }
        QPushButton[role="sidebar"]:focus { border: 2px solid $WINE_TEXT; padding: 0 10px; }
        QFrame#segmented { background: $PANEL_SOFT; border: 1px solid $BORDER; border-radius: 10px; }
        QPushButton[role="segment"] { background: transparent; border: none; border-radius: 7px; color: $INK_SOFT; padding: 6px 12px; }
        QPushButton[role="segment"]:hover { color: $INK; }
        QPushButton[role="segment"]:checked { background: $PANEL; color: $INK; }
        QPushButton[role="segment"]:focus { border: 2px solid $WINE_TEXT; padding: 4px 10px; }
        QPushButton[role="segment"]:disabled { background: transparent; color: $INK_FAINT; }
        QPushButton[role="segment"]:checked:disabled { background: $PANEL; }
        QLineEdit, QComboBox { background: $PANEL; color: $INK; border: 1px solid $FIELD_BORDER; border-radius: 10px; padding: 9px 12px; min-height: 20px;
            selection-background-color: $WINE; selection-color: #FFFFFF; }
        QLineEdit:focus, QComboBox:focus { border: 2px solid $WINE_TEXT; padding: 8px 11px; }
        QLineEdit:disabled, QComboBox:disabled { background: $PANEL_SOFT; border-color: $BORDER; color: $INK_FAINT; }
        QComboBox QAbstractItemView { background: $PANEL; color: $INK; border: 1px solid $BORDER; selection-background-color: $WINE_TINT; selection-color: $INK; }
        QToolButton { background: transparent; color: $INK_SOFT; border: none; border-radius: 6px; padding: 2px 6px; font-weight: 700; }
        QToolButton:hover { background: $HOVER; color: $INK; }
        QTableWidget { background: $PANEL; border: none; gridline-color: $BORDER; selection-background-color: $WINE_TINT; selection-color: $INK; }
        QHeaderView::section { background: $PANEL; color: $INK_FAINT; border: none; border-bottom: 1px solid $BORDER; padding: 8px 6px; font-weight: 700; }
        QPlainTextEdit { background: $PANEL; color: $INK; border: 1px solid $BORDER; border-radius: 10px; padding: 10px;
            selection-background-color: $WINE; selection-color: #FFFFFF; }
        QMenu { background: $PANEL; color: $INK; border: 1px solid $BORDER; padding: 4px; }
        QMenu::item { padding: 6px 18px; border-radius: 6px; }
        QMenu::item:selected { background: $WINE_TINT; color: $INK; }
        QStatusBar, QStatusBar QLabel { background: $BG; color: $INK_SOFT; }
        QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
        QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
        QScrollBar::handle { background: $FIELD_BORDER; border-radius: 4px; }
        QScrollBar::handle:vertical { min-height: 32px; }
        QScrollBar::handle:horizontal { min-width: 32px; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    )";
    const QHash<QString, QString> tokens{
        {"$BG", css(theme.bg)}, {"$PANEL_SOFT", css(theme.panelSoft)}, {"$PANEL", css(theme.panel)},
        {"$BORDER", css(theme.border)}, {"$INK_SOFT", css(theme.inkSoft)}, {"$INK_FAINT", css(theme.inkFaint)},
        {"$INK", css(theme.ink)}, {"$WINE_DEEP", css(theme.wineDeep)}, {"$WINE_TINT", css(theme.wineTint)},
        {"$WINE_TEXT", css(theme.wineText)}, {"$WINE", css(theme.wine)}, {"$ERROR_TINT", css(theme.errorTint)},
        {"$ERROR", css(theme.error)}, {"$GOLD_TINT", css(theme.goldTint)}, {"$GOLD", css(theme.gold)},
        {"$HOVER", css(theme.hover)}, {"$FIELD_BORDER", css(theme.fieldBorder)}, {"$HERO_START", css(theme.heroStart)},
        {"$HERO_END", css(theme.heroEnd)}};
    // Replace longer tokens first so $WINE does not consume $WINE_TINT.
    auto keys = tokens.keys();
    std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const auto& key : keys)
        sheet.replace(key, tokens.value(key));
    return withFamilies(sheet);
}

// Structure only: platform colors stay in charge, including high-contrast themes.
QString nativeSheet()
{
    QString sheet = R"(
        QLabel[role="title"] { font-family: "$DISPLAY"; font-size: 32px; font-weight: 700; }
        QLabel[role="brand"] { font-family: "$DISPLAY"; font-size: 23px; font-weight: 700; }
        QLabel[role="section"] { font-size: 17px; font-weight: 700; }
        QLabel[role="strong"] { font-weight: 700; }
        QLabel[role="pill"] { font-size: 13px; font-weight: 700; }
        QLabel[role="code"] { border: 1px solid palette(mid); border-radius: 10px; padding: 10px 12px; }
        QLabel[role="gpuRate"] { font-family: "$DISPLAY"; font-size: 30px; font-weight: 700; }
        QLabel[role="unit"] { font-family: "$DISPLAY_LIGHT"; font-size: 17px; font-weight: 300; }
        QLabel#notice { border: 1px solid palette(mid); border-radius: 10px; padding: 12px 14px; }
        QFrame#panel, QFrame#gpuCard, QFrame#gpuEmpty, QFrame#hero { border: 1px solid palette(mid); border-radius: 14px; }
        QFrame#gpuDivider { background: palette(mid); border: none; }
        QFrame#sidebar { border: none; border-right: 1px solid palette(mid); }
        QLabel[role="heroValue"] { font-family: "$DISPLAY"; font-size: 64px; font-weight: 700; }
        QLabel[role="heroUnit"] { font-family: "$DISPLAY_LIGHT"; font-size: 28px; font-weight: 300; }
        QLabel[role="heroTitle"] { font-family: "$DISPLAY"; font-size: 40px; font-weight: 700; }
        QLabel[role="statValue"] { font-family: "$DISPLAY"; font-size: 28px; font-weight: 700; }
        QWidget#heroStat { border: none; border-left: 1px solid palette(mid); }
        QListWidget#navigation { background: transparent; border: none; outline: none; font-weight: 700; }
        QListWidget#navigation::item { height: 44px; border-radius: 10px; padding-left: 12px; margin-bottom: 4px; }
        QListWidget#navigation::item:selected { background: palette(highlight); color: palette(highlighted-text); }
        QListWidget#navigation[compact="true"]::item { padding-left: 14px; }
        QPushButton { padding: 10px 16px; font-weight: 700; }
        QPushButton[role="segment"] { padding: 6px 12px; }
        QPushButton[role="link"] { background: transparent; border: none; color: palette(link); padding: 4px 0; text-align: left; }
        QPushButton[role="link"]:hover, QPushButton[role="link"]:focus { text-decoration: underline; }
        QPushButton[role="icon"] { padding: 6px; }
        QPushButton[role="sidebar"] { background: transparent; border: none; border-radius: 10px; padding: 0 12px; min-height: 44px; text-align: left; }
        QPushButton[role="sidebar"][compact="true"] { padding: 0 14px; }
        QLineEdit, QComboBox { padding: 9px; min-height: 20px; }
    )";
    return withFamilies(sheet);
}

// The wallet's typefaces: Saira SemiCondensed for headings and figures, Source Sans Pro for text.
struct BrandFonts
{
    QString display = QStringLiteral("Saira SemiCondensed");
    QString displayLight;
    QString body = QStringLiteral("Source Sans Pro");
};

const BrandFonts& brandFonts()
{
    static const BrandFonts fonts = [] {
        BrandFonts result;
        auto load = [](const char* file) { return QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFont(file)); };
        const auto bold = load(":/fonts/SairaSemiCondensed-Bold.ttf");
        if (!bold.isEmpty())
            result.display = bold.first();
        // The light face comes from another foundry, so the family name it shares with the bold face
        // can resolve to the bold face. Prefer its own legacy family where the platform lists one.
        result.displayLight = result.display;
        for (const auto& family : load(":/fonts/SairaSemiCondensed-Light.ttf"))
            if (family != result.display)
                result.displayLight = family;
        const auto body = load(":/fonts/SourceSansPro-Regular.ttf");
        if (!body.isEmpty())
            result.body = body.first();
        load(":/fonts/SourceSansPro-Bold.ttf");
        return result;
    }();
    return fonts;
}

QString withFamilies(QString sheet)
{
    // Replace the longer token first so $DISPLAY does not consume $DISPLAY_LIGHT.
    sheet.replace("$DISPLAY_LIGHT", brandFonts().displayLight);
    return sheet.replace("$DISPLAY", brandFonts().display);
}

// Source Sans runs small, so its default is a step above common UI sizes, while larger
// accessibility text settings still scale it.
QFont bodyFont(QFont font)
{
    if (font.family() == brandFonts().body && font.pointSizeF() > 0)
        return font;
    const qreal points = font.pointSizeF() > 0 ? font.pointSizeF() : font.pixelSize() * 0.75;
    font.setFamily(brandFonts().body);
    font.setPointSizeF(std::max<qreal>(11.25, points * 1.1));
    return font;
}

QFont monoFont(int pixelSize)
{
    const auto families = QFontDatabase::families();
    for (const auto* family : {"Cascadia Mono", "Consolas", "DejaVu Sans Mono", "Ubuntu Mono", "Liberation Mono"})
        if (families.contains(family))
        {
            QFont font(family);
            font.setPixelSize(pixelSize);
            return font;
        }
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(pixelSize);
    return font;
}

QPainterPath svgPath(const QString& data)
{
    static const QRegularExpression token("([MmLlHhVvCcZz])|([-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?)");
    QPainterPath path;
    QList<double> numbers;
    QChar command;
    QPointF current, start;
    auto flush = [&] {
        const bool relative = command.isLower();
        int i = 0;
        switch (command.toUpper().unicode())
        {
        case 'M':
            for (; i + 1 < numbers.size(); i += 2)
            {
                current = (relative ? current : QPointF()) + QPointF(numbers[i], numbers[i + 1]);
                if (i == 0)
                {
                    path.moveTo(current);
                    start = current;
                }
                else
                    path.lineTo(current);
            }
            break;
        case 'L':
            for (; i + 1 < numbers.size(); i += 2)
                path.lineTo(current = (relative ? current : QPointF()) + QPointF(numbers[i], numbers[i + 1]));
            break;
        case 'H':
            for (; i < numbers.size(); ++i)
            {
                current.setX((relative ? current.x() : 0) + numbers[i]);
                path.lineTo(current);
            }
            break;
        case 'V':
            for (; i < numbers.size(); ++i)
            {
                current.setY((relative ? current.y() : 0) + numbers[i]);
                path.lineTo(current);
            }
            break;
        case 'C':
            for (; i + 5 < numbers.size(); i += 6)
            {
                const QPointF base = relative ? current : QPointF();
                path.cubicTo(base + QPointF(numbers[i], numbers[i + 1]), base + QPointF(numbers[i + 2], numbers[i + 3]),
                    base + QPointF(numbers[i + 4], numbers[i + 5]));
                current = base + QPointF(numbers[i + 4], numbers[i + 5]);
            }
            break;
        case 'Z':
            path.closeSubpath();
            current = start;
            break;
        }
        numbers.clear();
    };
    for (auto match = token.globalMatch(data); match.hasNext();)
    {
        const auto part = match.next();
        if (part.capturedLength(1))
        {
            if (!command.isNull())
                flush();
            command = part.captured(1).at(0);
        }
        else
            numbers.append(part.captured(2).toDouble());
    }
    if (!command.isNull())
        flush();
    return path;
}

// Outline icons drawn on a 24-unit grid, so they stay sharp at any scale without an SVG module.
QPainterPath glyphPath(Glyph glyph, bool& filled)
{
    QPainterPath path;
    filled = false;
    switch (glyph)
    {
    case Glyph::Overview:
        path.addRoundedRect(QRectF(3.5, 3.5, 7, 9), 1.5, 1.5);
        path.addRoundedRect(QRectF(13.5, 3.5, 7, 5), 1.5, 1.5);
        path.addRoundedRect(QRectF(13.5, 11.5, 7, 9), 1.5, 1.5);
        path.addRoundedRect(QRectF(3.5, 15.5, 7, 5), 1.5, 1.5);
        break;
    case Glyph::Setup:
        path.moveTo(4, 7);
        path.lineTo(13, 7);
        path.moveTo(17, 7);
        path.lineTo(20, 7);
        path.moveTo(4, 17);
        path.lineTo(7, 17);
        path.moveTo(11, 17);
        path.lineTo(20, 17);
        path.addEllipse(QPointF(15, 7), 2, 2);
        path.addEllipse(QPointF(9, 17), 2, 2);
        break;
    case Glyph::Gpu:
        path.addRoundedRect(QRectF(2.5, 6, 19, 11), 2, 2);
        path.addEllipse(QPointF(9, 11.5), 2.6, 2.6);
        path.moveTo(15, 9.5);
        path.lineTo(18.5, 9.5);
        path.moveTo(15, 13.5);
        path.lineTo(18.5, 13.5);
        for (const qreal x : {6.0, 10.0, 14.0})
        {
            path.moveTo(x, 17);
            path.lineTo(x, 19.5);
        }
        break;
    case Glyph::Activity:
        path.moveTo(3, 12);
        path.lineTo(7, 12);
        path.lineTo(10, 5);
        path.lineTo(14, 19);
        path.lineTo(17, 12);
        path.lineTo(21, 12);
        break;
    case Glyph::Settings:
    {
        QPolygonF cog;
        const std::pair<qreal, qreal> outline[] = {{-16, 7}, {-9, 9.3}, {9, 9.3}, {16, 7}};
        for (int tooth = 0; tooth < 8; ++tooth)
            for (const auto& [offset, radius] : outline)
            {
                const qreal angle = qDegreesToRadians(tooth * 45.0 + offset);
                cog << QPointF(12 + radius * std::cos(angle), 12 + radius * std::sin(angle));
            }
        path.addPolygon(cog);
        path.closeSubpath();
        path.addEllipse(QPointF(12, 12), 3, 3);
        break;
    }
    case Glyph::Help:
        path.addEllipse(QPointF(12, 12), 9, 9);
        path.moveTo(9.5, 9.5);
        path.arcTo(QRectF(9.5, 7, 5, 5), 180, -240);
        path.lineTo(12, 12.8);
        path.lineTo(12, 13.8);
        path.addEllipse(QPointF(12, 17), 0.5, 0.5);
        break;
    case Glyph::Play:
        filled = true;
        path.moveTo(7, 4.5);
        path.lineTo(7, 19.5);
        path.lineTo(19.5, 12);
        path.closeSubpath();
        break;
    case Glyph::Stop:
        filled = true;
        path.addRoundedRect(QRectF(5, 5, 14, 14), 2.5, 2.5);
        break;
    case Glyph::Copy:
        path.addRoundedRect(QRectF(9, 9, 11, 11), 2, 2);
        path.moveTo(15, 9);
        path.lineTo(15, 6);
        path.arcTo(QRectF(11, 4, 4, 4), 0, 90);
        path.lineTo(6, 4);
        path.arcTo(QRectF(4, 4, 4, 4), 90, 90);
        path.lineTo(4, 13);
        path.arcTo(QRectF(4, 11, 4, 4), 180, 90);
        path.lineTo(9, 15);
        break;
    case Glyph::Temperature:
        path.moveTo(14, 14.6);
        path.lineTo(14, 5);
        path.arcTo(QRectF(10, 3, 4, 4), 0, 180);
        path.lineTo(10, 14.6);
        path.arcTo(QRectF(8.2, 14, 7.6, 7.6), 121.8, 296.4);
        path.closeSubpath();
        break;
    case Glyph::Fan:
    {
        path.addEllipse(QPointF(12, 12), 9, 9);
        path.addEllipse(QPointF(12, 12), 1.5, 1.5);
        QPainterPath blade;
        blade.moveTo(12, 10.3);
        blade.cubicTo(11, 7.4, 12.6, 4.8, 15.2, 5.6);
        for (int i = 0; i < 3; ++i)
        {
            QTransform turn;
            turn.translate(12, 12);
            turn.rotate(120.0 * i);
            turn.translate(-12, -12);
            path.addPath(turn.map(blade));
        }
        break;
    }
    case Glyph::Power:
        path.moveTo(13, 3);
        path.lineTo(5, 13.5);
        path.lineTo(11, 13.5);
        path.lineTo(10, 21);
        path.lineTo(18, 10.5);
        path.lineTo(12, 10.5);
        path.closeSubpath();
        break;
    }
    return path;
}

QPixmap glyphPixmap(Glyph glyph, const QColor& color, int size, qreal ratio = 0)
{
    if (ratio <= 0)
        ratio = std::max<qreal>(qApp->devicePixelRatio(), 1);
    QPixmap pixmap(QSize(size, size) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size / 24.0, size / 24.0);
    bool filled = false;
    const auto path = glyphPath(glyph, filled);
    if (filled)
        painter.fillPath(path, color);
    else
    {
        painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }
    return pixmap;
}

QIcon glyphIcon(Glyph glyph, const QColor& normal, const QColor& selected = QColor(), int size = 20)
{
    QIcon icon;
    for (const qreal ratio : {1.0, 2.0, std::max<qreal>(qApp->devicePixelRatio(), 1)})
    {
        icon.addPixmap(glyphPixmap(glyph, normal, size, ratio), QIcon::Normal);
        if (selected.isValid())
            icon.addPixmap(glyphPixmap(glyph, selected, size, ratio), QIcon::Selected);
    }
    return icon;
}

void repolish(QWidget* widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void setRole(QWidget* widget, const char* role)
{
    if (widget->property("role").toString() == role)
        return;
    widget->setProperty("role", role);
    repolish(widget);
}

QLabel* label(const QString& text, const char* role = nullptr)
{
    auto* result = new QLabel(text);
    result->setTextFormat(Qt::PlainText);
    if (role)
        result->setProperty("role", role);
    return result;
}

// QLabel's word-wrap heuristic prefers a narrow column; short details should prefer one line
// and wrap only when the window is narrow.
class DetailLabel : public QLabel
{
public:
    using QLabel::QLabel;
    QSize sizeHint() const override
    {
        const auto hint = QLabel::sizeHint();
        const auto margins = contentsMargins();
        return {std::max(hint.width(), fontMetrics().horizontalAdvance(text()) + margins.left() + margins.right() + 4), hint.height()};
    }
};

// Box layouts center or bottom-align labels, so a unit takes the top margin that puts its
// baseline on its figure's whenever either font changes. Add both with Qt::AlignTop.
class BaselineAligner : public QObject
{
public:
    BaselineAligner(QLabel* figure, QLabel* unit) : QObject(unit), figure_(figure), unit_(unit)
    {
        figure->installEventFilter(this);
        unit->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::FontChange)
            unit_->setContentsMargins(0, std::max(0, figure_->fontMetrics().ascent() - unit_->fontMetrics().ascent()), 0, 0);
        return false;
    }

private:
    QLabel *figure_, *unit_;
};

// Small bold capitals; QSS cannot carry letter spacing or capitalization. They follow the
// body text size, so larger text settings enlarge them too.
QLabel* caption(const QString& text)
{
    auto* result = label(text, "caption");
    QFont font = bodyFont(QApplication::font());
    font.setPointSizeF(font.pointSizeF() * 0.8);
    font.setBold(true);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.8);
    result->setFont(font);
    return result;
}

QFrame* panel()
{
    auto* frame = new QFrame;
    frame->setObjectName("panel");
    return frame;
}

QPushButton* button(const QString& text, const char* role = nullptr)
{
    auto* result = new QPushButton(text);
    result->setCursor(Qt::PointingHandCursor);
    if (role)
        result->setProperty("role", role);
    return result;
}

QWidget* scrollPage(QWidget* content)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

double hashValue(const QJsonValue& value)
{
    bool ok = false;
    const double result = value.isString() ? value.toString().toULongLong(&ok, 16) : value.toDouble();
    return (value.isString() && !ok) || !std::isfinite(result) || result < 0 ? 0 : result / 1000000.;
}

bool hasReading(const QJsonValue& value, bool allowZero)
{
    return value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= 0 &&
        (allowZero || value.toDouble() != 0);
}

//! A sensor reading with its unit, or empty when the device does not report one.
QString sensor(const QJsonValue& value, const QString& unit, bool allowZero = false)
{
    return hasReading(value, allowZero) ? QString::number(value.toDouble(), 'f', 0) + unit : QString();
}

//! Stands in for unavailable figures; setValue() announces it as "Unavailable".
QString dash()
{
    return QString(QChar(0x2014));
}

// A dash keeps unavailable figures compact; assistive technology still hears the word.
void setValue(QLabel* label, const QString& text)
{
    label->setText(text);
    label->setAccessibleName(text == dash() ? QStringLiteral("Unavailable") : QString());
}

QString abbreviated(const QString& text)
{
    return text.size() > 22 ? text.left(10) + QString::fromUtf8("…") + text.right(8) : text;
}

QString durationText(qint64 seconds)
{
    seconds = std::max<qint64>(seconds, 0);
    // A no-break space keeps each number with its unit.
    const QChar space(0x00a0);
    if (seconds < 60)
        return QString::number(seconds) + space + "s";
    if (seconds < 3600)
        return QString::number(seconds / 60) + space + "min";
    return QString::number(seconds / 3600) + "h" + space + QString::number((seconds / 60) % 60) + "m";
}

QString sessionEndText(const QDateTime& ended)
{
    const QLocale locale;
    static const QRegularExpression seconds("[:.]ss");
    auto timeFormat = locale.timeFormat(QLocale::ShortFormat);
    timeFormat.remove(seconds);
    const auto time = locale.toString(ended.time(), timeFormat);
    if (ended.date() == QDate::currentDate())
        return "Ended " + time;
    return "Ended " + locale.toString(ended.date(), QLocale::ShortFormat) + ", " + time;
}

double niceStep(double raw)
{
    const double magnitude = std::pow(10.0, std::floor(std::log10(raw)));
    for (const double factor : {1.0, 2.0, 2.5, 5.0})
        if (raw <= factor * magnitude)
            return factor * magnitude;
    return 10 * magnitude;
}

struct ToneColors
{
    QColor background, dot, text, edge;
};

ToneColors toneColors(const GuiTheme& theme, Tone tone, const QPalette& palette)
{
    if (!theme.branded)
    {
        const QColor ink = palette.color(QPalette::WindowText);
        QColor edge = ink;
        edge.setAlphaF(0.5);
        return {Qt::transparent, tone == Tone::Positive ? palette.color(QPalette::Highlight) : ink, ink, edge};
    }
    switch (tone)
    {
    case Tone::Positive:
        return {theme.tealTint, theme.teal, theme.tealText, QColor()};
    case Tone::Warning:
        return {theme.goldTint, theme.gold, theme.gold, QColor()};
    case Tone::Danger:
        return {theme.errorTint, theme.error, theme.error, QColor()};
    case Tone::Neutral:
        break;
    }
    return {theme.hover, theme.inkFaint, theme.inkSoft, QColor()};
}
}

// A status label drawn as a pill with a leading dot, so the state never relies on color alone.
class StatusPill : public QLabel
{
public:
    explicit StatusPill(const GuiTheme& colors, QWidget* parent = nullptr) : QLabel(parent), colors_(colors)
    {
        setTextFormat(Qt::PlainText);
        setProperty("role", "pill");
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    void setStatus(const QString& text, Tone tone)
    {
        tone_ = tone;
        if (text != this->text())
        {
            setText(text);
            updateGeometry();
        }
        update();
    }
    QSize sizeHint() const override
    {
        const QFontMetrics metrics(font());
        return QSize(metrics.horizontalAdvance(text()) + 39, std::max(28, metrics.height() + 10));
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto tone = toneColors(colors_, tone_, palette());
        const QRectF pill = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(tone.edge.isValid() ? QPen(tone.edge, 1) : QPen(Qt::NoPen));
        painter.setBrush(tone.background);
        painter.drawRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tone.dot);
        painter.drawEllipse(QPointF(15, pill.center().y()), 4, 4);
        painter.setPen(tone.text);
        const auto area = QRectF(pill).adjusted(25, 0, -12, 0);
        painter.drawText(area, Qt::AlignVCenter | Qt::AlignLeft,
            fontMetrics().elidedText(text(), Qt::ElideRight, int(area.width())));
    }

private:
    const GuiTheme& colors_;
    Tone tone_ = Tone::Neutral;
};

// The overview banner: the wallet's wine gradient and faint Firo mark, cropped by the top-right corner.
class HeroFrame : public QFrame
{
public:
    explicit HeroFrame(const GuiTheme& colors) : colors_(colors) { setObjectName("hero"); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOption option;
        option.initFrom(this);
        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
        if (!colors_.branded)
            return;
        static const QPainterPath symbol = [] {
            QPainterPath path;
            for (const auto* outline : firoSymbol)
                path.addPath(svgPath(outline));
            return path;
        }();
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), 14, 14);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setClipPath(clip);
        painter.translate(width() - 234, -46);
        painter.scale(270.0 / 520, 270.0 / 520);
        painter.setOpacity(0.07);
        painter.fillPath(symbol, Qt::white);
    }

private:
    const GuiTheme& colors_;
};

// The chart needs only a short, bounded session history, not a charting dependency.
class HashrateChart : public QWidget
{
public:
    explicit HashrateChart(const GuiTheme& colors, QWidget* parent = nullptr) : QWidget(parent), colors_(colors)
    {
        setMinimumHeight(150);
        setAccessibleName("Local hashrate history");
        setAccessibleDescription("Use View history for timestamped hashrate readings.");
    }
    void add(double value)
    {
        const auto now = QDateTime::currentSecsSinceEpoch();
        if (points_.isEmpty() || now - points_.last().x() >= 5)
            points_.append(QPointF(now, value));
        else
            points_.last().setY(value);
        while (!points_.isEmpty() && (points_.first().x() < now - 21600 || points_.size() > 4321))
            points_.removeFirst();
        update();
    }
    void reset() { points_.clear(); update(); }
    bool isEmpty() const { return points_.isEmpty(); }
    //! The plot's right edge: now while mining, or the last reading once the session stops.
    qint64 end() const
    {
        return dimmed_ && !points_.isEmpty() ? qint64(points_.last().x()) : QDateTime::currentSecsSinceEpoch();
    }
    void setRange(int seconds) { range_ = seconds; update(); }
    void setDimmed(bool dimmed)
    {
        if (dimmed_ != dimmed)
        {
            dimmed_ = dimmed;
            update();
        }
    }
    void showHistory()
    {
        QDialog dialog(this);
        dialog.setPalette(palette());
        dialog.setFont(font());
        dialog.setWindowTitle("Hashrate history");
        dialog.resize(440, 360);
        auto* layout = new QVBoxLayout(&dialog);
        auto* table = new QTableWidget(0, 2);
        table->setObjectName("hashrateHistory");
        table->setAccessibleName("Timestamped hashrate readings");
        table->setHorizontalHeaderLabels({"Time", "Hashrate (MH/s)"});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        const auto earliest = end() - range_;
        for (const auto& point : points_)
        {
            if (point.x() < earliest)
                continue;
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(QDateTime::fromSecsSinceEpoch(qint64(point.x())).toString("HH:mm:ss")));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(point.y(), 'f', 1)));
        }
        layout->addWidget(table);
        auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(close);
        dialog.exec();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont axisFont = font();
        axisFont.setPixelSize(12);
        painter.setFont(axisFont);
        const QFontMetrics metrics(axisFont);
        const int labels = metrics.horizontalAdvance("8888") + 10;
        const QRectF plot(labels, 10, width() - labels - 12, height() - metrics.height() - 22);
        if (plot.width() <= 0 || plot.height() <= 0)
            return;
        if (points_.isEmpty())
        {
            painter.setPen(colors_.inkFaint);
            painter.drawText(plot, Qt::AlignCenter | Qt::TextWordWrap, "Hashrate history appears when mining starts");
            return;
        }
        const QColor line = dimmed_ ? colors_.inkFaint : colors_.chartLine;
        const auto endTime = end();
        double peak = 0;
        for (const auto& point : points_)
            if (point.x() >= endTime - range_)
                peak = std::max(peak, point.y());
        const double target = std::max(peak * 1.15, 10.0);
        const double step = niceStep(target / 3);
        const double maximum = step * std::ceil(target / step);
        for (double value = 0; value <= maximum + step / 2; value += step)
        {
            const auto y = plot.bottom() - value / maximum * plot.height();
            painter.setPen(QPen(colors_.border, 1));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(colors_.inkFaint);
            painter.drawText(QRectF(0, y - 10, labels - 10, 20), Qt::AlignRight | Qt::AlignVCenter,
                QString::number(value, 'f', step < 1 ? 1 : 0));
        }
        for (int i = 0; i <= 4; ++i)
        {
            const auto x = plot.left() + i * plot.width() / 4;
            const auto time = endTime - range_ + i * range_ / 4;
            painter.setPen(colors_.inkFaint);
            painter.drawText(QRectF(x - 28, plot.bottom() + 8, 56, metrics.height() + 4), Qt::AlignCenter,
                i == 4 && !dimmed_ ? QStringLiteral("Now") : QDateTime::fromSecsSinceEpoch(time).toString("HH:mm"));
        }
        QPainterPath path, area;
        QPolygonF segment, dots;
        double sum = 0;
        int count = 0;
        double previousTime = 0;
        QPointF last;
        auto closeSegment = [&] {
            if (segment.size() > 1)
            {
                QPolygonF shape = segment;
                shape << QPointF(segment.last().x(), plot.bottom()) << QPointF(segment.first().x(), plot.bottom());
                area.addPolygon(shape);
            }
            else if (segment.size() == 1)
                dots << segment.first(); // A reading between gaps has no line to draw.
            segment.clear();
        };
        for (const auto& point : points_)
        {
            if (point.x() < endTime - range_)
                continue;
            const QPointF p(plot.right() - (endTime - point.x()) / range_ * plot.width(),
                plot.bottom() - point.y() / maximum * plot.height());
            if (segment.isEmpty() || point.x() - previousTime > 15)
            {
                closeSegment();
                path.moveTo(p);
            }
            else
                path.lineTo(p);
            segment << p;
            previousTime = point.x();
            last = p;
            if (point.y() > 0)
            {
                sum += point.y();
                ++count;
            }
        }
        closeSegment();
        painter.setClipRect(plot.adjusted(-6, -6, 6, 6));
        if (!dimmed_)
        {
            QColor fill = line;
            fill.setAlphaF(colors_.branded && colors_.bg.lightness() >= 128 ? 0.07 : 0.12);
            painter.fillPath(area, fill);
        }
        painter.setPen(QPen(line, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        painter.setPen(Qt::NoPen);
        painter.setBrush(line);
        for (const auto& dot : dots)
            painter.drawEllipse(dot, 2.5, 2.5);
        if (count > 1)
        {
            const double mean = sum / count;
            const auto y = plot.bottom() - mean / maximum * plot.height();
            painter.setPen(QPen(colors_.inkFaint, 1, Qt::DashLine));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            const auto text = QString("Avg %1").arg(mean, 0, 'f', 1);
            const QRectF box(plot.right() - metrics.horizontalAdvance(text) - 14, y - metrics.height() - 6,
                metrics.horizontalAdvance(text) + 8, metrics.height() + 2);
            painter.fillRect(box, colors_.panel);
            painter.setPen(colors_.inkFaint);
            painter.drawText(box, Qt::AlignCenter, text);
        }
        if (!dimmed_ && endTime - previousTime <= 15)
        {
            painter.setPen(QPen(colors_.panel, 3));
            painter.setBrush(line);
            painter.drawEllipse(last, 4.5, 4.5);
        }
    }

private:
    const GuiTheme& colors_;
    QList<QPointF> points_;
    int range_ = 3600;
    bool dimmed_ = false;
};

class Sparkline : public QWidget
{
public:
    explicit Sparkline(const GuiTheme& colors) : colors_(colors)
    {
        setFixedSize(132, 30);
        setAccessibleName("Recent GPU hashrate");
    }
    void add(double value)
    {
        values_.append(value);
        while (values_.size() > 40)
            values_.removeFirst();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (values_.size() < 2)
            return;
        const auto [low, high] = std::minmax_element(values_.begin(), values_.end());
        const double middle = (*low + *high) / 2;
        const double span = std::max({*high - *low, middle * 0.08, 0.1});
        QPainterPath path;
        for (int i = 0; i < values_.size(); ++i)
        {
            const QPointF point(1 + i * (width() - 2.0) / (values_.size() - 1),
                height() / 2.0 - (values_[i] - middle) / span * (height() - 4));
            if (i)
                path.lineTo(point);
            else
                path.moveTo(point);
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(colors_.chartLine, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
    }

private:
    const GuiTheme& colors_;
    QList<double> values_;
};

struct GpuReading
{
    QString name, meta, rate, status, statusTip, shares;
    QString temperature, fan, power; //!< Empty when the device does not report the sensor.
    Tone tone = Tone::Neutral;
};

class GpuCard : public QFrame
{
public:
    explicit GpuCard(const GuiTheme& colors) : colors_(colors)
    {
        setObjectName("gpuCard");
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(16, 12, 16, 10);
        layout->setSpacing(6);
        auto* top = new QHBoxLayout;
        top->setSpacing(12);
        badge_ = label("", "badge");
        badge_->setFixedSize(36, 36);
        badge_->setAlignment(Qt::AlignCenter);
        top->addWidget(badge_, 0, Qt::AlignTop);
        auto* names = new QVBoxLayout;
        names->setSpacing(0);
        name_ = label("", "strong");
        name_->setObjectName("gpuName");
        name_->setWordWrap(true);
        meta_ = label("", "faint");
        meta_->setObjectName("gpuMeta");
        meta_->setWordWrap(true);
        names->addWidget(name_);
        names->addWidget(meta_);
        top->addLayout(names, 1);
        status_ = new StatusPill(colors);
        status_->setObjectName("gpuStatus");
        top->addWidget(status_, 0, Qt::AlignTop);
        layout->addLayout(top);
        auto* rateRow = new QHBoxLayout;
        rateRow->setSpacing(5);
        rate_ = label("", "gpuRate");
        rate_->setObjectName("gpuHashrate");
        auto* unit = label("MH/s", "unit");
        unit->setObjectName("gpuUnit");
        rateRow->addWidget(rate_, 0, Qt::AlignTop);
        rateRow->addWidget(unit, 0, Qt::AlignTop);
        new BaselineAligner(rate_, unit);
        rateRow->addStretch();
        spark_ = new Sparkline(colors);
        rateRow->addWidget(spark_, 0, Qt::AlignVCenter);
        layout->addLayout(rateRow);
        auto* divider = new QFrame;
        divider->setObjectName("gpuDivider");
        divider->setFixedHeight(1);
        layout->addWidget(divider);
        auto* sensors = new QHBoxLayout;
        sensors->setSpacing(5);
        auto addSensor = [&](Glyph glyph, const char* name) {
            auto* icon = new QLabel;
            icon->setFixedSize(16, 16);
            icons_.append({icon, glyph});
            auto* value = label("", "muted");
            value->setObjectName(name);
            sensors->addWidget(icon);
            sensors->addWidget(value);
            sensors->addSpacing(10);
            return value;
        };
        temperature_ = addSensor(Glyph::Temperature, "gpuTemperature");
        fan_ = addSensor(Glyph::Fan, "gpuFan");
        power_ = addSensor(Glyph::Power, "gpuPower");
        sensors->addStretch();
        shares_ = label("", "faint");
        shares_->setObjectName("gpuShares");
        sensors->addWidget(shares_);
        layout->addLayout(sensors);
        refreshIcons();
    }
    void setReading(const GpuReading& reading)
    {
        name_->setText(reading.name);
        meta_->setText(reading.meta);
        setValue(rate_, reading.rate);
        status_->setStatus(reading.status, reading.tone);
        status_->setToolTip(reading.statusTip);
        setSensors(reading.temperature, reading.fan, reading.power);
        shares_->setText(reading.shares);
    }
    // A lost connection can leave stale readings in the API; show none until fresh ones arrive.
    void setWaiting()
    {
        setValue(rate_, dash());
        status_->setStatus("Waiting", Tone::Warning);
        status_->setToolTip("Waiting for fresh statistics");
        setSensors({}, {}, {});
    }
    void setStopping()
    {
        status_->setStatus("Stopping", Tone::Neutral);
        status_->setToolTip(QString());
    }
    void addRate(double rate) { spark_->add(rate); }
    void refreshIcons()
    {
        badge_->setPixmap(glyphPixmap(Glyph::Gpu, colors_.wineText, 20));
        for (const auto& [icon, glyph] : icons_)
            icon->setPixmap(glyphPixmap(glyph, colors_.inkFaint, 16));
    }

private:
    // Empty readings show a dash and are announced as unavailable.
    void setSensors(const QString& temperature, const QString& fan, const QString& power)
    {
        const QList<std::pair<QLabel*, QString>> sensors{{temperature_, temperature}, {fan_, fan}, {power_, power}};
        const QStringList names{"Temperature", "Fan", "Power"};
        for (int i = 0; i < sensors.size(); ++i)
        {
            const auto& [value, text] = sensors[i];
            value->setText(text.isEmpty() ? dash() : text);
            value->setAccessibleName(names[i] + ' ' + (text.isEmpty() ? QStringLiteral("unavailable") : text));
        }
    }

    const GuiTheme& colors_;
    QLabel *badge_, *name_, *meta_, *rate_, *temperature_, *fan_, *power_, *shares_;
    StatusPill* status_;
    Sparkline* spark_;
    QList<std::pair<QLabel*, Glyph>> icons_;
};

// Cards flow into as many columns as fit, so a rig with many GPUs stays readable.
class GpuGrid : public QWidget
{
public:
    explicit GpuGrid(const GuiTheme& colors) : colors_(colors)
    {
        setObjectName("overviewDevices");
        layout_ = new QGridLayout(this);
        layout_->setContentsMargins(0, 0, 0, 0);
        layout_->setSpacing(16);
        // The grid may shrink below its current columns; the next resize reflows the cards.
        layout_->setSizeConstraint(QLayout::SetNoConstraint);
    }
    QSize minimumSizeHint() const override
    {
        int width = 0;
        for (auto* card : cards_)
            width = std::max(width, card->minimumSizeHint().width());
        return {width, layout_->minimumSize().height()};
    }
    int count() const { return cards_.size(); }
    GpuCard* card(int index) const { return cards_.value(index); }
    void setCount(int count)
    {
        if (count == cards_.size())
            return;
        while (cards_.size() > count)
            delete cards_.takeLast();
        while (cards_.size() < count)
            cards_.append(new GpuCard(colors_));
        arrange(true);
    }
    void refreshIcons()
    {
        for (auto* card : cards_)
            card->refreshIcons();
    }
    //! Recheck the columns after readings change a card's minimum width.
    void refreshLayout() { arrange(false); }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        arrange(false);
    }

private:
    void arrange(bool force)
    {
        // Larger text widens each card, so the column count follows the cards' real minimum width.
        int cardWidth = 300;
        for (auto* card : cards_)
            cardWidth = std::max(cardWidth, card->minimumSizeHint().width());
        const int spacing = layout_->horizontalSpacing();
        const int columns = std::clamp((width() + spacing) / (cardWidth + spacing), 1, 3);
        if (!force && columns == columns_)
            return;
        columns_ = columns;
        for (auto* card : cards_)
            layout_->removeWidget(card);
        for (int i = 0; i < cards_.size(); ++i)
            layout_->addWidget(cards_[i], i / columns, i % columns);
        for (int column = 0; column < 3; ++column)
            layout_->setColumnStretch(column, column < columns ? 1 : 0);
    }

    const GuiTheme& colors_;
    QGridLayout* layout_;
    QList<GpuCard*> cards_;
    int columns_ = 0;
};

void MainWindow::installBrandFonts()
{
    QApplication::setFont(bodyFont(QApplication::font()));
}

MainWindow::~MainWindow()
{
    // Quitting mid-session, for example at logout, still remembers it.
    if (hasReadings_)
        recordSession();
    // The pages' widgets hold references to colors_, so delete them while it is still alive.
    delete centralWidget();
}

void MainWindow::updateTheme()
{
    if (highContrastEnabled() || appearance_ == "system")
    {
        // A user's high-contrast setting always takes precedence over the theme.
        // Replacing a style sheet restores the palette it saved, so the palette follows it.
        *colors_ = nativeTheme(QApplication::palette());
        setStyleSheet(nativeSheet());
        setPalette(QApplication::palette());
    }
    else
    {
        *colors_ = brandTheme(appearance_ == "dark");
        const auto& theme = *colors_;
        QPalette colors = QApplication::palette();
        auto color = [&](QPalette::ColorRole role, const QColor& value) { colors.setColor(role, value); };
        color(QPalette::Window, theme.bg);
        color(QPalette::WindowText, theme.ink);
        color(QPalette::Base, theme.panel);
        color(QPalette::AlternateBase, theme.panelSoft);
        color(QPalette::Text, theme.ink);
        color(QPalette::Button, theme.panel);
        color(QPalette::ButtonText, theme.ink);
        color(QPalette::BrightText, Qt::white);
        color(QPalette::Highlight, theme.wine);
        color(QPalette::HighlightedText, Qt::white);
        color(QPalette::Link, theme.wineText);
        color(QPalette::LinkVisited, theme.wineDeep);
        color(QPalette::ToolTipBase, theme.panel);
        color(QPalette::ToolTipText, theme.ink);
        color(QPalette::PlaceholderText, theme.inkFaint);
        color(QPalette::Light, theme.panel);
        color(QPalette::Midlight, theme.hover);
        color(QPalette::Mid, theme.border);
        color(QPalette::Dark, theme.fieldBorder);
        color(QPalette::Shadow, theme.inkFaint);
        for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
            colors.setColor(QPalette::Disabled, role, theme.inkFaint);
        setStyleSheet(brandedSheet(theme));
        setPalette(colors);
    }
    refreshIcons();
    update();
}

void MainWindow::refreshIcons()
{
    if (!navigation_)
        return;
    const auto& theme = *colors_;
    const QColor normal = theme.branded ? theme.inkSoft : palette().color(QPalette::WindowText);
    const QColor selected = theme.branded ? theme.wineText : palette().color(QPalette::HighlightedText);
    const QList<Glyph> glyphs{Glyph::Overview, Glyph::Setup, Glyph::Gpu, Glyph::Activity};
    for (int i = 0; i < navigation_->count() && i < glyphs.size(); ++i)
        navigation_->item(i)->setIcon(glyphIcon(glyphs[i], normal, selected));
    settingsButton_->setIcon(glyphIcon(Glyph::Settings, normal));
    helpButton_->setIcon(glyphIcon(Glyph::Help, normal));
    copyAddress_->setIcon(glyphIcon(Glyph::Copy, normal, QColor(), 18));
    const bool primary = start_->property("role").toString() == "primary";
    const bool setup = start_->property("glyph").toString() == "setup";
    const QColor onPrimary = theme.branded ? QColor(Qt::white) : palette().color(QPalette::ButtonText);
    start_->setIcon(glyphIcon(setup ? Glyph::Setup : primary ? Glyph::Play : Glyph::Stop,
        primary ? onPrimary : theme.branded ? theme.wineText : palette().color(QPalette::ButtonText), QColor(), 16));
    gpuGrid_->refreshIcons();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
        // Let Qt finish propagating the application palette before repolishing
        // our stylesheet; otherwise Qt 6.2 can restore a child's old colors.
        QTimer::singleShot(0, this, &MainWindow::updateTheme);
    return QMainWindow::eventFilter(watched, event);
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), controller_(this), colors_(std::make_unique<GuiTheme>(brandTheme(false)))
{
    setWindowTitle("Firominer");
    setWindowIcon(QIcon(":/firominer.ico"));
    setFont(bodyFont(QApplication::font()));
    const QSize available = screen()->availableGeometry().size() - QSize(40, 80);
    resize(QSize(1120, 800).boundedTo(available));
    setMinimumSize(QSize(640, 400).boundedTo(available));
    qApp->installEventFilter(this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    connect(QGuiApplication::styleHints()->accessibility(), &QAccessibilityHints::contrastPreferenceChanged,
        this, [this] { updateTheme(); });
#endif

    auto* shell = new QWidget;
    auto* outer = new QHBoxLayout(shell);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(sidebar());

    auto* workspace = new QWidget;
    workspace->setObjectName("workspace");
    auto* content = new QVBoxLayout(workspace);
    content->setContentsMargins(28, 22, 28, 12);
    content->setSpacing(16);
    auto* header = new QFormLayout;
    header->setContentsMargins(0, 0, 0, 0);
    header->setRowWrapPolicy(QFormLayout::WrapLongRows);
    header->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto* heading = new QVBoxLayout;
    heading->setContentsMargins(0, 0, 0, 0);
    heading->setSpacing(2);
    title_ = label("Overview", "title");
    heading->addWidget(caption("This computer · " + QHostInfo::localHostName().section('.', 0, 0)));
    heading->addWidget(title_);
    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(14);
    controls->addStretch();
    state_ = new StatusPill(*colors_);
    state_->setObjectName("miningState");
    state_->setText("Stopped");
    runtime_ = label("Ready when you are", "muted");
    runtime_->setObjectName("miningRuntime");
    start_ = button("Start pool mining", "primary");
    start_->setObjectName("startMining");
    start_->setMinimumWidth(170);
    start_->setIconSize(QSize(16, 16));
    controls->addWidget(state_);
    controls->addWidget(runtime_);
    controls->addWidget(start_);
    auto* headingWidget = new QWidget;
    headingWidget->setLayout(heading);
    header->addRow(headingWidget, controls);
    content->addLayout(header);
    notice_ = label("");
    notice_->setObjectName("notice");
    notice_->setWordWrap(true);
    notice_->hide();
    content->addWidget(notice_);
    pages_ = new QStackedWidget;
    // Pages scroll independently; their size hints must not expand the whole window.
    pages_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    pages_->addWidget(overviewPage());
    pages_->addWidget(setupPage());
    pages_->addWidget(devicesPage());
    pages_->addWidget(activityPage());
    content->addWidget(pages_, 1);
    outer->addWidget(workspace, 1);
    auto* shellScroll = scrollPage(shell);
    shellScroll->setObjectName("shellScroll");
    setCentralWidget(shellScroll);
    statusBar()->setSizeGripEnabled(true);

    connect(navigation_, &QListWidget::currentRowChanged, this, [this](int row) {
        pages_->setCurrentIndex(row);
        const QStringList titles{"Overview", "Mining setup", "Your GPUs", "Activity"};
        title_->setText(titles.value(row));
    });
    navigation_->setCurrentRow(0);
    connect(start_, &QPushButton::clicked, this, &MainWindow::toggleMining);
    connect(settingsButton_, &QPushButton::clicked, this, &MainWindow::showSettings);
    connect(helpButton_, &QPushButton::clicked, this, [this] {
        QMessageBox::information(this, "Using Firominer",
            "1. Open Mining setup and choose Pool or Solo.\n"
            "   Pool uses your pool endpoint and payout account. Solo uses your own synced Firo node, RPC login and transparent reward address.\n"
            "2. Choose your GPU backend, or leave Automatic selected.\n"
            "3. Click Start mining.\n\n"
            "The GUI runs firominer beside it. Select another executable in Settings if needed. "
            "The miner needs a compatible GPU driver.\n\n"
            "Minimizing this window keeps mining. Closing while mining asks whether to stop "
            "or keep mining in the system tray, when available.\n\n"
            "For Solo, open Node setup & config for the matching firo.conf settings. Restart Firo Core after changes and keep it synced while mining.\n\n"
            "This GUI mines Mainnet. Other networks and advanced options remain available in the CLI.");
    });
    connect(&controller_, &MinerController::statistics, this, &MainWindow::updateStatistics);
    connect(&controller_, &MinerController::stateChanged, this, &MainWindow::setMiningState);
    connect(&controller_, &MinerController::logLine, this, &MainWindow::appendActivity);
    connect(&controller_, &MinerController::failure, this, &MainWindow::showFailure);
    connect(&controller_, &MinerController::nodeChecked, this, [this](bool, const QString& message) {
        nodeStatus_->setText(message);
        statusBar()->showMessage(message, 8000);
    });
    connect(&controller_, &MinerController::finished, this, [this] {
        if (closing_)
            QTimer::singleShot(0, this, &QWidget::close);
    });

    tray_ = new QSystemTrayIcon(windowIcon(), this);
    tray_->setToolTip("Firominer - stopped");
    auto* trayMenu = new QMenu(this);
    trayMenu->addAction("Show Firominer", this, [this] { showNormal(); raise(); activateWindow(); });
    trayMenu->addAction("Stop mining", &controller_, &MinerController::stop);
    trayMenu->addSeparator();
    trayMenu->addAction("Quit", this, [this] { showNormal(); close(); });
    tray_->setContextMenu(trayMenu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
        { showNormal(); raise(); activateWindow(); }
    });
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray_->show();
    loadSettings();
    resize(size().boundedTo(available));
    clearReadings();
    for (auto* input : {poolInput_, walletInput_, workerInput_, passwordInput_, nodeInput_, rpcUserInput_, rpcPasswordInput_,
             rewardInput_, coinbaseMessageInput_, devicesInput_})
        connect(input, &QLineEdit::textChanged, this, [this] {
            if (currentState_ != "Stopped")
                return;
            updateConnectionSummary();
            updateOverview();
        });
    connect(backendInput_, &QComboBox::currentIndexChanged, this, &MainWindow::updateOverview);
}

QWidget* MainWindow::sidebar()
{
    sidebar_ = new QFrame;
    sidebar_->setObjectName("sidebar");
    sidebar_->setFixedWidth(220);
    auto* side = new QVBoxLayout(sidebar_);
    side->setContentsMargins(12, 22, 12, 14);
    side->setSpacing(4);
    auto* brand = new QHBoxLayout;
    brand->setContentsMargins(10, 0, 0, 0);
    brand->setSpacing(10);
    auto* icon = label("");
    icon->setPixmap(windowIcon().pixmap(30, 30));
    brandName_ = label("firominer", "brand");
    brand->addWidget(icon);
    brand->addWidget(brandName_, 1);
    side->addLayout(brand);
    side->addSpacing(20);
    navigation_ = new QListWidget;
    navigation_->setObjectName("navigation");
    navigation_->setAccessibleName("Navigation");
    navigation_->setIconSize(QSize(20, 20));
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigationNames_ = QStringList{"Overview", "Mining setup", "GPUs", "Activity"};
    navigation_->addItems(navigationNames_);
    for (int i = 0; i < navigation_->count(); ++i)
        navigation_->item(i)->setData(Qt::AccessibleTextRole, navigationNames_[i]);
    navigation_->setMinimumHeight(200);
    side->addWidget(navigation_, 1);
    settingsButton_ = button("Settings", "sidebar");
    settingsButton_->setObjectName("settingsButton");
    helpButton_ = button("Help", "sidebar");
    for (auto* item : {settingsButton_, helpButton_})
    {
        item->setIconSize(QSize(20, 20));
        side->addWidget(item);
    }
    sidebarFooter_ = label(QString("Mainnet · FiroPoW · %1").arg(FIROMINER_GUI_VERSION), "faint");
    sidebarFooter_->setContentsMargins(12, 8, 0, 0);
    sidebarFooter_->setWordWrap(true);
    side->addWidget(sidebarFooter_);
    return sidebar_;
}

QWidget* MainWindow::heroPanel()
{
    hero_ = new HeroFrame(*colors_);
    auto* layout = new QBoxLayout(QBoxLayout::LeftToRight, hero_);
    heroLayout_ = layout;
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(20);
    auto* summary = new QVBoxLayout;
    summary->setSpacing(2);
    heroCaption_ = caption("Total hashrate");
    summary->addWidget(heroCaption_);
    hashrateRow_ = new QWidget;
    auto* rateRow = new QHBoxLayout(hashrateRow_);
    rateRow->setContentsMargins(0, 0, 0, 0);
    rateRow->setSpacing(8);
    hashrate_ = label("0.0", "heroValue");
    hashrate_->setObjectName("totalHashrate");
    hashrateUnit_ = label("MH/s", "heroUnit");
    hashrateUnit_->setObjectName("totalHashrateUnit");
    rateRow->addWidget(hashrate_, 0, Qt::AlignTop);
    rateRow->addWidget(hashrateUnit_, 0, Qt::AlignTop);
    new BaselineAligner(hashrate_, hashrateUnit_);
    rateRow->addStretch();
    summary->addWidget(hashrateRow_);
    heroTitle_ = label("", "heroTitle");
    heroTitle_->setObjectName("heroTitle");
    heroTitle_->setWordWrap(true);
    summary->addWidget(heroTitle_);
    gpuCount_ = label("", "heroSoft");
    gpuCount_->setObjectName("heroDetail");
    gpuCount_->setWordWrap(true);
    gpuCount_->setMinimumWidth(220);
    summary->addSpacing(6);
    summary->addWidget(gpuCount_);
    layout->addLayout(summary, 1);

    auto statsRow = [](QWidget*& container) {
        container = new QWidget;
        auto* row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        return row;
    };
    // Details are a step smaller than the body text and scale with it.
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.87);
    auto addStat = [&small](QHBoxLayout* row, const QString& title, QLabel*& value, QLabel*& detail) {
        auto* stat = new QWidget;
        stat->setObjectName("heroStat");
        stat->setAttribute(Qt::WA_StyledBackground);
        auto* column = new QVBoxLayout(stat);
        column->setContentsMargins(18, 2, 14, 2);
        column->setSpacing(0);
        auto* name = caption(title);
        column->addWidget(name);
        value = label("", "statValue");
        column->addWidget(value);
        detail = new DetailLabel;
        detail->setTextFormat(Qt::PlainText);
        detail->setProperty("role", "heroSoft");
        detail->setWordWrap(true);
        detail->setFont(small);
        column->addWidget(detail);
        column->addStretch();
        row->addWidget(stat);
        return name;
    };
    auto* live = statsRow(liveStats_);
    acceptedLabel_ = addStat(live, "Accepted shares", accepted_, shareDetail_);
    acceptedLabel_->setObjectName("acceptedLabel");
    accepted_->setObjectName("acceptedShares");
    lastShareLabel_ = addStat(live, "Last share", lastShare_, lastShareDetail_);
    lastShare_->setObjectName("lastShare");
    addStat(live, "GPU power", power_, powerDetail_);
    power_->setObjectName("totalPower");
    powerDetail_->setToolTip("Hashrate for each watt the GPUs report");
    layout->addWidget(liveStats_, 0, Qt::AlignVCenter);

    auto* session = statsRow(sessionStats_);
    sessionStats_->setObjectName("lastSession");
    addStat(session, "Last session", sessionRuntime_, sessionEnded_);
    sessionRuntime_->setObjectName("sessionRuntime");
    sessionAcceptedLabel_ = addStat(session, "Accepted", sessionAccepted_, sessionRejected_);
    sessionAccepted_->setObjectName("sessionAccepted");
    QLabel* averageUnit = nullptr;
    addStat(session, "Average", sessionAverage_, averageUnit);
    sessionAverage_->setObjectName("sessionAverage");
    averageUnit->setText("MH/s");
    layout->addWidget(sessionStats_, 0, Qt::AlignVCenter);
    return hero_;
}

QWidget* MainWindow::overviewPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);
    layout->addWidget(heroPanel());

    auto* middle = new QBoxLayout(QBoxLayout::LeftToRight);
    middleLayout_ = middle;
    middle->setSpacing(16);
    auto* chartPanel = panel();
    auto* chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(18, 16, 18, 10);
    chartLayout->setSpacing(6);
    auto* chartTop = new QHBoxLayout;
    auto* chartTitles = new QVBoxLayout;
    chartTitles->setSpacing(0);
    chartTitles->addWidget(label("Hashrate", "section"));
    chartSubtitle_ = label("MH/s · this session", "faint");
    chartSubtitle_->setObjectName("chartSubtitle");
    chartTitles->addWidget(chartSubtitle_);
    chartTop->addLayout(chartTitles, 1);
    auto* ranges = new QFrame;
    ranges->setObjectName("segmented");
    auto* rangeLayout = new QHBoxLayout(ranges);
    rangeLayout->setContentsMargins(3, 3, 3, 3);
    rangeLayout->setSpacing(2);
    QList<QPushButton*> rangeButtons;
    const QStringList rangeNames{"Last 15 minutes", "Last hour", "Last 6 hours"};
    for (const auto& text : {"15m", "1h", "6h"})
    {
        auto* range = button(text, "segment");
        range->setCheckable(true);
        range->setAutoExclusive(true);
        range->setChecked(QString(text) == "1h");
        range->setAccessibleName(rangeNames.value(rangeButtons.size()));
        rangeButtons.append(range);
        rangeLayout->addWidget(range);
    }
    chartTop->addWidget(ranges, 0, Qt::AlignTop);
    chartLayout->addLayout(chartTop);
    chart_ = new HashrateChart(*colors_);
    chartLayout->addWidget(chart_, 1);
    auto* history = button("View history", "link");
    history->setObjectName("viewHistory");
    chartLayout->addWidget(history, 0, Qt::AlignLeft);
    connect(history, &QPushButton::clicked, chart_, &HashrateChart::showHistory);
    const QList<int> intervals{900, 3600, 21600};
    for (int i = 0; i < rangeButtons.size(); ++i)
        connect(rangeButtons[i], &QPushButton::clicked, chart_, [this, intervals, i] { chart_->setRange(intervals[i]); });
    middle->addWidget(chartPanel, 3);

    auto* connection = panel();
    connection->setMinimumWidth(240);
    auto* poolLayout = new QVBoxLayout(connection);
    poolLayout->setContentsMargins(18, 16, 18, 14);
    poolLayout->setSpacing(2);
    auto* poolTop = new QHBoxLayout;
    connectionTitle_ = label("Pool", "section");
    poolTop->addWidget(connectionTitle_, 1);
    poolState_ = new StatusPill(*colors_);
    poolState_->setObjectName("poolState");
    poolTop->addWidget(poolState_);
    poolLayout->addLayout(poolTop);
    poolLayout->addSpacing(8);
    auto addField = [&](const QString& text, QLabel*& value) {
        auto* name = caption(text);
        poolLayout->addWidget(name);
        value = label("", "strong");
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        poolLayout->addWidget(value);
        poolLayout->addSpacing(6);
        return name;
    };
    endpointLabel_ = addField("Pool", pool_);
    workerLabel_ = addField("Worker", worker_);
    rewardLabel_ = caption("Payout address");
    poolLayout->addWidget(rewardLabel_);
    auto* payoutRow = new QHBoxLayout;
    payoutRow->setSpacing(4);
    wallet_ = label("");
    wallet_->setObjectName("payoutSummary");
    wallet_->setFont(monoFont(13));
    wallet_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    payoutRow->addWidget(wallet_, 1);
    copyAddress_ = button("", "icon");
    copyAddress_->setObjectName("copyAddress");
    copyAddress_->setToolTip("Copy payout address");
    copyAddress_->setAccessibleName("Copy payout address");
    copyAddress_->setIconSize(QSize(18, 18));
    payoutRow->addWidget(copyAddress_);
    poolLayout->addLayout(payoutRow);
    poolLayout->addSpacing(6);
    connect(copyAddress_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText((soloMode_->isChecked() ? rewardInput_ : walletInput_)->text().trimmed());
        statusBar()->showMessage("Address copied", 3000);
    });
    poolLayout->addStretch();
    auto* edit = button("Edit mining setup", "link");
    connect(edit, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(1); });
    poolLayout->addWidget(edit, 0, Qt::AlignLeft);
    middle->addWidget(connection, 2);
    layout->addLayout(middle, 1);

    auto* gpuHeader = new QHBoxLayout;
    gpuHeader->setSpacing(8);
    gpuHeader->addWidget(label("Your GPUs", "section"));
    gpuSummary_ = label("", "faint");
    gpuSummary_->setObjectName("gpuSummary");
    gpuHeader->addWidget(gpuSummary_, 1);
    auto* details = button(QString::fromUtf8("GPU details →"), "link");
    connect(details, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(2); });
    gpuHeader->addWidget(details);
    layout->addLayout(gpuHeader);
    gpuGrid_ = new GpuGrid(*colors_);
    layout->addWidget(gpuGrid_);
    gpuEmpty_ = new QFrame;
    gpuEmpty_->setObjectName("gpuEmpty");
    auto* emptyLayout = new QHBoxLayout(gpuEmpty_);
    emptyLayout->setContentsMargins(18, 14, 18, 14);
    auto* emptyText = label("Your GPUs appear here once mining starts. Automatic uses every compatible GPU; "
        "choose specific devices in Mining setup.", "muted");
    emptyText->setWordWrap(true);
    emptyLayout->addWidget(emptyText);
    layout->addWidget(gpuEmpty_);
    return scrollPage(page);
}

QWidget* MainWindow::setupPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* card = panel();
    auto* body = new QVBoxLayout(card);
    body->setContentsMargins(20, 18, 20, 18);
    body->setSpacing(14);
    auto* modes = new QHBoxLayout;
    modes->addWidget(label("Mining mode", "section"), 1);
    auto* modeSwitch = new QFrame;
    modeSwitch->setObjectName("segmented");
    auto* modeLayout = new QHBoxLayout(modeSwitch);
    modeLayout->setContentsMargins(3, 3, 3, 3);
    modeLayout->setSpacing(2);
    auto* group = new QButtonGroup(this);
    poolMode_ = button("Pool", "segment");
    soloMode_ = button("Solo · own node", "segment");
    poolMode_->setObjectName("poolMode");
    soloMode_->setObjectName("soloMode");
    for (auto* mode : {poolMode_, soloMode_})
    {
        mode->setCheckable(true);
        group->addButton(mode);
        modeLayout->addWidget(mode);
    }
    poolMode_->setChecked(true);
    modes->addWidget(modeSwitch);
    body->addLayout(modes);
    auto* heading = new QFormLayout;
    heading->setContentsMargins(0, 0, 0, 0);
    heading->setRowWrapPolicy(QFormLayout::WrapLongRows);
    setupHeading_ = label("Connect to a mining pool", "section");
    nodeGuideButton_ = button("Node setup && config", "link");
    nodeGuideButton_->setObjectName("nodeGuideButton");
    nodeGuideButton_->setCheckable(true);
    heading->addRow(setupHeading_, nodeGuideButton_);
    body->addLayout(heading);
    setupIntro_ = label("", "muted");
    setupIntro_->setWordWrap(true);
    body->addWidget(setupIntro_);
    nodeGuide_ = new QWidget;
    nodeGuide_->setObjectName("nodeGuide");
    auto* guide = new QVBoxLayout(nodeGuide_);
    guide->setContentsMargins(0, 0, 0, 0);
    guide->setSpacing(10);
    auto* instructions = label("Local node example: update the existing entries in firo.conf. "
        "Use a strong, unique password and enter the same password below.", "muted");
    instructions->setWordWrap(true);
    guide->addWidget(instructions);
    auto* config = label("server=1\nrpcbind=127.0.0.1\nrpcallowip=127.0.0.1\nrpcport=8888\n"
        "rpcuser=miner\nrpcpassword=CHANGE_ME", "code");
    config->setFont(monoFont(13));
    config->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    config->setWordWrap(true);
    guide->addWidget(config);
    auto* restart = label("Restart Firo Core after saving, wait until fully synced, and keep it open while mining. "
        "8888 is the Mainnet RPC default; match your existing port if different. "
        "These settings allow mining on this computer only. For another computer, configure the node's bind address and allow only the miner's IP.", "muted");
    restart->setWordWrap(true);
    guide->addWidget(restart);
    body->addWidget(nodeGuide_);
    nodeGuide_->hide();
    connect(nodeGuideButton_, &QPushButton::toggled, nodeGuide_, &QWidget::setVisible);

    auto makeForm = [](QWidget* parent) {
        auto* form = new QFormLayout(parent);
        form->setContentsMargins(0, 0, 0, 0);
        form->setVerticalSpacing(12);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        return form;
    };
    auto field = [&](QFormLayout* form, const QString& caption, const char* name,
                     const QString& placeholder, const QString& tip = QString()) {
        auto* input = new QLineEdit;
        input->setObjectName(name);
        input->setAccessibleName(caption);
        input->setPlaceholderText(placeholder);
        input->setMaxLength(1024);
        input->setMinimumWidth(200);
        auto* rowLabel = new QWidget;
        rowLabel->setMinimumWidth(180);
        auto* captionLayout = new QHBoxLayout(rowLabel);
        captionLayout->setContentsMargins(0, 0, 0, 0);
        captionLayout->setSpacing(4);
        auto* text = label(caption, "strong");
        text->setBuddy(input);
        captionLayout->addWidget(text);
        if (!tip.isEmpty())
        {
            input->setToolTip(tip);
            input->setAccessibleDescription(tip);
            auto* help = new QToolButton;
            help->setText("?");
            help->setAutoRaise(true);
            help->setAccessibleName(caption + " help");
            help->setToolTip(tip);
            connect(help, &QToolButton::clicked, this, [help] {
                QToolTip::showText(help->mapToGlobal(QPoint(0, help->height())), help->toolTip(), help);
            });
            captionLayout->addWidget(help);
        }
        captionLayout->addStretch();
        form->addRow(rowLabel, input);
        return input;
    };
    poolFields_ = new QWidget;
    auto* poolForm = makeForm(poolFields_);
    poolInput_ = field(poolForm, "Pool endpoint", "poolInput", "stratum+tcp://pool.example:3333");
    walletInput_ = field(poolForm, "Payout address / account", "walletInput", "Your Firo payout address or pool username");
    workerInput_ = field(poolForm, "Worker name", "workerInput", "Optional, for example desktop-01");
    passwordInput_ = field(poolForm, "Pool password", "passwordInput", "x (unless your pool specifies another password)");
    passwordInput_->setEchoMode(QLineEdit::Password);
    passwordInput_->setToolTip("Kept only for this session. It is not saved to disk.");
    body->addWidget(poolFields_);
    soloFields_ = new QWidget;
    auto* soloForm = makeForm(soloFields_);
    nodeInput_ = field(soloForm, "Node endpoint", "nodeInput", "http://127.0.0.1:8888",
        "In firo.conf, set server=1, rpcbind=127.0.0.1, rpcallowip=127.0.0.1 and rpcport=8888. "
        "Restart Firo Core after saving. 127.0.0.1 is this computer; match your existing RPC port if different. "
        "Remote RPC uses HTTP: use a trusted private connection, never an exposed Internet endpoint.");
    rpcUserInput_ = field(soloForm, "RPC username", "rpcUserInput", "Same as rpcuser in firo.conf",
        "Set rpcuser=miner in firo.conf, or enter your existing rpcuser here. Restart Firo Core after changes. "
        "This is the node login, not your wallet address.");
    rpcPasswordInput_ = field(soloForm, "RPC password", "rpcPasswordInput", "Same as rpcpassword in firo.conf",
        "Set rpcpassword to a strong, unique password in firo.conf and enter it here. Restart Firo Core after changes. "
        "This is not your wallet encryption password. Kept only for this session; not saved to disk.");
    rpcPasswordInput_->setEchoMode(QLineEdit::Password);
    auto* passwordField = new QWidget;
    delete soloForm->replaceWidget(rpcPasswordInput_, passwordField);
    auto* passwordRowLayout = new QHBoxLayout(passwordField);
    passwordRowLayout->setContentsMargins(0, 0, 0, 0);
    passwordRowLayout->addWidget(rpcPasswordInput_, 1);
    auto* reveal = button("Show");
    reveal->setAccessibleName("Show RPC password");
    reveal->setCheckable(true);
    passwordRowLayout->addWidget(reveal);
    connect(reveal, &QPushButton::toggled, this, [this, reveal](bool visible) {
        rpcPasswordInput_->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        reveal->setText(visible ? "Hide" : "Show");
        reveal->setAccessibleName(visible ? "Hide RPC password" : "Show RPC password");
    });
    rewardInput_ = field(soloForm, "Reward address", "rewardInput", "Your transparent Mainnet Firo address",
        "Use a transparent receiving address from your Firo wallet. Spark addresses cannot receive solo block rewards. "
        "Rewards arrive only when you find a block and become spendable after enough confirmations.");
    auto* addressNote = label("Transparent address required. Spark addresses are not supported for solo rewards.", "faint");
    addressNote->setWordWrap(true);
    soloForm->addRow("", addressNote);
    coinbaseMessageInput_ = field(soloForm, "Coinbase message", "coinbaseMessageInput", "Optional, for example Zed",
        "Public text embedded in blocks you mine. Up to 80 UTF-8 bytes. "
        "Requires a Firo node with coinbase-message support. Explorer display depends on the explorer.");
    auto* check = new QHBoxLayout;
    check->setSpacing(10);
    testNode_ = button("Test node");
    testNode_->setObjectName("testNode");
    nodeStatus_ = label("Connection not checked", "muted");
    nodeStatus_->setObjectName("nodeStatus");
    nodeStatus_->setWordWrap(true);
    check->addWidget(testNode_);
    check->addWidget(nodeStatus_, 1);
    soloForm->addRow("", check);
    connect(testNode_, &QPushButton::clicked, this, [this] { controller_.testNode(configuration()); });
    for (auto* input : {nodeInput_, rpcUserInput_, rpcPasswordInput_, rewardInput_, coinbaseMessageInput_})
        connect(input, &QLineEdit::textChanged, this, [this] { nodeStatus_->setText("Connection not checked"); });
    body->addWidget(soloFields_);
    auto* gpu = new QWidget;
    auto* gpuForm = makeForm(gpu);
    backendInput_ = new QComboBox;
    backendInput_->setObjectName("backendInput");
    backendInput_->addItem("Automatic · all compatible GPUs", "auto");
    backendInput_->addItem("NVIDIA CUDA", "cuda");
    backendInput_->addItem("OpenCL", "opencl");
    auto* backendLabel = label("GPU backend", "strong");
    backendLabel->setMinimumWidth(180);
    backendLabel->setBuddy(backendInput_);
    gpuForm->addRow(backendLabel, backendInput_);
    body->addWidget(gpu);
    devicesRow_ = new QWidget;
    devicesInput_ = field(makeForm(devicesRow_), "Device numbers", "devicesInput", "All devices, or numbers such as 0, 1");
    devicesInput_->setToolTip("Device numbers from firominer --list-devices for the selected backend.");
    connect(backendInput_, &QComboBox::currentIndexChanged, this, [this] {
        devicesInput_->setEnabled(!controller_.isRunning() && backendInput_->currentData().toString() != "auto");
        devicesRow_->setVisible(backendInput_->currentData().toString() != "auto");
    });
    body->addWidget(devicesRow_);
    auto* note = label("Passwords stay in this session only. Automatic uses all compatible GPUs.", "faint");
    note->setWordWrap(true);
    saveSetup_ = button("Save setup");
    saveSetup_->setObjectName("saveSetup");
    connect(saveSetup_, &QPushButton::clicked, this, [this] {
        if (saveSettings())
        {
            updateConnectionSummary();
            statusBar()->showMessage("Mining setup saved", 4000);
        }
    });
    auto* actions = new QHBoxLayout;
    actions->addWidget(saveSetup_);
    actions->addWidget(note, 1);
    body->addLayout(actions);
    connect(soloMode_, &QPushButton::toggled, this, &MainWindow::updateMiningMode);
    layout->addWidget(card);
    layout->addStretch();
    return scrollPage(page);
}

QTableWidget* MainWindow::deviceTable()
{
    auto* table = new QTableWidget(0, 6);
    table->setObjectName("allDevices");
    table->setAccessibleName("GPU statistics");
    table->setHorizontalHeaderLabels({"Device", "Hashrate", "Temp", "Fan", "Power", "Status"});
    table->verticalHeader()->hide();
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setMinimumSectionSize(table->fontMetrics().horizontalAdvance("Device name"));
    table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->setShowGrid(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setFocusPolicy(Qt::StrongFocus);
    table->setWordWrap(true);
    deviceTables_.append(table);
    return table;
}

QWidget* MainWindow::devicesPage()
{
    auto* page = panel();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 20, 20, 20);
    auto* text = label("Live readings from your active mining session. GPU sensors depend on driver support. "
        "Use Mining setup to select the GPUs for your next session.", "muted");
    text->setWordWrap(true);
    layout->addWidget(text);
    layout->addSpacing(12);
    layout->addWidget(deviceTable(), 1);
    auto* setup = button("Choose GPUs in mining setup", "link");
    connect(setup, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(1); });
    layout->addWidget(setup, 0, Qt::AlignLeft);
    return scrollPage(page);
}

QWidget* MainWindow::activityPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* toolbar = new QHBoxLayout;
    auto* description = label("Session activity · most recent 2,000 lines", "muted");
    description->setWordWrap(true);
    toolbar->addWidget(description, 1);
    auto* exportLog = button("Save log");
    auto* clear = button("Clear");
    toolbar->addWidget(exportLog);
    toolbar->addWidget(clear);
    layout->addLayout(toolbar);
    log_ = new QPlainTextEdit;
    log_->setObjectName("activityLog");
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(2000);
    log_->setFont(monoFont(12));
    layout->addWidget(log_, 1);
    connect(clear, &QPushButton::clicked, log_, &QPlainTextEdit::clear);
    connect(exportLog, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getSaveFileName(this, "Save activity log", "firominer.log", "Log files (*.log);;All files (*)");
        if (path.isEmpty())
            return;
        QFile file(path);
        const auto bytes = log_->toPlainText().toUtf8();
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
            showFailure("Could not save the activity log: " + file.errorString());
        else
            statusBar()->showMessage("Activity log saved", 4000);
    });
    return scrollPage(page);
}

MiningConfig MainWindow::configuration() const
{
    return {executable_, poolInput_->text().trimmed(), walletInput_->text().trimmed(),
        workerInput_->text().trimmed(), passwordInput_->text().isEmpty() ? QStringLiteral("x") : passwordInput_->text(),
        backendInput_->currentData().toString(),
        backendInput_->currentData().toString() == "auto" ? QString() : devicesInput_->text().trimmed(),
        soloMode_->isChecked(), nodeInput_->text().trimmed(), rpcUserInput_->text(),
        rpcPasswordInput_->text(), rewardInput_->text().trimmed(), coinbaseMessageInput_->text()};
}

void MainWindow::loadSettings()
{
    QSettings settings;
    appearance_ = settings.value("appearance/theme", "light").toString();
    if (appearance_ != "light" && appearance_ != "dark" && appearance_ != "system")
        appearance_ = "light";
    updateTheme();
#ifdef Q_OS_WIN
    const auto minerName = QStringLiteral("firominer.exe");
#else
    const auto minerName = QStringLiteral("firominer");
#endif
    executable_ = settings.value("miner/executable", QDir(QCoreApplication::applicationDirPath()).filePath(minerName)).toString();
    poolInput_->setText(settings.value("pool/endpoint").toString());
    walletInput_->setText(settings.value("pool/wallet").toString());
    workerInput_->setText(settings.value("pool/worker", QHostInfo::localHostName().section('.', 0, 0)).toString());
    nodeInput_->setText(settings.value("solo/endpoint", "http://127.0.0.1:8888").toString());
    rpcUserInput_->setText(settings.value("solo/username", "miner").toString());
    rewardInput_->setText(settings.value("solo/rewardAddress").toString());
    coinbaseMessageInput_->setText(settings.value("solo/coinbaseMessage").toString());
    backendInput_->setCurrentIndex(std::max(0, backendInput_->findData(settings.value("miner/backend", "auto").toString())));
    devicesInput_->setText(settings.value("miner/devices").toString());
    devicesInput_->setEnabled(backendInput_->currentData().toString() != "auto");
    devicesRow_->setVisible(backendInput_->currentData().toString() != "auto");
    lastSession_.runtime = settings.value("session/runtime", 0).toLongLong();
    lastSession_.accepted = settings.value("session/accepted", 0).toLongLong();
    lastSession_.rejected = settings.value("session/rejected", 0).toLongLong();
    lastSession_.average = settings.value("session/average", 0).toDouble();
    lastSession_.ended = settings.value("session/ended").toDateTime();
    lastSession_.solo = settings.value("session/solo", false).toBool();
    hasSession_ = lastSession_.runtime > 0 && lastSession_.ended.isValid();
    soloMode_->setChecked(settings.value("mining/solo", false).toBool());
    poolMode_->setChecked(!soloMode_->isChecked());
    restoreGeometry(settings.value("window/geometry").toByteArray());
    updateMiningMode();
}

bool MainWindow::needsRpcPassword() const
{
    auto config = configuration();
    if (!config.solo || !config.rpcPassword.isEmpty())
        return false;
    config.rpcPassword = QStringLiteral("x");
    return MinerController::validate(config).isEmpty();
}

bool MainWindow::saveSettings()
{
    for (auto* input : {poolInput_, nodeInput_})
    {
        const QUrl endpoint(input->text().trimmed());
        if (!endpoint.userInfo().isEmpty() || endpoint.authority().contains('@') || endpoint.hasQuery() || endpoint.hasFragment())
        {
            (input == nodeInput_ ? soloMode_ : poolMode_)->setChecked(true);
            navigation_->setCurrentRow(1);
            input->setFocus();
            showFailure("Keep credentials out of endpoint URLs. Use the separate login fields and remove any query or fragment.");
            return false;
        }
    }
    QSettings settings;
    settings.setValue("appearance/theme", appearance_);
    settings.setValue("miner/executable", executable_);
    settings.setValue("pool/endpoint", poolInput_->text().trimmed());
    settings.setValue("pool/wallet", walletInput_->text().trimmed());
    settings.setValue("pool/worker", workerInput_->text().trimmed());
    settings.setValue("mining/solo", soloMode_->isChecked());
    settings.setValue("solo/endpoint", nodeInput_->text().trimmed());
    settings.setValue("solo/username", rpcUserInput_->text());
    settings.setValue("solo/rewardAddress", rewardInput_->text().trimmed());
    settings.setValue("solo/coinbaseMessage", coinbaseMessageInput_->text());
    settings.setValue("miner/backend", backendInput_->currentData());
    settings.setValue("miner/devices", devicesInput_->text().trimmed());
    settings.setValue("window/geometry", saveGeometry());
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        showFailure("Could not save settings. Check that your user settings folder is writable.");
        return false;
    }
    return true;
}

void MainWindow::recordSession()
{
    // A session that never reported its runtime keeps the previous one, as saved.
    if (runtimeSeconds_ <= 0)
        return;
    lastSession_.runtime = runtimeSeconds_;
    lastSession_.accepted = acceptedCount_;
    lastSession_.rejected = rejectedCount_;
    lastSession_.average = rateSamples_ ? rateSum_ / rateSamples_ : 0;
    lastSession_.ended = QDateTime::currentDateTime();
    lastSession_.solo = soloMode_->isChecked();
    hasSession_ = true;
    QSettings settings;
    settings.setValue("session/runtime", lastSession_.runtime);
    settings.setValue("session/accepted", lastSession_.accepted);
    settings.setValue("session/rejected", lastSession_.rejected);
    settings.setValue("session/average", lastSession_.average);
    settings.setValue("session/ended", lastSession_.ended);
    settings.setValue("session/solo", lastSession_.solo);
}

void MainWindow::updateConnectionSummary()
{
    const bool solo = soloMode_->isChecked();
    const QUrl endpoint((solo ? nodeInput_ : poolInput_)->text().trimmed());
    const auto port = endpoint.port();
    pool_->setText(endpoint.host().isEmpty() ? "Not configured" : endpoint.host() + (port > 0 ? ":" + QString::number(port) : QString()));
    worker_->setText(workerInput_->text().trimmed().isEmpty() ? "Default" : workerInput_->text().trimmed());
    const auto address = (solo ? rewardInput_ : walletInput_)->text().trimmed();
    wallet_->setText(address.isEmpty() ? "Not configured" : abbreviated(address));
    wallet_->setToolTip(address);
    copyAddress_->setEnabled(!address.isEmpty());
}

void MainWindow::updateMiningMode()
{
    const bool solo = soloMode_->isChecked();
    poolFields_->setVisible(!solo);
    soloFields_->setVisible(solo);
    nodeGuideButton_->setVisible(solo);
    if (!solo)
        nodeGuideButton_->setChecked(false);
    setupHeading_->setText(solo ? "Connect to your Firo node" : "Connect to a mining pool");
    setupIntro_->setText(solo ? "Enable RPC in firo.conf, restart Firo Core and let it finish syncing." :
        "Enter your pool endpoint and payout account. A pool's SOLO endpoint also belongs here.");
    acceptedLabel_->setText(solo ? "Blocks accepted" : "Accepted shares");
    acceptedLabel_->setToolTip(solo ? "Blocks accepted by your node this session. Rewards still need confirmations before they can be spent." : "");
    lastShareLabel_->setText(solo ? "Last block" : "Last share");
    connectionTitle_->setText(solo ? "Node" : "Pool");
    endpointLabel_->setText(solo ? "Node" : "Pool");
    rewardLabel_->setText(solo ? "Reward address" : "Payout address");
    copyAddress_->setToolTip(solo ? "Copy reward address" : "Copy payout address");
    copyAddress_->setAccessibleName(copyAddress_->toolTip());
    workerLabel_->setVisible(!solo);
    worker_->setVisible(!solo);
    if (currentState_ == "Stopped")
        clearReadings();
    updateConnectionSummary();
    updateOverview();
}

void MainWindow::toggleMining()
{
    if (controller_.isRunning())
    {
        controller_.stop();
        return;
    }
    notice_->hide();
    const auto config = configuration();
    const auto error = MinerController::validate(config);
    if (!error.isEmpty())
    {
        showNotice(error, "warning");
        appendActivity(error);
        const QFileInfo miner(config.executable);
        if (!miner.isFile() || !miner.isExecutable())
        {
            // The miner's path lives in Settings, not Mining setup.
            showSettings();
            return;
        }
        navigation_->setCurrentRow(1);
        if (needsRpcPassword())
            rpcPasswordInput_->setFocus();
        return;
    }
    if (!saveSettings())
        return;
    updateConnectionSummary();
    clearReadings();
    controller_.start(config);
}

void MainWindow::clearReadings()
{
    hasReadings_ = connectionLost_ = false;
    acceptedCount_ = rejectedCount_ = runtimeSeconds_ = rateSamples_ = 0;
    rateSum_ = 0;
    activeGpus_ = totalGpus_ = 0;
    const bool solo = soloMode_->isChecked();
    setValue(hashrate_, "0.0");
    accepted_->setText("0");
    shareDetail_->setText(solo ? "This session · 0 rejected · 0 failed" : "0 rejected · 0 failed");
    setValue(lastShare_, dash());
    lastShareDetail_->setText(solo ? "No block found this session" : "Waiting for the first share");
    setValue(power_, dash());
    powerDetail_->setText("Waiting for statistics");
    gpuGrid_->setCount(0);
    for (auto* table : deviceTables_)
    {
        table->clearSpans();
        table->clearContents();
        table->setRowCount(1);
        table->setSpan(0, 0, 1, 6);
        table->setItem(0, 0, new QTableWidgetItem("GPU details appear when mining starts"));
    }
    updateOverview();
}

void MainWindow::setMiningState(const QString& state)
{
    if (state == "Stopped" && currentState_ != "Stopped" && hasReadings_)
        recordSession();
    currentState_ = state;
    const bool running = state != "Stopped";
    for (auto* field : {poolInput_, walletInput_, workerInput_, passwordInput_, nodeInput_, rpcUserInput_, rpcPasswordInput_, rewardInput_, coinbaseMessageInput_})
        field->setEnabled(!running);
    backendInput_->setEnabled(!running);
    poolMode_->setEnabled(!running);
    soloMode_->setEnabled(!running);
    testNode_->setEnabled(!running);
    saveSetup_->setEnabled(!running);
    devicesInput_->setEnabled(!running && backendInput_->currentData().toString() != "auto");
    if (state == "Stopped")
        clearReadings();
    else if (state == "Checking node")
    {
        navigation_->setCurrentRow(1);
        nodeStatus_->setText(QString::fromUtf8("Checking node, sync and reward address…"));
    }
    else if (state == "Reconnecting")
    {
        setValue(hashrate_, dash());
        setValue(power_, dash());
        setValue(lastShare_, dash());
        powerDetail_->setText("Waiting for statistics");
        lastShareDetail_->setText(soloMode_->isChecked() ? "Waiting for fresh block statistics" : "Waiting for fresh share statistics");
        for (int i = 0; i < gpuGrid_->count(); ++i)
            gpuGrid_->card(i)->setWaiting();
        for (auto* table : deviceTables_)
            if (table->columnSpan(0, 0) == 1)
                for (int row = 0; row < table->rowCount(); ++row)
                    for (int col = 1; col < 6; ++col)
                        if (auto* item = table->item(row, col))
                            item->setText(col == 5 ? "Waiting for statistics" : "Unavailable");
    }
    else if (state == "Stopping")
    {
        for (int i = 0; i < gpuGrid_->count(); ++i)
            gpuGrid_->card(i)->setStopping();
        for (auto* table : deviceTables_)
            if (table->columnSpan(0, 0) == 1)
                for (int row = 0; row < table->rowCount(); ++row)
                    if (auto* item = table->item(row, 5))
                        item->setText("Stopping");
    }
    if (tray_)
        tray_->setToolTip("Firominer - " + state.toLower());
    if (state == "Starting")
        navigation_->setCurrentRow(0);
    updateOverview();
}

void MainWindow::updateOverview()
{
    if (!gpuGrid_)
        return;
    const bool solo = soloMode_->isChecked();
    const auto& state = currentState_;
    const bool stopped = state == "Stopped";
    const bool checking = state == "Checking node";
    // Fresh statistics with the pool or node connected, whether the GPUs are mining, paused or preparing work.
    const bool connected = hasReadings_ && (state == "Mining" || state == "Paused" || state == "Preparing GPUs");
    const QString setupError = stopped ? MinerController::validate(configuration()) : QString();
    const bool ready = stopped && setupError.isEmpty();
    const bool needsPassword = stopped && !ready && needsRpcPassword();

    state_->setStatus(state, state == "Mining" ? Tone::Positive : stopped || state == "Stopping" ? Tone::Neutral : Tone::Warning);
    auto setStart = [this](const QString& text, const char* role, const char* glyph) {
        start_->setText(text);
        const bool changed = start_->property("role").toString() != role || start_->property("glyph").toString() != glyph;
        start_->setProperty("glyph", glyph);
        setRole(start_, role);
        if (changed)
            refreshIcons();
    };
    if (checking)
        setStart("Cancel check", "secondary", "stop");
    else if (!stopped)
        setStart("Stop mining", "secondary", "stop");
    else if (ready)
        setStart(solo ? "Start solo mining" : "Start pool mining", "primary", "play");
    else if (needsPassword)
        setStart("Enter RPC password", "primary", "setup");
    else
        setStart("Set up mining", "primary", "setup");
    start_->setEnabled(state != "Stopping");
    runtime_->setText(stopped ? (ready ? "Ready when you are" : needsPassword ? "RPC password needed" : "Not set up yet") :
        checking ? "Checking solo setup" :
        connected ? "Running " + durationText(runtimeSeconds_) :
        state == "Reconnecting" ? "Waiting for statistics" :
        state == "Stopping" ? QString::fromUtf8("Stopping…") : QString::fromUtf8("Starting up…"));

    const bool live = !stopped && !checking;
    hashrateRow_->setVisible(live);
    heroTitle_->setVisible(!live);
    liveStats_->setVisible(live);
    sessionStats_->setVisible(stopped && hasSession_);
    if (stopped)
    {
        const auto endpointText = (solo ? nodeInput_ : poolInput_)->text().trimmed();
        const QUrl endpoint(endpointText);
        const auto address = (solo ? rewardInput_ : walletInput_)->text().trimmed();
        // A fresh setup gets directions instead of its first validation error.
        const bool untouched = address.isEmpty() && (endpointText.isEmpty() || (solo && endpointText == MiningConfig().nodeUrl));
        const bool configured = ready || needsPassword;
        heroCaption_->setText(configured ? "Ready to mine" : "Get started");
        heroTitle_->setText(configured ? (solo ? "Solo mining" : "Pool mining") : "Set up mining");
        const auto destination = QString("%1:%2 · %3 to %4").arg(endpoint.host()).arg(endpoint.port())
            .arg(solo ? "rewards" : "payouts", abbreviated(address));
        gpuCount_->setText(ready ? destination :
            needsPassword ? destination + ". Enter your RPC password to start; it is never saved." :
            untouched ? (solo ? "Connect your Firo node and reward address in Mining setup, then start mining." :
                "Add your pool and payout address in Mining setup, then start mining.") : setupError);
    }
    else if (checking)
    {
        heroCaption_->setText("Solo mining");
        heroTitle_->setText("Checking your node");
        gpuCount_->setText(QString::fromUtf8("Checking the node, its sync and your reward address…"));
    }
    else
    {
        heroCaption_->setText("Total hashrate");
        if (state == "Reconnecting")
            gpuCount_->setText(connectionLost_ ?
                QString("%1 connection lost. Retrying automatically; your GPUs stay ready.").arg(solo ? "Node" : "Pool") :
                QString("Waiting for the miner's statistics. Retrying automatically."));
        else if (state == "Stopping")
            gpuCount_->setText(QString::fromUtf8("Stopping the miner…"));
        else if (!hasReadings_ || !totalGpus_)
            gpuCount_->setText("Preparing your GPUs. Hashrate appears in a moment.");
        else
        {
            auto text = QString(totalGpus_ == 1 ? "%1 of %2 GPU mining" : "%1 of %2 GPUs mining").arg(activeGpus_).arg(totalGpus_);
            if (runtimeSeconds_ >= 300 && rateSamples_)
                text += QString(" · session average %1 MH/s").arg(rateSum_ / rateSamples_, 0, 'f', 1);
            gpuCount_->setText(text);
        }
    }
    if (stopped && hasSession_)
    {
        sessionRuntime_->setText(durationText(lastSession_.runtime));
        sessionEnded_->setText(sessionEndText(lastSession_.ended));
        sessionAcceptedLabel_->setText(lastSession_.solo ? "Blocks" : "Accepted");
        sessionAccepted_->setText(QLocale().toString(lastSession_.accepted));
        sessionRejected_->setText(QString("%1 rejected").arg(QLocale().toString(lastSession_.rejected)));
        setValue(sessionAverage_, lastSession_.average > 0 ? QString::number(lastSession_.average, 'f', 1) : dash());
    }

    chart_->setDimmed(!hasReadings_);
    chartSubtitle_->setText(hasReadings_ ? "MH/s · this session" : chart_->isEmpty() ? "MH/s · appears when mining starts" : "MH/s · last session");

    if (connected)
        poolState_->setStatus("Connected", Tone::Positive);
    else if (state == "Reconnecting")
        poolState_->setStatus("Reconnecting", Tone::Warning);
    else if (state == "Stopping")
        poolState_->setStatus("Disconnecting", Tone::Neutral);
    else if (!stopped)
        poolState_->setStatus("Connecting", Tone::Warning);
    else
        poolState_->setStatus("Not connected", Tone::Neutral);

    gpuSummary_->setText(state == "Stopping" ? QString() : gpuGrid_->count() && state != "Reconnecting" ?
        QString::fromUtf8("· %1 of %2 mining").arg(activeGpus_).arg(totalGpus_) :
        live ? QString::fromUtf8("· waiting for statistics") : QString());
    gpuGrid_->setVisible(gpuGrid_->count() > 0);
    gpuEmpty_->setVisible(gpuGrid_->count() == 0);
}

void MainWindow::updateStatistics(const QJsonObject& statistics)
{
    const auto mining = statistics.value("mining").toObject();
    const auto shares = mining.value("shares").toArray();
    const auto devices = statistics.value("devices").toArray();
    const bool connected = statistics.value("connection").toObject().value("connected").toBool();
    const bool solo = soloMode_->isChecked();
    acceptedCount_ = shares.at(0).toInteger();
    rejectedCount_ = shares.at(1).toInteger();
    const auto failed = shares.at(2).toInteger();
    connectionLost_ = !connected;
    const qint64 lastSubmission = acceptedCount_ + rejectedCount_ + failed > 0 ? shares.at(3).toInteger() : -1;
    accepted_->setText(QLocale().toString(acceptedCount_));
    shareDetail_->setText((solo ? "This session · " : QString()) + QString("%1 rejected · %2 failed").arg(rejectedCount_).arg(failed));
    if (!connected)
    {
        // A pool disconnect can leave the API's previous hashrate and sensors populated.
        setMiningState("Reconnecting");
        return;
    }
    // The chart keeps the last session, dimmed, until this one's first reading replaces it.
    if (!hasReadings_)
        chart_->reset();
    hasReadings_ = true;
    runtimeSeconds_ = statistics.value("host").toObject().value("runtime").toInteger();
    const auto rate = hashValue(mining.value("hashrate"));
    setValue(hashrate_, QString::number(rate, 'f', 1));
    chart_->add(rate);
    // The chart keeps only six hours, so the session average sums every hashing reading itself.
    if (rate > 0)
    {
        rateSum_ += rate;
        ++rateSamples_;
    }
    setValue(lastShare_, lastSubmission >= 0 ? durationText(lastSubmission) + " ago" :
        solo ? QStringLiteral("None yet") : dash());
    if (solo)
        lastShareDetail_->setText(acceptedCount_ > 0 ? QString("%1 found this session").arg(acceptedCount_ == 1 ? "1 block" : QLocale().toString(acceptedCount_) + " blocks") :
            lastSubmission >= 0 ? QStringLiteral("Not accepted by your node") :
            currentState_ == "Mining" ? "Mining normally · no block found yet" : "No block found this session");
    else
        lastShareDetail_->setText(acceptedCount_ > 0 && runtimeSeconds_ > 0 ?
            "About 1 every " + durationText(std::max<qint64>(1, runtimeSeconds_ / acceptedCount_)) : "Waiting for the first share");
    int active = 0;
    double totalPower = 0;
    int powerReadings = 0;
    for (auto* table : deviceTables_)
    {
        table->clearSpans();
        table->setRowCount(devices.size());
    }
    gpuGrid_->setCount(devices.size());
    for (int row = 0; row < devices.size(); ++row)
    {
        const auto device = devices[row].toObject();
        const auto hardware = device.value("hardware").toObject();
        const auto sensors = hardware.value("sensors").toArray();
        const auto info = device.value("mining").toObject();
        const bool paused = info.value("paused").toBool();
        const auto deviceRate = hashValue(info.value("hashrate"));
        if (!paused && deviceRate > 0)
            ++active;
        if (hasReading(sensors.at(2), false))
        { totalPower += sensors.at(2).toDouble(); ++powerReadings; }
        const QString status = paused ? "Paused" : deviceRate > 0 ? "Mining" : "Preparing";
        const auto name = hardware.value("name").toString();
        const auto meta = "GPU " + QString::number(device.value("_index").toInt()) + " · " + device.value("_mode").toString();
        const auto temperature = sensor(sensors.at(0), "°C");
        const auto fan = sensor(sensors.at(1), "%", sensors.at(0).toDouble() > 0);
        const auto power = sensor(sensors.at(2), " W");
        auto reported = [](const QString& text) { return text.isEmpty() ? QStringLiteral("Unavailable") : text; };
        const QStringList values{name + "\n" + meta, QString::number(deviceRate, 'f', 1) + " MH/s", reported(temperature),
            reported(fan), reported(power), status};
        for (auto* table : deviceTables_)
        {
            for (int col = 0; col < values.size(); ++col)
            {
                auto* item = new QTableWidgetItem(values[col]);
                if (col == 5)
                {
                    item->setToolTip(info.value("pause_reason").toString());
                }
                else if (col == 0)
                    item->setToolTip(name + "\nPCI: " + hardware.value("pci").toString());
                table->setItem(row, col, item);
            }
        }
        GpuReading reading;
        reading.name = name;
        reading.meta = meta;
        reading.rate = QString::number(deviceRate, 'f', 1);
        reading.status = status;
        reading.statusTip = info.value("pause_reason").toString();
        reading.tone = deviceRate > 0 && !paused ? Tone::Positive : Tone::Warning;
        reading.temperature = temperature;
        reading.fan = fan;
        reading.power = power;
        const auto deviceShares = info.value("shares").toArray();
        if (!deviceShares.isEmpty())
        {
            const auto count = deviceShares.at(0).toInteger();
            reading.shares = QLocale().toString(count) + (solo ? (count == 1 ? " block" : " blocks") : (count == 1 ? " share" : " shares"));
        }
        auto* card = gpuGrid_->card(row);
        card->setReading(reading);
        if (!paused)
            card->addRate(deviceRate);
    }
    gpuGrid_->refreshLayout();
    activeGpus_ = active;
    totalGpus_ = devices.size();
    setValue(power_, powerReadings ? QString::number(totalPower, 'f', 0) + " W" : dash());
    powerDetail_->setText(!powerReadings ? QStringLiteral("Not reported by your GPUs") :
        powerReadings != devices.size() ? QString("Partial · %1 of %2 devices").arg(powerReadings).arg(devices.size()) :
        rate > 0 ? QString("%1 MH/J").arg(rate / totalPower, 0, 'f', 2) : QStringLiteral("Reported by your GPUs"));
    updateOverview();
}

void MainWindow::appendActivity(const QString& line)
{
    log_->appendPlainText(QTime::currentTime().toString("HH:mm:ss") + "  " + line);
}

void MainWindow::showNotice(const QString& message, const char* tone)
{
    notice_->setText(message);
    notice_->setProperty("tone", tone);
    repolish(notice_);
    notice_->show();
}

void MainWindow::showFailure(const QString& message)
{
    showNotice(message, "danger");
    appendActivity(message);
    if (!isVisible())
    { showNormal(); raise(); }
}

void MainWindow::showSettings()
{
    QDialog dialog(this);
    dialog.setObjectName("settingsDialog");
    dialog.setPalette(palette());
    dialog.setFont(font());
    dialog.setWindowTitle("Firominer settings");
    dialog.resize(650, 320);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(16);
    auto* appearance = new QFormLayout;
    auto* theme = new QComboBox;
    theme->setObjectName("themeInput");
    theme->setAccessibleName("Color theme");
    theme->addItem("Light", "light");
    theme->addItem("Dark", "dark");
    theme->addItem("System", "system");
    theme->setCurrentIndex(theme->findData(appearance_));
    appearance->addRow("Color theme", theme);
    layout->addLayout(appearance);
    auto* themeNote = label("System follows your computer's colors. High-contrast settings always take priority.", "muted");
    themeNote->setWordWrap(true);
    layout->addWidget(themeNote);
    layout->addWidget(label("Miner executable", "section"));
    auto* row = new QHBoxLayout;
    auto* path = new QLineEdit(executable_);
    path->setObjectName("minerExecutableInput");
    path->setAccessibleName("Miner executable path");
    auto* browse = button(QString::fromUtf8("Browse…"));
    row->addWidget(path, 1);
    row->addWidget(browse);
    layout->addLayout(row);
    auto* note = label("Use the firominer executable included in your downloaded package. "
        "Keep it with its companion libraries. Changes apply to the next session.", "muted");
    note->setWordWrap(true);
    layout->addWidget(note);
    connect(browse, &QPushButton::clicked, &dialog, [path, &dialog] {
        const auto selected = QFileDialog::getOpenFileName(&dialog, "Choose firominer", path->text());
        if (!selected.isEmpty())
            path->setText(selected);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    if (auto* save = buttons->button(QDialogButtonBox::Save))
        save->setProperty("role", "primary");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QFileInfo file(path->text().trimmed());
        if (path->text().trimmed() != executable_ && (!file.isFile() || !file.isExecutable()))
        {
            QMessageBox::warning(&dialog, "Miner not found", "Select a valid firominer executable.");
            return;
        }
        const auto previous = executable_;
        const auto previousTheme = appearance_;
        executable_ = file.absoluteFilePath();
        appearance_ = theme->currentData().toString();
        if (saveSettings())
        {
            updateTheme();
            updateOverview();
            dialog.accept();
        }
        else
        {
            executable_ = previous;
            appearance_ = previousTheme;
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::setSidebarCompact(bool compact)
{
    if (compact == sidebarCompact_)
        return;
    sidebarCompact_ = compact;
    sidebar_->setFixedWidth(compact ? 72 : 220);
    brandName_->setVisible(!compact);
    sidebarFooter_->setVisible(!compact);
    for (int i = 0; i < navigation_->count(); ++i)
    {
        navigation_->item(i)->setText(compact ? QString() : navigationNames_[i]);
        navigation_->item(i)->setToolTip(compact ? navigationNames_[i] : QString());
    }
    const QList<std::pair<QPushButton*, QString>> buttons{{settingsButton_, "Settings"}, {helpButton_, "Help"}};
    for (const auto& [item, name] : buttons)
    {
        item->setText(compact ? QString() : name);
        item->setToolTip(compact ? name : QString());
        item->setAccessibleName(name);
        item->setProperty("compact", compact);
        repolish(item);
    }
    navigation_->setProperty("compact", compact);
    repolish(navigation_);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    const auto direction = width() < 1000 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    heroLayout_->setDirection(direction);
    middleLayout_->setDirection(direction);
    setSidebarCompact(width() < 900);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (currentState_ == "Checking node")
        controller_.stop();
    if (!controller_.isRunning())
    {
        if (!saveSettings())
        {
            const auto choice = QMessageBox::warning(this, "Settings were not saved",
                "Your changes could not be saved. Stay in Firominer to correct them, or discard them and quit.",
                QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
            if (choice != QMessageBox::Discard)
            {
                closing_ = false;
                event->ignore();
                return;
            }
        }
        event->accept();
        return;
    }
    event->ignore();
    if (closing_)
        return;
    QMessageBox prompt(QMessageBox::Question, "Mining is running", "What should Firominer do?", QMessageBox::NoButton, this);
    auto* stop = prompt.addButton("Stop mining and quit", QMessageBox::DestructiveRole);
    QPushButton* keep = nullptr;
    if (QSystemTrayIcon::isSystemTrayAvailable())
        keep = prompt.addButton("Keep mining in tray", QMessageBox::AcceptRole);
    auto* cancel = prompt.addButton(QMessageBox::Cancel);
    prompt.setDefaultButton(cancel);
    prompt.setEscapeButton(cancel);
    prompt.exec();
    if (prompt.clickedButton() == stop)
    {
        closing_ = true;
        if (controller_.isRunning())
            controller_.stop();
        else
            QTimer::singleShot(0, this, &QWidget::close);
    }
    else if (keep && prompt.clickedButton() == keep)
        hide();
}
