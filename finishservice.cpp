#include "finishservice.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

namespace {
constexpr quint16 kPort = 45454;
constexpr quint64 kMinimumFreeBytes = 10ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr int kMinimumBatteryPercent = 55;
constexpr const char *kProjectId = "t14-finish-service-v1";
constexpr const char *kCodes = "TG564843,TG333041,TG323932,TG610982,TG148675";
constexpr double kDefaultLatitude = 32.7157;
constexpr double kDefaultLongitude = -117.1611;
constexpr const char *kDefaultTimeZone = "America/Los_Angeles";
}

FinishService::FinishService(QObject *parent) : QObject(parent)
{
    m_projectRoot = QDir(QCoreApplication::applicationDirPath()).absolutePath();
}

bool FinishService::start()
{
    if (!m_server.listen(QHostAddress::LocalHost, kPort))
        return false;

    connect(&m_server, &QTcpServer::newConnection, this, &FinishService::onNewConnection);

    const Safety safety = checkSafety();
    if (safety.ok)
        attemptTgRegistrationOnce();

    return true;
}

void FinishService::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (!socket)
            continue;

        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            const QString command = QString::fromUtf8(socket->readAll()).trimmed();
            const QString response = handleCommand(command);
            socket->write(response.toUtf8());
            socket->write("\n");
            socket->flush();
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

QString FinishService::contextPath() const
{
    const QByteArray env = qgetenv("WE6JBO_CONTEXT_FILE");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    return QDir::homePath() + "/.local/state/we6jbo-context/context.json";
}

FinishService::Context FinishService::loadContext(QString *error) const
{
    Context c;
    QFile f(contextPath());
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = "Cannot read context file: " + f.fileName();
        return c;
    }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = "Invalid context JSON";
        return c;
    }

    const QJsonObject o = doc.object();
    c.date = QDate::fromString(o.value("date").toString(), Qt::ISODate);
    c.weekday = o.value("weekday").toString();
    c.timezone = o.value("timezone").toString();
    c.locationLabel = o.value("location_label").toString();
    const QJsonObject t = o.value("time").toObject();
    c.timeVisible = t.value("visible").toBool(true);
    c.timeDisplay = t.value("display").toString();
    c.timePolicy = t.value("policy").toString();

    if (!c.date.isValid()) {
        if (error) *error = "Context date is missing or invalid";
    }
    return c;
}

int FinishService::batteryPercent() const
{
    QDir power("/sys/class/power_supply");
    const QStringList entries = power.entryList(QStringList() << "BAT*", QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        QFile f(power.filePath(entry + "/capacity"));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        bool ok = false;
        const int value = QString::fromUtf8(f.readAll()).trimmed().toInt(&ok);
        if (ok)
            return value;
    }
    return -1;
}

FinishService::Safety FinishService::checkSafety() const
{
    Safety s;
    QStorageInfo storage(QDir::homePath());
    storage.refresh();
    s.freeBytes = storage.bytesAvailable();
    s.batteryPercent = batteryPercent();

    if (s.freeBytes < kMinimumFreeBytes) {
        s.reason = "less than 10 GiB of free disk space";
        return s;
    }
    if (s.batteryPercent < kMinimumBatteryPercent) {
        s.reason = s.batteryPercent < 0
            ? "battery percentage could not be read"
            : QString("battery is %1%; at least 55% is required").arg(s.batteryPercent);
        return s;
    }
    s.ok = true;
    return s;
}

FinishService::WeatherDay FinishService::fetchWeather(const Context &context) const
{
    WeatherDay out;

    bool latOk = false;
    bool lonOk = false;
    const double latitude = qEnvironmentVariable("T14FINISH_LAT").toDouble(&latOk);
    const double longitude = qEnvironmentVariable("T14FINISH_LON").toDouble(&lonOk);
    const double lat = latOk ? latitude : kDefaultLatitude;
    const double lon = lonOk ? longitude : kDefaultLongitude;
    const QString tz = qEnvironmentVariableIsSet("T14FINISH_TIMEZONE")
        ? qEnvironmentVariable("T14FINISH_TIMEZONE") : QString::fromLatin1(kDefaultTimeZone);

    QUrl url("https://api.open-meteo.com/v1/forecast");
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(lat, 'f', 4));
    query.addQueryItem("longitude", QString::number(lon, 'f', 4));
    query.addQueryItem("daily", "sunset,rain_sum,precipitation_probability_max");
    query.addQueryItem("timezone", tz);
    query.addQueryItem("start_date", context.date.toString(Qt::ISODate));
    query.addQueryItem("end_date", context.date.toString(Qt::ISODate));
    url.setQuery(query);

    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(QNetworkRequest(url));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        out.error = reply->errorString();
        reply->deleteLater();
        return out;
    }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
    reply->deleteLater();
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        out.error = "Weather service returned invalid JSON";
        return out;
    }

    const QJsonObject daily = doc.object().value("daily").toObject();
    const QJsonArray sunsets = daily.value("sunset").toArray();
    const QJsonArray rain = daily.value("rain_sum").toArray();
    const QJsonArray prob = daily.value("precipitation_probability_max").toArray();
    if (sunsets.isEmpty()) {
        out.error = "Weather service did not return sunset data";
        return out;
    }

    const QDateTime sunsetDt = QDateTime::fromString(sunsets.at(0).toString(), Qt::ISODate);
    if (!sunsetDt.isValid()) {
        out.error = "Sunset time was invalid";
        return out;
    }

    out.sunset = sunsetDt.time();
    out.rainMm = rain.isEmpty() ? 0.0 : rain.at(0).toDouble();
    out.precipitationProbability = prob.isEmpty() ? 0 : prob.at(0).toInt();
    out.ok = true;
    return out;
}

