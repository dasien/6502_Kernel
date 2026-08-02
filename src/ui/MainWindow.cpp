#include "MainWindow.h"
#include <QApplication>
#include <QMenuBar>
#include <QStatusBar>
#include <QMessageBox>
#include <QActionGroup>
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
    , modem_(nullptr)
    , execution_timer_(new QTimer(this))
    , irq_timer_(new QTimer(this))
    , is_running_(false)
    , execution_cycle_count_(0)
{
    setupUI();
    setupMenus();
    connectSignals();
    
    // Auto-start system powered on and running
    computer_->power_on();

    // Emulated Hayes modem: bridges the ACIA to TCP (the terminal dials BBSes).
    modem_ = new Modem(computer_->getAcia(), this);

#ifdef HAVE_SID_AUDIO
    // SID sound chip: stream its synthesized PCM to the default audio output.
    sid_audio_ = new SidAudio(computer_->getSid(), this);
#endif

    display_widget_->startRefresh();
    display_widget_->setFocus();
    is_running_ = true;
    execution_timer_->start(1); // 1ms intervals for 1MHz operation

    // Drive the PIA interval-timer IRQ at ~60 Hz (wall-clock), independent of
    // emulation speed. This is the system "jiffy" tick behind BASIC's ON IRQ.
    connect(irq_timer_, &QTimer::timeout, this, [this]() {
        computer_->getPia()->pulseTimerIrq();
    });
    irq_timer_->start(16); // ~62.5 Hz
    
    // Initialize status
    updateStatus();
    
    // Start status update timer
    status_timer_->start(100); // Update every 100ms
}

MainWindow::~MainWindow()
{
    // Order matters. sid_audio_ and modem_ are QObject children of this window, so
    // Qt would destroy them AFTER this body runs -- but they hold raw pointers into
    // computer_ (getSid() / getAcia()). Deleting the machine first left the audio
    // sink still started, so its thread kept calling generateSamples() on a freed
    // Sid (freed mutex, freed regs_): an intermittent use-after-free on every close
    // while sound was playing. Tear the consumers down first, then the machine.
#ifdef HAVE_SID_AUDIO
    delete sid_audio_;
    sid_audio_ = nullptr;
#endif
    delete modem_;
    modem_ = nullptr;

    delete computer_;
    computer_ = nullptr;
}


void MainWindow::onResetClicked()
{
    computer_->reset();
    execution_cycle_count_ = 0;

    status_label_->setText("System reset - Running");
}

void MainWindow::onNmiClicked()
{
    // Trigger a non-maskable interrupt: BASIC handles it if ON NMI is enabled,
    // otherwise the kernel ISR breaks back to the monitor (a "stop" key).
    if (computer_)
    {
        computer_->getCpu()->requestNmi();
        status_label_->setText("NMI");
    }
    display_widget_->setFocus();  // keep typing focus on the display
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

    // Paint the area around the display black (not the default light gray): when
    // the window is larger than the 640x400 screen (zoomed, maximized, or full
    // screen) the surround stays dark instead of a harsh bright field. The #id
    // selector targets only this widget, so children keep their own styling.
    central_widget_->setObjectName("central");
    central_widget_->setStyleSheet("QWidget#central { background-color: black; }");
    
    main_layout_ = new QVBoxLayout(central_widget_);
    
    // Create horizontal layout for display and status sidebar
    display_layout_ = new QHBoxLayout();
    display_layout_->setSpacing(0); // No spacing between display and sidebar
    display_layout_->setContentsMargins(0, 0, 0, 0); // No margins
    
    // Create display widget
    display_widget_ = new DisplayWidget(computer_->getVideoChip(), computer_->getMemory(), this);
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

    // Add the CPU status sidebar directly to the display row (display_widget added
    // above). It's hidden by default and toggled from the View menu; Reset/NMI now
    // live in the Control menu, so the old button row is gone.
    display_layout_->addWidget(status_sidebar_);

    // Add display layout to main layout
    main_layout_->addLayout(display_layout_);
    
    // Add spacing before status bar
    main_layout_->addSpacing(40);
    
    // Set window properties
    setWindowTitle("MFC 6502");

    // The register panel is a debug view now -- hidden by default (toggle from the
    // View menu). Hide it BEFORE sizing so the default window doesn't reserve its
    // width; showing it (or zooming) refits the window via fitWindowToContents().
    status_sidebar_->hide();

    // Size the window to the content; the View > Zoom actions and the register
    // toggle refit it (and it stays freely resizable / full-screen-capable).
    resize(sizeHint());
    setMinimumSize(sizeHint());
    
    // Set up status bar (dark to match the black surround)
    statusBar()->setStyleSheet("QStatusBar { background: black; } QStatusBar QLabel { color: #bbb; }");
    status_label_ = new QLabel("System running", this);

    statusBar()->addWidget(status_label_);
}

