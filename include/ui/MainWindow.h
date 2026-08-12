#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QElapsedTimer>
#include <QTimer>
#include "Computer6502.h"
#include "DisplayWidget.h"
#include "Modem.h"
#ifdef HAVE_SID_AUDIO
#include "SidAudio.h"
#endif

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onResetClicked();
    void onNmiClicked();
    void updateStatus();
    void onDisplayKeyPressed(uint8_t ascii_code);
    void onDisplayKeyStateChanged(uint8_t mask);

private:
    void setupUI();
    void setupMenus();
    void connectSignals();
    void updateCpuStatusSidebar();
    void fitWindowToContents();   ///< resize the window to exactly fit its content (zoom / panel toggle)
    
    // UI Components
    QWidget* central_widget_;
    QVBoxLayout* main_layout_;
    QHBoxLayout* control_layout_;
    QHBoxLayout* display_layout_;
    
    DisplayWidget* display_widget_;
    QWidget* status_sidebar_;   ///< CPU register/PC/SP panel; hidden by default, View-menu toggle
    QVBoxLayout* sidebar_layout_;

    QLabel* status_label_;
    
    // Status sidebar labels
    QLabel* cpu_header_label_;
    QLabel* current_byte_label_;
    QLabel* reg_a_label_;
    QLabel* reg_x_label_;
    QLabel* reg_y_label_;
    QLabel* reg_pc_label_;
    QLabel* reg_sp_label_;
    QLabel* flags_header_label_;
    QLabel* flags_values_label_;
    
    QTimer* status_timer_;
    
    // Computer system
    Computer::Computer6502* computer_;
    Modem* modem_;        ///< emulated Hayes modem bridging the ACIA to TCP
#ifdef HAVE_SID_AUDIO
    SidAudio* sid_audio_ = nullptr; ///< SID sound-chip audio output bridge
#endif
    QTimer* execution_timer_;
    /// Real time, so the emulated clock can be paced against it rather than against
    /// however often Qt gets round to firing the execution timer.
    QElapsedTimer wall_clock_;
    qint64 last_run_ns_ = 0;
    
    bool is_running_;
    int execution_cycle_count_;
};

#endif // MAINWINDOW_H