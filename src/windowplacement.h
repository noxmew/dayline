#pragma once

#include <QObject>
#include <QWindow>
#include <QtQml/qqmlregistration.h>

class WindowPlacement final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit WindowPlacement(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void center(QWindow *window, QWindow *origin);
};
