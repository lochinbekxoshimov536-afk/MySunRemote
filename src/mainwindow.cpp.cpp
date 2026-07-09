#include "mainwindow.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QRandomGenerator>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setupUi();

    m_ownId = generateLocalId();
    m_ownPassword = generateLocalPassword();
    m_ownIdField->setText(m_ownId);
    m_ownPasswordField->setText(m_ownPassword);

    connect(&m_passwordRotateTimer, &QTimer::timeout, this, &MainWindow::onHostPasswordRotate);
    m_passwordRotateTimer.start(2 * 60 * 1000);

    startHosting();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupUi()
{
    setWindowTitle("MySunRemote");
    resize(1000, 700);

    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *mainLayout = new QHBoxLayout(central);

    auto *leftPanel = new QWidget(central);
    leftPanel->setFixedWidth(320);
    auto *leftLayout = new QVBoxLayout(leftPanel);

    auto *ownGroup = new QGroupBox("Bu kompyuter", leftPanel);
    auto *ownForm = new QFormLayout(ownGroup);

    m_ownIdField = new QLineEdit(ownGroup);
    m_ownIdField->setReadOnly(true);
    m_ownIdField->setStyleSheet("font-weight: bold; font-size: 14px;");

    m_ownPasswordField = new QLineEdit(ownGroup);
    m_ownPasswordField->setReadOnly(true);
    m_ownPasswordField->setStyleSheet("font-weight: bold; font-size: 14px; letter-spacing: 2px;");

    auto *copyIdButton = new QPushButton("ID'ni nusxalash", ownGroup);
    connect(copyIdButton, &QPushButton::clicked, this, &MainWindow::onCopyOwnIdClicked);

    ownForm->addRow("ID:", m_ownIdField);
    ownForm->addRow("Parol:", m_ownPasswordField);
    ownForm->addRow(copyIdButton);

    m_hostStatusLabel = new QLabel("Holat: signalizatsiya serveriga ulanmoqda...", ownGroup);
    m_hostStatusLabel->setWordWrap(true);
    ownForm->addRow(m_hostStatusLabel);

    auto *connectGroup = new QGroupBox("Boshqa qurilmaga ulanish", leftPanel);
    auto *connectForm = new QFormLayout(connectGroup);

    m_targetIdField = new QLineEdit(connectGroup);
    m_targetIdField->setPlaceholderText("Masofadagi ID");

    m_targetPasswordField = new QLineEdit(connectGroup);
    m_targetPasswordField->setPlaceholderText("Parol");
    m_targetPasswordField->setEchoMode(QLineEdit::Password);

    m_connectButton = new QPushButton("Ulanish", connectGroup);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::onConnectButtonClicked);

    m_connectionStatusLabel = new QLabel("", connectGroup);
    m_connectionStatusLabel->setWordWrap(true);

    connectForm->addRow("ID:", m_targetIdField);
    connectForm->addRow("Parol:", m_targetPasswordField);
    connectForm->addRow(m_connectButton);
    connectForm->addRow(m_connectionStatusLabel);

    auto *fileGroup = new QGroupBox("Fayl uzatish", leftPanel);
    auto *fileLayout = new QVBoxLayout(fileGroup);
    m_sendFileButton = new QPushButton("Fayl yuborish...", fileGroup);
    m_sendFileButton->setEnabled(false);
    connect(m_sendFileButton, &QPushButton::clicked, this, &MainWindow::onSendFileClicked);
    m_fileStatusLabel = new QLabel("", fileGroup);
    m_fileStatusLabel->setWordWrap(true);
    fileLayout->addWidget(m_sendFileButton);
    fileLayout->addWidget(m_fileStatusLabel);

    auto *clipboardGroup = new QGroupBox("Bufer almashish (Clipboard)", leftPanel);
    auto *clipboardLayout = new QVBoxLayout(clipboardGroup);
    m_clipboardTextEdit = new QTextEdit(clipboardGroup);
    m_clipboardTextEdit->setPlaceholderText("Qabul qilingan/yuboriladigan matn shu yerda...");
    m_clipboardTextEdit->setMaximumHeight(100);
    m_sendClipboardButton = new QPushButton("Joriy clipboard'ni yuborish", clipboardGroup);
    m_sendClipboardButton->setEnabled(false);
    connect(m_sendClipboardButton, &QPushButton::clicked, this, &MainWindow::onSendClipboardClicked);
    clipboardLayout->addWidget(m_clipboardTextEdit);
    clipboardLayout->addWidget(m_sendClipboardButton);

    leftLayout->addWidget(ownGroup);
    leftLayout->addWidget(connectGroup);
    leftLayout->addWidget(fileGroup);
    leftLayout->addWidget(clipboardGroup);
    leftLayout->addStretch();

    m_remoteVideoLabel = new QLabel(central);
    m_remoteVideoLabel->setAlignment(Qt::AlignCenter);
    m_remoteVideoLabel->setStyleSheet("background-color: #1a1a1a; color: #888;");
    m_remoteVideoLabel->setText("Ulanish o'rnatilmagan");
    m_remoteVideoLabel->setMinimumSize(640, 480);

    mainLayout->addWidget(leftPanel);
    mainLayout->addWidget(m_remoteVideoLabel, 1);
}

