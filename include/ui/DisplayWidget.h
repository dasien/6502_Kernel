#ifndef DISPLAYWIDGET_H
#define DISPLAYWIDGET_H

#include <QWidget>
#include <QTimer>
#include <QFont>
#include <QPainter>
#include <QPaintEvent>
#include <QKeyEvent>
#include "VIC.h"

class DisplayWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DisplayWidget(Computer::VIC* video_chip, QWidget* parent = nullptr);

    // Display configuration
    void setCharacterSize(int width, int height);
    void setBackgroundColor(const QColor& color);
    void setForegroundColor(const QColor& color);
    void setFont(const QFont& font);

    // Refresh control
    void setRefreshRate(int hz);
    void startRefresh() const;
    void stopRefresh() const;

signals:
    void keyPressed(uint8_t ascii_code);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private slots:
    void refreshDisplay();
    void blinkCursor();

private:
    Computer::VIC* video_chip_;
    QTimer* refresh_timer_;
    
    // Display settings
    QFont character_font_;
    QColor background_color_;
    QColor foreground_color_;
    int char_width_;
    int char_height_;
    int refresh_rate_hz_;
    
    // Cached display state
    bool needs_full_redraw_;
    bool has_focus_;
    
    // Cursor state
    bool show_cursor_;
    QTimer* cursor_timer_;
    
    // Helper methods
    void setupFont();
    void calculateCharacterSize();
    QChar asciiToChar(uint8_t ascii_code) const;
    void drawCharacterAt(QPainter& painter, int x, int y, uint8_t character);
    void drawCursor(QPainter& painter);
    uint8_t qtKeyToAscii(QKeyEvent* event) const;
};

#endif // DISPLAYWIDGET_H