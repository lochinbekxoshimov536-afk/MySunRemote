#ifndef INPUTCONTROL_H
#define INPUTCONTROL_H

#include <QObject>
#include <QPoint>
#include <QString>

class InputControl : public QObject
{
    Q_OBJECT
public:
    explicit InputControl(QObject *parent = nullptr);
    ~InputControl();

    void moveMouse(double normalizedX, double normalizedY);
    void clickMouse(int button, bool press);
    void scrollMouse(int delta);
    void keyEvent(int qtKeyCode, const QString &text, bool press);

    static QPoint currentScreenSize();

private:
#ifdef Q_OS_LINUX
    void *m_display = nullptr;
    bool initX11();
#endif
};

#endif