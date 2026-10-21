#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSettings>
#include <QTimer>

class QQuickWindow;

class DesktopController final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(QString supportMessage READ supportMessage NOTIFY supportedChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY appearanceChanged)
    Q_PROPERTY(qreal panelOpacity READ panelOpacity WRITE setPanelOpacity NOTIFY appearanceChanged)
    Q_PROPERTY(bool startOnDesktop READ startOnDesktop CONSTANT)
    Q_PROPERTY(bool needsGnomeBridge READ needsGnomeBridge CONSTANT)
    Q_PROPERTY(bool preparingDesktop READ preparingDesktop NOTIFY preparingDesktopChanged)

public:
    explicit DesktopController(bool demo, bool requested, QObject *parent = nullptr);
    ~DesktopController() override;
    void attach(QQuickWindow *manager, QQuickWindow *desktop);
    bool supported() const { return m_supported; }
    QString supportMessage() const { return m_supportMessage; }
    bool enabled() const { return m_enabled; }
    bool locked() const;
    qreal panelOpacity() const;
    bool startOnDesktop() const { return m_requested && m_supported && (!m_ddeSession || m_ddeDesktopWindow); }
    bool needsGnomeBridge() const { return m_needsGnomeBridge; }
    bool preparingDesktop() const { return m_showWhenSupported; }
    void setEnabled(bool enabled);
    void setLocked(bool locked);
    void setPanelOpacity(qreal opacity);

    Q_INVOKABLE void openManager();
    Q_INVOKABLE bool showDesktop();
    Q_INVOKABLE void resetPosition();
    Q_INVOKABLE void beginMove();
    Q_INVOKABLE void beginResize();
    Q_INVOKABLE void updateInteraction();
    Q_INVOKABLE void endInteraction();

signals:
    void supportedChanged();
    void enabledChanged();
    void appearanceChanged();
    void preparingDesktopChanged();
    void desktopActivationFailed();

private:
    void constrainGeometry();
    QRect constrainedGeometry(QRect rect) const;
    void saveGeometry();
    void applyDesktopHints();
    void applyDdeDesktopParent();
    void raiseDdeWaylandDesktop();
    void updateDdeDesktopWindow();
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
    void updateSupport();
    bool enableGnomeBridge();
    void cancelDesktopPreparation();
    QSettings m_settings;
    QPointer<QQuickWindow> m_manager;
    QPointer<QQuickWindow> m_desktop;
    QTimer m_saveTimer;
    QTimer m_bridgeTimer;
    bool m_supported = false;
    bool m_protocolSupported = false;
    bool m_needsGnomeBridge = false;
    bool m_ddeSession = false;
    bool m_ddeWaylandSession = false;
    quint32 m_rootWindow = 0;
    quint32 m_clientListAtom = 0;
    quint32 m_ddeDesktopWindow = 0;
    bool m_requested;
    bool m_enabled = false;
    bool m_showWhenSupported = false;
    QString m_supportMessage;
    enum class Interaction { None, Move, Resize };
    Interaction m_interaction = Interaction::None;
    QPoint m_pointerOrigin;
    QRect m_geometryOrigin;
};
