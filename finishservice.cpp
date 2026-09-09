#include "finishservice.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTcpSocket>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtMath>

#include <cmath>

namespace {
constexpr quint16 kPort = 45454;
constexpr quint64 kMinimumFreeBytes = 10ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr int kMinimumBatteryPercent = 55;
constexpr int kPackageRevision = 4;
constexpr const char *kProjectId = "t14-finish-service-v1";
constexpr const char *kCodes = "TG564843,TG333041,TG323932,TG610982,TG148675";

// Default forecast point for the San Carlos / Cowles Mountain area of San Diego.
constexpr double kDefaultLatitude = 32.8039;
constexpr double kDefaultLongitude = -117.0400;
constexpr const char *kDefaultLocationLabel = "San Carlos, San Diego, CA";
constexpr const char *kDefaultTimeZone = "America/Los_Angeles";

constexpr double kOfficialZenithDegrees = 90.833;
constexpr double kPi = 3.14159265358979323846;
constexpr int kWeatherTimeoutMs = 10000;

static double normalizeDegrees(double value)
{
    while (value < 0.0) value += 360.0;
    while (value >= 360.0) value -= 360.0;
    return value;
}

static double normalizeHours(double value)
{
    while (value < 0.0) value += 24.0;
    while (value >= 24.0) value -= 24.0;
    return value;
}

static double degToRad(double degrees)
{
    return degrees * kPi / 180.0;
}

static double radToDeg(double radians)
{
    return radians * 180.0 / kPi;
}
}

FinishService::FinishService(QObject *parent) : QObject(parent)
{
    const QString configured = qEnvironmentVariable("T14FINISH_PROJECT_ROOT");
    m_projectRoot = configured.isEmpty()
        ? QStringLiteral("/home/we6jbo/Projects/T14FinishService")
        : configured;
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

    if (!c.date.isValid() && error)
        *error = "Context date is missing or invalid";
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
    const double configuredLat = qEnvironmentVariable("T14FINISH_LAT").toDouble(&latOk);
    const double configuredLon = qEnvironmentVariable("T14FINISH_LON").toDouble(&lonOk);
    const double lat = latOk ? configuredLat : kDefaultLatitude;
    const double lon = lonOk ? configuredLon : kDefaultLongitude;
    const QString tz = qEnvironmentVariableIsSet("T14FINISH_TIMEZONE")
        ? qEnvironmentVariable("T14FINISH_TIMEZONE") : QString::fromLatin1(kDefaultTimeZone);

    QUrl url("https://api.open-meteo.com/v1/forecast");
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(lat, 'f', 4));
    query.addQueryItem("longitude", QString::number(lon, 'f', 4));
    query.addQueryItem("daily", "rain_sum,precipitation_probability_max");
    query.addQueryItem("timezone", tz);
    query.addQueryItem("start_date", context.date.toString(Qt::ISODate));
    query.addQueryItem("end_date", context.date.toString(Qt::ISODate));
    url.setQuery(query);

    QNetworkAccessManager manager;
    QNetworkReply *reply = manager.get(QNetworkRequest(url));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(kWeatherTimeoutMs);
    loop.exec();

    if (!timer.isActive()) {
        reply->abort();
        reply->deleteLater();
        out.error = "weather request timed out";
        return out;
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        out.error = reply->errorString();
        reply->deleteLater();
        return out;
    }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
    reply->deleteLater();
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        out.error = "weather service returned invalid JSON";
        return out;
    }

    const QJsonObject daily = doc.object().value("daily").toObject();
    const QJsonArray rain = daily.value("rain_sum").toArray();
    const QJsonArray prob = daily.value("precipitation_probability_max").toArray();
    if (rain.isEmpty() && prob.isEmpty()) {
        out.error = "weather service did not return rain data for the requested date";
        return out;
    }

    out.rainMm = rain.isEmpty() ? 0.0 : rain.at(0).toDouble();
    out.precipitationProbability = prob.isEmpty() ? 0 : prob.at(0).toInt();
    out.rainy = out.rainMm > 0.0 || out.precipitationProbability >= 50;
    out.known = true;
    out.source = QStringLiteral("Open-Meteo forecast for %1 (%2,%3)")
        .arg(QString::fromLatin1(kDefaultLocationLabel))
        .arg(lat, 0, 'f', 4)
        .arg(lon, 0, 'f', 4);
    return out;
}

