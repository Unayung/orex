#include "terminalitem.h"

// libghostty has struct fields named "emit", which Qt defines as an empty
// macro, so the macro is set aside while its headers are read.
#pragma push_macro("emit")
#undef emit
#include <ghostty/vt.h>
#pragma pop_macro("emit")

#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleHints>
#include <QTimer>
#include <QtMath>

#include <utility>
#include <vector>

namespace {

QColor toColor(GhosttyColorRgb c)
{
    return QColor(c.r, c.g, c.b);
}

// Ghostty names keys by their place on a US keyboard, and Qt names them by
// the character they produce, so the shifted punctuation keys are listed
// under the key that types them on a US layout. The unshifted codepoint is
// what the Kitty keyboard protocol reports as the key's identity.
struct KeyMapping {
    GhosttyKey key = GHOSTTY_KEY_UNIDENTIFIED;
    char32_t unshifted = 0;
};

KeyMapping mapKey(int key, bool keypad)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return {GhosttyKey(GHOSTTY_KEY_A + (key - Qt::Key_A)), char32_t('a' + (key - Qt::Key_A))};
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        const int n = key - Qt::Key_0;
        return {GhosttyKey((keypad ? GHOSTTY_KEY_NUMPAD_0 : GHOSTTY_KEY_DIGIT_0) + n), char32_t('0' + n)};
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F25)
        return {GhosttyKey(GHOSTTY_KEY_F1 + (key - Qt::Key_F1)), 0};

    if (keypad) {
        switch (key) {
        case Qt::Key_Plus: return {GHOSTTY_KEY_NUMPAD_ADD, '+'};
        case Qt::Key_Minus: return {GHOSTTY_KEY_NUMPAD_SUBTRACT, '-'};
        case Qt::Key_Asterisk: return {GHOSTTY_KEY_NUMPAD_MULTIPLY, '*'};
        case Qt::Key_Slash: return {GHOSTTY_KEY_NUMPAD_DIVIDE, '/'};
        case Qt::Key_Period: return {GHOSTTY_KEY_NUMPAD_DECIMAL, '.'};
        case Qt::Key_Comma: return {GHOSTTY_KEY_NUMPAD_COMMA, ','};
        case Qt::Key_Equal: return {GHOSTTY_KEY_NUMPAD_EQUAL, '='};
        case Qt::Key_Enter: return {GHOSTTY_KEY_NUMPAD_ENTER, 0};
        default: break;
        }
    }

    switch (key) {
    case Qt::Key_QuoteLeft: case Qt::Key_AsciiTilde: return {GHOSTTY_KEY_BACKQUOTE, '`'};
    case Qt::Key_Backslash: case Qt::Key_Bar: return {GHOSTTY_KEY_BACKSLASH, '\\'};
    case Qt::Key_BracketLeft: case Qt::Key_BraceLeft: return {GHOSTTY_KEY_BRACKET_LEFT, '['};
    case Qt::Key_BracketRight: case Qt::Key_BraceRight: return {GHOSTTY_KEY_BRACKET_RIGHT, ']'};
    case Qt::Key_Comma: case Qt::Key_Less: return {GHOSTTY_KEY_COMMA, ','};
    case Qt::Key_Equal: case Qt::Key_Plus: return {GHOSTTY_KEY_EQUAL, '='};
    case Qt::Key_Minus: case Qt::Key_Underscore: return {GHOSTTY_KEY_MINUS, '-'};
    case Qt::Key_Period: case Qt::Key_Greater: return {GHOSTTY_KEY_PERIOD, '.'};
    case Qt::Key_Apostrophe: case Qt::Key_QuoteDbl: return {GHOSTTY_KEY_QUOTE, '\''};
    case Qt::Key_Semicolon: case Qt::Key_Colon: return {GHOSTTY_KEY_SEMICOLON, ';'};
    case Qt::Key_Slash: case Qt::Key_Question: return {GHOSTTY_KEY_SLASH, '/'};
    case Qt::Key_Exclam: return {GHOSTTY_KEY_DIGIT_1, '1'};
    case Qt::Key_At: return {GHOSTTY_KEY_DIGIT_2, '2'};
    case Qt::Key_NumberSign: return {GHOSTTY_KEY_DIGIT_3, '3'};
    case Qt::Key_Dollar: return {GHOSTTY_KEY_DIGIT_4, '4'};
    case Qt::Key_Percent: return {GHOSTTY_KEY_DIGIT_5, '5'};
    case Qt::Key_AsciiCircum: return {GHOSTTY_KEY_DIGIT_6, '6'};
    case Qt::Key_Ampersand: return {GHOSTTY_KEY_DIGIT_7, '7'};
    case Qt::Key_Asterisk: return {GHOSTTY_KEY_DIGIT_8, '8'};
    case Qt::Key_ParenLeft: return {GHOSTTY_KEY_DIGIT_9, '9'};
    case Qt::Key_ParenRight: return {GHOSTTY_KEY_DIGIT_0, '0'};
    case Qt::Key_Space: return {GHOSTTY_KEY_SPACE, ' '};

    case Qt::Key_Backspace: return {GHOSTTY_KEY_BACKSPACE, 0};
    // Qt reports Shift+Tab as its own key; the Shift modifier is still set.
    case Qt::Key_Tab: case Qt::Key_Backtab: return {GHOSTTY_KEY_TAB, 0};
    case Qt::Key_Return: case Qt::Key_Enter: return {GHOSTTY_KEY_ENTER, 0};
    case Qt::Key_Escape: return {GHOSTTY_KEY_ESCAPE, 0};
    case Qt::Key_Insert: return {GHOSTTY_KEY_INSERT, 0};
    case Qt::Key_Delete: return {GHOSTTY_KEY_DELETE, 0};
    case Qt::Key_Home: return {GHOSTTY_KEY_HOME, 0};
    case Qt::Key_End: return {GHOSTTY_KEY_END, 0};
    case Qt::Key_PageUp: return {GHOSTTY_KEY_PAGE_UP, 0};
    case Qt::Key_PageDown: return {GHOSTTY_KEY_PAGE_DOWN, 0};
    case Qt::Key_Up: return {GHOSTTY_KEY_ARROW_UP, 0};
    case Qt::Key_Down: return {GHOSTTY_KEY_ARROW_DOWN, 0};
    case Qt::Key_Left: return {GHOSTTY_KEY_ARROW_LEFT, 0};
    case Qt::Key_Right: return {GHOSTTY_KEY_ARROW_RIGHT, 0};

    // Modifier keys produce nothing in legacy mode, but the Kitty protocol
    // can ask for them, so they need real key codes too.
    case Qt::Key_Shift: return {GHOSTTY_KEY_SHIFT_LEFT, 0};
    case Qt::Key_Control: return {GHOSTTY_KEY_CONTROL_LEFT, 0};
    case Qt::Key_Alt: return {GHOSTTY_KEY_ALT_LEFT, 0};
    case Qt::Key_Meta: case Qt::Key_Super_L: return {GHOSTTY_KEY_META_LEFT, 0};
    case Qt::Key_Super_R: return {GHOSTTY_KEY_META_RIGHT, 0};
    case Qt::Key_CapsLock: return {GHOSTTY_KEY_CAPS_LOCK, 0};
    case Qt::Key_NumLock: return {GHOSTTY_KEY_NUM_LOCK, 0};
    case Qt::Key_ScrollLock: return {GHOSTTY_KEY_SCROLL_LOCK, 0};
    case Qt::Key_Menu: return {GHOSTTY_KEY_CONTEXT_MENU, 0};
    case Qt::Key_Print: return {GHOSTTY_KEY_PRINT_SCREEN, 0};
    case Qt::Key_Pause: return {GHOSTTY_KEY_PAUSE, 0};
    default: return {};
    }
}

