#pragma once

#include <QObject>
#include <QTcpServer>
#include <QJsonObject>
#include <QDate>
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
        bool ok = false;
        QTime sunset;
        double rainMm = 0.0;
        int precipitationProbability = 0;
        QString error;
    };

    QTcpServer m_server;
    QString m_projectRoot;

    QString contextPath() const;
    Context loadContext(QString *error = nullptr) const;
    Safety checkSafety() const;
    int batteryPercent() const;
    WeatherDay fetchWeather(const Context &context) const;
    QString handleCommand(const QString &command);
    QString deadlineResponse(const Context &context);
    bool specialDay(const QDate &date) const;
    int sunsetOffsetMinutes(const Context &context) const;
    void attemptTgRegistrationOnce();
    QString compactJson(const QJsonObject &obj) const;
};
