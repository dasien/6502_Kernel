#include "DisplayWidget.h"
#include "Cp437Font.h"
#include <QImage>
#include <QPainter>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <algorithm>
#include <cstdio>

// CP437 -> Unicode for the high half (0x80-0xFF), so selected box-drawing,
// accented, and symbol glyphs copy to the clipboard faithfully. 0x20-0x7E are
// plain ASCII; control codes copy as spaces.
static const unsigned short kCp437ToUnicode[128] = {
    0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
    0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
    0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
    0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
    0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
    0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
    0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
    0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0,
};

DisplayWidget::DisplayWidget(Computer::VIC* video_chip, Computer::Memory* memory, QWidget* parent)
    : QWidget(parent)
    , video_chip_(video_chip)
    , memory_(memory)
    , refresh_timer_(new QTimer(this))
    , background_color_(Qt::black)
    , foreground_color_(Qt::green)
    , char_width_(8)    // 80 cols × 8px = 640 (classic VGA 80×25 text geometry)
    , char_height_(16)  // 25 rows × 16px = 400
    , refresh_rate_hz_(60)
    , needs_full_redraw_(true)
    , has_focus_(false)
    , show_cursor_(false)
    , cursor_timer_(new QTimer(this))
    , has_selection_(false)
    , selecting_(false)
    , sel_anchor_cell_(0)
    , sel_cursor_cell_(0)
{
    initPalette();
    // Glyphs render from the embedded CP437 character ROM (no QFont).
    calculateCharacterSize();

    // Set initial widget size based on character dimensions
    const int widget_width = Computer::VIC::kScreenWidth * char_width_;
    const int widget_height = Computer::VIC::kScreenHeight * char_height_;
    setFixedSize(widget_width, widget_height);

    // Setup refresh timer
    connect(refresh_timer_, &QTimer::timeout, this, &DisplayWidget::refreshDisplay);
    setRefreshRate(refresh_rate_hz_);
    
    // Setup cursor blink timer (~500ms). The cursor is only painted while the
    // widget has focus (see blinkCursor / paintEvent).
    connect(cursor_timer_, &QTimer::timeout, this, &DisplayWidget::blinkCursor);
    cursor_timer_->start(500);
    
    // Widget properties
    setAutoFillBackground(true);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, background_color_);
    setPalette(palette);
    
    // Enable keyboard input
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_KeyCompression, false);
}

void DisplayWidget::setCharacterSize(const int width, const int height)
{
    char_width_ = width;
    char_height_ = height;

    const int widget_width = Computer::VIC::kScreenWidth * char_width_;
    const int widget_height = Computer::VIC::kScreenHeight * char_height_;
    setFixedSize(widget_width, widget_height);
    
    needs_full_redraw_ = true;
    update();
}

void DisplayWidget::setBackgroundColor(const QColor& color)
{
    background_color_ = color;
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, background_color_);
    setPalette(palette);
    needs_full_redraw_ = true;
    update();
}

void DisplayWidget::setForegroundColor(const QColor& color)
{
    foreground_color_ = color;
    needs_full_redraw_ = true;
    update();
}

void DisplayWidget::setFont(const QFont& font)
{
    character_font_ = font;
    calculateCharacterSize();
    needs_full_redraw_ = true;
    update();
}

void DisplayWidget::setRefreshRate(const int hz)
{
    refresh_rate_hz_ = hz;
    if (refresh_timer_->isActive())
    {
        refresh_timer_->setInterval(1000 / refresh_rate_hz_);
    }
}

void DisplayWidget::startRefresh() const
{
    refresh_timer_->start(1000 / refresh_rate_hz_);
}

void DisplayWidget::stopRefresh() const
{
    refresh_timer_->stop();
}

void DisplayWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    
    if (!video_chip_)
    {
        return;
    }
    
    QPainter painter(this);
    painter.fillRect(rect(), background_color_);

    // Glyphs are blitted from the CP437 character ROM (see drawCharacterAt);
    // no QFont is involved.
    for (int y = 0; y < Computer::VIC::kScreenHeight; ++y)
    {
        for (int x = 0; x < Computer::VIC::kScreenWidth; ++x)
        {
            const uint8_t character = video_chip_->getCharacterAt(x, y);
            const uint8_t attr = video_chip_->getColorAt(x, y);
            drawCharacterAt(painter, x, y, character, attr);
        }
    }
    
    // Overlay the text selection (translucent, so the glyphs show through).
    if (has_selection_)
    {
        for (int y = 0; y < Computer::VIC::kScreenHeight; ++y)
            for (int x = 0; x < Computer::VIC::kScreenWidth; ++x)
                if (cellSelected(x, y))
                    painter.fillRect(x * char_width_, y * char_height_,
                                     char_width_, char_height_, QColor(120, 160, 255, 96));
    }

    // Draw cursor if widget has focus
    if (has_focus_ && show_cursor_)
    {
        drawCursor(painter);
    }
    
    needs_full_redraw_ = false;
}

void DisplayWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)
    needs_full_redraw_ = true;
}

void DisplayWidget::refreshDisplay()
{
    if (video_chip_ && (video_chip_->isDirty() || needs_full_redraw_))
    {
        update();
        video_chip_->clearDirty();
    }
}

void DisplayWidget::setupFont()
{
    // Period-appropriate 1980s computer font for authentic 6502 system feel
    // Try period-appropriate fonts in order of preference
    QStringList period_fonts = {
        "Monaco",           // Classic 80s Mac terminal font
        "Menlo",            // Modern Monaco variant
        "Liberation Mono",  // Classic terminal style
        "DejaVu Sans Mono", // Clean 80s terminal feel
        "IBM Plex Mono",    // Based on IBM computer fonts
        "Consolas",         // Microsoft's terminal font
        "Courier New"       // Final fallback
    };

    // Try each font until we find one that's available
    bool font_found = false;
    QString selected_font = "none";
    for (const QString& font_name : period_fonts) {
        character_font_ = QFont(font_name, 14);
        QFontInfo font_info(character_font_);
        if (font_info.exactMatch()) {
            font_found = true;
            selected_font = font_name;
            printf("DisplayWidget: Using period-appropriate font: %s\n", font_name.toStdString().c_str());
            break;
        } else {
            printf("DisplayWidget: Font not available: %s (actual: %s)\n",
                   font_name.toStdString().c_str(),
                   font_info.family().toStdString().c_str());
        }
    }

    // Final fallback to system monospace if none found
    if (!font_found) {
        character_font_ = QFont("monospace", 14);
        character_font_.setStyleHint(QFont::TypeWriter);
        QFontInfo fallback_info(character_font_);
        selected_font = fallback_info.family();
        printf("DisplayWidget: Using system fallback font: %s\n", selected_font.toStdString().c_str());
    }

    character_font_.setFixedPitch(true);
    character_font_.setStyleHint(QFont::TypeWriter);
    character_font_.setPixelSize(14); // Sized for the 8x16px character cells (80x25)
    character_font_.setWeight(QFont::Normal); // Keep normal weight for authentic feel
}

void DisplayWidget::calculateCharacterSize()
{
    // Keep the explicitly set 8x16 pixel grid - don't let font metrics override it.
    // This maintains the intended 640x400 display (80 cols × 8px = 640,
    // 25 rows × 16px = 400 -- classic VGA 80x25 text geometry).
    // char_width_ and char_height_ are set in the constructor - don't change them.
}

