#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H

#include <QObject>
#include <QTimer>
#include <QImage>
#include <QElapsedTimer>

class ScreenCapture : public QObject
{
    Q_OBJECT
public:
    explicit ScreenCapture(QObject *parent = nullptr);
    ~ScreenCapture();

    void startCapture(int fps = 20, int quality = 75);
    void stopCapture();
    bool isCapturing() const;

signals:
    void frameCaptured(const QImage &frame);
    void frameCapturedCompressed(const QByteArray &jpegData, int width, int height);
    void error(const QString &message);

private slots:
    void onTimer();

private:
    QImage captureFrame();
    QTimer *m_timer;
    int m_fps = 20;
    int m_quality = 75;
    bool m_capturing = false;
};

#endif