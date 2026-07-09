#include "webrtcmanager.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QUrl>

#include <api/create_peerconnection_factory.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <api/rtc_event_log/rtc_event_log_factory.h>
#include <third_party/libyuv/include/libyuv.h>

// ------------------------------------------------------------
// LocalScreenTrackSource
// ------------------------------------------------------------
void LocalScreenTrackSource::pushFrame(const QImage &image)
{
    if (image.isNull()) return;

    QImage rgb = image.format() == QImage::Format_RGB32
                     ? image
                     : image.convertToFormat(QImage::Format_RGB32);

    int width = rgb.width();
    int height = rgb.height();

    rtc::scoped_refptr<webrtc::I420Buffer> i420Buffer =
        webrtc::I420Buffer::Create(width, height);

    libyuv::ARGBToI420(
        rgb.constBits(), rgb.bytesPerLine(),
        i420Buffer->MutableDataY(), i420Buffer->StrideY(),
        i420Buffer->MutableDataU(), i420Buffer->StrideU(),
        i420Buffer->MutableDataV(), i420Buffer->StrideV(),
        width, height);

    webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
        .set_video_frame_buffer(i420Buffer)
        .set_timestamp_us(rtc::TimeMicros())
        .set_rotation(webrtc::kVideoRotation_0)
        .build();

    OnFrame(frame);
}

// ------------------------------------------------------------
// PCObserver
// ------------------------------------------------------------
class WebRTCManager::PCObserver : public webrtc::PeerConnectionObserver
{
public:
    explicit PCObserver(WebRTCManager *owner) : m_owner(owner) {}

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}

    void OnIceCandidate(const webrtc::IceCandidateInterface *candidate) override
    {
        std::string sdp;
        candidate->ToString(&sdp);

        QJsonObject candJson;
        candJson["sdpMid"] = QString::fromStdString(candidate->sdp_mid());
        candJson["sdpMLineIndex"] = candidate->sdp_mline_index();
        candJson["candidate"] = QString::fromStdString(sdp);

        QMetaObject::invokeMethod(m_owner, [this, candJson]() {
            QJsonObject msg;
            msg["type"] = "ice_candidate";
            msg["candidate"] = candJson;
            m_owner->sendSignalingMessage(msg);
        }, Qt::QueuedConnection);
    }

    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState state) override
    {
        if (state == webrtc::PeerConnectionInterface::kIceConnectionConnected) {
            QMetaObject::invokeMethod(m_owner, [this]() { emit m_owner->peerConnected(); }, Qt::QueuedConnection);
        } else if (state == webrtc::PeerConnectionInterface::kIceConnectionDisconnected ||
                   state == webrtc::PeerConnectionInterface::kIceConnectionFailed) {
            QMetaObject::invokeMethod(m_owner, [this]() { emit m_owner->peerDisconnected(); }, Qt::QueuedConnection);
        }
    }

    void OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) override;

    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel) override;

    void OnRenegotiationNeeded() override {}
    void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface>) override {}
    void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface>) override {}

private:
    WebRTCManager *m_owner;
};

// ------------------------------------------------------------
// VideoSinkAdapter
// ------------------------------------------------------------
class WebRTCManager::VideoSinkAdapter : public rtc::VideoSinkInterface<webrtc::VideoFrame>
{
public:
    explicit VideoSinkAdapter(WebRTCManager *owner) : m_owner(owner) {}

    void OnFrame(const webrtc::VideoFrame &frame) override
    {
        rtc::scoped_refptr<webrtc::I420BufferInterface> buffer = frame.video_frame_buffer()->ToI420();

        QImage image(buffer->width(), buffer->height(), QImage::Format_RGB32);
        libyuv::I420ToARGB(
            buffer->DataY(), buffer->StrideY(),
            buffer->DataU(), buffer->StrideU(),
            buffer->DataV(), buffer->StrideV(),
            image.bits(), image.bytesPerLine(),
            buffer->width(), buffer->height());

        QImage copy = image.copy();
        QMetaObject::invokeMethod(m_owner, [this, copy]() {
            emit m_owner->remoteFrameReceived(copy);
        }, Qt::QueuedConnection);
    }

private:
    WebRTCManager *m_owner;
};