// Resize the window to fit its content after a fixed-size child changes (display
// zoom) or the register panel is shown/hidden. Force the layout to recompute first
// (a stale hint would clip the display), then resize to the fresh sizeHint. The
// window stays freely resizable / full-screen-capable; the minimum tracks the
// content so it can't be dragged small enough to clip the screen.
void MainWindow::fitWindowToContents()
{
    if (central_widget_->layout())
        central_widget_->layout()->activate();
    setMinimumSize(0, 0);            // allow shrinking (e.g. zoom 3x -> 1x)
    resize(sizeHint());
    setMinimumSize(sizeHint());
}

void MainWindow::setupMenus()
{
    // File menu
    QMenu* file_menu = menuBar()->addMenu("&File");
    
    // NOTE: these use Ctrl+SHIFT deliberately. Application QAction shortcuts are
    // resolved before the focused widget's keyPressEvent, and DisplayWidget maps
    // Ctrl+A..Z to control bytes $01..$1A for the guest -- so a plain Ctrl+letter
    // shortcut here silently steals that key from the running program. Ctrl+Q used
    // to close the emulator instead of reaching EDIT's guarded "unsaved changes"
    // quit (losing the document), and Ctrl+R reset the machine instead of starting
    // TERM's XMODEM receive. Ctrl+Shift+<key> cannot be produced as a control byte,
    // so the guest keeps the whole Ctrl+letter space.
    QAction* exit_action = file_menu->addAction("E&xit");
    exit_action->setShortcut(QKeySequence("Ctrl+Shift+Q"));
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    // Control menu: the machine controls that used to be on-screen buttons.
    QMenu* control_menu = menuBar()->addMenu("&Control");

    QAction* reset_action = control_menu->addAction("&Reset");
    reset_action->setShortcut(QKeySequence("Ctrl+Shift+R"));
    connect(reset_action, &QAction::triggered, this, &MainWindow::onResetClicked);

    QAction* nmi_action = control_menu->addAction("&NMI");
    nmi_action->setShortcut(QKeySequence("Ctrl+Shift+N"));
    connect(nmi_action, &QAction::triggered, this, &MainWindow::onNmiClicked);

    // View menu: toggle the CPU register/PC/SP panel (a debug view, off by default).
    QMenu* view_menu = menuBar()->addMenu("&View");

    QAction* show_registers_action = view_menu->addAction("CPU &Registers");
    show_registers_action->setCheckable(true);
    show_registers_action->setChecked(false);          // hidden by default
    connect(show_registers_action, &QAction::toggled, this, [this](bool checked) {
        status_sidebar_->setVisible(checked);
        fitWindowToContents();                         // grow/shrink the window to fit
    });

    // Zoom submenu: integer scale of the display (crisp, nearest-neighbor). The
    // window resizes to fit each factor; the black surround handles any slack.
    view_menu->addSeparator();
    QMenu* zoom_menu = view_menu->addMenu("&Zoom");
    QActionGroup* zoom_group = new QActionGroup(this);   // exclusive: one factor at a time
    for (int f = 1; f <= 3; ++f)
    {
        QAction* zoom_action = zoom_menu->addAction(QString("%1x").arg(f));
        zoom_action->setCheckable(true);
        zoom_action->setChecked(f == 1);                 // 1x is the native default
        zoom_action->setShortcut(QKeySequence(QString("Ctrl+%1").arg(f)));
        zoom_group->addAction(zoom_action);
        connect(zoom_action, &QAction::triggered, this, [this, f]() {
            display_widget_->setScale(f);
            fitWindowToContents();
        });
    }

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
    // Reset/NMI are wired to the Control-menu actions in setupMenus().
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
            // Pump the modem once per tick: drain the ACIA TX into the
            // protocol (inbound socket data arrives via Qt signals).
            if (modem_) modem_->poll();
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