GhosttyMods mapMods(Qt::KeyboardModifiers m)
{
    GhosttyMods mods = 0;
    if (m & Qt::ShiftModifier) mods |= GHOSTTY_MODS_SHIFT;
    if (m & Qt::ControlModifier) mods |= GHOSTTY_MODS_CTRL;
    if (m & Qt::AltModifier) mods |= GHOSTTY_MODS_ALT;
    if (m & Qt::MetaModifier) mods |= GHOSTTY_MODS_SUPER;
    return mods;
}

// The encoder wants the text a key types before Ctrl turns it into a control
// character, and it must not see control characters at all. Qt gives "\x03"
// for Ctrl+C, so such text is dropped and the encoder works from the key.
bool isPrintable(const QString &text)
{
    if (text.isEmpty())
        return false;
    for (QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return false;
    }
    return true;
}

bool modeEnabled(GhosttyTerminal term, GhosttyMode mode)
{
    GhosttyTerminalModeConfig config{mode, false};
    return ghostty_terminal_get(term, GHOSTTY_TERMINAL_DATA_MODE, &config) == GHOSTTY_SUCCESS
        && config.value;
}

GhosttyMouseButton mapButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton: return GHOSTTY_MOUSE_BUTTON_LEFT;
    case Qt::RightButton: return GHOSTTY_MOUSE_BUTTON_RIGHT;
    case Qt::MiddleButton: return GHOSTTY_MOUSE_BUTTON_MIDDLE;
    default: return GHOSTTY_MOUSE_BUTTON_UNKNOWN;
    }
}

} // namespace

struct TerminalItem::Private {
    TerminalItem *q = nullptr;

    GhosttyTerminal term = nullptr;
    GhosttyRenderState render = nullptr;
    GhosttyRenderStateRowIterator rowIter = nullptr;
    GhosttyRenderStateRowCells rowCells = nullptr;
    GhosttyKeyEncoder encoder = nullptr;
    GhosttyKeyEvent keyEvent = nullptr;
    GhosttyMouseEncoder mouseEncoder = nullptr;
    GhosttyMouseEvent mouseEvent = nullptr;
    GhosttySelectionGesture gesture = nullptr;
    GhosttySelectionGestureEvent gesturePress = nullptr;
    GhosttySelectionGestureEvent gestureDrag = nullptr;
    GhosttySelectionGestureEvent gestureRelease = nullptr;

    // Index 0 is regular; bit 0 adds bold and bit 1 adds italic.
    QFont fonts[4];
    int cellWidth = 1;
    int cellHeight = 1;
    qreal baseline = 0;
    qreal underlineOffset = 0;
    qreal strikeOffset = 0;
    qreal lineWidth = 1;

    int fitCols = 0;
    int fitRows = 0;

    // True while an update() is already waiting for the next frame, so a
    // burst of feed() calls asks the scene graph for one repaint, not many.
    bool repaintQueued = false;

    QString preedit;
    QColor selectionColor;

    // libghostty's own default colors, which a theme falls back to.
    GhosttyColorRgb builtinFg{};
    GhosttyColorRgb builtinBg{};
    bool hasSelection = false;

    // True while the program holds the screen with synchronized output. The
    // render state then keeps the frame captured when the hold began.
    bool held = false;
    QTimer holdTimer;

    QTimer blinkTimer;
    bool blinkOn = true;
    bool cursorBlinks = false;

    // True while a button press went to the program as a mouse report, so
    // its drag and release go there too even if Shift changes midway.
    bool pressReported = false;
    int wheelAngle = 0;

    // One cell of the row being painted, already resolved to colors.
    struct Cell {
        int x = 0;
        int width = 1;
        QString text;
        QColor fg;
        QColor bg;
        bool hasBg = false;
        int font = 0;
        bool faint = false;
        bool invisible = false;
        bool underline = false;
        bool strike = false;
        QColor underlineColor;
    };
    std::vector<Cell> line;
    QByteArray textBuf = QByteArray(64, Qt::Uninitialized);

    static void onRenderHold(GhosttyTerminal terminal, void *userdata, bool held);
    void installCallbacks();
    void endHold();
    void setupFont();
    // Recomputes the cols and rows that fit, and announces a change.
    void refit(TerminalItem *q);
    qreal pointSize = 12;
    void scheduleRepaint();
    void resetBlink();
    void readCell(int x, const GhosttyRenderStateColors &colors, int selStart, int selEnd);
    void drawLine(QPainter *p, int y);

    QByteArray encodeKey(GhosttyKeyAction action, GhosttyKey key, GhosttyMods mods,
                         const QByteArray &utf8, char32_t unshifted);
    void sendKey(QKeyEvent *e, GhosttyKeyAction action);
    void sendUserInput(const QByteArray &bytes);
    void paste(QClipboard::Mode mode);
    void reportFocus(GhosttyFocusEvent event);

    bool mouseTracking(Qt::KeyboardModifiers mods) const;
    void sendMouse(GhosttyMouseAction action, GhosttyMouseButton button, Qt::KeyboardModifiers mods,
                   QPointF pos, bool anyPressed);
    void scrollViewport(intptr_t rows);
    void scrollToBottom();

    bool gridRefAt(QPointF pos, GhosttyGridRef *ref) const;
    void applySelection(const GhosttySelection *selection);
    QByteArray selectionText() const;
    void copySelection(QClipboard::Mode mode);
};

void TerminalItem::Private::onRenderHold(GhosttyTerminal, void *userdata, bool held)
{
    auto *d = static_cast<Private *>(userdata);
    if (held) {
        // Nothing after the start of the hold has been applied yet, so this
        // is the finished frame the program wants left on screen.
        ghostty_render_state_update(d->render, d->term);
        d->held = true;
        d->holdTimer.start();
    } else {
        d->held = false;
        d->holdTimer.stop();
        d->scheduleRepaint();
    }
}

void TerminalItem::Private::installCallbacks()
{
    ghostty_terminal_set(term, GHOSTTY_TERMINAL_OPT_USERDATA, this);
    ghostty_terminal_set(term, GHOSTTY_TERMINAL_OPT_RENDER_HOLD,
                         reinterpret_cast<const void *>(&Private::onRenderHold));
}

