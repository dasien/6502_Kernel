#include "DisplayWidget.h"
#include <QPainter>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <cstdio>

DisplayWidget::DisplayWidget(Computer::VIC* video_chip, QWidget* parent)
    : QWidget(parent)
    , video_chip_(video_chip)
    , refresh_timer_(new QTimer(this))
    , background_color_(Qt::black)
    , foreground_color_(Qt::green)
    , char_width_(11)   // Significantly reduced for much tighter character spacing
    , char_height_(19)  // 25 rows × 19px = 475px (close to 480px for 4:3 aspect ratio)
    , refresh_rate_hz_(60)
    , needs_full_redraw_(true)
    , has_focus_(false)
    , show_cursor_(false)
    , cursor_timer_(new QTimer(this))
{
    setupFont();
    calculateCharacterSize();

    // Set initial widget size based on character dimensions
    const int widget_width = Computer::VIC::kScreenWidth * char_width_;
    const int widget_height = Computer::VIC::kScreenHeight * char_height_;
    setFixedSize(widget_width, widget_height);

    // Setup refresh timer
    connect(refresh_timer_, &QTimer::timeout, this, &DisplayWidget::refreshDisplay);
    setRefreshRate(refresh_rate_hz_);
    
    // Setup cursor blink timer (disabled)
    connect(cursor_timer_, &QTimer::timeout, this, &DisplayWidget::blinkCursor);
    // cursor_timer_->start(500); // Cursor disabled - comment out to remove blinking
    
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
    
    // Set up painter for character rendering
    painter.setFont(character_font_);
    painter.setPen(foreground_color_);
    
    for (int y = 0; y < Computer::VIC::kScreenHeight; ++y)
    {
        for (int x = 0; x < Computer::VIC::kScreenWidth; ++x)
        {
            const uint8_t character = video_chip_->getCharacterAt(x, y);
            if (character != 0x00) // Don't draw null characters
            {
                drawCharacterAt(painter, x, y, character);
            }
        }
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
    character_font_.setPixelSize(17); // Properly sized for 11x19px character cells
    character_font_.setWeight(QFont::Normal); // Keep normal weight for authentic feel
}

void DisplayWidget::calculateCharacterSize()
{
    // Keep our explicitly set 11x19 pixel grid - don't let font metrics override it
    // This ensures we maintain the intended 440x475 display dimensions (very tight char spacing)
    // (40 columns × 11 pixels = 440, 25 rows × 19 pixels = 475)

    // char_width_ and char_height_ are already set in constructor - don't change them!
    // Dramatically reduced char_width_ from 16 to 11 pixels for much tighter character spacing
}

QChar DisplayWidget::asciiToChar(const uint8_t ascii_code) const
{
    // Handle standard ASCII printable characters
    if (ascii_code >= 0x20 && ascii_code <= 0x7E)
    {
        return QChar(ascii_code);
    }
    
    // Handle some common non-printable characters
    switch (ascii_code)
    {
        case 0x00: return QChar(' '); // Null -> space
        case 0x0A: return QChar(' '); // LF -> space (should be handled by display logic)
        case 0x0D: return QChar(' '); // CR -> space (should be handled by display logic)
        default: return QChar(0x2592); // Medium shade block for unknown characters
    }
}

void DisplayWidget::drawCharacterAt(QPainter& painter, const int x, const int y, const uint8_t character)
{
    const QChar ch = asciiToChar(character);

    const int pixel_x = x * char_width_;

    // Better baseline calculation for larger font - center vertically in the cell
    const QFontMetrics metrics(character_font_);
    const int font_height = metrics.height();
    const int font_ascent = metrics.ascent();
    const int vertical_offset = (char_height_ - font_height) / 2 + font_ascent;
    const int pixel_y = y * char_height_ + vertical_offset;

    painter.drawText(pixel_x, pixel_y, QString(ch));
}

void DisplayWidget::drawCursor(QPainter& painter)
{
    // Simple cursor at bottom-right of display (where input would appear)
    // In a real implementation, cursor position would be tracked by the 6502 system
    const int cursor_x = 0; // Column 0 for now
    const int cursor_y = 24; // Bottom row

    const int pixel_x = cursor_x * char_width_;
    const int pixel_y = (cursor_y + 1) * char_height_ - 2;
    
    // Draw a simple underscore cursor
    painter.setPen(foreground_color_);
    painter.drawLine(pixel_x, pixel_y, pixel_x + char_width_ - 1, pixel_y);
}

void DisplayWidget::keyPressEvent(QKeyEvent* event)
{
    if (!event)
    {
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
    show_cursor_ = false; // Keep cursor disabled even on focus
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
        case Qt::Key_Up:
            return 0x11; // Cursor up (for potential monitor navigation)
        case Qt::Key_Down:
            return 0x12; // Cursor down
        case Qt::Key_Left:
            return 0x13; // Cursor left
        case Qt::Key_Right:
            return 0x14; // Cursor right
        case Qt::Key_Home:
            return 0x19; // Home
        case Qt::Key_Delete:
            return 0x7F; // Delete
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