QTime FinishService::localSunset(const QDate &date, double latitude, double longitude, QString *error) const
{
    if (!date.isValid()) {
        if (error) *error = "invalid date for sunset calculation";
        return {};
    }

    const int n = date.dayOfYear();
    const double lngHour = longitude / 15.0;
    const double t = n + ((18.0 - lngHour) / 24.0);
    const double m = (0.9856 * t) - 3.289;

    double l = m + (1.916 * std::sin(degToRad(m)))
                 + (0.020 * std::sin(2.0 * degToRad(m)))
                 + 282.634;
    l = normalizeDegrees(l);

    double ra = radToDeg(std::atan(0.91764 * std::tan(degToRad(l))));
    ra = normalizeDegrees(ra);
    const double lQuadrant = std::floor(l / 90.0) * 90.0;
    const double raQuadrant = std::floor(ra / 90.0) * 90.0;
    ra += lQuadrant - raQuadrant;
    ra /= 15.0;

    const double sinDec = 0.39782 * std::sin(degToRad(l));
    const double cosDec = std::cos(std::asin(sinDec));
    const double cosH = (std::cos(degToRad(kOfficialZenithDegrees))
                         - (sinDec * std::sin(degToRad(latitude))))
                        / (cosDec * std::cos(degToRad(latitude)));

    if (cosH < -1.0 || cosH > 1.0) {
        if (error) *error = "sunset is not defined for this date/location";
        return {};
    }

    const double h = radToDeg(std::acos(cosH)) / 15.0;
    const double localMeanTime = h + ra - (0.06571 * t) - 6.622;
    const double utcHours = normalizeHours(localMeanTime - lngHour);

    int seconds = qRound(utcHours * 3600.0);
    if (seconds >= 24 * 3600)
        seconds -= 24 * 3600;

    QDate utcDate = date;
    // For western longitudes, a sunset whose normalized UTC hour is shortly after
    // midnight belongs to the following UTC date. This matters around DST changes.
    if (longitude < 0.0 && utcHours < 12.0)
        utcDate = utcDate.addDays(1);
    const QTimeZone utcZone(QByteArrayLiteral("UTC"));
    const QDateTime utcDateTime(utcDate, QTime(0, 0), utcZone);
    const QDateTime eventUtc = utcDateTime.addSecs(seconds);
    const QTimeZone zone{QByteArray(kDefaultTimeZone)};
    if (!zone.isValid()) {
        if (error) *error = "America/Los_Angeles timezone is unavailable";
        return {};
    }

    return eventUtc.toTimeZone(zone).time();
}