// The terminal has no clock, so a program that never releases its hold
// would freeze the pane. This also runs when the user scrolls or selects,
// since showing what they asked for matters more than a tidy frame.
void TerminalItem::Private::endHold()
{
    if (!held)
        return;
    // Setting the mode directly doesn't call the hold callback.
    GhosttyTerminalModeConfig config{GHOSTTY_MODE_SYNC_OUTPUT, false};
    ghostty_terminal_set(term, GHOSTTY_TERMINAL_OPT_MODE, &config);
    held = false;
    holdTimer.stop();
    scheduleRepaint();
}

void TerminalItem::Private::setupFont()
{
    QFont base = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    base.setPointSizeF(pointSize);
    base.setKerning(false);

    // The grid uses whole pixels so that cell backgrounds meet without seams,
    // but a font's advance is rarely a whole number. Letter spacing makes up
    // the difference, so a run of ASCII drawn in one call lands on the grid.
    const QFontMetricsF fm(base);
    cellWidth = qMax(1, qCeil(fm.horizontalAdvance(QLatin1Char('M'))));
    cellHeight = qMax(1, qCeil(fm.height()));
    baseline = fm.ascent() + (cellHeight - fm.height()) / 2;
    underlineOffset = fm.underlinePos();
    strikeOffset = -fm.strikeOutPos();
    lineWidth = qMax<qreal>(1, fm.lineWidth());

    for (int i = 0; i < 4; ++i) {
        QFont f = base;
        f.setBold(i & 1);
        f.setItalic(i & 2);
        const qreal advance = QFontMetricsF(f).horizontalAdvance(QLatin1Char('M'));
        f.setLetterSpacing(QFont::AbsoluteSpacing, cellWidth - advance);
        fonts[i] = f;
    }
}

void TerminalItem::Private::scheduleRepaint()
{
    // During a hold the captured frame stays on screen; the end of the hold
    // asks for the repaint.
    if (repaintQueued || held)
        return;
    repaintQueued = true;
    q->update();
    // The input method places its candidate window from the cursor
    // rectangle, which moves with program output.
    if (q->hasActiveFocus())
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
}

// A cursor that just moved or was typed at is shown at once, and the blink
// starts over from there.
void TerminalItem::Private::resetBlink()
{
    if (!blinkOn) {
        blinkOn = true;
        q->update();
    }
    if (blinkTimer.isActive())
        blinkTimer.start();
}

void TerminalItem::Private::readCell(int x, const GhosttyRenderStateColors &colors, int selStart, int selEnd)
{
    GhosttyCell raw = 0;
    ghostty_render_state_row_cells_get(rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
    GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
    ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
    // The wide character before this spacer already covers it.
    if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL)
        return;

    GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
    ghostty_render_state_row_cells_get(rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);

    Cell cell;
    cell.x = x;
    cell.width = wide == GHOSTTY_CELL_WIDE_WIDE ? 2 : 1;

    GhosttyBuffer buf{reinterpret_cast<uint8_t *>(textBuf.data()), size_t(textBuf.size()), 0};
    GhosttyResult r = ghostty_render_state_row_cells_get(
        rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &buf);
    if (r == GHOSTTY_OUT_OF_SPACE) {
        textBuf.resize(qsizetype(buf.len));
        buf = {reinterpret_cast<uint8_t *>(textBuf.data()), size_t(textBuf.size()), 0};
        r = ghostty_render_state_row_cells_get(
            rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &buf);
    }
    if (r == GHOSTTY_SUCCESS && buf.len > 0)
        cell.text = QString::fromUtf8(textBuf.constData(), qsizetype(buf.len));

    // A cell without its own color takes the terminal's default, which the
    // render state reports with the palette.
    GhosttyColorRgb rgb;
    cell.fg = ghostty_render_state_row_cells_get(rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &rgb)
            == GHOSTTY_SUCCESS
        ? toColor(rgb)
        : toColor(colors.foreground);
    cell.hasBg = ghostty_render_state_row_cells_get(rowCells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &rgb)
        == GHOSTTY_SUCCESS;
    cell.bg = cell.hasBg ? toColor(rgb) : toColor(colors.background);
    if (style.inverse) {
        std::swap(cell.fg, cell.bg);
        cell.hasBg = true;
    }
    if (x >= selStart && x <= selEnd) {
        if (selectionColor.isValid())
            cell.bg = selectionColor;
        else
            std::swap(cell.fg, cell.bg);
        cell.hasBg = true;
    }

    cell.font = (style.bold ? 1 : 0) | (style.italic ? 2 : 0);
    cell.faint = style.faint;
    cell.invisible = style.invisible;
    cell.strike = style.strikethrough;
    cell.underline = style.underline != 0;
    switch (style.underline_color.tag) {
    case GHOSTTY_STYLE_COLOR_PALETTE:
        cell.underlineColor = toColor(colors.palette[style.underline_color.value.palette]);
        break;
    case GHOSTTY_STYLE_COLOR_RGB:
        cell.underlineColor = toColor(style.underline_color.value.rgb);
        break;
    default:
        cell.underlineColor = cell.fg;
        break;
    }
    line.push_back(std::move(cell));
}

void TerminalItem::Private::drawLine(QPainter *p, int y)
{
    const int top = y * cellHeight;
    for (const Cell &c : line) {
        if (c.hasBg)
            p->fillRect(c.x * cellWidth, top, c.width * cellWidth, cellHeight, c.bg);
    }

    auto penColor = [](const Cell &c) {
        QColor color = c.fg;
        if (c.faint)
            color.setAlphaF(0.5f);
        return color;
    };

    // Plain ASCII in one font and color is drawn as one string, because a
    // drawText call per cell is slow on a full screen. Anything else may come
    // from a fallback font with its own advance, so it gets a call per cell
    // to stay on the grid.
    QString run;
    const Cell *runStart = nullptr;
    auto flush = [&] {
        if (runStart && !run.trimmed().isEmpty()) {
            p->setFont(fonts[runStart->font]);
            p->setPen(penColor(*runStart));
            p->drawText(QPointF(runStart->x * cellWidth, top + baseline), run);
        }
        run.clear();
        runStart = nullptr;
    };
    for (const Cell &c : line) {
        if (c.invisible || c.text.isEmpty()) {
            flush();
            continue;
        }
        const bool ascii = c.text.size() == 1 && c.text[0].unicode() >= 0x20 && c.text[0].unicode() < 0x7f;
        if (ascii && runStart && runStart->font == c.font && runStart->fg == c.fg
            && runStart->faint == c.faint && runStart->x + run.size() == c.x) {
            run += c.text;
            continue;
        }
        flush();
        if (ascii) {
            runStart = &c;
            run = c.text;
            continue;
        }
        p->setFont(fonts[c.font]);
        p->setPen(penColor(c));
        p->drawText(QPointF(c.x * cellWidth, top + baseline), c.text);
    }
    flush();

    for (const Cell &c : line) {
        const qreal left = c.x * cellWidth;
        const qreal width = c.width * cellWidth;
        if (c.underline)
            p->fillRect(QRectF(left, top + baseline + underlineOffset, width, lineWidth), c.underlineColor);
        if (c.strike)
            p->fillRect(QRectF(left, top + baseline + strikeOffset, width, lineWidth), penColor(c));
    }
}

