#include "desktopcontroller.h"

#include <QGuiApplication>
#include <QCursor>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QQuickWindow>
#include <QScreen>
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace {
xcb_connection_t *connection()
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    return native ? native->connection() : nullptr;
}

xcb_atom_t atom(xcb_connection_t *conn, const QByteArray &name)
{
    auto *reply = xcb_intern_atom_reply(conn, xcb_intern_atom(conn, false, name.size(), name.constData()), nullptr);
    const auto result = reply ? reply->atom : XCB_ATOM_NONE;
    std::free(reply);
    return result;
}
}

DesktopController::DesktopController(bool demo, bool requested, QObject *parent)
    : QObject(parent), m_settings("Dayline", demo ? "preview" : "dayline"),
      m_requested(requested || m_settings.value("desktop/enabled", false).toBool())
{
    auto *conn = connection();
    if (!conn) {
        m_supportMessage = "桌面模式需要 X11 或 XWayland；当前 Qt 使用原生 Wayland。";
        return;
    }
    const auto root = xcb_setup_roots_iterator(xcb_get_setup(conn)).data->root;
    auto *reply = xcb_get_property_reply(conn,
        xcb_get_property(conn, false, root, atom(conn, "_NET_SUPPORTED"), XCB_ATOM_ATOM, 0, 4096), nullptr);
    if (reply && reply->format == 32) {
        const auto *values = static_cast<xcb_atom_t *>(xcb_get_property_value(reply));
        const auto count = xcb_get_property_value_length(reply) / int(sizeof(xcb_atom_t));
        m_supported = true;
        for (const auto &name : {"_NET_WM_WINDOW_TYPE_DESKTOP", "_NET_WM_STATE_BELOW",
                                "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER", "_NET_WM_DESKTOP"}) {
            if (std::find(values, values + count, atom(conn, name)) == values + count)
                m_supported = false;
        }
    }
    std::free(reply);
    m_protocolSupported = m_supported;
    const bool wayland = qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland" || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    m_needsGnomeBridge = wayland && qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains("GNOME", Qt::CaseInsensitive);
    if (m_needsGnomeBridge) {
        auto *watcher = new QDBusServiceWatcher("org.dayline.DesktopBridge", QDBusConnection::sessionBus(),
            QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
        connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] { updateSupport(); });
        connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { updateSupport(); });
    }
    updateSupport();
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(350);
    connect(&m_saveTimer, &QTimer::timeout, this, &DesktopController::saveGeometry);
    m_bridgeTimer.setSingleShot(true);
    m_bridgeTimer.setInterval(4000);
    connect(&m_bridgeTimer, &QTimer::timeout, this, [this] {
        cancelDesktopPreparation();
        emit desktopActivationFailed();
    });
}

void DesktopController::attach(QQuickWindow *manager, QQuickWindow *desktop)
{
    m_manager = manager;
    m_desktop = desktop;
    if (m_manager)
        connect(m_manager, &QWindow::visibleChanged, this, [this](bool visible) {
            if (!visible) cancelDesktopPreparation();
        });
    if (!m_desktop || !m_protocolSupported) return;
    m_desktop->setTransientParent(nullptr);
    const QRect saved = m_settings.value("desktop/geometry").toRect();
    if (saved.isValid()) m_desktop->setGeometry(saved);
    else resetPosition();
    constrainGeometry();
    m_desktop->create();
    applyDesktopHints();
    const auto scheduleSave = [this] { if (m_enabled) m_saveTimer.start(); };
    connect(m_desktop, &QWindow::xChanged, this, scheduleSave);
    connect(m_desktop, &QWindow::yChanged, this, scheduleSave);
    connect(m_desktop, &QWindow::widthChanged, this, scheduleSave);
    connect(m_desktop, &QWindow::heightChanged, this, scheduleSave);
    const auto watchScreen = [this](QScreen *screen) {
        if (!screen) return;
        connect(screen, &QScreen::availableGeometryChanged, this, [this] { constrainGeometry(); });
    };
    watchScreen(qGuiApp->primaryScreen());
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, [this, watchScreen](QScreen *screen) {
        if (m_desktop) m_desktop->setScreen(screen);
        watchScreen(screen);
        constrainGeometry();
    });
    if (m_requested && m_supported) showDesktop();
}