QString MainWindow::generateLocalId() const
{
    return QString::number(QRandomGenerator::global()->bounded(100000000, 999999999));
}

QString MainWindow::generateLocalPassword() const
{
    return QString("%1").arg(QRandomGenerator::global()->bounded(0, 999999), 6, 10, QChar('0'));
}

void MainWindow::startHosting()
{
    m_hostManager = std::make_unique<WebRTCManager>();
    m_inputControl = std::make_unique<InputControl>();
    m_screenCapture = std::make_unique<ScreenCapture>();

    if (!m_hostManager->initialize()) {
        m_hostStatusLabel->setText("Xato: WebRTC ishga tushmadi");
        return;
    }

    connect(m_hostManager.get(), &WebRTCManager::signalingConnected, this, &MainWindow::onHostSignalingConnected);
    connect(m_hostManager.get(), &WebRTCManager::dataChannelOpen, this, &MainWindow::onHostDataChannelOpen);
    connect(m_hostManager.get(), &WebRTCManager::dataChannelMessageReceived, this, &MainWindow::onHostDataChannelMessage);
    connect(m_hostManager.get(), &WebRTCManager::peerConnected, this, &MainWindow::onHostPeerConnected);
    connect(m_hostManager.get(), &WebRTCManager::peerDisconnected, this, &MainWindow::onHostPeerDisconnected);

    connect(m_screenCapture.get(), &ScreenCapture::frameCaptured, this, [this](const QImage &frame) {
        m_hostManager->pushLocalFrame(frame);
    });

    m_hostManager->connectToSignaling(kSignalingServerUrl, m_ownId, m_ownId, WebRTCManager::Role::Host);
}

void MainWindow::onHostSignalingConnected()
{
    m_hostStatusLabel->setText("Holat: tayyor, ulanishlarni kutmoqda");
}

void MainWindow::onHostPasswordRotate()
{
    m_ownPassword = generateLocalPassword();
    m_ownPasswordField->setText(m_ownPassword);
}

void MainWindow::onHostDataChannelOpen()
{
    m_awaitingAuth = true;
    m_hostStatusLabel->setText("Holat: kimdir ulanmoqda, parol kutilmoqda...");
}

void MainWindow::onHostDataChannelMessage(const QJsonObject &msg)
{
    QString type = msg["type"].toString();

    if (m_awaitingAuth) {
        if (type != "auth") return;
        QString providedPassword = msg["password"].toString();
        if (providedPassword != m_ownPassword) {
            m_hostStatusLabel->setText("Holat: noto'g'ri parol, ulanish rad etildi");
            return;
        }
        m_awaitingAuth = false;
        m_hostStatusLabel->setText("Holat: boshqariladi — masofaviy foydalanuvchi ulandi");
        m_screenCapture->startCapture(-1, 20, true);
        return;
    }

    if (type == "mouse_move") {
        m_inputControl->moveMouse(msg["x"].toDouble(), msg["y"].toDouble());
    } else if (type == "mouse_click") {
        m_inputControl->clickMouse(msg["button"].toInt(), msg["press"].toBool());
    } else if (type == "mouse_scroll") {
        m_inputControl->scrollMouse(msg["delta"].toInt());
    } else if (type == "key_event") {
        m_inputControl->keyEvent(msg["keyCode"].toInt(), msg["text"].toString(), msg["press"].toBool());
    } else if (type == "clipboard") {
        QString text = msg["text"].toString();
        m_clipboardTextEdit->setPlainText(text);
        QGuiApplication::clipboard()->setText(text);
    }
}

void MainWindow::onHostPeerConnected()
{
    m_hostStatusLabel->setText("Holat: P2P ulanish o'rnatildi (shifrlangan)");
}

void MainWindow::onHostPeerDisconnected()
{
    m_screenCapture->stopCapture();
    m_awaitingAuth = false;
    m_hostStatusLabel->setText("Holat: tayyor, ulanishlarni kutmoqda");
}