void WebRTCManager::PCObserver::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
{
    auto track = transceiver->receiver()->track();
    if (track && track->kind() == webrtc::MediaStreamTrackInterface::kVideoKind) {
        auto *videoTrack = static_cast<webrtc::VideoTrackInterface *>(track.get());
        videoTrack->AddOrUpdateSink(m_owner->m_videoSink.get(), rtc::VideoSinkWants());
    }
}

// ------------------------------------------------------------
// DCObserver
// ------------------------------------------------------------
class WebRTCManager::DCObserver : public webrtc::DataChannelObserver
{
public:
    explicit DCObserver(WebRTCManager *owner) : m_owner(owner) {}

    void OnStateChange() override
    {
        if (m_owner->m_dataChannel &&
            m_owner->m_dataChannel->state() == webrtc::DataChannelInterface::kOpen) {
            m_owner->m_dataChannelReady = true;
            QMetaObject::invokeMethod(m_owner, [this]() { emit m_owner->dataChannelOpen(); }, Qt::QueuedConnection);
        }
    }

    void OnMessage(const webrtc::DataBuffer &buffer) override
    {
        QByteArray raw(reinterpret_cast<const char *>(buffer.data.data()),
                        static_cast<int>(buffer.data.size()));
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject()) return;

        QJsonObject obj = doc.object();
        QMetaObject::invokeMethod(m_owner, [this, obj]() {
            emit m_owner->dataChannelMessageReceived(obj);
        }, Qt::QueuedConnection);
    }

    void OnBufferedAmountChange(uint64_t) override {}

private:
    WebRTCManager *m_owner;
};

// ------------------------------------------------------------
// CreateSDPObserver / SetSDPObserver
// ------------------------------------------------------------
class WebRTCManager::CreateSDPObserver : public webrtc::CreateSessionDescriptionObserver
{
public:
    using Callback = std::function<void(webrtc::SessionDescriptionInterface *)>;
    explicit CreateSDPObserver(Callback cb) : m_cb(std::move(cb)) {}

    void OnSuccess(webrtc::SessionDescriptionInterface *desc) override { m_cb(desc); }
    void OnFailure(webrtc::RTCError error) override
    {
        qWarning() << "SDP yaratishda xato:" << error.message();
    }

private:
    Callback m_cb;
};

class WebRTCManager::SetSDPObserver : public webrtc::SetSessionDescriptionObserver
{
public:
    void OnSuccess() override {}
    void OnFailure(webrtc::RTCError error) override
    {
        qWarning() << "SDP o'rnatishda xato:" << error.message();
    }
};

// ------------------------------------------------------------
// WebRTCManager
// ------------------------------------------------------------
WebRTCManager::WebRTCManager(QObject *parent) : QObject(parent)
{
    connect(&m_webSocket, &QWebSocket::connected, this, &WebRTCManager::onWsConnected);
    connect(&m_webSocket, &QWebSocket::disconnected, this, &WebRTCManager::onWsDisconnected);
    connect(&m_webSocket, &QWebSocket::textMessageReceived, this, &WebRTCManager::onWsTextMessageReceived);
    connect(&m_webSocket, &QWebSocket::errorOccurred, this, &WebRTCManager::onWsError);
}

WebRTCManager::~WebRTCManager()
{
    disconnect();
    if (m_peerConnection) m_peerConnection->Close();
    m_networkThread.reset();
    m_workerThread.reset();
    m_signalingThread.reset();
}