void DesktopController::updateSupport()
{
    m_supported = m_protocolSupported;
    if (!m_protocolSupported) {
        m_supportMessage = "当前窗口管理器未提供日笺需要的 X11 桌面窗口协议。";
    } else if (m_needsGnomeBridge) {
        QDBusInterface bridge("org.dayline.DesktopBridge", "/org/dayline/DesktopBridge", "org.dayline.DesktopBridge");
        bridge.setTimeout(1500);
        const QDBusReply<uint> version = bridge.call("GetVersion");
        m_supported = version.isValid() && version.value() == 1;
        m_supportMessage = m_supported ? "GNOME 桌面桥已连接，月历可在桌面图标层上接收操作。"
            : "GNOME Wayland 需要 Dayline Desktop Bridge 扩展。首次安装扩展后，请重新登录桌面；未加载扩展时无法操作桌面月历。";
    } else {
        m_supportMessage = "使用 X11 桌面窗口协议；UOS DDE 的层级和交互需在实机确认。";
    }
    if (!m_supported && m_enabled) setEnabled(false);
    emit supportedChanged();
    if (m_supported && m_showWhenSupported) {
        showDesktop();
    }
}

bool DesktopController::enableGnomeBridge()
{
    QDBusInterface extensions("org.gnome.Shell.Extensions", "/org/gnome/Shell/Extensions", "org.gnome.Shell.Extensions");
    extensions.setTimeout(3000);
    const QDBusReply<bool> result = extensions.call("EnableExtension", "dayline-desktop@dayline");
    if (result.isValid() && result.value()) {
        updateSupport();
        return true;
    }
    m_supportMessage = "请重新登录桌面后，再点击“贴到桌面”。";
    emit supportedChanged();
    return false;
}

void DesktopController::cancelDesktopPreparation()
{
    if (!m_showWhenSupported) return;
    m_showWhenSupported = false;
    m_bridgeTimer.stop();
    emit preparingDesktopChanged();
}

void DesktopController::applyDesktopHints()
{
    auto *conn = connection();
    const auto id = m_desktop->winId();
    const xcb_atom_t type = atom(conn, "_NET_WM_WINDOW_TYPE_DESKTOP");
    const QByteArray role("dayline-desktop");
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, id, atom(conn, "WM_WINDOW_ROLE"), XCB_ATOM_STRING, 8, role.size(), role.constData());
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, id, atom(conn, "_NET_WM_WINDOW_TYPE"), XCB_ATOM_ATOM, 32, 1, &type);
    const xcb_atom_t states[] = { atom(conn, "_NET_WM_STATE_BELOW"), atom(conn, "_NET_WM_STATE_SKIP_TASKBAR"),
                                 atom(conn, "_NET_WM_STATE_SKIP_PAGER"), atom(conn, "_NET_WM_STATE_STICKY") };
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, id, atom(conn, "_NET_WM_STATE"), XCB_ATOM_ATOM, 32, 4, states);
    const uint32_t allDesktops = std::numeric_limits<uint32_t>::max();
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, id, atom(conn, "_NET_WM_DESKTOP"), XCB_ATOM_CARDINAL, 32, 1, &allDesktops);
    xcb_flush(conn);
}

bool DesktopController::locked() const { return m_settings.value("desktop/locked", true).toBool(); }
qreal DesktopController::panelOpacity() const { return m_settings.value("desktop/opacity", 0.82).toReal(); }

void DesktopController::setLocked(bool locked)
{
    if (locked) endInteraction();
    m_settings.setValue("desktop/locked", locked);
    emit appearanceChanged();
}

