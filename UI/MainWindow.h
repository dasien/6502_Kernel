#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include "Computer6502.h"
#include "DisplayWidget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void onResetClicked();
    void updateStatus();
    void onDisplayKeyPressed(uint8_t ascii_code);

private:
    void setupUI();
    void setupMenus();
    void connectSignals();
    void updateCpuStatusSidebar();
    
    // UI Components
    QWidget* central_widget_;
    QVBoxLayout* main_layout_;
    QHBoxLayout* control_layout_;
    QHBoxLayout* display_layout_;
    
    DisplayWidget* display_widget_;
    QWidget* status_sidebar_;
    QVBoxLayout* sidebar_layout_;
    
    QPushButton* reset_button_;
    
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
    QTimer* execution_timer_;
    
    bool is_running_;
    int execution_cycle_count_;
};

#endif // MAINWINDOW_H