bool WebRTCManager::initialize()
{
    if (m_initialized) return true;

    m_networkThread = rtc::Thread::CreateWithSocketServer();
    m_networkThread->Start();
    m_workerThread = rtc::Thread::Create();
    m_workerThread->Start();
    m_signalingThread = rtc::Thread::Create();
    m_signalingThread->Start();

    m_factory = webrtc::CreatePeerConnectionFactory(
        m_networkThread.get(), m_workerThread.get(), m_signalingThread.get(),
        nullptr /* default_adm */,
        webrtc::CreateBuiltinAudioEncoderFactory(),
        webrtc::CreateBuiltinAudioDecoderFactory(),
        webrtc::CreateBuiltinVideoEncoderFactory(),
        webrtc::CreateBuiltinVideoDecoderFactory(),
        nullptr /* audio_mixer */,
        nullptr /* audio_processing */);

    if (!m_factory) {
        emit errorOccurred("PeerConnectionFactory yaratib bo'lmadi");
        return false;
    }

    m_initialized = true;
    return true;
}

void WebRTCManager::connectToSignaling(const QString &wsUrl, const QString &roomId,
                                        const QString &clientId, Role role)
{
    if (!m_initialized) {
        emit errorOccurred("Avval initialize() chaqiring");
        return;
    }
    m_roomId = roomId;
    m_clientId = clientId;
    m_role = role;
    m_webSocket.open(QUrl(wsUrl));
}

void WebRTCManager::disconnect()
{
    if (m_webSocket.state() == QAbstractSocket::ConnectedState) {
        m_webSocket.close();
    }
}

bool WebRTCManager::isConnected() const
{
    return m_peerConnection &&
           m_peerConnection->ice_connection_state() ==
               webrtc::PeerConnectionInterface::kIceConnectionConnected;
}

void WebRTCManager::onWsConnected()
{
    emit signalingConnected();

    QJsonObject joinMsg;
    joinMsg["type"] = "join";
    joinMsg["room"] = m_roomId;
    joinMsg["client_id"] = m_clientId;
    sendSignalingMessage(joinMsg);

    createPeerConnection();
}

void WebRTCManager::onWsDisconnected()
{
    emit signalingDisconnected();
}

void WebRTCManager::onWsError(QAbstractSocket::SocketError)
{
    emit errorOccurred(m_webSocket.errorString());
}

void WebRTCManager::sendSignalingMessage(const QJsonObject &msg)
{
    QJsonObject full = msg;
    full["client_id"] = m_clientId;
    if (!m_targetId.isEmpty()) full["target"] = m_targetId;
    m_webSocket.sendTextMessage(QJsonDocument(full).toJson(QJsonDocument::Compact));
}

void WebRTCManager::onWsTextMessageReceived(const QString &message)
{
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;
    handleSignalingMessage(doc.object());
}

void WebRTCManager::handleSignalingMessage(const QJsonObject &msg)
{
    QString type = msg["type"].toString();

    if (type == "peer_joined") {
        m_targetId = msg["client_id"].toString();
        if (m_role == Role::Host) {
            createOffer();
        }
    } else if (type == "offer") {
        m_targetId = msg["from"].toString();
        createAnswer(msg["sdp"].toString().toStdString());
    } else if (type == "answer") {
        webrtc::SdpParseError err;
        auto desc = webrtc::CreateSessionDescription(
            webrtc::SdpType::kAnswer, msg["sdp"].toString().toStdString(), &err);
        if (desc) {
            m_peerConnection->SetRemoteDescription(new rtc::RefCountedObject<SetSDPObserver>(), desc.release());
        } else {
            emit errorOccurred(QString("SDP answer parse xatosi: %1").arg(QString::fromStdString(err.description)));
        }
    } else if (type == "ice_candidate") {
        QJsonObject cand = msg["candidate"].toObject();
        webrtc::SdpParseError err;
        std::unique_ptr<webrtc::IceCandidateInterface> candidate(webrtc::CreateIceCandidate(
            cand["sdpMid"].toString().toStdString(),
            cand["sdpMLineIndex"].toInt(),
            cand["candidate"].toString().toStdString(),
            &err));
        if (candidate && m_peerConnection) {
            m_peerConnection->AddIceCandidate(candidate.get());
        }
    }
}

