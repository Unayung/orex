// Drives one TerminalItem offscreen: restores a recorded Rex stream, saves
// what it draws as PNGs, and prints the bytes that key, paste, IME, focus,
// mouse and wheel events produce, so the item can be checked without Rex.
//
//   QT_QPA_PLATFORM=offscreen ./term_harness stream.bin outdir [colors.toml]
//
// colors.toml defaults to the current Omarchy theme. The exit status is 0
// only when every check passes.

#include "terminalitem.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include <cstdio>
#include <fstream>
#include <string>

namespace {

// The recorded stream's snapshot is this long; the rest is live output.
constexpr int kSnapshotBytes = 1453;

QByteArray captured;
int failures = 0;

void check(const char *label, const QByteArray &expected)
{
    const bool ok = captured == expected;
    failures += ok ? 0 : 1;
    std::printf("%-28s %-28s %s\n", label, captured.toHex().constData(), ok ? "ok" : "MISMATCH");
    captured.clear();
}

void key(TerminalItem &item, int k, Qt::KeyboardModifiers mods, const QString &text)
{
    QKeyEvent press(QEvent::KeyPress, k, mods, text);
    QCoreApplication::sendEvent(&item, &press);
    QKeyEvent release(QEvent::KeyRelease, k, mods, text);
    QCoreApplication::sendEvent(&item, &release);
}

void expect(const char *label, bool ok, const QString &detail = QString())
{
    failures += ok ? 0 : 1;
    std::printf("%-44s %s %s\n", label, ok ? "ok" : "FAILED", qPrintable(detail));
}

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

long rssKb()
{
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0)
            return std::stol(line.substr(6));
    }
    return -1;
}

// The item's cell size, worked out the way TerminalItem does it, so that
// events can be aimed at a cell.
QSizeF cellSize()
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(12);
    const QFontMetricsF fm(font);
    return QSizeF(qCeil(fm.horizontalAdvance(QLatin1Char('M'))), qCeil(fm.height()));
}

QPointF cellCenter(int col, int row)
{
    const QSizeF cell = cellSize();
    return QPointF((col + 0.5) * cell.width(), (row + 0.5) * cell.height());
}

// Near the right edge of a cell. A drag takes in its last cell only once
// the pointer passes the middle of it, as in Ghostty.
QPointF cellEnd(int col, int row)
{
    const QSizeF cell = cellSize();
    return QPointF((col + 0.9) * cell.width(), (row + 0.5) * cell.height());
}

// Mouse and wheel events go through the window, as real ones do, so that
// Qt Quick's grab and delivery rules apply. Each event gets its own time so
// that separate gestures are never taken for a double click.
qint64 clockMs = 100000;

void mouse(QQuickWindow &window, QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons,
           QPointF pos, Qt::KeyboardModifiers mods = Qt::NoModifier, qint64 step = 1000)
{
    clockMs += step;
    QMouseEvent event(type, pos, window.mapToGlobal(pos), button, buttons, mods);
    event.setTimestamp(quint64(clockMs));
    QCoreApplication::sendEvent(&window, &event);
}

void drag(QQuickWindow &window, QPointF from, QPointF to, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    mouse(window, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, from, mods);
    mouse(window, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton, (from + to) / 2, mods, 10);
    mouse(window, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton, to, mods, 10);
    mouse(window, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, to, mods, 10);
}

void clicks(QQuickWindow &window, QPointF pos, int count)
{
    for (int i = 0; i < count; ++i) {
        mouse(window, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, pos, Qt::NoModifier,
              i == 0 ? 1000 : 50);
        mouse(window, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, pos, Qt::NoModifier, 10);
    }
}

void wheel(QQuickWindow &window, QPointF pos, int angle, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QWheelEvent event(pos, window.mapToGlobal(pos), QPoint(), QPoint(0, angle), Qt::NoButton, mods,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&window, &event);
}

// The offscreen platform deactivates its window about a second after
// activating it, which a compositor doesn't do. Swallowing the window's
// focus-out keeps the item focused for checks that wait that long.
class KeepFocus : public QObject {
public:
    bool eventFilter(QObject *, QEvent *event) override
    {
        return event->type() == QEvent::FocusOut;
    }
};

QImage frame(QQuickWindow &window)
{
    QCoreApplication::processEvents();
    return window.grabWindow();
}

// What Ctrl+Shift+C puts on the clipboard. A sentinel shows when nothing was
// copied because nothing was selected.
QString copied(TerminalItem &item)
{
    QGuiApplication::clipboard()->setText(QStringLiteral("<nothing selected>"));
    key(item, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("\x03"));
    captured.clear();
    return QGuiApplication::clipboard()->text();
}

