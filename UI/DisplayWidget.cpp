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
    , char_width_(8)
    , char_height_(16)
    , refresh_rate_hz_(60)
    , needs_full_redraw_(true)
    , has_focus_(false)
    , show_cursor_(false)
    , cursor_timer_(new QTimer(this))
{
    setupFont();
    calculateCharacterSize();
    
    // Set widget size based on character dimensions
    int widget_width = Computer::VIC::kScreenWidth * char_width_;
    int widget_height = Computer::VIC::kScreenHeight * char_height_;
    //setFixedSize(widget_width, widget_height);
    setFixedSize(440, 400);

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

void DisplayWidget::setCharacterSize(int width, int height)
{
    char_width_ = width;
    char_height_ = height;
    
    int widget_width = Computer::VIC::kScreenWidth * char_width_;
    int widget_height = Computer::VIC::kScreenHeight * char_height_;
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

void DisplayWidget::setRefreshRate(int hz)
{
    refresh_rate_hz_ = hz;
    if (refresh_timer_->isActive())
    {
        refresh_timer_->setInterval(1000 / refresh_rate_hz_);
    }
}

void DisplayWidget::startRefresh()
{
    refresh_timer_->start(1000 / refresh_rate_hz_);
}

void DisplayWidget::stopRefresh()
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
    
    // Draw all characters
    const auto& screen_buffer = video_chip_->getScreenBuffer();
    
    for (int y = 0; y < Computer::VIC::kScreenHeight; ++y)
    {
        for (int x = 0; x < Computer::VIC::kScreenWidth; ++x)
        {
            uint8_t character = video_chip_->getCharacterAt(x, y);
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
    // Set up a monospace font suitable for terminal display
    // Scale font size to match the 2x larger character cells
    character_font_ = QFont("Courier New", 14);
    character_font_.setFixedPitch(true);
    character_font_.setStyleHint(QFont::TypeWriter);
}

void DisplayWidget::calculateCharacterSize()
{
    // Keep our explicitly set 16x16 pixel grid - don't let font metrics override it
    // This ensures we maintain the intended 640x400 display dimensions
    // (40 columns × 16 pixels = 640, 25 rows × 16 pixels = 400)
    
    // char_width_ and char_height_ are already set in constructor - don't change them!
    // This creates a true square pixel grid for that classic retro computer look
}

QChar DisplayWidget::asciiToChar(uint8_t ascii_code) const
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

void DisplayWidget::drawCharacterAt(QPainter& painter, int x, int y, uint8_t character)
{
    QChar ch = asciiToChar(character);
    
    int pixel_x = x * char_width_;
    int pixel_y = y * char_height_ + char_height_ - 2; // Adjust for font baseline
    
    painter.drawText(pixel_x, pixel_y, QString(ch));
}

void DisplayWidget::drawCursor(QPainter& painter)
{
    // Simple cursor at bottom-right of display (where input would appear)
    // In a real implementation, cursor position would be tracked by the 6502 system
    int cursor_x = 0; // Column 0 for now
    int cursor_y = 24; // Bottom row
    
    int pixel_x = cursor_x * char_width_;
    int pixel_y = (cursor_y + 1) * char_height_ - 2;
    
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
    
    uint8_t ascii_code = qtKeyToAscii(event);
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
    int key = event->key();
    Qt::KeyboardModifiers modifiers = event->modifiers();
    QString text = event->text();
    
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
        uint8_t ascii = static_cast<uint8_t>(key);
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