void MainWindow::onConnectButtonClicked()
{
    QString targetId = m_targetIdField->text().trimmed();
    QString targetPassword = m_targetPasswordField->text().trimmed();

    if (targetId.isEmpty() || targetPassword.isEmpty()) {
        QMessageBox::warning(this, "Xato", "ID va parolni kiriting");
        return;
    }

    m_connectButton->setEnabled(false);
    m_connectionStatusLabel->setText("Ulanmoqda...");

    m_controllerManager = std::make_unique<WebRTCManager>();
    if (!m_controllerManager->initialize()) {
        m_connectionStatusLabel->setText("Xato: WebRTC ishga tushmadi");
        m_connectButton->setEnabled(true);
        return;
    }

    connect(m_controllerManager.get(), &WebRTCManager::remoteFrameReceived, this, &MainWindow::onControllerRemoteFrame);
    connect(m_controllerManager.get(), &WebRTCManager::dataChannelOpen, this, &MainWindow::onControllerDataChannelOpen);
    connect(m_controllerManager.get(), &WebRTCManager::dataChannelMessageReceived, this, &MainWindow::onControllerDataChannelMessage);
    connect(m_controllerManager.get(), &WebRTCManager::peerConnected, this, &MainWindow::onControllerPeerConnected);
    connect(m_controllerManager.get(), &WebRTCManager::peerDisconnected, this, &MainWindow::onControllerPeerDisconnected);
    connect(m_controllerManager.get(), &WebRTCManager::errorOccurred, this, &MainWindow::onControllerError);

    m_targetPasswordField->setProperty("pendingPassword", targetPassword);

    m_controllerManager->connectToSignaling(kSignalingServerUrl, targetId, m_ownId, WebRTCManager::Role::Controller);
}

void MainWindow::onControllerDataChannelOpen()
{
    QJsonObject authMsg;
    authMsg["type"] = "auth";
    authMsg["password"] = m_targetPasswordField->property("pendingPassword").toString();
    m_controllerManager->sendDataChannelMessage(authMsg);

    m_connectionStatusLabel->setText("Ulandi — video kutilmoqda...");
    m_sendFileButton->setEnabled(true);
    m_sendClipboardButton->setEnabled(true);
}

void MainWindow::onControllerRemoteFrame(const QImage &frame)
{
    QPixmap pixmap = QPixmap::fromImage(frame).scaled(
        m_remoteVideoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_remoteVideoLabel->setPixmap(pixmap);
}

void MainWindow::onControllerDataChannelMessage(const QJsonObject &msg)
{
    QString type = msg["type"].toString();
    if (type == "clipboard") {
        m_clipboardTextEdit->setPlainText(msg["text"].toString());
    }
}

void MainWindow::onControllerPeerConnected()
{
    m_connectionStatusLabel->setText("Holat: P2P ulanish o'rnatildi (shifrlangan)");
    m_connectButton->setEnabled(true);
}

void MainWindow::onControllerPeerDisconnected()
{
    m_connectionStatusLabel->setText("Ulanish uzildi");
    m_remoteVideoLabel->setText("Ulanish o'rnatilmagan");
    m_remoteVideoLabel->setPixmap(QPixmap());
    m_sendFileButton->setEnabled(false);
    m_sendClipboardButton->setEnabled(false);
    m_connectButton->setEnabled(true);
}

void MainWindow::onControllerError(const QString &message)
{
    m_connectionStatusLabel->setText("Xato: " + message);
    m_connectButton->setEnabled(true);
}

void MainWindow::onSendFileClicked()
{
    if (!m_controllerManager) return;

    QString filePath = QFileDialog::getOpenFileName(this, "Yuboriladigan faylni tanlang");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_fileStatusLabel->setText("Xato: faylni ochib bo'lmadi");
        return;
    }

    QString fileName = QFileInfo(filePath).fileName();
    QByteArray data = file.readAll();
    const int CHUNK_SIZE = 16 * 1024;

    for (int offset = 0; offset < data.size(); offset += CHUNK_SIZE) {
        QJsonObject chunkMsg;
        chunkMsg["type"] = "file_chunk";
        chunkMsg["filename"] = fileName;
        chunkMsg["offset"] = offset;
        chunkMsg["total"] = data.size();
        chunkMsg["data"] = QString::fromLatin1(data.mid(offset, CHUNK_SIZE).toBase64());
        m_controllerManager->sendDataChannelMessage(chunkMsg);
    }

    m_fileStatusLabel->setText(QString("Yuborildi: %1 (%2 KB)").arg(fileName).arg(data.size() / 1024));
}

void MainWindow::onSendClipboardClicked()
{
    if (!m_controllerManager) return;

    QString text = QGuiApplication::clipboard()->text();
    m_clipboardTextEdit->setPlainText(text);

    QJsonObject msg;
    msg["type"] = "clipboard";
    msg["text"] = text;
    m_controllerManager->sendDataChannelMessage(msg);
}

void MainWindow::onCopyOwnIdClicked()
{
    QGuiApplication::clipboard()->setText(m_ownId);
}