void DisplayWidget::initPalette()
{
    // Standard 8-color ANSI palette (indices 0-7) plus bright variants (8-15).
    // The attribute byte selects fg (bits 0-2) and bg (bits 3-5); bit 6 brightens
    // the foreground. Index 2 (green) is the system default to match the classic
    // green-on-black look.
    palette_[0]  = QColor(0,   0,   0);   // black
    palette_[1]  = QColor(170, 0,   0);   // red
    palette_[2]  = QColor(0,   170, 0);   // green
    palette_[3]  = QColor(170, 85,  0);   // yellow/brown
    palette_[4]  = QColor(0,   0,   170); // blue
    palette_[5]  = QColor(170, 0,   170); // magenta
    palette_[6]  = QColor(0,   170, 170); // cyan
    palette_[7]  = QColor(170, 170, 170); // white/gray
    palette_[8]  = QColor(85,  85,  85);  // bright black (gray)
    palette_[9]  = QColor(255, 85,  85);  // bright red
    palette_[10] = QColor(85,  255, 85);  // bright green
    palette_[11] = QColor(255, 255, 85);  // bright yellow
    palette_[12] = QColor(85,  85,  255); // bright blue
    palette_[13] = QColor(255, 85,  255); // bright magenta
    palette_[14] = QColor(85,  255, 255); // bright cyan
    palette_[15] = QColor(255, 255, 255); // bright white
}

void DisplayWidget::resolveCellColors(const uint8_t glyph, const uint8_t attr,
                                      QColor& fg, QColor& bg) const
{
    Q_UNUSED(glyph) // the glyph is a full 8-bit CP437 code point now; all
                    // styling (reverse/bright/color) lives in the attribute.
    const bool reverse = (attr & Computer::VIC::kAttrReverse) != 0;

    if (attr == Computer::VIC::kDefaultAttr)
    {
        // Exact default attribute: use the configured colors so the display is
        // pixel-identical to the pre-color era (green on black).
        fg = foreground_color_;
        bg = background_color_;
        return;
    }

    const int fg_index = (attr & Computer::VIC::kAttrFgMask) +
                         ((attr & Computer::VIC::kAttrBright) ? 8 : 0);
    const int bg_index = (attr & Computer::VIC::kAttrBgMask) >> Computer::VIC::kAttrBgShift;
    fg = palette_[fg_index & 0x0F];
    bg = palette_[bg_index & 0x07];

    if (reverse)
    {
        const QColor tmp = fg;
        fg = bg;
        bg = tmp;
    }
}

// Blit one 8x16 glyph from the CP437 character ROM into a cell: each scanline
// byte's bits select fg (1) or bg (0). The cell is exactly 8x16, so the image
// maps 1:1 (no scaling).
void DisplayWidget::blitGlyph(QPainter& painter, const int x, const int y,
                              const uint8_t glyph, const QColor& fg, const QColor& bg)
{
    const QRgb fg_rgb = fg.rgb();
    const QRgb bg_rgb = bg.rgb();
    const uint8_t* rows = &Computer::kCp437Font[glyph * 16];

    QImage img(8, 16, QImage::Format_RGB32);
    for (int r = 0; r < 16; ++r)
    {
        const uint8_t bits = rows[r];
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(r));
        for (int c = 0; c < 8; ++c)
            line[c] = (bits & (0x80 >> c)) ? fg_rgb : bg_rgb;
    }
    painter.drawImage(x * char_width_, y * char_height_, img);
}

void DisplayWidget::drawCharacterAt(QPainter& painter, const int x, const int y,
                                    const uint8_t glyph, const uint8_t attr)
{
    QColor fg, bg;
    resolveCellColors(glyph, attr, fg, bg);
    blitGlyph(painter, x, y, glyph, fg, bg);
}

void DisplayWidget::drawCursor(QPainter& painter)
{
    // The VIC owns the hardware cursor: the kernel pushes its cell to VREG_CURSOR
    // on every PRINT_CHAR, and full-screen programs (the editor) can hide it. The
    // cell is a linear 0..1999 index; bit15 of the high byte means hidden.
    uint16_t cursor_index = 0;
    bool hidden = false;
    video_chip_->getCursorCell(cursor_index, hidden);
    if (hidden || cursor_index >= Computer::VIC::kScreenSize)
    {
        return;
    }

    const int cursor_x = cursor_index % Computer::VIC::kScreenWidth;
    const int cursor_y = cursor_index / Computer::VIC::kScreenWidth;

    // Block cursor: re-blit the cell's glyph with foreground/background swapped
    // (inverse video). Blink toggles it, so the character under it stays
    // readable. Colors come from the cell's own attribute.
    const uint8_t glyph = video_chip_->getCharacterAt(cursor_x, cursor_y);
    const uint8_t attr = video_chip_->getColorAt(cursor_x, cursor_y);
    QColor fg, bg;
    resolveCellColors(glyph, attr, fg, bg);
    blitGlyph(painter, cursor_x, cursor_y, glyph, bg, fg); // swapped = inverse
}