QByteArray TerminalItem::Private::encodeKey(GhosttyKeyAction action, GhosttyKey key, GhosttyMods mods,
                                            const QByteArray &utf8, char32_t unshifted)
{
    ghostty_key_event_set_action(keyEvent, action);
    ghostty_key_event_set_key(keyEvent, key);
    ghostty_key_event_set_mods(keyEvent, mods);
    // Shift is spent on choosing the character when the key types one, so
    // the encoder must not report it again as a modifier.
    ghostty_key_event_set_consumed_mods(keyEvent, utf8.isEmpty() ? 0 : (mods & GHOSTTY_MODS_SHIFT));
    ghostty_key_event_set_composing(keyEvent, false);
    ghostty_key_event_set_utf8(keyEvent, utf8.isEmpty() ? nullptr : utf8.constData(), size_t(utf8.size()));
    ghostty_key_event_set_unshifted_codepoint(keyEvent, unshifted);

    // The program can switch cursor-key or Kitty keyboard modes at any time,
    // so the encoder is brought up to date before every key.
    ghostty_key_encoder_setopt_from_terminal(encoder, term);

    char buf[128];
    size_t len = 0;
    GhosttyResult r = ghostty_key_encoder_encode(encoder, keyEvent, buf, sizeof buf, &len);
    if (r == GHOSTTY_OUT_OF_SPACE) {
        QByteArray big(qsizetype(len), Qt::Uninitialized);
        r = ghostty_key_encoder_encode(encoder, keyEvent, big.data(), size_t(big.size()), &len);
        return r == GHOSTTY_SUCCESS ? big.left(qsizetype(len)) : QByteArray();
    }
    return r == GHOSTTY_SUCCESS ? QByteArray(buf, qsizetype(len)) : QByteArray();
}

void TerminalItem::Private::sendKey(QKeyEvent *e, GhosttyKeyAction action)
{
    const KeyMapping mapping = mapKey(e->key(), e->modifiers() & Qt::KeypadModifier);
    const QString text = isPrintable(e->text()) ? e->text() : QString();
    char32_t unshifted = mapping.unshifted;
    if (!unshifted && !text.isEmpty())
        unshifted = text.toLower().toUcs4().value(0);

    const QByteArray bytes = encodeKey(action, mapping.key, mapMods(e->modifiers()), text.toUtf8(), unshifted);
    if (bytes.isEmpty())
        return;
    // A release only reaches the program under the Kitty protocol. It isn't
    // typing, so it leaves the selection and the scroll position alone.
    if (action == GHOSTTY_KEY_ACTION_RELEASE)
        emit q->input(bytes);
    else
        sendUserInput(bytes);
}

// What the user types goes to the live screen: the view returns to the
// bottom, a selection made for copying is done with, and the cursor shows.
void TerminalItem::Private::sendUserInput(const QByteArray &bytes)
{
    applySelection(nullptr);
    scrollToBottom();
    resetBlink();
    emit q->input(bytes);
}

void TerminalItem::Private::paste(QClipboard::Mode mode)
{
    QByteArray data = QGuiApplication::clipboard()->text(mode).toUtf8();
    if (data.isEmpty())
        return;

    // ghostty_terminal_paste would need a write-pty callback, and that would
    // also make this local copy answer the program's queries, which the
    // server's terminal already does. The encoder alone does the framing.
    const bool bracketed = modeEnabled(term, GHOSTTY_MODE_BRACKETED_PASTE);
    // Bracketing adds "\e[200~" and "\e[201~"; nothing else grows.
    QByteArray out(data.size() + 12, Qt::Uninitialized);
    size_t written = 0;
    GhosttyResult r = ghostty_paste_encode(data.data(), size_t(data.size()), bracketed,
                                           out.data(), size_t(out.size()), &written);
    if (r == GHOSTTY_OUT_OF_SPACE) {
        out.resize(qsizetype(written));
        r = ghostty_paste_encode(data.data(), size_t(data.size()), bracketed,
                                 out.data(), size_t(out.size()), &written);
    }
    if (r != GHOSTTY_SUCCESS || written == 0)
        return;
    // Pasting the selection is the usual reason to have one, so the paste
    // keeps it, unlike typing.
    scrollToBottom();
    emit q->input(out.left(qsizetype(written)));
}

void TerminalItem::Private::reportFocus(GhosttyFocusEvent event)
{
    if (!modeEnabled(term, GHOSTTY_MODE_FOCUS_EVENT))
        return;
    char buf[8];
    size_t len = 0;
    if (ghostty_focus_encode(event, buf, sizeof buf, &len) == GHOSTTY_SUCCESS && len > 0)
        emit q->input(QByteArray(buf, qsizetype(len)));
}

// Shift keeps the mouse for selecting even when the program wants it, as in
// most terminals.
bool TerminalItem::Private::mouseTracking(Qt::KeyboardModifiers mods) const
{
    bool tracking = false;
    ghostty_terminal_get(term, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &tracking);
    return tracking && !(mods & Qt::ShiftModifier);
}

void TerminalItem::Private::sendMouse(GhosttyMouseAction action, GhosttyMouseButton button,
                                      Qt::KeyboardModifiers mods, QPointF pos, bool anyPressed)
{
    // The program picks the tracking mode and report format, so they are
    // read from the terminal for every event, like the key encoder's modes.
    ghostty_mouse_encoder_setopt_from_terminal(mouseEncoder, term);
    GhosttyMouseEncoderSize size = GHOSTTY_INIT_SIZED(GhosttyMouseEncoderSize);
    size.screen_width = uint32_t(qMax(1.0, q->width()));
    size.screen_height = uint32_t(qMax(1.0, q->height()));
    size.cell_width = uint32_t(cellWidth);
    size.cell_height = uint32_t(cellHeight);
    ghostty_mouse_encoder_setopt(mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size);
    ghostty_mouse_encoder_setopt(mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &anyPressed);
    const bool trackLastCell = true;
    ghostty_mouse_encoder_setopt(mouseEncoder, GHOSTTY_MOUSE_ENCODER_OPT_TRACK_LAST_CELL, &trackLastCell);

    ghostty_mouse_event_set_action(mouseEvent, action);
    if (button == GHOSTTY_MOUSE_BUTTON_UNKNOWN)
        ghostty_mouse_event_clear_button(mouseEvent);
    else
        ghostty_mouse_event_set_button(mouseEvent, button);
    ghostty_mouse_event_set_mods(mouseEvent, mapMods(mods));
    ghostty_mouse_event_set_position(mouseEvent, GhosttyMousePosition{float(pos.x()), float(pos.y())});

    char buf[64];
    size_t len = 0;
    if (ghostty_mouse_encoder_encode(mouseEncoder, mouseEvent, buf, sizeof buf, &len) == GHOSTTY_SUCCESS && len > 0)
        emit q->input(QByteArray(buf, qsizetype(len)));
}

void TerminalItem::Private::scrollViewport(intptr_t rows)
{
    GhosttyTerminalScrollViewport scroll{};
    scroll.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
    scroll.value.delta = rows;
    ghostty_terminal_scroll_viewport(term, scroll);
    endHold();
    scheduleRepaint();
}

