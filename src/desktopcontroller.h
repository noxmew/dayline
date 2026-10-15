#pragma once

#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QTimer>

class QQuickWindow;

class DesktopController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(QString supportMessage READ supportMessage NOTIFY supportedChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY appearanceChanged)
    Q_PROPERTY(qreal panelOpacity READ panelOpacity WRITE setPanelOpacity NOTIFY appearanceChanged)
    Q_PROPERTY(bool startOnDesktop READ startOnDesktop CONSTANT)
    Q_PROPERTY(bool needsGnomeBridge READ needsGnomeBridge CONSTANT)

public:
    explicit DesktopController(bool demo, bool requested, QObject *parent = nullptr);
    void attach(QQuickWindow *manager, QQuickWindow *desktop);
    bool supported() const { return m_supported; }
    QString supportMessage() const { return m_supportMessage; }
    bool enabled() const { return m_enabled; }
    bool locked() const;
    qreal panelOpacity() const;
    bool startOnDesktop() const { return m_requested && m_supported; }
    bool needsGnomeBridge() const { return m_needsGnomeBridge; }
    void setEnabled(bool enabled);
    void setLocked(bool locked);
    void setPanelOpacity(qreal opacity);

    Q_INVOKABLE void openManager();
    Q_INVOKABLE void showDesktop();
    Q_INVOKABLE void resetPosition();
    Q_INVOKABLE void moveBy(int dx, int dy);
    Q_INVOKABLE void resizeBy(int dx, int dy);
    Q_INVOKABLE void enableGnomeBridge();

signals:
    void supportedChanged();
    void enabledChanged();
    void appearanceChanged();

private:
    void constrainGeometry();
    void saveGeometry();
    void applyDesktopHints();
    void updateSupport();
    QSettings m_settings;
    QPointer<QQuickWindow> m_manager;
    QPointer<QQuickWindow> m_desktop;
    QTimer m_saveTimer;
    bool m_supported = false;
    bool m_protocolSupported = false;
    bool m_needsGnomeBridge = false;
    bool m_requested;
    bool m_enabled = false;
    QString m_supportMessage;
};
