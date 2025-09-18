#include "MainWindow.h"
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QMessageBox>
#include <cstdio>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , central_widget_(nullptr)
    , main_layout_(nullptr)
    , control_layout_(nullptr)
    , display_layout_(nullptr)
    , display_widget_(nullptr)
    , status_sidebar_(nullptr)
    , sidebar_layout_(nullptr)
    , reset_button_(nullptr)
    , status_label_(nullptr)
    , cpu_header_label_(nullptr)
    , current_byte_label_(nullptr)
    , reg_a_label_(nullptr)
    , reg_x_label_(nullptr)
    , reg_y_label_(nullptr)
    , reg_pc_label_(nullptr)
    , reg_sp_label_(nullptr)
    , flags_header_label_(nullptr)
    , flags_values_label_(nullptr)
    , status_timer_(new QTimer(this))
    , computer_(new Computer::Computer6502())
    , execution_timer_(new QTimer(this))
    , is_running_(false)
    , execution_cycle_count_(0)
{
    setupUI();
    setupMenus();
    connectSignals();
    
    // Auto-start system powered on and running
    computer_->power_on();
    display_widget_->startRefresh();
    display_widget_->setFocus();
    is_running_ = true;
    execution_timer_->start(1); // 1ms intervals for 1MHz operation
    
    // Initialize status
    updateStatus();
    
    // Start status update timer
    status_timer_->start(100); // Update every 100ms
}

MainWindow::~MainWindow()
{
    delete computer_;
}


void MainWindow::onResetClicked()
{
    computer_->reset();
    execution_cycle_count_ = 0;
    
    status_label_->setText("System reset - Running");
}




void MainWindow::updateStatus()
{
    if (computer_)
    {
        updateCpuStatusSidebar();
    }
}


void MainWindow::onDisplayKeyPressed(uint8_t ascii_code)
{
    if (!computer_)
    {
        return;
    }
    
    // Send keypress directly to PIA
    computer_->getPia()->addKeypress(ascii_code);
}

void MainWindow::setupUI()
{
    // Create central widget and main layout
    central_widget_ = new QWidget(this);
    setCentralWidget(central_widget_);
    
    main_layout_ = new QVBoxLayout(central_widget_);
    
    // Create horizontal layout for display and status sidebar
    display_layout_ = new QHBoxLayout();
    display_layout_->setSpacing(0); // No spacing between display and sidebar
    display_layout_->setContentsMargins(0, 0, 0, 0); // No margins
    
    // Create display widget
    display_widget_ = new DisplayWidget(computer_->getVideoChip(), this);
    display_layout_->addWidget(display_widget_);
    
    // Connect display widget keyboard input to PIA
    connect(display_widget_, &DisplayWidget::keyPressed, this, &MainWindow::onDisplayKeyPressed);
    
    // Create status sidebar
    status_sidebar_ = new QWidget(this);
    status_sidebar_->setFixedWidth(110); // Exactly 10 characters wide
    status_sidebar_->setStyleSheet("QWidget { background-color: #f0f0f0; border: 1px solid #ccc; }");
    
    sidebar_layout_ = new QVBoxLayout(status_sidebar_);
    sidebar_layout_->setSpacing(2);
    sidebar_layout_->setContentsMargins(5, 5, 5, 5);
    
    // Create status sidebar labels
    cpu_header_label_ = new QLabel("CPU", status_sidebar_);
    cpu_header_label_->setAlignment(Qt::AlignCenter);
    cpu_header_label_->setStyleSheet("QLabel { font-weight: bold; color: #333; }");
    sidebar_layout_->addWidget(cpu_header_label_);
    
    current_byte_label_ = new QLabel("0x00", status_sidebar_);
    current_byte_label_->setAlignment(Qt::AlignCenter);
    current_byte_label_->setStyleSheet("QLabel { font-family: monospace; color: #666; }");
    sidebar_layout_->addWidget(current_byte_label_);
    
    sidebar_layout_->addSpacing(5);
    
    reg_a_label_ = new QLabel("A: 0x00", status_sidebar_);
    reg_a_label_->setStyleSheet("QLabel { font-family: monospace; }");
    sidebar_layout_->addWidget(reg_a_label_);
    
    reg_x_label_ = new QLabel("X: 0x00", status_sidebar_);
    reg_x_label_->setStyleSheet("QLabel { font-family: monospace; }");
    sidebar_layout_->addWidget(reg_x_label_);
    
    reg_y_label_ = new QLabel("Y: 0x00", status_sidebar_);
    reg_y_label_->setStyleSheet("QLabel { font-family: monospace; }");
    sidebar_layout_->addWidget(reg_y_label_);
    
    reg_pc_label_ = new QLabel("PC: 0000", status_sidebar_);
    reg_pc_label_->setStyleSheet("QLabel { font-family: monospace; }");
    sidebar_layout_->addWidget(reg_pc_label_);
    
    reg_sp_label_ = new QLabel("SP: 0xFF", status_sidebar_);
    reg_sp_label_->setStyleSheet("QLabel { font-family: monospace; }");
    sidebar_layout_->addWidget(reg_sp_label_);
    
    sidebar_layout_->addSpacing(5);
    
    flags_header_label_ = new QLabel("NV-BDIZC", status_sidebar_);
    flags_header_label_->setStyleSheet("QLabel { font-family: monospace; color: #666; }");
    sidebar_layout_->addWidget(flags_header_label_);
    
    flags_values_label_ = new QLabel("00100000", status_sidebar_);
    flags_values_label_->setStyleSheet("QLabel { font-family: monospace; }");
    sidebar_layout_->addWidget(flags_values_label_);
    
    sidebar_layout_->addStretch();
    
    // Create a container for sidebar + reset button
    QWidget* sidebar_container = new QWidget(this);
    QVBoxLayout* sidebar_container_layout = new QVBoxLayout(sidebar_container);
    sidebar_container_layout->setContentsMargins(0, 0, 0, 0);
    sidebar_container_layout->setSpacing(5);
    
    // Add the status sidebar to the container
    sidebar_container_layout->addWidget(status_sidebar_);
    
    // Create control buttons
    reset_button_ = new QPushButton("Reset", this);
    
    // Add reset button centered below the sidebar
    QHBoxLayout* reset_layout = new QHBoxLayout();
    reset_layout->addStretch();
    reset_layout->addWidget(reset_button_);
    reset_layout->addStretch();
    
    sidebar_container_layout->addLayout(reset_layout);
    
    // Add sidebar container to horizontal layout (display_widget already added above)
    display_layout_->addWidget(sidebar_container);
    
    // Add display layout to main layout
    main_layout_->addLayout(display_layout_);
    
    // Add spacing before status bar
    main_layout_->addSpacing(40);
    
    // Set window properties
    setWindowTitle("6502 Computer Emulator");
    
    // Let the window size itself naturally instead of fixing the size
    resize(sizeHint());
    setMinimumSize(sizeHint());
    
    // Set up status bar
    status_label_ = new QLabel("System running", this);
    
    statusBar()->addWidget(status_label_);
}

