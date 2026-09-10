#pragma once

#include <QObject>
#include <QTcpServer>
#include <QDate>
#include <QJsonObject>
#include <QTime>
#include <QString>

class QTcpSocket;

class FinishService : public QObject
{
    Q_OBJECT
public:
    explicit FinishService(QObject *parent = nullptr);
    bool start();

private slots:
    void onNewConnection();

private:
    struct Context {
        QDate date;
        QString weekday;
        QString timezone;
        QString locationLabel;
        bool timeVisible = true;
        QString timeDisplay;
        QString timePolicy;
    };

    struct Safety {
        bool ok = false;
        quint64 freeBytes = 0;
        int batteryPercent = -1;
        QString reason;
    };

    struct WeatherDay {
        bool known = false;
        bool rainy = false;
        double rainMm = 0.0;
        int precipitationProbability = 0;
        QString source;
        QString error;
    };

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
    QString deadlineResponse(const Context &context) const;
    QString codingStateResponse(const Context &context, bool asJson) const;
    bool specialDay(const QDate &date) const;
    int sunsetOffsetMinutes(const Context &context) const;
    void attemptTgRegistrationOnce();
    QString compactJson(const QJsonObject &obj) const;
    QString batteryFeaturePolicy(const QString &feature) const;
    void recordBatteryActivity(const QString &category) const;
};
