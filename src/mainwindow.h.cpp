#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <memory>

#include "webrtcmanager.h"
#include "screencapture.h"
#include "inputcontrol.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onHostPasswordRotate();
    void onHostSignalingConnected();
    void onHostDataChannelOpen();
    void onHostDataChannelMessage(const QJsonObject &msg);
    void onHostPeerConnected();
    void onHostPeerDisconnected();

    void onConnectButtonClicked();
    void onControllerRemoteFrame(const QImage &frame);
    void onControllerDataChannelOpen();
    void onControllerDataChannelMessage(const QJsonObject &msg);
    void onControllerPeerConnected();
    void onControllerPeerDisconnected();
    void onControllerError(const QString &message);

    void onSendFileClicked();
    void onSendClipboardClicked();
    void onCopyOwnIdClicked();

private:
    void setupUi();
    void startHosting();
    QString generateLocalId() const;
    QString generateLocalPassword() const;

    std::unique_ptr<WebRTCManager> m_hostManager;
    std::unique_ptr<ScreenCapture> m_screenCapture;
    QString m_ownId;
    QString m_ownPassword;
    QTimer m_passwordRotateTimer;
    bool m_awaitingAuth = false;

    std::unique_ptr<WebRTCManager> m_controllerManager;
    std::unique_ptr<InputControl> m_inputControl;

    QLineEdit *m_ownIdField;
    QLineEdit *m_ownPasswordField;
    QLabel *m_hostStatusLabel;

    QLineEdit *m_targetIdField;
    QLineEdit *m_targetPasswordField;
    QPushButton *m_connectButton;
    QLabel *m_connectionStatusLabel;

    QLabel *m_remoteVideoLabel;

    QPushButton *m_sendFileButton;
    QLabel *m_fileStatusLabel;

    QTextEdit *m_clipboardTextEdit;
    QPushButton *m_sendClipboardButton;

    static constexpr const char *kSignalingServerUrl = "wss://localhost:8765/ws";
};

#endif