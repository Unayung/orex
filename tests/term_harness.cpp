// Drives one TerminalItem offscreen: restores a recorded Rex stream, saves
// what it draws as PNGs, and prints the bytes that key, paste, IME and focus
// events produce, so both halves of the item can be checked without Rex.
//
//   QT_QPA_PLATFORM=offscreen ./term_harness stream.bin outdir

#include "terminalitem.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QQuickWindow>
#include <QSGRendererInterface>

#include <cstdio>

namespace {

// The recorded stream's snapshot is this long; the rest is live output.
constexpr int kSnapshotBytes = 1453;

QByteArray captured;

void check(const char *label, const QByteArray &expected)
{
    const bool ok = captured == expected;
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

    return ok ? 0 : 1;
}
