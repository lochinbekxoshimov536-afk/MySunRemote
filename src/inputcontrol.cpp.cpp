#include "inputcontrol.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>

InputControl::InputControl(QObject *parent) : QObject(parent) {}
InputControl::~InputControl() {}

QPoint InputControl::currentScreenSize()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    return screen ? screen->size() : QPoint(1920, 1080);
}

void InputControl::moveMouse(double normalizedX, double normalizedY)
{
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dx = static_cast<LONG>(normalizedX * 65535);
    input.mi.dy = static_cast<LONG>(normalizedY * 65535);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &input, sizeof(INPUT));
}

void InputControl::clickMouse(int button, bool press)
{
    INPUT input = {};
    input.type = INPUT_MOUSE;
    switch (button) {
    case 1: input.mi.dwFlags = press ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
    case 2: input.mi.dwFlags = press ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
    case 3: input.mi.dwFlags = press ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    default: return;
    }
    SendInput(1, &input, sizeof(INPUT));
}

void InputControl::scrollMouse(int delta)
{
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = delta * WHEEL_DELTA;
    SendInput(1, &input, sizeof(INPUT));
}

static WORD qtKeyToVk(int qtKey)
{
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) return static_cast<WORD>(qtKey);
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) return static_cast<WORD>(qtKey);
    switch (qtKey) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Return: case Qt::Key_Enter: return VK_RETURN;
    case Qt::Key_Backspace: return VK_BACK;
    case Qt::Key_Tab: return VK_TAB;
    case Qt::Key_Escape: return VK_ESCAPE;
    case Qt::Key_Shift: return VK_SHIFT;
    case Qt::Key_Control: return VK_CONTROL;
    case Qt::Key_Alt: return VK_MENU;
    case Qt::Key_Left: return VK_LEFT;
    case Qt::Key_Right: return VK_RIGHT;
    case Qt::Key_Up: return VK_UP;
    case Qt::Key_Down: return VK_DOWN;
    case Qt::Key_Delete: return VK_DELETE;
    default: return 0;
    }
}

void InputControl::keyEvent(int qtKeyCode, const QString &text, bool press)
{
    Q_UNUSED(text);
    WORD vk = qtKeyToVk(qtKeyCode);
    if (vk == 0) return;
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = press ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

#elif defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

InputControl::InputControl(QObject *parent) : QObject(parent) { initX11(); }
InputControl::~InputControl() { if (m_display) XCloseDisplay(static_cast<Display*>(m_display)); }
bool InputControl::initX11()
{
    m_display = XOpenDisplay(nullptr);
    if (!m_display) qWarning() << "X display ochilmadi! Wayland?";
    return m_display != nullptr;
}
QPoint InputControl::currentScreenSize()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    return screen ? screen->size() : QPoint(1920, 1080);
}
void InputControl::moveMouse(double nx, double ny)
{
    if (!m_display) return;
    QPoint s = currentScreenSize();
    int x = nx * s.x(), y = ny * s.y();
    XTestFakeMotionEvent(static_cast<Display*>(m_display), -1, x, y, CurrentTime);
    XFlush(static_cast<Display*>(m_display));
}
void InputControl::clickMouse(int button, bool press)
{
    if (!m_display) return;
    unsigned int xbtn = (button==2) ? 3 : (button==3) ? 2 : 1;
    XTestFakeButtonEvent(static_cast<Display*>(m_display), xbtn, press?True:False, CurrentTime);
    XFlush(static_cast<Display*>(m_display));
}
void InputControl::scrollMouse(int delta)
{
    if (!m_display) return;
    unsigned int btn = (delta>0) ? 4 : 5;
    Display *d = static_cast<Display*>(m_display);
    XTestFakeButtonEvent(d, btn, True, CurrentTime);
    XTestFakeButtonEvent(d, btn, False, CurrentTime);
    XFlush(d);
}
static KeySym qtKeyToKeysym(int qtKey)
{
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) return XK_a + (qtKey - Qt::Key_A);
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) return XK_0 + (qtKey - Qt::Key_0);
    switch (qtKey) {
    case Qt::Key_Space: return XK_space;
    case Qt::Key_Return: case Qt::Key_Enter: return XK_Return;
    case Qt::Key_Backspace: return XK_BackSpace;
    case Qt::Key_Tab: return XK_Tab;
    case Qt::Key_Escape: return XK_Escape;
    case Qt::Key_Shift: return XK_Shift_L;
    case Qt::Key_Control: return XK_Control_L;
    case Qt::Key_Alt: return XK_Alt_L;
    case Qt::Key_Left: return XK_Left;
    case Qt::Key_Right: return XK_Right;
    case Qt::Key_Up: return XK_Up;
    case Qt::Key_Down: return XK_Down;
    case Qt::Key_Delete: return XK_Delete;
    default: return NoSymbol;
    }
}
void InputControl::keyEvent(int qtKeyCode, const QString &text, bool press)
{
    Q_UNUSED(text);
    if (!m_display) return;
    KeySym sym = qtKeyToKeysym(qtKeyCode);
    if (sym == NoSymbol) return;
    KeyCode code = XKeysymToKeycode(static_cast<Display*>(m_display), sym);
    if (!code) return;
    XTestFakeKeyEvent(static_cast<Display*>(m_display), code, press?True:False, CurrentTime);
    XFlush(static_cast<Display*>(m_display));
}