void DesktopController::setPanelOpacity(qreal opacity)
{
    m_settings.setValue("desktop/opacity", std::clamp(opacity, 0.25, 1.0));
    emit appearanceChanged();
}

void DesktopController::setEnabled(bool enabled)
{
    if ((enabled && !m_supported) || !m_desktop || m_enabled == enabled) return;
    m_enabled = enabled;
    m_settings.setValue("desktop/enabled", enabled);
    if (enabled) {
        constrainGeometry();
        applyDesktopHints();
        m_desktop->show();
    } else {
        saveGeometry();
        openManager();
        m_desktop->hide();
    }
    emit enabledChanged();
}

void DesktopController::openManager()
{
    if (!m_manager) return;
    m_manager->showNormal();
    m_manager->raise();
    m_manager->requestActivate();
}

bool DesktopController::showDesktop()
{
    if (!m_supported) {
        if (!m_protocolSupported || !m_needsGnomeBridge) return false;
        if (m_showWhenSupported) return true;
        m_showWhenSupported = true;
        emit preparingDesktopChanged();
        m_bridgeTimer.start();
        if (!enableGnomeBridge()) {
            cancelDesktopPreparation();
            return false;
        }
        return true;
    }
    cancelDesktopPreparation();
    setEnabled(true);
    if (m_manager) m_manager->hide();
    return true;
}

void DesktopController::resetPosition()
{
    if (!m_desktop || !qGuiApp->primaryScreen()) return;
    const QRect area = qGuiApp->primaryScreen()->availableGeometry();
    const QSize size(qMin(1050, area.width() - 96), qMin(740, area.height() - 64));
    m_desktop->setGeometry(QRect(QPoint(area.right() - size.width() + 1, area.top()), size));
    constrainGeometry();
}

void DesktopController::constrainGeometry()
{
    if (!m_desktop || !qGuiApp->primaryScreen()) return;
    const QRect rect = constrainedGeometry(m_desktop->geometry());
    if (rect != m_desktop->geometry()) m_desktop->setGeometry(rect);
}

QRect DesktopController::constrainedGeometry(QRect rect) const
{
    if (!qGuiApp->primaryScreen()) return rect;
    const QRect area = qGuiApp->primaryScreen()->availableGeometry();
    rect.setSize(rect.size().boundedTo(area.size()));
    rect.moveLeft(qBound(area.left(), rect.left(), area.right() - rect.width() + 1));
    rect.moveTop(qBound(area.top(), rect.top(), area.bottom() - rect.height() + 1));
    return rect;
}

void DesktopController::beginMove()
{
    if (locked() || !m_desktop) return;
    m_interaction = Interaction::Move;
    m_pointerOrigin = QCursor::pos();
    m_geometryOrigin = m_desktop->geometry();
}

void DesktopController::beginResize()
{
    if (locked() || !m_desktop) return;
    m_interaction = Interaction::Resize;
    m_pointerOrigin = QCursor::pos();
    m_geometryOrigin = m_desktop->geometry();
}

void DesktopController::updateInteraction()
{
    if (locked() || !m_desktop || m_interaction == Interaction::None) return;
    // Read the global pointer independently of the moving window's local
    // coordinate system, and issue one final geometry request per motion.
    const QPoint delta = QCursor::pos() - m_pointerOrigin;
    QRect target = m_geometryOrigin;
    if (m_interaction == Interaction::Move) {
        target.translate(delta);
    } else {
        target.setSize(QSize(qMax(700, target.width() + delta.x()),
                             qMax(520, target.height() + delta.y())));
    }
    target = constrainedGeometry(target);
    if (target != m_desktop->geometry()) m_desktop->setGeometry(target);
}

void DesktopController::endInteraction()
{
    if (m_interaction == Interaction::None) return;
    m_interaction = Interaction::None;
    m_saveTimer.start();
}

void DesktopController::saveGeometry()
{
    if (m_desktop) m_settings.setValue("desktop/geometry", m_desktop->geometry());
}
