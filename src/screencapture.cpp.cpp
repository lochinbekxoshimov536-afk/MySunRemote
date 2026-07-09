#include "screencapture.h"
#include <QGuiApplication>
#include <QScreen>
#include <QBuffer>
#include <QDebug>

ScreenCapture::ScreenCapture(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ScreenCapture::onTimer);
}
ScreenCapture::~ScreenCapture() { stopCapture(); }

void ScreenCapture::startCapture(int fps, int quality)
{
    if (m_capturing) return;
    m_fps = qBound(1, fps, 60);
    m_quality = qBound(10, quality, 100);
    m_capturing = true;
    m_timer->start(1000 / m_fps);
    qDebug() << "ScreenCapture started, FPS:" << m_fps;
}

void ScreenCapture::stopCapture()
{
    if (!m_capturing) return;
    m_timer->stop();
    m_capturing = false;
    qDebug() << "ScreenCapture stopped";
}

bool ScreenCapture::isCapturing() const { return m_capturing; }

QImage ScreenCapture::captureFrame()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        emit error("No screen found");
        return QImage();
    }
    QPixmap pm = screen->grabWindow(0);
    return pm.toImage();
}

void ScreenCapture::onTimer()
{
    QImage img = captureFrame();
    if (img.isNull()) {
        emit error("Frame capture failed");
        return;
    }
    img = img.convertToFormat(QImage::Format_RGB32);
    emit frameCaptured(img);

    QByteArray jpeg;
    QBuffer buf(&jpeg);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "JPEG", m_quality);
    buf.close();
    emit frameCapturedCompressed(jpeg, img.width(), img.height());
}