void DisplayWidget::keyPressEvent(QKeyEvent* event)
{
    if (!event)
    {
        return;
    }

    // Navigation keys are sent as ANSI escape sequences (ESC [ X), which a
    // full-screen program decodes; this keeps them distinct from the single
    // control bytes. Each byte is delivered as a separate keypress.
    const char* seq = nullptr;
    switch (event->key())
    {
        case Qt::Key_Up:       seq = "\x1b[A"; break;
        case Qt::Key_Down:     seq = "\x1b[B"; break;
        case Qt::Key_Right:    seq = "\x1b[C"; break;
        case Qt::Key_Left:     seq = "\x1b[D"; break;
        case Qt::Key_Home:     seq = "\x1b[H"; break;
        case Qt::Key_End:      seq = "\x1b[F"; break;
        case Qt::Key_PageUp:   seq = "\x1b[5~"; break;
        case Qt::Key_PageDown: seq = "\x1b[6~"; break;
        default: break;
    }
    if (seq)
    {
        for (const char* p = seq; *p; ++p) emit keyPressed(static_cast<uint8_t>(*p));
        QWidget::keyPressEvent(event);
        return;
    }

    // Ctrl+letter -> control byte 0x01..0x1A (for editor commands).
    if ((event->modifiers() & Qt::ControlModifier) &&
        event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z)
    {
        emit keyPressed(static_cast<uint8_t>(event->key() - Qt::Key_A + 1));
        QWidget::keyPressEvent(event);
        return;
    }

    const uint8_t ascii_code = qtKeyToAscii(event);
    if (ascii_code != 0)
    {
        emit keyPressed(ascii_code);
    }

    QWidget::keyPressEvent(event);
}

void DisplayWidget::focusInEvent(QFocusEvent* event)
{
    has_focus_ = true;
    show_cursor_ = true; // show immediately on focus; blink toggles it thereafter
    update();
    QWidget::focusInEvent(event);
}

void DisplayWidget::focusOutEvent(QFocusEvent* event)
{
    has_focus_ = false;
    show_cursor_ = false;
    update();
    QWidget::focusOutEvent(event);
}

// ---- text selection + clipboard copy --------------------------------------

// Map a pixel position to a clamped cell index (row * width + col).
int DisplayWidget::cellIndexAt(const QPoint& pos) const
{
    int x = pos.x() / char_width_;
    int y = pos.y() / char_height_;
    x = std::clamp(x, 0, Computer::VIC::kScreenWidth - 1);
    y = std::clamp(y, 0, Computer::VIC::kScreenHeight - 1);
    return y * Computer::VIC::kScreenWidth + x;
}

// A cell is selected if its linear index lies within the anchor..cursor span
// (reading order), i.e. a terminal-style linear selection that wraps rows.
bool DisplayWidget::cellSelected(int x, int y) const
{
    if (!has_selection_) return false;
    const int idx = y * Computer::VIC::kScreenWidth + x;
    const int lo = std::min(sel_anchor_cell_, sel_cursor_cell_);
    const int hi = std::max(sel_anchor_cell_, sel_cursor_cell_);
    return idx >= lo && idx <= hi;
}