// Reads an Omarchy colors.toml into Rex's theme message.
QJsonObject themeFromToml(const QString &path, QColor *selection)
{
    QFile file(path);
    QJsonObject theme;
    if (!file.open(QIODevice::ReadOnly))
        return theme;
    QJsonArray palette;
    static const QRegularExpression line(QStringLiteral("^(\\w+)\\s*=\\s*\"#?([0-9a-fA-F]{6})\""));
    for (const QString &text : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = line.match(text.trimmed());
        if (!m.hasMatch())
            continue;
        const QString key = m.captured(1);
        const QString hex = m.captured(2);
        if (key == QLatin1String("foreground") || key == QLatin1String("background")
            || key == QLatin1String("cursor"))
            theme.insert(key, QLatin1Char('#') + hex);
        else if (key.startsWith(QLatin1String("color")))
            palette.append(key.mid(5) + QLatin1Char('=') + hex);
        else if (key == QLatin1String("selection"))
            *selection = QColor(QLatin1Char('#') + hex);
    }
    theme.insert(QStringLiteral("palette"), palette);
    return theme;
}

bool grab(QQuickWindow &window, const QString &path)
{
    QCoreApplication::processEvents();
    const bool ok = window.grabWindow().save(path);
    std::printf("saved %s: %s\n", qPrintable(path), ok ? "yes" : "FAILED");
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s stream.bin outdir\n", argv[0]);
        return 2;
    }
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    const QByteArray stream = file.readAll();
    const QString outDir = QString::fromLocal8Bit(argv[2]);

    QQuickWindow window;
    window.resize(900, 500);
    TerminalItem item(window.contentItem());
    QObject::connect(&item, &TerminalItem::input, [](const QByteArray &b) { captured += b; });
    QObject::connect(&item, &TerminalItem::gridSizeChanged, [](int c, int r) {
        std::printf("gridSizeChanged %d x %d\n", c, r);
    });
    item.setSize(QSizeF(900, 500));
    item.setSize(QSizeF(901, 501)); // Fits the same grid, so no second signal.
    item.setSize(QSizeF(900, 500));
    window.show();

    // Output before any snapshot lands in the fresh 80x24 terminal; the
    // restore below must replace that terminal and free it.
    item.feed("before the snapshot\r\n");
    if (!item.restoreSnapshot(stream.left(kSnapshotBytes))) {
        std::fprintf(stderr, "restoreSnapshot failed\n");
        return 1;
    }
    std::printf("restoreSnapshot: ok (second restore: %s)\n",
                item.restoreSnapshot(stream.left(kSnapshotBytes)) ? "ok" : "FAILED");
    std::printf("restoreSnapshot of garbage: %s\n",
                item.restoreSnapshot("GHOSTSNPnot really") ? "ACCEPTED" : "rejected");
    item.feed(stream.mid(kSnapshotBytes));
    item.feed("\x1b[1;32mgreen bold\x1b[0m 中文寬字 \x1b[7minverse\x1b[0m\r\n");
    item.feed("\x1b[4munderline\x1b[0m \x1b[3mitalic\x1b[0m \x1b[9mstrike\x1b[0m "
              "\x1b[38;5;208m256-orange\x1b[0m \x1b[48;2;40;60;160m truecolor bg \x1b[0m "
              "\x1b[2mfaint\x1b[0m ─│┼ ❯ 漢");

    bool ok = grab(window, outDir + "/term_unfocused.png");
    window.requestActivate();
    item.forceActiveFocus();
    std::printf("hasActiveFocus: %s\n", item.hasActiveFocus() ? "yes" : "no");
    ok = grab(window, outDir + "/term_focused.png") && ok;
    // A block cursor over a wide character must cover both cells and show
    // the character in the background color.
    item.feed("\r\ncursor on wide: 漢字 end\x1b[6D");
    ok = grab(window, outDir + "/term_cursor_wide.png") && ok;
    captured.clear();

    std::printf("%-28s %-28s\n", "event", "bytes (hex)");
    key(item, Qt::Key_Up, Qt::NoModifier, QString());
    check("Up", "\x1b[A");
    key(item, Qt::Key_C, Qt::ControlModifier, QStringLiteral("\x03"));
    check("Ctrl+C", "\x03");
    key(item, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
    check("Enter", "\r");
    key(item, Qt::Key_A, Qt::NoModifier, QStringLiteral("a"));
    check("a", "a");

    item.feed("\x1b[?1h");
    key(item, Qt::Key_Up, Qt::NoModifier, QString());
    check("Up (app cursor mode)", "\x1bOA");
    item.feed("\x1b[?1l");

    // With the Kitty keyboard protocol's disambiguate flag, Ctrl+C becomes a
    // CSI u sequence, which shows the encoder follows the terminal's modes.
    item.feed("\x1b[>1u");
    key(item, Qt::Key_C, Qt::ControlModifier, QStringLiteral("\x03"));
    check("Ctrl+C (kitty disambiguate)", "\x1b[99;5u");
    item.feed("\x1b[<u");

    QInputMethodEvent ime(QString(), {});
    ime.setCommitString(QStringLiteral("中文"));
    QCoreApplication::sendEvent(&item, &ime);
    check("IME commit 中文", QByteArray("中文"));

    // The restored shell turned bracketed paste on, so it is turned off
    // first to see the unbracketed framing.
    QGuiApplication::clipboard()->setText(QStringLiteral("x\ny"));
    item.feed("\x1b[?2004l");
    key(item, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("\x16"));
    check("Ctrl+Shift+V", "x\ry");
    item.feed("\x1b[?2004h");
    key(item, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier, QStringLiteral("\x16"));
    check("Ctrl+Shift+V (bracketed)", "\x1b[200~x\ny\x1b[201~");

    item.feed("\x1b[?1004h");
    item.setFocus(false);
    check("focus out (mode 1004)", "\x1b[O");
    item.forceActiveFocus();
    check("focus in (mode 1004)", "\x1b[I");

    // ---- Theme, selection, scrollback, mouse, sync output, blink ----

    // The checks below wait on timers, and the cursor drawing depends on
    // focus, so focus must not drop out from under them.
    KeepFocus keepFocus;
    window.installEventFilter(&keepFocus);
    item.forceActiveFocus();

    const QString tomlPath = argc > 3 ? QString::fromLocal8Bit(argv[3])
                                      : QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme/colors.toml");
    QColor selectionColor;
    const QJsonObject theme = themeFromToml(tomlPath, &selectionColor);
    expect("theme read from colors.toml", theme.value(QStringLiteral("background")).toString() == QStringLiteral("#191724"),
           theme.value(QStringLiteral("background")).toString());
    item.setTheme(theme);
    item.setSelectionColor(selectionColor);
    item.feed("\x1b[?2004l\x1b[2J\x1b[H");
    item.feed("theme: \x1b[31mred \x1b[32mgreen \x1b[33myellow \x1b[34mblue \x1b[35mmagenta \x1b[36mcyan "
              "\x1b[90mbright-black\x1b[0m default\r\n");
    item.feed("選取這一行的中文寬字 and ASCII mixed\r\n");
    // 74 cells of ASCII and six wide characters need 86 cells, so the line
    // wraps softly after the third character.
    item.feed("wrap " + QByteArray(69, 'x') + "漢字漢字漢字\r\n");
    item.feed("$ ");
    ok = grab(window, outDir + "/term_theme.png") && ok;

    // Cells 2 through 13 of row 1 run from 取 to 中.
    drag(window, cellCenter(2, 1), cellEnd(13, 1));
    ok = grab(window, outDir + "/term_selection.png") && ok;
    QString text = copied(item);
    expect("copy of drag over wide chars", text == QStringLiteral("取這一行的中"), text);

    drag(window, cellCenter(0, 2), cellEnd(5, 3));
    text = copied(item);
    expect("copy of 2 rows with a soft wrap and CJK",
           text == QStringLiteral("wrap ") + QString(69, QLatin1Char('x')) + QStringLiteral("漢字漢字漢字"),
           QStringLiteral("\"%1\"").arg(text));

    clicks(window, cellCenter(26, 1), 2);
    text = copied(item);
    expect("double click selects a word", text == QStringLiteral("ASCII"), text);
    clicks(window, cellCenter(26, 1), 3);
    text = copied(item);
    expect("triple click selects a line", text == QStringLiteral("選取這一行的中文寬字 and ASCII mixed"), text);

    clicks(window, cellCenter(26, 1), 2);
    item.feed("\x1b[s\x1b[8;1Houtput on another row\x1b[u");
    text = copied(item);
    expect("selection kept by output on other rows", text == QStringLiteral("ASCII"), text);
    item.feed("\x1b[s\x1b[2;26HAPPLE\x1b[u");
    text = copied(item);
    expect("selection cleared when its text changes", text == QStringLiteral("<nothing selected>"), text);
    clicks(window, cellCenter(26, 1), 2);
    key(item, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    captured.clear();
    text = copied(item);
    expect("selection cleared by a key", text == QStringLiteral("<nothing selected>"), text);

    // Scrollback: enough lines to push the screen into history, then the
    // wheel moves the view up and a key brings it back.
    for (int i = 1; i <= 60; ++i)
        item.feed(QStringLiteral("\r\nscrollback line %1").arg(i, 3, 10, QLatin1Char('0')).toUtf8());
    const QImage bottom = frame(window);
    wheel(window, cellCenter(10, 10), 2 * 120);
    ok = grab(window, outDir + "/term_scrollback.png") && ok;
    const QImage scrolled = frame(window);
    expect("wheel scrolls the view back", scrolled != bottom);
    key(item, Qt::Key_PageUp, Qt::ShiftModifier, QString());
    expect("Shift+PageUp scrolls a page further", frame(window) != scrolled && captured.isEmpty());
    key(item, Qt::Key_D, Qt::NoModifier, QStringLiteral("d"));
    captured.clear();
    expect("a key snaps back to the bottom", frame(window) == bottom);

    item.feed("\x1b[?1000h\x1b[?1006h");
    mouse(window, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, cellCenter(5, 3));
    check("SGR left press at (5,3)", "\x1b[<0;6;4M");
    mouse(window, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, cellCenter(5, 3));
    check("SGR left release at (5,3)", "\x1b[<0;6;4m");
    wheel(window, cellCenter(5, 3), 120);
    check("SGR wheel up at (5,3)", "\x1b[<64;6;4M");
    wheel(window, cellCenter(5, 3), -120);
    check("SGR wheel down at (5,3)", "\x1b[<65;6;4M");
    // Row 1 now shows scrollback output.
    drag(window, cellCenter(0, 1), cellEnd(3, 1), Qt::ShiftModifier);
    check("Shift+drag sends no report", "");
    text = copied(item);
    expect("Shift+drag selects instead", text == QStringLiteral("scro"), text);
    item.feed("\x1b[?1006l\x1b[?1000l");

    item.feed("\x1b[?1049h");
    wheel(window, cellCenter(5, 3), -120);
    check("alt screen wheel down", "\x1b[B\x1b[B\x1b[B");
    wheel(window, cellCenter(5, 3), 120);
    check("alt screen wheel up", "\x1b[A\x1b[A\x1b[A");
    item.feed("\x1b[?1h");
    wheel(window, cellCenter(5, 3), 120);
    check("alt screen wheel up, app keys", "\x1bOA\x1bOA\x1bOA");
    item.feed("\x1b[?1l\x1b[?1049l");

    // Synchronized output: the frame from the start of the hold stays until
    // the program ends it, or until a second has passed.
    const QImage beforeHold = frame(window);
    item.feed("\x1b[?2026h\x1b[5;1Hdrawn during the hold");
    expect("mode 2026 holds the frame", frame(window) == beforeHold);
    item.feed("\x1b[?2026l");
    const QImage afterHold = frame(window);
    expect("ending mode 2026 shows the new frame", afterHold != beforeHold);
    item.feed("\x1b[?2026h\x1b[6;1Hnever released");
    expect("a second hold holds again", frame(window) == afterHold);
    wait(1150);
    expect("the hold ends by itself after 1 s", frame(window) != afterHold);

    // Blink: a blinking cursor disappears within one 600 ms period and comes
    // back at once on a keypress; a steady one stays.
    expect("item kept focus through the waits", item.hasActiveFocus());
    captured.clear();
    item.feed("\x1b[1 q");
    const QImage blinkShown = frame(window);
    wait(700);
    expect("blinking cursor blinks", frame(window) != blinkShown);
    key(item, Qt::Key_E, Qt::NoModifier, QStringLiteral("e"));
    captured.clear();
    expect("keypress shows the cursor again", frame(window) == blinkShown);
    item.feed("\x1b[6 q");
    const QImage bar = frame(window);
    expect("program sets a bar cursor", bar != blinkShown);
    item.feed("\x1b[2 q");
    const QImage steady = frame(window);
    wait(700);
    expect("steady cursor stays", frame(window) == steady);

    // setTheme replaces everything, so an empty theme brings back the
    // built-in colors, and repeating it must not grow memory.
    item.setTheme(QJsonObject());
    const QImage builtIn = frame(window);
    item.setTheme(theme);
    expect("empty theme restores built-in colors", frame(window) != builtIn);
    for (int i = 0; i < 50; ++i)
        item.setTheme(theme);
    const long before = rssKb();
    for (int i = 0; i < 300; ++i) {
        item.setTheme(i % 2 ? theme : QJsonObject());
        if (i % 30 == 0)
            frame(window);
    }
    const long after = rssKb();
    expect("RSS over 300 setTheme calls", after - before < 512,
           QStringLiteral("%1 kB -> %2 kB (%3%4 kB)").arg(before).arg(after).arg(after >= before ? "+" : "").arg(after - before));

    std::printf("failures: %d\n", failures);
    // Leak checkers report at exit without flushing stdio first.
    std::fflush(stdout);
    return ok && failures == 0 ? 0 : 1;
}
