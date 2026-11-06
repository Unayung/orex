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
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QPainter>
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

} // namespace

struct TerminalItem::Private {
    GhosttyTerminal term = nullptr;
    GhosttyRenderState render = nullptr;
    GhosttyRenderStateRowIterator rowIter = nullptr;
    GhosttyRenderStateRowCells rowCells = nullptr;
    GhosttyKeyEncoder encoder = nullptr;
    GhosttyKeyEvent keyEvent = nullptr;

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

    void setupFont();
    void scheduleRepaint(TerminalItem *q);
    void readCell(int x, const GhosttyRenderStateColors &colors);
    void drawLine(QPainter *p, int y);
    void sendKey(TerminalItem *q, QKeyEvent *e, GhosttyKeyAction action);
    void paste(TerminalItem *q);
    void reportFocus(TerminalItem *q, GhosttyFocusEvent event);
};

void TerminalItem::Private::setupFont()
{
    QFont base = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    base.setPointSizeF(12);
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

void TerminalItem::Private::scheduleRepaint(TerminalItem *q)
{
    if (repaintQueued)
        return;
    repaintQueued = true;
    q->update();
    // The input method places its candidate window from the cursor
    // rectangle, which moves with program output.
    if (q->hasActiveFocus())
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
}

void TerminalItem::Private::readCell(int x, const GhosttyRenderStateColors &colors)
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

void TerminalItem::Private::sendKey(TerminalItem *q, QKeyEvent *e, GhosttyKeyAction action)
{
    const KeyMapping mapping = mapKey(e->key(), e->modifiers() & Qt::KeypadModifier);
    const GhosttyMods mods = mapMods(e->modifiers());
    const QString text = isPrintable(e->text()) ? e->text() : QString();
    const QByteArray utf8 = text.toUtf8();

    char32_t unshifted = mapping.unshifted;
    if (!unshifted && !text.isEmpty())
        unshifted = text.toLower().toUcs4().value(0);

    ghostty_key_event_set_action(keyEvent, action);
    ghostty_key_event_set_key(keyEvent, mapping.key);
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
        if (r == GHOSTTY_SUCCESS && len > 0)
            emit q->input(big.left(qsizetype(len)));
        return;
    }
    if (r == GHOSTTY_SUCCESS && len > 0)
        emit q->input(QByteArray(buf, qsizetype(len)));
}

void TerminalItem::Private::paste(TerminalItem *q)
{
    QByteArray data = QGuiApplication::clipboard()->text().toUtf8();
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
    if (r == GHOSTTY_SUCCESS && written > 0)
        emit q->input(out.left(qsizetype(written)));
}

void TerminalItem::Private::reportFocus(TerminalItem *q, GhosttyFocusEvent event)
{
    if (!modeEnabled(term, GHOSTTY_MODE_FOCUS_EVENT))
        return;
    char buf[8];
    size_t len = 0;
    if (ghostty_focus_encode(event, buf, sizeof buf, &len) == GHOSTTY_SUCCESS && len > 0)
        emit q->input(QByteArray(buf, qsizetype(len)));
}

TerminalItem::TerminalItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , d(new Private)
{
    setFlag(ItemAcceptsInputMethod, true);
    setOpaquePainting(true);

    // Until a snapshot arrives, output still needs somewhere to go.
    ghostty_terminal_new(nullptr, &d->term, 80, 24);
    ghostty_render_state_new(nullptr, &d->render);
    ghostty_render_state_row_iterator_new(nullptr, &d->rowIter);
    ghostty_render_state_row_cells_new(nullptr, &d->rowCells);
    ghostty_key_encoder_new(nullptr, &d->encoder);
    ghostty_key_event_new(nullptr, &d->keyEvent);
    d->setupFont();

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

    // The render state is built for one terminal and keeps per-row data from
    // it, so it is replaced along with the terminal.
    GhosttyRenderState render = nullptr;
    ghostty_render_state_new(nullptr, &render);
    ghostty_render_state_free(d->render);
    d->render = render;
    ghostty_terminal_free(d->term);
    d->term = restored;

    // The caller can't know where the snapshot ends without decoding it, so
    // any live bytes that came in the same buffer are applied here.
    if (end < size_t(snapshot.size()))
        ghostty_terminal_vt_write(d->term, bytes + end, size_t(snapshot.size()) - end);
    d->scheduleRepaint(this);
    return true;
}