#elif defined(Q_OS_MAC)
#include <ApplicationServices/ApplicationServices.h>
InputControl::InputControl(QObject *parent) : QObject(parent) {}
InputControl::~InputControl() {}
QPoint InputControl::currentScreenSize()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    return screen ? screen->size() : QPoint(1920, 1080);
}
void InputControl::moveMouse(double nx, double ny)
{
    QPoint s = currentScreenSize();
    CGPoint p = CGPointMake(nx*s.x(), ny*s.y());
    CGEventRef e = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, p, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}
void InputControl::clickMouse(int button, bool press)
{
    CGEventType type;
    CGMouseButton btn;
    switch(button) {
    case 1: type = press ? kCGEventLeftMouseDown : kCGEventLeftMouseUp; btn = kCGMouseButtonLeft; break;
    case 2: type = press ? kCGEventRightMouseDown : kCGEventRightMouseUp; btn = kCGMouseButtonRight; break;
    default: type = press ? kCGEventOtherMouseDown : kCGEventOtherMouseUp; btn = kCGMouseButtonCenter; break;
    }
    CGEventRef e = CGEventCreate(nullptr);
    CGPoint pos = CGEventGetLocation(e);
    CFRelease(e);
    CGEventRef click = CGEventCreateMouseEvent(nullptr, type, pos, btn);
    CGEventPost(kCGHIDEventTap, click);
    CFRelease(click);
}
void InputControl::scrollMouse(int delta)
{
    CGEventRef e = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 1, delta);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}
static CGKeyCode qtKeyToMacKeycode(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_A: return 0; case Qt::Key_S: return 1; case Qt::Key_D: return 2;
    case Qt::Key_Space: return 49;
    case Qt::Key_Return: case Qt::Key_Enter: return 36;
    case Qt::Key_Backspace: return 51;
    case Qt::Key_Tab: return 48;
    case Qt::Key_Escape: return 53;
    case Qt::Key_Left: return 123;
    case Qt::Key_Right: return 124;
    case Qt::Key_Down: return 125;
    case Qt::Key_Up: return 126;
    default: return 0xFF;
    }
}
void InputControl::keyEvent(int qtKeyCode, const QString &text, bool press)
{
    Q_UNUSED(text);
    CGKeyCode code = qtKeyToMacKeycode(qtKeyCode);
    if (code == 0xFF) return;
    CGEventRef e = CGEventCreateKeyboardEvent(nullptr, code, press);
    CGEventPost(kCGHIDEventTap, e);
    CFRelease(e);
}
#endif