void TerminalItem::Private::scrollToBottom()
{
    bool atBottom = true;
    ghostty_terminal_get(term, GHOSTTY_TERMINAL_DATA_VIEWPORT_ACTIVE, &atBottom);
    if (atBottom)
        return;
    GhosttyTerminalScrollViewport scroll{};
    scroll.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
    ghostty_terminal_scroll_viewport(term, scroll);
    scheduleRepaint();
}

// The cell under a point, clamped to the grid so that a drag past an edge
// still selects up to that edge.
bool TerminalItem::Private::gridRefAt(QPointF pos, GhosttyGridRef *ref) const
{
    uint16_t cols = 0;
    uint16_t rows = 0;
    ghostty_terminal_get(term, GHOSTTY_TERMINAL_DATA_COLS, &cols);
    ghostty_terminal_get(term, GHOSTTY_TERMINAL_DATA_ROWS, &rows);
    if (cols == 0 || rows == 0)
        return false;
    GhosttyPoint point{};
    point.tag = GHOSTTY_POINT_TAG_VIEWPORT;
    point.value.coordinate.x = uint16_t(qBound(0, qFloor(pos.x() / cellWidth), cols - 1));
    point.value.coordinate.y = uint32_t(qBound(0, qFloor(pos.y() / cellHeight), rows - 1));
    *ref = GHOSTTY_INIT_SIZED(GhosttyGridRef);
    return ghostty_terminal_grid_ref(term, point, ref) == GHOSTTY_SUCCESS;
}

// The terminal keeps the selection as tracked references, so it follows its
// text through new output and scrolling, and the render state marks it.
void TerminalItem::Private::applySelection(const GhosttySelection *selection)
{
    if (!selection && !hasSelection)
        return;
    ghostty_terminal_set(term, GHOSTTY_TERMINAL_OPT_SELECTION, selection);
    hasSelection = selection != nullptr;
    endHold();
    scheduleRepaint();
}

QByteArray TerminalItem::Private::selectionText() const
{
    if (!hasSelection)
        return {};
    // Plain text with soft wraps joined and trailing blanks trimmed is what
    // Ghostty itself copies. The struct is filled in order because one of
    // its fields is named "emit", which Qt's macro would swallow.
    const GhosttyTerminalSelectionFormatOptions options{
        sizeof(GhosttyTerminalSelectionFormatOptions), GHOSTTY_FORMATTER_FORMAT_PLAIN, true, true, nullptr};
    uint8_t *ptr = nullptr;
    size_t len = 0;
    if (ghostty_terminal_selection_format_alloc(term, nullptr, options, &ptr, &len) != GHOSTTY_SUCCESS)
        return {};
    const QByteArray text(reinterpret_cast<const char *>(ptr), qsizetype(len));
    ghostty_free(nullptr, ptr, len);
    return text;
}

void TerminalItem::Private::copySelection(QClipboard::Mode mode)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (mode == QClipboard::Selection && !clipboard->supportsSelection())
        return;
    const QByteArray text = selectionText();
    if (!text.isEmpty())
        clipboard->setText(QString::fromUtf8(text), mode);
}

TerminalItem::TerminalItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , d(new Private)
{
    d->q = this;
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    // Any-event mouse tracking reports motion with no button held.
    setAcceptHoverEvents(true);
    setOpaquePainting(true);

    // Until a snapshot arrives, output still needs somewhere to go.
    ghostty_terminal_new(nullptr, &d->term, 80, 24);
    d->installCallbacks();
    ghostty_render_state_new(nullptr, &d->render);
    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    ghostty_render_state_update(d->render, d->term);
    ghostty_render_state_get(d->render, GHOSTTY_RENDER_STATE_DATA_COLORS, &colors);
    d->builtinFg = colors.foreground;
    d->builtinBg = colors.background;
    ghostty_render_state_row_iterator_new(nullptr, &d->rowIter);
    ghostty_render_state_row_cells_new(nullptr, &d->rowCells);
    ghostty_key_encoder_new(nullptr, &d->encoder);
    ghostty_key_event_new(nullptr, &d->keyEvent);
    ghostty_mouse_encoder_new(nullptr, &d->mouseEncoder);
    ghostty_mouse_event_new(nullptr, &d->mouseEvent);
    ghostty_selection_gesture_new(nullptr, &d->gesture);
    ghostty_selection_gesture_event_new(nullptr, &d->gesturePress, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_PRESS);
    ghostty_selection_gesture_event_new(nullptr, &d->gestureDrag, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_DRAG);
    ghostty_selection_gesture_event_new(nullptr, &d->gestureRelease, GHOSTTY_SELECTION_GESTURE_EVENT_TYPE_RELEASE);
    d->setupFont();

    d->holdTimer.setSingleShot(true);
    d->holdTimer.setInterval(1000);
    connect(&d->holdTimer, &QTimer::timeout, this, [this] { d->endHold(); });

    d->blinkTimer.setInterval(600);
    connect(&d->blinkTimer, &QTimer::timeout, this, [this] {
        // The timer runs whenever the item has focus; it only redraws when
        // the program asked for a blinking cursor.
        if (!d->cursorBlinks && d->blinkOn)
            return;
        d->blinkOn = !d->blinkOn || !d->cursorBlinks;
        update();
    });

    // A hidden item or one without a window may never paint, so the pending
    // repaint is forgotten here; otherwise later output would never ask for
    // another frame.
    auto resetRepaint = [this] {
        d->repaintQueued = false;
        update();
    };
    connect(this, &QQuickItem::visibleChanged, this, resetRepaint);
    connect(this, &QQuickItem::windowChanged, this, resetRepaint);
}

TerminalItem::~TerminalItem()
{
    // QQuickItem's destructor leaves the window and emits windowChanged after
    // this body has run, so the handlers that use d must go first.
    disconnect(this, &QQuickItem::visibleChanged, this, nullptr);
    disconnect(this, &QQuickItem::windowChanged, this, nullptr);
    ghostty_selection_gesture_event_free(d->gestureRelease);
    ghostty_selection_gesture_event_free(d->gestureDrag);
    ghostty_selection_gesture_event_free(d->gesturePress);
    // The gesture holds tracked references into the terminal, so it goes
    // first and is told which terminal they belong to.
    ghostty_selection_gesture_free(d->gesture, d->term);
    ghostty_mouse_event_free(d->mouseEvent);
    ghostty_mouse_encoder_free(d->mouseEncoder);
    ghostty_key_event_free(d->keyEvent);
    ghostty_key_encoder_free(d->encoder);
    ghostty_render_state_row_cells_free(d->rowCells);
    ghostty_render_state_row_iterator_free(d->rowIter);
    ghostty_render_state_free(d->render);
    ghostty_terminal_free(d->term);
    delete d;
}

// The properties report the grid that fits the item, which is what
// gridSizeChanged announces, not the size the server last gave resizeGrid.
int TerminalItem::cols() const
{
    return d->fitCols;
}

int TerminalItem::rows() const
{
    return d->fitRows;
}

