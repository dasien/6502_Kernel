#ifndef DISPLAYWIDGET_H
#define DISPLAYWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QString>
#include "VIC.h"
#include "Memory.h"

class DisplayWidget : public QWidget
{
    Q_OBJECT

public:
    // memory is used to read the kernel's tracked cursor position
    // (CURSOR_X/CURSOR_Y) so the on-screen cursor follows where text is typed.
    DisplayWidget(Computer::VIC* video_chip, Computer::Memory* memory, QWidget* parent = nullptr);

    // Display configuration
    void setCharacterSize(int width, int height);
    // Integer zoom: cell = (8*factor) x (16*factor) px, glyphs nearest-neighbor
    // scaled so text stays crisp. factor 1 = native 640x400.
    void setScale(int factor);
    void setBackgroundColor(const QColor& color);
    void setForegroundColor(const QColor& color);
    void setFont(const QFont& font);

    // Refresh control
    void setRefreshRate(int hz);
    void startRefresh() const;
    void stopRefresh() const;

signals:
    void keyPressed(uint8_t ascii_code);
    /// Live control-key state changed; carries the full PIA::kKey* bitmask.
    void keyStateChanged(uint8_t mask);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    // Mouse text selection: drag to select cells, copy to the clipboard on release.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    /// Map a Qt key to its PIA::kKey* bit, or 0 if it isn't a control key.
    static uint8_t controlKeyBit(int qt_key);
    /// Set or clear `bit` in key_state_ and emit keyStateChanged if it moved.
    void updateKeyState(uint8_t bit, bool down);

private slots:
    void refreshDisplay();
    void blinkCursor();

private:
    Computer::VIC* video_chip_;
    Computer::Memory* memory_;
    QTimer* refresh_timer_;
    
    // Display settings
    QFont character_font_;
    QColor background_color_;
    QColor foreground_color_;
    int char_width_;
    int char_height_;
    int refresh_rate_hz_;

    /// Live control-key bitmask mirrored into the PIA's kKeyState register.
    uint8_t key_state_ = 0;
    
    // Cached display state
    bool needs_full_redraw_;
    bool has_focus_;
    
    // Cursor state
    bool show_cursor_;
    QTimer* cursor_timer_;

    // Text-selection state (mouse drag -> copy). Cells are linear indices
    // (row * kScreenWidth + col) in reading order.
    bool has_selection_;
    bool selecting_;
    int  sel_anchor_cell_;
    int  sel_cursor_cell_;
    
    // 16-entry color palette (8 base + 8 bright) for the attribute planes.
    QColor palette_[16];

    // Helper methods
    void setupFont();
    void calculateCharacterSize();
    void initPalette();
    // Resolve a cell's foreground/background QColors from its glyph + attribute
    // byte (handles reverse-video, bright, and the legacy char-bit7 reverse).
    void resolveCellColors(uint8_t glyph, uint8_t attr, QColor& fg, QColor& bg) const;
    // Blit one 8x16 CP437 glyph (from the character ROM) into a cell. `scale` is 1
    // for a normal cell and 2 for one on a double-size row.
    // y_offset is a PIXEL shift applied to this cell only -- never a painter
    // translate, so a future sprite pass cannot inherit the scroll offset.
    void blitGlyph(QPainter& painter, int x, int y, uint8_t glyph,
                   const QColor& fg, const QColor& bg, int scale = 1, int y_offset = 0);
    void drawCharacterAt(QPainter& painter, int x, int y, uint8_t glyph, uint8_t attr,
                         int scale = 1, int y_offset = 0);
    // Sprites: pixel-positioned, transparent background, drawn OVER the cell planes
    // and deliberately outside the scroll region's offset and clip -- not moving with
    // the region is the entire reason they exist.
    void drawSprites(QPainter& painter);
    void drawCursor(QPainter& painter);
    uint8_t qtKeyToAscii(QKeyEvent* event) const;
    // Selection helpers.
    int  cellIndexAt(const QPoint& pos) const;   // pixel -> clamped cell index
    bool cellSelected(int x, int y) const;       // is cell (x,y) in the selection?
    QString selectionText() const;               // selected cells -> text (CP437->Unicode)
};

#endif // DISPLAYWIDGET_H