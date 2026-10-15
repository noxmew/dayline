#pragma once

#include <QDate>
#include <QObject>
#include <QSettings>
#include <QSet>
#include <QSqlDatabase>
#include <QVariantList>

class ICloudSync;

class CalendarStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString monthLabel READ monthLabel NOTIFY changed)
    Q_PROPERTY(QString monthEnglish READ monthEnglish NOTIFY changed)
    Q_PROPERTY(QString selectedDate READ selectedDate NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedHeading READ selectedHeading NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedWeekday READ selectedWeekday NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedDay READ selectedDay NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedIsToday READ selectedIsToday NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList monthDays READ monthDays NOTIFY changed)
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY changed)
    Q_PROPERTY(QVariantList writableCalendars READ writableCalendars NOTIFY changed)
    Q_PROPERTY(QVariantList selectedEvents READ selectedEvents NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList upcomingEvents READ upcomingEvents NOTIFY changed)
    Q_PROPERTY(QVariantList visibleEvents READ visibleEvents NOTIFY changed)
    Q_PROPERTY(int monthCount READ monthCount NOTIFY changed)
    Q_PROPERTY(int weekCount READ weekCount NOTIFY changed)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(bool demo READ demo CONSTANT)

public:
    explicit CalendarStore(bool demo, QObject *parent = nullptr);
    bool initialize();
    QString monthLabel() const;
    QString monthEnglish() const;
    QString selectedDate() const { return m_selected.toString(Qt::ISODate); }
    QString selectedHeading() const;
    QString selectedWeekday() const;
    QString selectedDay() const { return QString::number(m_selected.day()); }
    bool selectedIsToday() const { return m_selected == QDate::currentDate(); }
    QVariantList monthDays() const;
    QVariantList calendars() const;
    QVariantList writableCalendars() const;
    void attachICloud(ICloudSync *sync);
    QString databasePath() const { return m_db.databaseName(); }
    QVariantList selectedEvents() const;
    QVariantList upcomingEvents() const;
    QVariantList visibleEvents() const;
    int monthCount() const;
    int weekCount() const;
    QString searchQuery() const { return m_search; }
    void setSearchQuery(const QString &query);
    QString error() const { return m_error; }
    bool darkMode() const { return m_settings.value("appearance/dark", false).toBool(); }
    void setDarkMode(bool dark);
    bool demo() const { return m_demo; }

    Q_INVOKABLE void changeMonth(int offset);
    Q_INVOKABLE void goToday();
    Q_INVOKABLE void selectDate(const QString &date);
    Q_INVOKABLE void toggleCalendar(const QString &id);
    Q_INVOKABLE bool saveEvent(const QVariantMap &event);
    Q_INVOKABLE bool deleteEvent(const QString &id);
    Q_INVOKABLE void clearError();

signals:
    void changed();
    void selectionChanged();
    void errorChanged();
    void darkModeChanged();

private:
    void reload();
    void seedPreview();
    void setError(const QString &message);
    QVariantList eventsOn(const QDate &date) const;
    bool includes(const QVariantMap &event) const;
    static QString calendarColor(const QString &id);
    static QString calendarName(const QString &id);

    bool m_demo;
    QDate m_month;
    QDate m_selected;
    QSqlDatabase m_db;
    QSettings m_settings;
    QVariantList m_events;
    QSet<QString> m_enabled {"work", "life", "ideas"};
    QString m_search;
    QString m_error;
    ICloudSync *m_icloud = nullptr;
};