void TerminalItem::feed(const QByteArray &vt)
{
    ghostty_terminal_vt_write(d->term, reinterpret_cast<const uint8_t *>(vt.constData()), size_t(vt.size()));
    d->scheduleRepaint(this);
}

void TerminalItem::resizeGrid(int cols, int rows)
{
    if (cols < 1 || rows < 1 || cols > UINT16_MAX || rows > UINT16_MAX)
        return;
    ghostty_terminal_resize(d->term, uint16_t(cols), uint16_t(rows), uint32_t(d->cellWidth),
                            uint32_t(d->cellHeight));
    d->scheduleRepaint(this);
}

// Qt Quick calls this during the sync phase, with the GUI thread blocked, so
// reading the terminal here does not race with feed().
void TerminalItem::paint(QPainter *painter)
{
    d->repaintQueued = false;
    if (ghostty_render_state_update(d->render, d->term) != GHOSTTY_SUCCESS)
        return;

    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    ghostty_render_state_get(d->render, GHOSTTY_RENDER_STATE_DATA_COLORS, &colors);
    GhosttyRenderStateCursor cursor = GHOSTTY_INIT_SIZED(GhosttyRenderStateCursor);
    ghostty_render_state_get(d->render, GHOSTTY_RENDER_STATE_DATA_CURSOR, &cursor);

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
        d->line.clear();
        int x = 0;
        while (ghostty_render_state_row_cells_next(d->rowCells))
            d->readCell(x++, colors);
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
    if (!showCursor)
        return;

    const QColor cursorColor = colors.cursor_has_value ? toColor(colors.cursor) : defaultFg;
    const QRectF cell(cursorX * cw, top, cursorCell.width * cw, ch);
    auto style = cursor.visual_style;
    if (!hasActiveFocus())
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
    // Shift+Insert pastes the clipboard too, as Ghostty does: voxtype types
    // dictation by putting it on the clipboard and pressing Shift+Insert.
    if ((event->key() == Qt::Key_V && mods == (Qt::ControlModifier | Qt::ShiftModifier)) ||
        (event->key() == Qt::Key_Insert && mods == Qt::ShiftModifier)) {
        d->paste(this);
        event->accept();
        return;
    }
    d->sendKey(this, event, event->isAutoRepeat() ? GHOSTTY_KEY_ACTION_REPEAT : GHOSTTY_KEY_ACTION_PRESS);
    // Every key belongs to the program, including ones that encode to
    // nothing, so none of them travel on to parent items.
    event->accept();
}

void TerminalItem::keyReleaseEvent(QKeyEvent *event)
{
    // Auto-repeat arrives as release and press pairs; only the press half
    // means anything, and the encoder reports it as a repeat.
    if (!event->isAutoRepeat())
        d->sendKey(this, event, GHOSTTY_KEY_ACTION_RELEASE);
    event->accept();
}

void TerminalItem::inputMethodEvent(QInputMethodEvent *event)
{
    // Committed text goes to the program as typed, the way a terminal sends
    // any other text.
    if (!event->commitString().isEmpty())
        emit input(event->commitString().toUtf8());
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
    const int cols = qFloor(newGeometry.width() / d->cellWidth);
    const int rows = qFloor(newGeometry.height() / d->cellHeight);
    if (cols < 1 || rows < 1 || (cols == d->fitCols && rows == d->fitRows))
        return;
    d->fitCols = cols;
    d->fitRows = rows;
    emit gridSizeChanged(cols, rows);
}

void TerminalItem::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    d->reportFocus(this, GHOSTTY_FOCUS_GAINED);
    update();
}

void TerminalItem::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    d->reportFocus(this, GHOSTTY_FOCUS_LOST);
    update();
}
