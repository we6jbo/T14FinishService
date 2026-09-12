/*
 * T14FinishService core service declaration
 * -----------------------------------------
 * This header is the maintenance map for the background service.  The small
 * structs carry parsed machine context, safety state, weather, and final timing
 * decisions.  Private methods are grouped by responsibility: context/safety,
 * weather/cache/sunset, decision formatting, TG registration, and battery policy.
 *
 * When adding a new command:
 *   1. Declare any helper here.
 *   2. Implement it in finishservice.cpp.
 *   3. Add command parsing in handleCommand().
 *   4. If users invoke it through t14-finish, update scripts/t14-finish too.
 */
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QDate>
#include <QJsonObject>
#include <QTime>
#include <QString>

class QTcpSocket;

// Long-running localhost-only Qt service.
class FinishService : public QObject
{
    Q_OBJECT
public:
    explicit FinishService(QObject *parent = nullptr);
    bool start();

private slots:
    void onNewConnection();

private:
    // Parsed WE6JBO context. Never infer/display time when timeVisible is false.
    struct Context {
        QDate date;
        QString weekday;
        QString timezone;
        QString locationLabel;
        bool timeVisible = true;
        QString timeDisplay;
        QString timePolicy;
    };

    // Disk/battery snapshot. Disk is a hard write gate; battery is adaptive.
    struct Safety {
        bool ok = false;
        quint64 freeBytes = 0;
        int batteryPercent = -1;
        QString reason;
    };

    // One day of weather/sunset information, from Internet or cache.
    struct WeatherDay {
        bool known = false;
        bool rainy = false;
        double rainMm = 0.0;
        int precipitationProbability = 0;
        QTime sunset;
        bool fromCache = false;
        QString source;
        QString error;
    };

    // Final session decision used by human and machine-readable responses.
    struct Decision {
        bool ok = false;
        QString state;
        QString reason;
        QString currentDisplay;
        QTime currentTime;
        QTime deadline;
        QTime sunset;
        int minutesRemaining = 0;
        int offsetMinutes = 0;
        bool rainKnown = false;
        bool rainy = false;
        double rainMm = 0.0;
        int precipitationProbability = 0;
        QString weatherSource;
        QString weatherError;
    };

    // TCP server is bound only to QHostAddress::LocalHost on port 45454.
    QTcpServer m_server;
    QString m_projectRoot;

    QString contextPath() const;
    Context loadContext(QString *error = nullptr) const;
    Safety checkSafety() const;
    int batteryPercent() const;
    WeatherDay fetchWeather(const Context &context) const;
    WeatherDay readWeatherCache(const QDate &date, int maxAgeSeconds) const;
    void writeWeatherCache(const QJsonObject &dailyObject, double latitude, double longitude, const QString &source) const;
    QTime cachedSunset(const QDate &date, double latitude, double longitude, QString *error = nullptr) const;
    void ensureSunsetCache(const QDate &anchorDate, double latitude, double longitude) const;
    QTime localSunset(const QDate &date, double latitude, double longitude, QString *error = nullptr) const;
    QString cacheDirectory() const;
    bool currentTimeFromContext(const Context &context, QTime *time, QString *display, QString *error) const;
    Decision makeDecision(const Context &context) const;
    QString handleCommand(const QString &command);
    QString deadlineResponse(const Context &context, int warningMinutes = 90) const;
    QString codingStateResponse(const Context &context, bool asJson) const;
    bool specialDay(const QDate &date) const;
    int sunsetOffsetMinutes(const Context &context) const;
    void attemptTgRegistrationOnce();
    QString compactJson(const QJsonObject &obj) const;
    QString batteryFeaturePolicy(const QString &feature) const;
    void recordBatteryActivity(const QString &category) const;
};