// Build the selected text: each row's selected columns, trailing spaces trimmed,
// rows joined by '\n'. CP437 high glyphs map to Unicode; controls become spaces.
QString DisplayWidget::selectionText() const
{
    if (!has_selection_ || !video_chip_) return QString();
    const int W = Computer::VIC::kScreenWidth;
    const int lo = std::min(sel_anchor_cell_, sel_cursor_cell_);
    const int hi = std::max(sel_anchor_cell_, sel_cursor_cell_);
    const int r0 = lo / W, r1 = hi / W;
    QString out;
    for (int r = r0; r <= r1; ++r)
    {
        const int c0 = (r == r0) ? (lo % W) : 0;
        const int c1 = (r == r1) ? (hi % W) : (W - 1);
        QString row;
        for (int c = c0; c <= c1; ++c)
        {
            const uint8_t g = video_chip_->getCharacterAt(c, r);
            if (g >= 0x20 && g < 0x7F)      row.append(QChar(g));
            else if (g >= 0x80)             row.append(QChar(kCp437ToUnicode[g - 0x80]));
            else                            row.append(QChar(' '));
        }
        while (row.endsWith(' ')) row.chop(1);   // trim trailing spaces
        if (r != r0) out.append('\n');
        out.append(row);
    }
    return out;
}

void DisplayWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) { QWidget::mousePressEvent(event); return; }
    selecting_ = true;
    has_selection_ = true;
    sel_anchor_cell_ = sel_cursor_cell_ = cellIndexAt(event->pos());
    update();
}

void DisplayWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!selecting_) { QWidget::mouseMoveEvent(event); return; }
    sel_cursor_cell_ = cellIndexAt(event->pos());
    update();
}

void DisplayWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!selecting_) { QWidget::mouseReleaseEvent(event); return; }
    selecting_ = false;
    sel_cursor_cell_ = cellIndexAt(event->pos());
    if (sel_anchor_cell_ == sel_cursor_cell_)
    {
        has_selection_ = false;              // a plain click clears any selection
    }
    else
    {
        const QString text = selectionText();
        if (!text.isEmpty()) QGuiApplication::clipboard()->setText(text);
    }
    update();
}

void DisplayWidget::blinkCursor()
{
    if (has_focus_)
    {
        show_cursor_ = !show_cursor_;
        update();
    }
}

uint8_t DisplayWidget::qtKeyToAscii(QKeyEvent* event) const
{
    const int key = event->key();
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const QString text = event->text();
    
    // Filter out modifier keys that should be ignored
    switch (key)
    {
        case Qt::Key_Shift:
        case Qt::Key_Control:
        case Qt::Key_Alt:
        case Qt::Key_Meta:
        case Qt::Key_AltGr:
        case Qt::Key_CapsLock:
        case Qt::Key_NumLock:
        case Qt::Key_ScrollLock:
            return 0; // Ignore modifier keys
        default:
            break;
    }
    
    // Handle special keys
    switch (key)
    {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            return 0x0D; // Carriage Return
        case Qt::Key_Backspace:
            return 0x08; // Backspace
        case Qt::Key_Tab:
            return 0x09; // Tab
        case Qt::Key_Escape:
            return 0x1B; // Escape
        case Qt::Key_Space:
            return 0x20; // Space
        case Qt::Key_Delete:
            return 0x7F; // Delete
        // Cursor/navigation keys are intentionally not mapped: nothing in the
        // system uses them yet, and the old 0x11-0x14 codes collided with the
        // Ctrl-Q/R/S/T control bytes. A future full-screen editor will define a
        // deliberate scheme (likely ESC sequences) when it needs them.
        default:
            break;
    }
    
    // Handle printable characters from text() first (handles shift modifiers automatically)
    if (!text.isEmpty() && text.length() == 1)
    {
        QChar ch = text.at(0);
        if (ch.unicode() > 0 && ch.unicode() <= 0x7F) // Valid ASCII range
        {
            return static_cast<uint8_t>(ch.unicode());
        }
    }
    
    // Fallback for keys that don't produce text but should be handled
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
    {
        auto ascii = static_cast<uint8_t>(key);
        if (!(modifiers & Qt::ShiftModifier))
        {
            ascii += 32; // Convert to lowercase
        }
        return ascii;
    }
    
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
    {
        return static_cast<uint8_t>(key);
    }
    
    return 0; // Unknown key
}