void MainWindow::setupMenus()
{
    // File menu
    QMenu* file_menu = menuBar()->addMenu("&File");
    
    QAction* exit_action = file_menu->addAction("E&xit");
    exit_action->setShortcut(QKeySequence::Quit);
    connect(exit_action, &QAction::triggered, this, &QWidget::close);
    
    // Help menu
    QMenu* help_menu = menuBar()->addMenu("&Help");
    
    QAction* about_action = help_menu->addAction("&About");
    connect(about_action, &QAction::triggered, [this]() {
        QMessageBox::about(this, "About 6502 Emulator", 
                          "6502 Computer Emulator\\n\\n"
                          "A complete 6502 microprocessor system emulator\\n"
                          "with 40x25 character display.\\n\\n"
                          "Built with Qt and C++");
    });
}

void MainWindow::connectSignals()
{
    connect(reset_button_, &QPushButton::clicked, this, &MainWindow::onResetClicked);
    
    connect(status_timer_, &QTimer::timeout, this, &MainWindow::updateStatus);
    
    // Connect execution timer to run computer cycles
    connect(execution_timer_, &QTimer::timeout, [this]() {
        if (is_running_ && computer_)
        {
            // Run 1000 cycles per 1ms tick for 1MHz operation
            for (int i = 0; i < 1000; ++i)
            {
                computer_->run(1);
                execution_cycle_count_++;
            }
        }
    });
}

void MainWindow::updateCpuStatusSidebar()
{
    if (!computer_ || !computer_->getCpu())
    {
        return;
    }
    
    Computer::CPU6502* cpu = computer_->getCpu();
    
    // Get current byte at PC
    uint8_t current_byte = cpu->getCurrentByte();
    current_byte_label_->setText(QString("0x%1").arg(current_byte, 2, 16, QChar('0')).toUpper());
    
    // Update CPU registers
    reg_a_label_->setText(QString("A: 0x%1").arg(cpu->reg.A, 2, 16, QChar('0')).toUpper());
    reg_x_label_->setText(QString("X: 0x%1").arg(cpu->reg.X, 2, 16, QChar('0')).toUpper());
    reg_y_label_->setText(QString("Y: 0x%1").arg(cpu->reg.Y, 2, 16, QChar('0')).toUpper());
    reg_pc_label_->setText(QString("PC: %1").arg(cpu->reg.PC, 4, 16, QChar('0')).toUpper());
    reg_sp_label_->setText(QString("SP: 0x%1").arg(cpu->reg.SP, 2, 16, QChar('0')).toUpper());
    
    // Extract individual flag bits for display
    QString flags;
    flags += (cpu->reg.P & cpu->kNegative) ? '1' : '0';  // Bit 7: N
    flags += (cpu->reg.P & cpu->kOverflow) ? '1' : '0';  // Bit 6: V  
    flags += (cpu->reg.P & cpu->kUnused) ? '1' : '0';    // Bit 5: - (always 1)
    flags += (cpu->reg.P & cpu->kBreak) ? '1' : '0';     // Bit 4: B
    flags += (cpu->reg.P & cpu->kDecimal) ? '1' : '0';   // Bit 3: D
    flags += (cpu->reg.P & cpu->kInterrupt) ? '1' : '0'; // Bit 2: I
    flags += (cpu->reg.P & cpu->kZero) ? '1' : '0';      // Bit 1: Z
    flags += (cpu->reg.P & cpu->kCarry) ? '1' : '0';     // Bit 0: C
    
    flags_values_label_->setText(flags);
}