void WebRTCManager::createPeerConnection()
{
    webrtc::PeerConnectionInterface::RTCConfiguration config;
    config.servers.push_back([]{
        webrtc::PeerConnectionInterface::IceServer stun;
        stun.uri = "stun:stun.l.google.com:19302";
        return stun;
    }());
    // TURN serverni production uchun qo'shish mumkin:
    // webrtc::PeerConnectionInterface::IceServer turn;
    // turn.uri = "turn:myremote.com:3478";
    // turn.username = "myuser";
    // turn.password = "mypassword";
    // config.servers.push_back(turn);

    m_pcObserver = std::make_unique<PCObserver>(this);
    m_videoSink = std::make_unique<VideoSinkAdapter>(this);

    webrtc::PeerConnectionDependencies deps(m_pcObserver.get());
    auto result = m_factory->CreatePeerConnectionOrError(config, std::move(deps));
    if (!result.ok()) {
        emit errorOccurred("PeerConnection yaratilmadi: " + QString::fromStdString(result.error().message()));
        return;
    }
    m_peerConnection = result.value();

    if (m_role == Role::Host) {
        m_localVideoSource = rtc::make_ref_counted<LocalScreenTrackSource>();
        auto videoTrack = m_factory->CreateVideoTrack("screen0", m_localVideoSource.get());
        m_peerConnection->AddTrack(videoTrack, {"mysunremote_stream"});

        webrtc::DataChannelInit dcInit;
        dcInit.ordered = true;
        auto dcResult = m_peerConnection->CreateDataChannelOrError("control", &dcInit);
        if (dcResult.ok()) {
            m_dataChannel = dcResult.value();
            m_dcObserver = std::make_unique<DCObserver>(this);
            m_dataChannel->RegisterObserver(m_dcObserver.get());
        }
    }
}

void WebRTCManager::PCObserver::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel)
{
    m_owner->m_dataChannel = channel;
    m_owner->m_dcObserver = std::make_unique<DCObserver>(m_owner);
    channel->RegisterObserver(m_owner->m_dcObserver.get());
}

void WebRTCManager::createOffer()
{
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    auto observer = new rtc::RefCountedObject<CreateSDPObserver>(
        [this](webrtc::SessionDescriptionInterface *desc) {
            m_peerConnection->SetLocalDescription(new rtc::RefCountedObject<SetSDPObserver>(), desc);

            std::string sdpStr;
            desc->ToString(&sdpStr);

            QJsonObject msg;
            msg["type"] = "offer";
            msg["sdp"] = QString::fromStdString(sdpStr);
            sendSignalingMessage(msg);
        });
    m_peerConnection->CreateOffer(observer, options);
}

void WebRTCManager::createAnswer(const std::string &remoteSdp)
{
    if (!m_peerConnection) createPeerConnection();

    webrtc::SdpParseError err;
    auto desc = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, remoteSdp, &err);
    if (!desc) {
        emit errorOccurred(QString("SDP offer parse xatosi: %1").arg(QString::fromStdString(err.description)));
        return;
    }
    m_peerConnection->SetRemoteDescription(new rtc::RefCountedObject<SetSDPObserver>(), desc.release());

    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    auto observer = new rtc::RefCountedObject<CreateSDPObserver>(
        [this](webrtc::SessionDescriptionInterface *answerDesc) {
            m_peerConnection->SetLocalDescription(new rtc::RefCountedObject<SetSDPObserver>(), answerDesc);

            std::string sdpStr;
            answerDesc->ToString(&sdpStr);

            QJsonObject msg;
            msg["type"] = "answer";
            msg["sdp"] = QString::fromStdString(sdpStr);
            sendSignalingMessage(msg);
        });
    m_peerConnection->CreateAnswer(observer, options);
}

void WebRTCManager::pushLocalFrame(const QImage &frame)
{
    if (m_localVideoSource) {
        m_localVideoSource->pushFrame(frame);
    }
}

void WebRTCManager::sendDataChannelMessage(const QJsonObject &message)
{
    if (!m_dataChannel || !m_dataChannelReady) {
        qWarning() << "Data channel hali tayyor emas — xabar yuborilmadi";
        return;
    }
    QByteArray json = QJsonDocument(message).toJson(QJsonDocument::Compact);
    webrtc::DataBuffer buffer(rtc::CopyOnWriteBuffer(json.constData(), json.size()), false);
    m_dataChannel->Send(buffer);
}