bool FinishService::currentTimeFromContext(const Context &context, QTime *time, QString *display, QString *error) const
{
    if (!context.timeVisible) {
        if (error) *error = "context policy forbids exposing or inferring a user-visible clock time";
        return false;
    }

    QTime parsed;
    const QString raw = context.timeDisplay.trimmed();
    const QStringList formats = {"HH:mm", "H:mm", "h:mm AP", "hh:mm AP", "HH:mm:ss"};
    for (const QString &format : formats) {
        parsed = QTime::fromString(raw, format);
        if (parsed.isValid())
            break;
    }

    if (!parsed.isValid()) {
        if (context.timePolicy.compare("current", Qt::CaseInsensitive) != 0) {
            if (error) *error = "context time.display is invalid and time.policy does not permit using current time";
            return false;
        }
        const QTimeZone zone{QByteArray(kDefaultTimeZone)};
        parsed = QDateTime::currentDateTimeUtc().toTimeZone(zone).time();
    }

    if (time) *time = parsed;
    if (display) *display = raw.isEmpty() ? parsed.toString("HH:mm") : raw;
    return true;
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

FinishService::Decision FinishService::makeDecision(const Context &context) const
{
    Decision d;

    const Safety safety = checkSafety();
    if (!safety.ok) {
        d.state = "STOP_SAFETY";
        d.reason = safety.reason;
        return d;
    }

    QString timeError;
    if (!currentTimeFromContext(context, &d.currentTime, &d.currentDisplay, &timeError)) {
        d.state = context.timeVisible ? "ERROR" : "TIME_HIDDEN";
        d.reason = timeError;
        return d;
    }

    bool latOk = false;
    bool lonOk = false;
    const double configuredLat = qEnvironmentVariable("T14FINISH_LAT").toDouble(&latOk);
    const double configuredLon = qEnvironmentVariable("T14FINISH_LON").toDouble(&lonOk);
    const double lat = latOk ? configuredLat : kDefaultLatitude;
    const double lon = lonOk ? configuredLon : kDefaultLongitude;

    QString sunsetError;
    d.sunset = localSunset(context.date, lat, lon, &sunsetError);
    if (!d.sunset.isValid()) {
        d.state = "ERROR";
        d.reason = "unable to calculate sunset: " + sunsetError;
        return d;
    }

    const WeatherDay weather = fetchWeather(context);
    d.rainKnown = weather.known;
    d.rainy = weather.rainy;
    d.rainMm = weather.rainMm;
    d.precipitationProbability = weather.precipitationProbability;
    d.weatherSource = weather.source;
    d.weatherError = weather.error;

    const int day = context.date.dayOfWeek();
    if (weather.known && weather.rainy && (day == Qt::Monday || day == Qt::Tuesday)) {
        d.deadline = QTime(18, 45);
        d.reason = "rain override for Monday/Tuesday";
    } else if (weather.known && weather.rainy && day >= Qt::Wednesday && day <= Qt::Saturday) {
        d.deadline = QTime(16, 45);
        d.reason = "rain override for Wednesday-Saturday";
    } else {
        d.offsetMinutes = sunsetOffsetMinutes(context);
        d.deadline = d.sunset.addSecs(-d.offsetMinutes * 60);
        d.reason = QString("sunset minus %1 minutes").arg(d.offsetMinutes);
        if (!weather.known)
            d.reason += "; weather unavailable, using conservative non-rain schedule";
    }

    d.minutesRemaining = d.currentTime.secsTo(d.deadline) / 60;
    if (d.currentTime < d.deadline) {
        d.state = "KEEP_CODING";
    } else {
        d.state = "FINISH_CODING";
        d.minutesRemaining = 0;
    }

    d.ok = true;
    return d;
}

QString FinishService::deadlineResponse(const Context &context) const
{
    const Decision d = makeDecision(context);
    if (d.state == "STOP_SAFETY")
        return "STOP: " + d.reason + ".";
    if (d.state == "TIME_HIDDEN")
        return "TIME_HIDDEN: " + d.reason + ".";
    if (!d.ok)
        return "ERROR: " + d.reason;

    QString weatherText;
    if (d.rainKnown) {
        weatherText = QString("Rain: %1 mm, precipitation probability: %2%")
            .arg(d.rainMm, 0, 'f', 1)
            .arg(d.precipitationProbability);
    } else {
        weatherText = "Rain forecast unavailable; conservative non-rain schedule used";
    }

    return QString("The time is %1. Finish writing new program features by %2. Then compile, test, debug, document, and save. Rule: %3. Calculated San Carlos sunset: %4. %5.")
        .arg(d.currentDisplay,
             d.deadline.toString("h:mm AP"),
             d.reason,
             d.sunset.toString("h:mm AP"),
             weatherText);
}

QString FinishService::codingStateResponse(const Context &context, bool asJson) const
{
    const Safety safety = checkSafety();
    const Decision d = makeDecision(context);

    if (!asJson) {
        if (d.state == "STOP_SAFETY")
            return "STOP_SAFETY";
        if (d.state == "TIME_HIDDEN")
            return "TIME_HIDDEN";
        if (!d.ok)
            return "ERROR";
        return d.state;
    }

    QJsonObject o;
    o["project_id"] = QString::fromLatin1(kProjectId);
    o["package_revision"] = kPackageRevision;
    o["state"] = d.state;
    o["date"] = context.date.toString(Qt::ISODate);
    o["battery_percent"] = safety.batteryPercent;
    o["free_gib"] = double(safety.freeBytes) / (1024.0 * 1024.0 * 1024.0);
    o["location"] = context.locationLabel.isEmpty()
        ? QString::fromLatin1(kDefaultLocationLabel)
        : context.locationLabel;
    o["reason"] = d.reason;

    if (context.timeVisible && d.currentTime.isValid()) {
        o["current_time"] = d.currentDisplay;
        o["coding_deadline"] = d.deadline.toString("HH:mm");
        o["minutes_remaining"] = d.minutesRemaining;
        o["sunset"] = d.sunset.toString("HH:mm");
    }

    o["rain_known"] = d.rainKnown;
    if (d.rainKnown) {
        o["rainy"] = d.rainy;
        o["rain_mm"] = d.rainMm;
        o["precipitation_probability"] = d.precipitationProbability;
        o["weather_source"] = d.weatherSource;
    } else if (!d.weatherError.isEmpty()) {
        o["weather_error"] = d.weatherError;
    }

    if (d.state == "KEEP_CODING")
        o["next_action"] = "Continue coding, but check T14FinishService before starting another substantial feature.";
    else if (d.state == "FINISH_CODING")
        o["next_action"] = "Stop adding features. Compile, test, debug, document, and save.";
    else if (d.state == "STOP_SAFETY")
        o["next_action"] = "Stop writes until the battery and disk safety requirements pass.";

    return compactJson(o);
}

QString FinishService::compactJson(const QJsonObject &obj) const
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QString FinishService::handleCommand(const QString &rawCommand)
{
    const QString command = rawCommand.simplified();
    if (command.compare("ping", Qt::CaseInsensitive) == 0)
        return "pong";
    if (command.compare("codes", Qt::CaseInsensitive) == 0)
        return QString::fromLatin1(kCodes);

    const Safety safety = checkSafety();

    // The AI-friendly state command intentionally returns a stable machine token when
    // a safety gate fails instead of a prose STOP message.
    if (command.compare("coding-state", Qt::CaseInsensitive) == 0 && !safety.ok)
        return "STOP_SAFETY";
    if ((command.compare("coding-state --json", Qt::CaseInsensitive) == 0
         || command.compare("coding-state-json", Qt::CaseInsensitive) == 0) && !safety.ok) {
        QJsonObject o;
        o["project_id"] = QString::fromLatin1(kProjectId);
        o["package_revision"] = kPackageRevision;
        o["state"] = "STOP_SAFETY";
        o["reason"] = safety.reason;
        o["battery_percent"] = safety.batteryPercent;
        o["free_gib"] = double(safety.freeBytes) / (1024.0 * 1024.0 * 1024.0);
        o["next_action"] = "Stop writes until the battery and disk safety requirements pass.";
        return compactJson(o);
    }

    if (!safety.ok)
        return "STOP: " + safety.reason + ".";

    QString contextError;
    const Context context = loadContext(&contextError);
    if (!contextError.isEmpty())
        return "ERROR: " + contextError;

    if (command.compare("deadline", Qt::CaseInsensitive) == 0)
        return deadlineResponse(context);
    if (command.compare("coding-state", Qt::CaseInsensitive) == 0)
        return codingStateResponse(context, false);
    if (command.compare("coding-state --json", Qt::CaseInsensitive) == 0
        || command.compare("coding-state-json", Qt::CaseInsensitive) == 0)
        return codingStateResponse(context, true);

    if (command.compare("status", Qt::CaseInsensitive) == 0) {
        QJsonObject o;
        o["project_id"] = QString::fromLatin1(kProjectId);
        o["package_revision"] = kPackageRevision;
        o["date"] = context.date.toString(Qt::ISODate);
        o["weekday"] = context.weekday;
        o["timezone"] = context.timezone;
        o["location"] = context.locationLabel.isEmpty()
            ? QString::fromLatin1(kDefaultLocationLabel)
            : context.locationLabel;
        o["time_visible"] = context.timeVisible;
        if (context.timeVisible)
            o["time_display"] = context.timeDisplay;
        o["free_gib"] = double(safety.freeBytes) / (1024.0 * 1024.0 * 1024.0);
        o["battery_percent"] = safety.batteryPercent;
        o["codes"] = QString::fromLatin1(kCodes);
        o["sunset_mode"] = "local astronomical calculation";
        o["weather_area"] = QString::fromLatin1(kDefaultLocationLabel);
        return compactJson(o);
    }

    return "ERROR: commands are ping, status, deadline, coding-state, coding-state --json, codes";
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