QColor TerminalItem::selectionColor() const
{
    return d->selectionColor;
}

void TerminalItem::setSelectionColor(const QColor &color)
{
    if (d->selectionColor == color)
        return;
    d->selectionColor = color;
    emit selectionColorChanged();
    update();
}

bool TerminalItem::restoreSnapshot(const QByteArray &snapshot)
{
    const auto *bytes = reinterpret_cast<const uint8_t *>(snapshot.constData());
    GhosttySnapshotDecoder decoder = nullptr;
    if (ghostty_snapshot_decoder_new_buf(nullptr, &decoder, bytes, size_t(snapshot.size())) != GHOSTTY_SUCCESS)
        return false;

    // READY gives a terminal that can already be drawn; the loop then adds
    // the older scrollback until FINISH, which next() reports as NO_VALUE.
    GhosttyTerminal restored = nullptr;
    GhosttyResult r = ghostty_snapshot_decoder_ready(decoder, &restored);
    if (r == GHOSTTY_SUCCESS) {
        while ((r = ghostty_snapshot_decoder_next(decoder)) == GHOSTTY_SUCCESS) {
        }
    }
    size_t end = 0;
    const bool ok = r == GHOSTTY_NO_VALUE
        && ghostty_snapshot_decoder_get(decoder, GHOSTTY_SNAPSHOT_DECODER_DATA_SOURCE_OFFSET, &end)
            == GHOSTTY_SUCCESS;
    ghostty_snapshot_decoder_free(decoder);
    if (!ok) {
        // A half-restored terminal would show a screen with history missing,
        // so the old one stays until a whole snapshot arrives.
        ghostty_terminal_free(restored);
        return false;
    }

    // Selection and hold state belong to the old terminal and go with it.
    ghostty_selection_gesture_reset(d->gesture, d->term);
    d->hasSelection = false;
    d->held = false;
    d->holdTimer.stop();
    d->pressReported = false;

    // The render state is built for one terminal and keeps per-row data from
    // it, so it is replaced along with the terminal.
    GhosttyRenderState render = nullptr;
    ghostty_render_state_new(nullptr, &render);
    ghostty_render_state_free(d->render);
    d->render = render;
    ghostty_terminal_free(d->term);
    d->term = restored;
    d->installCallbacks();

    // The caller can't know where the snapshot ends without decoding it, so
    // any live bytes that came in the same buffer are applied here.
    if (end < size_t(snapshot.size()))
        ghostty_terminal_vt_write(d->term, bytes + end, size_t(snapshot.size()) - end);
    d->scheduleRepaint();
    return true;
}

void TerminalItem::feed(const QByteArray &vt)
{
    // A selection stays while output goes elsewhere, but once its own text
    // changes it no longer shows what the user picked.
    const QByteArray selected = d->selectionText();
    ghostty_terminal_vt_write(d->term, reinterpret_cast<const uint8_t *>(vt.constData()), size_t(vt.size()));
    if (d->hasSelection && d->selectionText() != selected)
        d->applySelection(nullptr);
    d->resetBlink();
    d->scheduleRepaint();
}

void TerminalItem::resizeGrid(int cols, int rows)
{
    if (cols < 1 || rows < 1 || cols > UINT16_MAX || rows > UINT16_MAX)
        return;
    ghostty_terminal_resize(d->term, uint16_t(cols), uint16_t(rows), uint32_t(d->cellWidth),
                            uint32_t(d->cellHeight));
    d->scheduleRepaint();
}

void TerminalItem::setTheme(const QJsonObject &theme)
{
    // "scheme" is left alone: it only answers the program's color scheme
    // query, and the server's terminal answers that.
    auto parse = [&theme](QLatin1StringView key, GhosttyColorRgb *rgb) {
        const QByteArray text = theme.value(key).toString().toUtf8();
        return !text.isEmpty() && ghostty_color_parse(text.constData(), size_t(text.size()), rgb) == GHOSTTY_SUCCESS;
    };
    // The render state only takes the default foreground and background
    // while both are set, and keeps the old ones once they are cleared, so a
    // missing color is set to the built-in one rather than cleared.
    GhosttyColorRgb fg = d->builtinFg;
    GhosttyColorRgb bg = d->builtinBg;
    parse(QLatin1StringView("foreground"), &fg);
    parse(QLatin1StringView("background"), &bg);
    ghostty_terminal_set(d->term, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &fg);
    ghostty_terminal_set(d->term, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &bg);
    // Without a cursor color the cursor takes the foreground; clearing it
    // works as documented.
    GhosttyColorRgb cursor;
    const bool hasCursor = parse(QLatin1StringView("cursor"), &cursor);
    ghostty_terminal_set(d->term, GHOSTTY_TERMINAL_OPT_COLOR_CURSOR, hasCursor ? &cursor : nullptr);

    // The palette entries are changes to the built-in palette, so they are
    // applied to a fresh copy of it each time.
    GhosttyColorRgb palette[256];
    ghostty_color_palette_default(palette);
    bool changed = false;
    const QJsonArray entries = theme.value(QLatin1StringView("palette")).toArray();
    for (const QJsonValue &entry : entries) {
        const QByteArray text = entry.toString().toUtf8();
        uint8_t index = 0;
        GhosttyColorRgb rgb;
        if (ghostty_color_parse_palette_entry(text.constData(), size_t(text.size()), &index, &rgb) == GHOSTTY_SUCCESS) {
            palette[index] = rgb;
            changed = true;
        }
    }
    ghostty_terminal_set(d->term, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, changed ? palette : nullptr);
    d->scheduleRepaint();
}

