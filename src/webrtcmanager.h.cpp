#ifndef WEBRTCMANAGER_H
#define WEBRTCMANAGER_H

#include <QObject>
#include <QWebSocket>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <memory>

#include <api/peer_connection_interface.h>
#include <api/media_stream_interface.h>
#include <api/data_channel_interface.h>
#include <api/video/video_frame.h>
#include <api/video/i420_buffer.h>
#include <pc/video_track_source.h>

class LocalScreenTrackSource : public rtc::AdaptedVideoTrackSource
{
public:
    LocalScreenTrackSource() = default;
    void pushFrame(const QImage &image);
    SourceState state() const override { return kLive; }
    bool remote() const override { return false; }
    bool is_screencast() const override { return true; }
    absl::optional<bool> needs_denoising() const override { return false; }
};

class WebRTCManager : public QObject
{
    Q_OBJECT
public:
    enum class Role { Host, Controller };

    explicit WebRTCManager(QObject *parent = nullptr);
    ~WebRTCManager();

    bool initialize();
    void connectToSignaling(const QString &wsUrl, const QString &roomId,
                             const QString &clientId, Role role);
    void disconnect();
    void pushLocalFrame(const QImage &frame);
    void sendDataChannelMessage(const QJsonObject &message);
    bool isConnected() const;

signals:
    void signalingConnected();
    void signalingDisconnected();
    void peerConnected();
    void peerDisconnected();
    void remoteFrameReceived(const QImage &frame);
    void dataChannelMessageReceived(const QJsonObject &message);
    void dataChannelOpen();
    void errorOccurred(const QString &message);

private slots:
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessageReceived(const QString &message);
    void onWsError(QAbstractSocket::SocketError error);

private:
    void handleSignalingMessage(const QJsonObject &msg);
    void createPeerConnection();
    void createOffer();
    void createAnswer(const std::string &remoteSdp);
    void sendSignalingMessage(const QJsonObject &msg);

    class PCObserver;
    class CreateSDPObserver;
    class SetSDPObserver;
    class DCObserver;
    class VideoSinkAdapter;

    QWebSocket m_webSocket;
    QString m_roomId;
    QString m_clientId;
    QString m_targetId;
    Role m_role = Role::Controller;

    std::unique_ptr<rtc::Thread> m_signalingThread;
    std::unique_ptr<rtc::Thread> m_workerThread;
    std::unique_ptr<rtc::Thread> m_networkThread;

    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_factory;
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> m_peerConnection;
    rtc::scoped_refptr<LocalScreenTrackSource> m_localVideoSource;
    rtc::scoped_refptr<webrtc::DataChannelInterface> m_dataChannel;

    std::unique_ptr<PCObserver> m_pcObserver;
    std::unique_ptr<DCObserver> m_dcObserver;
    std::unique_ptr<VideoSinkAdapter> m_videoSink;

    bool m_initialized = false;
    bool m_dataChannelReady = false;
};

#endif