bool FinishService::specialDay(const QDate &d) const
{
    if (d.dayOfWeek() == Qt::Saturday || d.dayOfWeek() == Qt::Sunday)
        return true;

    if (d == QDate(2026, 11, 11) || d == QDate(2027, 1, 18) || d == QDate(2027, 5, 31))
        return true;
    if (d >= QDate(2026, 11, 23) && d <= QDate(2026, 11, 27))
        return true;
    if (d >= QDate(2026, 12, 21) && d <= QDate(2027, 1, 1))
        return true;
    if (d >= QDate(2027, 3, 29) && d <= QDate(2027, 4, 5))
        return true;
    return false;
}

int FinishService::sunsetOffsetMinutes(const Context &context) const
{
    if (specialDay(context.date))
        return 250;

    const int day = context.date.dayOfWeek();
    if (day == Qt::Monday || day == Qt::Tuesday)
        return 225;
    return 235;
}

QString FinishService::deadlineResponse(const Context &context)
{
    if (!context.timeVisible)
        return "TIME_HIDDEN: context policy forbids exposing or inferring a user-visible clock time.";

    const Safety safety = checkSafety();
    if (!safety.ok)
        return "STOP: " + safety.reason + ".";

    const WeatherDay weather = fetchWeather(context);
    if (!weather.ok)
        return "ERROR: unable to obtain today's sunset/weather: " + weather.error;

    const int day = context.date.dayOfWeek();
    const bool rainy = weather.rainMm > 0.0 || weather.precipitationProbability >= 50;

    QTime finish;
    QString rule;
    if (rainy && (day == Qt::Monday || day == Qt::Tuesday)) {
        finish = QTime(18, 45);
        rule = "rain override for Monday/Tuesday";
    } else if (rainy && day >= Qt::Wednesday && day <= Qt::Saturday) {
        finish = QTime(16, 45);
        rule = "rain override for Wednesday-Saturday";
    } else {
        const int offset = sunsetOffsetMinutes(context);
        finish = weather.sunset.addSecs(-offset * 60);
        rule = QString("sunset minus %1 minutes").arg(offset);
    }

    QString now = context.timeDisplay;
    if (now.isEmpty())
        now = QTime::currentTime().toString("HH:mm");

    return QString("The time is %1. Finish writing the program by %2. Rule: %3. Sunset: %4. Rain: %5 mm, precipitation probability: %6%.")
        .arg(now,
             finish.toString("h:mm AP"),
             rule,
             weather.sunset.toString("h:mm AP"),
             QString::number(weather.rainMm, 'f', 1),
             QString::number(weather.precipitationProbability));
}

QString FinishService::compactJson(const QJsonObject &obj) const
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString FinishService::handleCommand(const QString &command)
{
    if (command.compare("ping", Qt::CaseInsensitive) == 0)
        return "pong";
    if (command.compare("codes", Qt::CaseInsensitive) == 0)
        return QString::fromLatin1(kCodes);

    const Safety safety = checkSafety();
    if (!safety.ok)
        return "STOP: " + safety.reason + ".";

    QString contextError;
    const Context context = loadContext(&contextError);
    if (!contextError.isEmpty())
        return "ERROR: " + contextError;

    if (command.compare("deadline", Qt::CaseInsensitive) == 0)
        return deadlineResponse(context);

    if (command.compare("status", Qt::CaseInsensitive) == 0) {
        QJsonObject o;
        o["project_id"] = QString::fromLatin1(kProjectId);
        o["date"] = context.date.toString(Qt::ISODate);
        o["weekday"] = context.weekday;
        o["timezone"] = context.timezone;
        o["time_visible"] = context.timeVisible;
        if (context.timeVisible)
            o["time_display"] = context.timeDisplay;
        o["free_gib"] = double(safety.freeBytes) / (1024.0 * 1024.0 * 1024.0);
        o["battery_percent"] = safety.batteryPercent;
        o["codes"] = QString::fromLatin1(kCodes);
        return compactJson(o);
    }

    return "ERROR: commands are ping, status, deadline, codes";
}

void FinishService::attemptTgRegistrationOnce()
{
    const QString stateDir = QDir::homePath() + "/.T14FinishService_backup";
    QDir().mkpath(stateDir);
    const QString marker = stateDir + "/tg_registration_attempted.json";
    if (QFileInfo::exists(marker))
        return;

    const QString helper = QStandardPaths::findExecutable("tg-register-project");
    bool started = false;
    if (!helper.isEmpty()) {
        QStringList args;
        args << "--project-id" << kProjectId
             << "--project-root" << m_projectRoot
             << "--codes" << kCodes
             << "--reference" << "User-supplied IDENTIFIERS TO USE in project request";
        started = QProcess::startDetached(helper, args);
    }

    QJsonObject o;
    o["attempted_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    o["helper_found"] = !helper.isEmpty();
    o["start_requested"] = started;
    o["project_id"] = QString::fromLatin1(kProjectId);
    o["codes"] = QString::fromLatin1(kCodes);

    QFile f(marker);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}