// Qt Quick calls this during the sync phase, with the GUI thread blocked, so
// reading the terminal here does not race with feed().
void TerminalItem::paint(QPainter *painter)
{
    d->repaintQueued = false;
    // During a hold the render state already has the frame to show.
    if (!d->held && ghostty_render_state_update(d->render, d->term) != GHOSTTY_SUCCESS)
        return;

    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    ghostty_render_state_get(d->render, GHOSTTY_RENDER_STATE_DATA_COLORS, &colors);
    GhosttyRenderStateCursor cursor = GHOSTTY_INIT_SIZED(GhosttyRenderStateCursor);
    ghostty_render_state_get(d->render, GHOSTTY_RENDER_STATE_DATA_CURSOR, &cursor);
    d->cursorBlinks = cursor.blinking;

    const QColor defaultFg = toColor(colors.foreground);
    const QColor defaultBg = toColor(colors.background);
    painter->fillRect(QRectF(0, 0, width(), height()), defaultBg);

    const bool showCursor = cursor.visible && cursor.viewport_has_value;
    const int cursorX = showCursor ? cursor.viewport_x - (cursor.wide_tail ? 1 : 0) : -1;
    Private::Cell cursorCell;
    cursorCell.x = cursorX;
    cursorCell.bg = defaultBg;

    // The whole texture is redrawn every frame, so every row is read and the
    // per-row dirty flags are only cleared, not used.
    ghostty_render_state_get(d->render, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &d->rowIter);
    int y = 0;
    while (ghostty_render_state_row_iterator_next(d->rowIter)) {
        ghostty_render_state_row_get(d->rowIter, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &d->rowCells);
        GhosttyRenderStateRowSelection selection = GHOSTTY_INIT_SIZED(GhosttyRenderStateRowSelection);
        const bool selected = ghostty_render_state_row_get(d->rowIter, GHOSTTY_RENDER_STATE_ROW_DATA_SELECTION,
                                                           &selection) == GHOSTTY_SUCCESS;
        d->line.clear();
        int x = 0;
        while (ghostty_render_state_row_cells_next(d->rowCells))
            d->readCell(x++, colors, selected ? selection.start_x : 1, selected ? selection.end_x : 0);
        d->drawLine(painter, y);
        if (showCursor && y == cursor.viewport_y) {
            for (const Private::Cell &c : d->line) {
                if (c.x == cursorX) {
                    cursorCell = c;
                    break;
                }
            }
        }
        ++y;
    }
    ghostty_render_state_clean(d->render);

    const int cw = d->cellWidth;
    const int ch = d->cellHeight;
    const qreal top = cursor.viewport_y * ch;

    if (!d->preedit.isEmpty() && cursor.viewport_has_value) {
        // Text the input method is still composing is drawn at the cursor,
        // underlined, until it is committed and sent to the program.
        painter->setFont(d->fonts[0]);
        const qreal w = QFontMetricsF(d->fonts[0]).horizontalAdvance(d->preedit);
        const qreal left = cursor.viewport_x * cw;
        painter->fillRect(QRectF(left, top, w, ch), defaultBg);
        painter->setPen(defaultFg);
        painter->drawText(QPointF(left, top + d->baseline), d->preedit);
        painter->fillRect(QRectF(left, top + d->baseline + d->underlineOffset, w, d->lineWidth), defaultFg);
        return;
    }
    const bool focused = hasActiveFocus();
    if (!showCursor || (focused && d->cursorBlinks && !d->blinkOn))
        return;

    const QColor cursorColor = colors.cursor_has_value ? toColor(colors.cursor) : defaultFg;
    const QRectF cell(cursorX * cw, top, cursorCell.width * cw, ch);
    auto style = cursor.visual_style;
    if (!focused)
        style = GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW;
    switch (style) {
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR:
        painter->fillRect(QRectF(cell.left(), top, qMax<qreal>(2, d->lineWidth * 2), ch), cursorColor);
        break;
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE:
        painter->fillRect(QRectF(cell.left(), cell.bottom() - 2, cell.width(), 2), cursorColor);
        break;
    case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW:
        painter->setPen(QPen(cursorColor, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(cell.adjusted(0.5, 0.5, -0.5, -0.5));
        break;
    default:
        // A filled block hides the character, so it is drawn again on top in
        // the cell's background color, the way terminals invert it.
        painter->fillRect(cell, cursorColor);
        if (!cursorCell.text.isEmpty() && !cursorCell.invisible) {
            painter->setFont(d->fonts[cursorCell.font]);
            painter->setPen(cursorCell.bg);
            painter->drawText(QPointF(cell.left(), top + d->baseline), cursorCell.text);
        }
        break;
    }
}

void TerminalItem::keyPressEvent(QKeyEvent *event)
{
    const auto mods = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const int key = event->key();
    // Shift+Insert pastes the clipboard too, as Ghostty does: voxtype types
    // dictation by putting it on the clipboard and pressing Shift+Insert.
    if ((key == Qt::Key_V && mods == (Qt::ControlModifier | Qt::ShiftModifier)) ||
        (key == Qt::Key_Insert && mods == Qt::ShiftModifier)) {
        d->paste(QClipboard::Clipboard);
        event->accept();
        return;
    }
    // Copy is taken even with nothing selected, so the program never sees a
    // shortcut the user meant for the terminal.
    if (key == Qt::Key_C && mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
        d->copySelection(QClipboard::Clipboard);
        event->accept();
        return;
    }
    if ((key == Qt::Key_PageUp || key == Qt::Key_PageDown) && mods == Qt::ShiftModifier) {
        uint16_t rows = 0;
        ghostty_terminal_get(d->term, GHOSTTY_TERMINAL_DATA_ROWS, &rows);
        d->scrollViewport(key == Qt::Key_PageUp ? -intptr_t(rows) : intptr_t(rows));
        event->accept();
        return;
    }
    d->sendKey(event, event->isAutoRepeat() ? GHOSTTY_KEY_ACTION_REPEAT : GHOSTTY_KEY_ACTION_PRESS);
    // Every key belongs to the program, including ones that encode to
    // nothing, so none of them travel on to parent items.
    event->accept();
}

void TerminalItem::keyReleaseEvent(QKeyEvent *event)
{
    // Auto-repeat arrives as release and press pairs; only the press half
    // means anything, and the encoder reports it as a repeat.
    if (!event->isAutoRepeat())
        d->sendKey(event, GHOSTTY_KEY_ACTION_RELEASE);
    event->accept();
}

void TerminalItem::inputMethodEvent(QInputMethodEvent *event)
{
    // Committed text goes to the program as typed, the way a terminal sends
    // any other text.
    if (!event->commitString().isEmpty())
        d->sendUserInput(event->commitString().toUtf8());
    if (d->preedit != event->preeditString()) {
        d->preedit = event->preeditString();
        update();
    }
    event->accept();
}

QVariant TerminalItem::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImCursorRectangle: {
        uint16_t x = 0;
        uint16_t y = 0;
        ghostty_terminal_get(d->term, GHOSTTY_TERMINAL_DATA_CURSOR_X, &x);
        ghostty_terminal_get(d->term, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &y);
        return QRectF(x * d->cellWidth, y * d->cellHeight, d->cellWidth, d->cellHeight);
    }
    case Qt::ImFont:
        return d->fonts[0];
    case Qt::ImHints:
        return int(Qt::ImhNoAutoUppercase | Qt::ImhNoPredictiveText);
    case Qt::ImSurroundingText:
        return QString();
    case Qt::ImCursorPosition:
    case Qt::ImAnchorPosition:
        return 0;
    default:
        return QQuickPaintedItem::inputMethodQuery(query);
    }
}

void TerminalItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    d->refit(this);
}

void TerminalItem::Private::refit(TerminalItem *q)
{
    const int cols = qFloor(q->width() / cellWidth);
    const int rows = qFloor(q->height() / cellHeight);
    if (cols < 1 || rows < 1 || (cols == fitCols && rows == fitRows))
        return;
    fitCols = cols;
    fitRows = rows;
    emit q->gridSizeChanged(cols, rows);
}

qreal TerminalItem::fontSize() const
{
    return d->pointSize;
}

void TerminalItem::setFontSize(qreal points)
{
    points = qBound<qreal>(6, points, 36);
    if (qFuzzyCompare(points, d->pointSize))
        return;
    d->pointSize = points;
    d->setupFont();
    // New cells change how many fit; the server's resize message then
    // brings the local terminal to the same size.
    d->refit(this);
    update();
    emit fontSizeChanged();
}

void TerminalItem::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    d->reportFocus(GHOSTTY_FOCUS_GAINED);
    d->blinkOn = true;
    d->blinkTimer.start();
    update();
}

void TerminalItem::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    d->reportFocus(GHOSTTY_FOCUS_LOST);
    d->blinkTimer.stop();
    d->blinkOn = true;
    update();
}

void TerminalItem::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    event->accept();
    const QPointF pos = event->position();
    if (d->pressReported || d->mouseTracking(event->modifiers())) {
        d->pressReported = true;
        d->sendMouse(GHOSTTY_MOUSE_ACTION_PRESS, mapButton(event->button()), event->modifiers(), pos, true);
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        d->paste(QClipboard::Selection);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;

    GhosttyGridRef ref;
    if (!d->gridRefAt(pos, &ref))
        return;
    // The gesture counts clicks from the time and distance between presses,
    // which gives word selection on a double click and line on a triple.
    const GhosttySurfacePosition surface{pos.x(), pos.y()};
    const uint64_t timeNs = uint64_t(event->timestamp()) * 1000000u;
    const uint64_t intervalNs = uint64_t(QGuiApplication::styleHints()->mouseDoubleClickInterval()) * 1000000u;
    const double distance = QGuiApplication::styleHints()->mouseDoubleClickDistance();
    ghostty_selection_gesture_event_set(d->gesturePress, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REF, &ref);
    ghostty_selection_gesture_event_set(d->gesturePress, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_POSITION, &surface);
    ghostty_selection_gesture_event_set(d->gesturePress, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_TIME_NS, &timeNs);
    ghostty_selection_gesture_event_set(d->gesturePress, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REPEAT_INTERVAL_NS,
                                        &intervalNs);
    ghostty_selection_gesture_event_set(d->gesturePress, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REPEAT_DISTANCE,
                                        &distance);
    GhosttySelection selection = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (ghostty_selection_gesture_event(d->gesture, d->term, d->gesturePress, &selection) == GHOSTTY_SUCCESS)
        d->applySelection(&selection);
    else
        d->applySelection(nullptr);
}

void TerminalItem::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    const QPointF pos = event->position();
    if (d->pressReported) {
        GhosttyMouseButton button = GHOSTTY_MOUSE_BUTTON_UNKNOWN;
        for (Qt::MouseButton b : {Qt::LeftButton, Qt::MiddleButton, Qt::RightButton}) {
            if (event->buttons() & b) {
                button = mapButton(b);
                break;
            }
        }
        d->sendMouse(GHOSTTY_MOUSE_ACTION_MOTION, button, event->modifiers(), pos, true);
        return;
    }
    if (!(event->buttons() & Qt::LeftButton))
        return;

    GhosttyGridRef ref;
    if (!d->gridRefAt(pos, &ref))
        return;
    uint16_t cols = 0;
    ghostty_terminal_get(d->term, GHOSTTY_TERMINAL_DATA_COLS, &cols);
    GhosttySelectionGestureGeometry geometry{cols, uint32_t(d->cellWidth), 0, uint32_t(qMax(1.0, height()))};
    const GhosttySurfacePosition surface{pos.x(), pos.y()};
    ghostty_selection_gesture_event_set(d->gestureDrag, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REF, &ref);
    ghostty_selection_gesture_event_set(d->gestureDrag, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_POSITION, &surface);
    ghostty_selection_gesture_event_set(d->gestureDrag, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_GEOMETRY, &geometry);
    GhosttySelection selection = GHOSTTY_INIT_SIZED(GhosttySelection);
    if (ghostty_selection_gesture_event(d->gesture, d->term, d->gestureDrag, &selection) == GHOSTTY_SUCCESS)
        d->applySelection(&selection);
}

void TerminalItem::mouseReleaseEvent(QMouseEvent *event)
{
    event->accept();
    if (d->pressReported) {
        const bool stillPressed = event->buttons() != Qt::NoButton;
        d->sendMouse(GHOSTTY_MOUSE_ACTION_RELEASE, mapButton(event->button()), event->modifiers(), event->position(),
                     stillPressed);
        d->pressReported = stillPressed;
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    GhosttyGridRef ref;
    const bool onCell = d->gridRefAt(event->position(), &ref);
    ghostty_selection_gesture_event_set(d->gestureRelease, GHOSTTY_SELECTION_GESTURE_EVENT_OPT_REF,
                                        onCell ? &ref : nullptr);
    ghostty_selection_gesture_event(d->gesture, d->term, d->gestureRelease, nullptr);
    // Linux users paste a finished selection with the middle button.
    d->copySelection(QClipboard::Selection);
}

// Qt sends this after the second press of a double click, which the gesture
// has already seen as a press, so there is nothing left to do.
void TerminalItem::mouseDoubleClickEvent(QMouseEvent *event)
{
    event->accept();
}

void TerminalItem::hoverMoveEvent(QHoverEvent *event)
{
    // Only any-event tracking reports motion without a button; the encoder
    // drops it for the other modes.
    if (!d->pressReported && d->mouseTracking(event->modifiers()))
        d->sendMouse(GHOSTTY_MOUSE_ACTION_MOTION, GHOSTTY_MOUSE_BUTTON_UNKNOWN, event->modifiers(), event->position(),
                     false);
    event->accept();
}

void TerminalItem::wheelEvent(QWheelEvent *event)
{
    event->accept();
    // Touchpads send many small deltas, so they are added up into notches of
    // 120, the size of one wheel click.
    d->wheelAngle += event->angleDelta().y();
    const int notches = d->wheelAngle / 120;
    d->wheelAngle -= notches * 120;
    if (notches == 0)
        return;

    if (d->mouseTracking(event->modifiers())) {
        // The wheel is reported as presses of buttons 4 (up) and 5 (down).
        const GhosttyMouseButton button = notches > 0 ? GHOSTTY_MOUSE_BUTTON_FOUR : GHOSTTY_MOUSE_BUTTON_FIVE;
        for (int i = 0; i < qAbs(notches); ++i)
            d->sendMouse(GHOSTTY_MOUSE_ACTION_PRESS, button, event->modifiers(), event->position(), false);
        return;
    }

    // Full-screen programs have no scrollback to show, so the wheel moves
    // their cursor instead, three lines a notch, as Ghostty does under
    // alternate scroll mode.
    GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    ghostty_terminal_get(d->term, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN, &screen);
    if (screen == GHOSTTY_TERMINAL_SCREEN_ALTERNATE && modeEnabled(d->term, GHOSTTY_MODE_ALT_SCROLL)) {
        const QByteArray arrow = d->encodeKey(GHOSTTY_KEY_ACTION_PRESS,
                                              notches > 0 ? GHOSTTY_KEY_ARROW_UP : GHOSTTY_KEY_ARROW_DOWN, 0, {}, 0);
        emit input(arrow.repeated(3 * qAbs(notches)));
        return;
    }
    d->scrollViewport(-3 * intptr_t(notches));
}

void TerminalItem::hoverEnterEvent(QHoverEvent *event)
{
    emit pointerEntered();
    QQuickPaintedItem::hoverEnterEvent(event);
}
