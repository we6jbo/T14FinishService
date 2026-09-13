/*
 * T14FinishService core implementation
 * ------------------------------------
 * MAINTENANCE OVERVIEW:
 * - Constants near the top define localhost port, safety thresholds, default San
 *   Carlos coordinates, weather/cache behavior, and portable TG identifiers.
 * - start()/onNewConnection() manage the background TCP service.
 * - context/safety helpers read WE6JBO context, disk, and battery state.
 * - weather/cache/sunset helpers prefer live Internet data and preserve persistent
 *   fallback data under /var/cache/t14finishservice.
 * - makeDecision()/deadlineResponse()/codingStateResponse() implement scheduling.
 * - handleCommand() is the text protocol entry point for scripts/t14-finish.
 * - battery policy calls record/consult telemetry but must keep cheap local reads
 *   available under 55% unless evidence shows a meaningful cost.
 *
 * Keep user-visible time in 12-hour AM/PM format. Respect hidden-time context.
 */
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
#include <QStringList>
#include <QStorageInfo>
#include <QTcpSocket>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtMath>

#include <cmath>

// ===== Compile-time policy and helper constants =====
namespace {
constexpr quint64 kMinimumFreeBytes = 10ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr int kMinimumBatteryPercent = 55;
constexpr int kPackageRevision = 15;
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
constexpr int kWeatherCacheMaxAgeSeconds = 6 * 60 * 60;
constexpr int kSunsetCacheDays = 365;

const QString kDefaultExternalTimingHelperNote = QStringLiteral(
    "NOTE FOR CHATGPT/OTHER CODING ASSISTANTS: t14-finish and T14FinishService are not part of the project currently being developed. "
    "They are external timing helpers used only to decide when feature work should stop. Do not add t14-finish, T14FinishService, "
    "or their files/code to the current project's source, ZIP/package, build system, documentation, or repository unless the user explicitly asks you to do so.");

static QString externalTimingHelperNote()
{
    const QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME").isEmpty()
        ? QDir::homePath() + "/.config"
        : qEnvironmentVariable("XDG_CONFIG_HOME");
    QFile file(configHome + "/t14finishservice/motd.txt");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString configured = QString::fromUtf8(file.readAll()).trimmed();
        if (!configured.isEmpty())
            return configured;
    }
    return kDefaultExternalTimingHelperNote;
}

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

// ===== Service startup and localhost protocol =====
FinishService::FinishService(QObject *parent) : QObject(parent)
{
    const QString configured = qEnvironmentVariable("T14FINISH_PROJECT_ROOT");
    m_projectRoot = configured.isEmpty()
        ? QStringLiteral("/home/we6jbo/Projects/T14FinishService")
        : configured;
}

bool FinishService::start()
{
    const QString configPath = QDir::homePath() + QStringLiteral("/.finishservice.json");
    QFile configFile(configPath);

    if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_startError = QStringLiteral(
            "ERROR: T14FinishService cannot read %1.\n"
            "The file must contain a valid TCP port, for example:\n"
            "{\n  \\\"port\\\": 41728\n}\n"
            "Both T14FinishService and t14-finish read the port from this file.")
            .arg(configPath);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(configFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_startError = QStringLiteral(
            "ERROR: %1 does not contain valid JSON.\n"
            "It must contain a port such as:\n"
            "{\n  \\\"port\\\": 41728\n}")
            .arg(configPath);
        return false;
    }

    const QJsonValue portValue = document.object().value(QStringLiteral("port"));
    if (!portValue.isDouble()) {
        m_startError = QStringLiteral(
            "ERROR: %1 must contain an integer \\\"port\\\" value from 1 through 65535.")
            .arg(configPath);
        return false;
    }

    const double rawPort = portValue.toDouble();
    const int port = static_cast<int>(rawPort);
    if (rawPort != static_cast<double>(port) || port < 1 || port > 65535) {
        m_startError = QStringLiteral(
            "ERROR: %1 contains an invalid port.\n"
            "The port must be an integer from 1 through 65535.")
            .arg(configPath);
        return false;
    }

    m_port = static_cast<quint16>(port);

    if (!m_server.listen(QHostAddress::LocalHost, m_port)) {
        m_startError = QStringLiteral(
            "ERROR: The configured T14FinishService port %1 is already in use or could not be opened.\n\n"
            "Change the port in:\n%2\n\n"
            "Both T14FinishService and the t14-finish client read the port from that file.\n"
            "After changing it, restart t14-finish-service.service.\n\n"
            "Qt network error: %3")
            .arg(m_port)
            .arg(configPath)
            .arg(m_server.errorString());
        return false;
    }

    connect(&m_server, &QTcpServer::newConnection, this, &FinishService::onNewConnection);

    const Safety safety = checkSafety();
    if (safety.ok) {
        bool latOk = false;
        bool lonOk = false;
        const double configuredLat = qEnvironmentVariable("T14FINISH_LAT").toDouble(&latOk);
        const double configuredLon = qEnvironmentVariable("T14FINISH_LON").toDouble(&lonOk);
        const double lat = latOk ? configuredLat : kDefaultLatitude;
        const double lon = lonOk ? configuredLon : kDefaultLongitude;
        // Keep at least 365 future sunset entries on disk. Existing entries are not
        // discarded, so they become historical data after later reboots.
        ensureSunsetCache(QDate::currentDate(), lat, lon);
        recordBatteryActivity("cache_write");
        attemptTgRegistrationOnce();
    }

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

// ===== Machine context and safety =====
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
    // Revision 7 keeps the 55% battery check, but it is no longer a blanket stop.
    // Low-cost local/cache operations remain available below 55%; learned high-drain
    // features are minimized or blocked individually. Disk space remains a hard gate.
    s.ok = true;
    if (s.batteryPercent < 0)
        s.reason = "battery percentage could not be read; adaptive battery policy is unavailable";
    else if (s.batteryPercent < kMinimumBatteryPercent)
        s.reason = QString("battery is %1%; adaptive low-battery policy is active").arg(s.batteryPercent);
    return s;
}

// ===== Persistent weather/sunset cache =====
QString FinishService::cacheDirectory() const
{
    return QStringLiteral("/var/cache/t14finishservice");
}

FinishService::WeatherDay FinishService::readWeatherCache(const QDate &date, int maxAgeSeconds) const
{
    WeatherDay out;
    QFile f(cacheDirectory() + "/weather.json");
    if (!f.open(QIODevice::ReadOnly))
        return out;

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return out;

    const QJsonObject root = doc.object();
    const QJsonObject days = root.value("days").toObject();
    const QJsonObject day = days.value(date.toString(Qt::ISODate)).toObject();
    if (day.isEmpty())
        return out;

    const QDateTime fetched = QDateTime::fromString(day.value("fetched_at").toString(), Qt::ISODate);
    if (maxAgeSeconds > 0 && (!fetched.isValid() || fetched.secsTo(QDateTime::currentDateTime()) > maxAgeSeconds))
        return out;

    out.known = true;
    out.rainMm = day.value("rain_mm").toDouble();
    out.precipitationProbability = day.value("precipitation_probability").toInt();
    out.rainy = day.value("rainy").toBool();
    out.sunset = QTime::fromString(day.value("sunset").toString(), "HH:mm:ss");
    out.fromCache = true;
    out.source = day.value("source").toString() + " [cache]";
    return out;
}

void FinishService::writeWeatherCache(const QJsonObject &dailyObject, double latitude, double longitude, const QString &source) const
{
    const QJsonArray dates = dailyObject.value("time").toArray();
    const QJsonArray rain = dailyObject.value("rain_sum").toArray();
    const QJsonArray prob = dailyObject.value("precipitation_probability_max").toArray();
    const QJsonArray sunsets = dailyObject.value("sunset").toArray();
    if (dates.isEmpty())
        return;

    QDir().mkpath(cacheDirectory());
    const QString path = cacheDirectory() + "/weather.json";
    QJsonObject root;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(existing.readAll(), &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject())
            root = doc.object();
    }

    QJsonObject days = root.value("days").toObject();
    const QString fetchedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (int i = 0; i < dates.size(); ++i) {
        const QString date = dates.at(i).toString();
        if (date.isEmpty())
            continue;
        const double rainMm = i < rain.size() ? rain.at(i).toDouble() : 0.0;
        const int probability = i < prob.size() ? prob.at(i).toInt() : 0;
        QJsonObject day;
        day["fetched_at"] = fetchedAt;
        day["rain_mm"] = rainMm;
        day["precipitation_probability"] = probability;
        day["rainy"] = rainMm > 0.0 || probability >= 50;
        if (i < sunsets.size()) {
            const QDateTime sunsetDateTime = QDateTime::fromString(sunsets.at(i).toString(), Qt::ISODate);
            if (sunsetDateTime.isValid())
                day["sunset"] = sunsetDateTime.time().toString("HH:mm:ss");
        }
        day["source"] = source;
        days[date] = day;
    }

    root["schema"] = 1;
    root["location"] = QString::fromLatin1(kDefaultLocationLabel);
    root["latitude"] = latitude;
    root["longitude"] = longitude;
    root["normal_freshness_seconds"] = kWeatherCacheMaxAgeSeconds;
    root["usage_policy"] = "online first; cache only when network is unavailable or intentionally suppressed by low-battery policy";
    root["days"] = days;

    const QString tmp = path + ".tmp";
    QFile out(tmp);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();
        QFile::remove(path);
        QFile::rename(tmp, path);
    }
}

FinishService::WeatherDay FinishService::fetchWeather(const Context &context) const
{
    bool latOk = false;
    bool lonOk = false;
    const double configuredLat = qEnvironmentVariable("T14FINISH_LAT").toDouble(&latOk);
    const double configuredLon = qEnvironmentVariable("T14FINISH_LON").toDouble(&lonOk);
    const double lat = latOk ? configuredLat : kDefaultLatitude;
    const double lon = lonOk ? configuredLon : kDefaultLongitude;
    const QString tz = qEnvironmentVariableIsSet("T14FINISH_TIMEZONE")
        ? qEnvironmentVariable("T14FINISH_TIMEZONE") : QString::fromLatin1(kDefaultTimeZone);

    auto cacheFallback = [&](const QString &why) -> WeatherDay {
        WeatherDay cached = readWeatherCache(context.date, -1);
        if (cached.known) {
            cached.source += " [offline fallback: " + why + "]";
            recordBatteryActivity("cache_read");
            return cached;
        }
        WeatherDay missing;
        missing.error = why + "; no cached weather entry is available for " + context.date.toString(Qt::ISODate);
        return missing;
    };

    // Cache is not the normal data source in revision 9. We try the Internet first.
    // The only exception is when adaptive low-battery policy intentionally suppresses
    // a network request, which is treated the same as temporary network unavailability.
    const int battery = batteryPercent();
    const QString weatherPolicy = batteryFeaturePolicy("weather_network");
    if (battery >= 0 && battery < kMinimumBatteryPercent
        && (weatherPolicy == "BLOCK" || weatherPolicy == "MINIMIZE")) {
        WeatherDay cached = readWeatherCache(context.date, -1);
        if (cached.known) {
            cached.source += QString(" [low-battery %1 fallback]").arg(weatherPolicy.toLower());
            recordBatteryActivity("cache_read");
            return cached;
        }
        if (weatherPolicy == "BLOCK") {
            WeatherDay out;
            out.error = "weather network refresh blocked by learned low-battery policy and no cached forecast exists";
            return out;
        }
        // MINIMIZE with no cache still permits one online request because otherwise
        // there is no weather information to make the deadline decision.
    }

    recordBatteryActivity("weather_network");
    QUrl url("https://api.open-meteo.com/v1/forecast");
    QUrlQuery query;
    query.addQueryItem("latitude", QString::number(lat, 'f', 4));
    query.addQueryItem("longitude", QString::number(lon, 'f', 4));
    query.addQueryItem("daily", "rain_sum,precipitation_probability_max,sunset");
    query.addQueryItem("timezone", tz);
    query.addQueryItem("start_date", context.date.toString(Qt::ISODate));
    query.addQueryItem("end_date", context.date.addDays(15).toString(Qt::ISODate));
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
        return cacheFallback("weather request timed out");
    }
    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        const QString error = reply->errorString();
        reply->deleteLater();
        return cacheFallback("weather request failed: " + error);
    }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll(), &pe);
    reply->deleteLater();
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return cacheFallback("weather service returned invalid JSON");

    const QJsonObject daily = doc.object().value("daily").toObject();
    const QJsonArray dates = daily.value("time").toArray();
    const QJsonArray rain = daily.value("rain_sum").toArray();
    const QJsonArray prob = daily.value("precipitation_probability_max").toArray();
    const QJsonArray sunsets = daily.value("sunset").toArray();
    if (dates.isEmpty() || (rain.isEmpty() && prob.isEmpty()))
        return cacheFallback("weather service did not return usable daily data");

    const QString source = QStringLiteral("Open-Meteo forecast for %1 (%2,%3)")
        .arg(QString::fromLatin1(kDefaultLocationLabel))
        .arg(lat, 0, 'f', 4)
        .arg(lon, 0, 'f', 4);

    // Persist every future day the provider returned. Old entries are retained, so the
    // file gradually becomes historical weather data across reboots.
    writeWeatherCache(daily, lat, lon, source);
    recordBatteryActivity("weather_network");

    const QString wanted = context.date.toString(Qt::ISODate);
    for (int i = 0; i < dates.size(); ++i) {
        if (dates.at(i).toString() != wanted)
            continue;
        WeatherDay out;
        out.rainMm = i < rain.size() ? rain.at(i).toDouble() : 0.0;
        out.precipitationProbability = i < prob.size() ? prob.at(i).toInt() : 0;
        out.rainy = out.rainMm > 0.0 || out.precipitationProbability >= 50;
        if (i < sunsets.size()) {
            const QDateTime sunsetDateTime = QDateTime::fromString(sunsets.at(i).toString(), Qt::ISODate);
            if (sunsetDateTime.isValid())
                out.sunset = sunsetDateTime.time();
        }
        out.known = true;
        out.fromCache = false;
        out.source = source;
        return out;
    }

    return cacheFallback("weather service did not include the requested date");
}

void FinishService::ensureSunsetCache(const QDate &anchorDate, double latitude, double longitude) const
{
    QDir().mkpath(cacheDirectory());
    const QString path = cacheDirectory() + "/sunset-365.json";

    QJsonObject existingRoot;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(existing.readAll(), &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject())
            existingRoot = doc.object();
    }

    const QJsonObject existingDays = existingRoot.value("days").toObject();
    const QDate lastWanted = anchorDate.addDays(kSunsetCacheDays - 1);
    if (existingDays.contains(anchorDate.toString(Qt::ISODate)) &&
        existingDays.contains(lastWanted.toString(Qt::ISODate)))
        return;

    QJsonObject days = existingDays;
    for (int i = 0; i < kSunsetCacheDays; ++i) {
        const QDate d = anchorDate.addDays(i);
        QString err;
        const QTime t = localSunset(d, latitude, longitude, &err);
        if (!t.isValid())
            continue;
        QJsonObject entry;
        entry["sunset"] = t.toString("HH:mm:ss");
        entry["source"] = "local astronomical calculation";
        days[d.toString(Qt::ISODate)] = entry;
    }

    QJsonObject root;
    root["schema"] = 1;
    root["generated_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["start_date"] = anchorDate.toString(Qt::ISODate);
    root["days_cached"] = days.size();
    root["minimum_forward_days"] = kSunsetCacheDays;
    root["retention"] = "existing entries are retained as historical sunset data";
    root["location"] = QString::fromLatin1(kDefaultLocationLabel);
    root["latitude"] = latitude;
    root["longitude"] = longitude;
    root["days"] = days;

    const QString tmp = path + ".tmp";
    QFile out(tmp);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();
        QFile::remove(path);
        QFile::rename(tmp, path);
    }
}

QTime FinishService::cachedSunset(const QDate &date, double latitude, double longitude, QString *error) const
{
    // This method is used only after online sunset/weather data is unavailable.
    // It reads the persistent cache first, then falls back to a local astronomical
    // calculation if the requested day was never cached.
    QFile f(cacheDirectory() + "/sunset-365.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject days = doc.object().value("days").toObject();
            const QJsonObject entry = days.value(date.toString(Qt::ISODate)).toObject();
            const QTime t = QTime::fromString(entry.value("sunset").toString(), "HH:mm:ss");
            if (t.isValid())
                return t;
        }
    }

    return localSunset(date, latitude, longitude, error);
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
    if (display) *display = parsed.toString("h:mm AP");
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

    // At 8:00 PM or later, do not create another coding deadline for tonight.
    // This also avoids unnecessary weather/network/sunset work after the work window.
    if (d.currentTime >= QTime(20, 0)) {
        d.state = "AFTER_HOURS";
        d.reason = "it is 8:00 PM or later; no new coding deadline will be set until the next day";
        d.ok = true;
        return d;
    }

    bool latOk = false;
    bool lonOk = false;
    const double configuredLat = qEnvironmentVariable("T14FINISH_LAT").toDouble(&latOk);
    const double configuredLon = qEnvironmentVariable("T14FINISH_LON").toDouble(&lonOk);
    const double lat = latOk ? configuredLat : kDefaultLatitude;
    const double lon = lonOk ? configuredLon : kDefaultLongitude;

    const WeatherDay weather = fetchWeather(context);
    d.rainKnown = weather.known;
    d.rainy = weather.rainy;
    d.rainMm = weather.rainMm;
    d.precipitationProbability = weather.precipitationProbability;
    d.weatherSource = weather.source;
    d.weatherError = weather.error;

    QString sunsetError;
    if (weather.sunset.isValid()) {
        // Online Open-Meteo is the normal sunset source. If fetchWeather had to
        // fall back to its persistent cache, the cached sunset travels with it.
        d.sunset = weather.sunset;
    } else if (weather.fromCache || !weather.known) {
        d.sunset = cachedSunset(context.date, lat, lon, &sunsetError);
    } else {
        // Internet was available but did not provide a usable sunset value. Do not
        // consult the cache in this case; calculate it locally instead.
        d.sunset = localSunset(context.date, lat, lon, &sunsetError);
    }
    if (!d.sunset.isValid()) {
        d.state = "ERROR";
        d.reason = "unable to determine sunset: " + sunsetError;
        return d;
    }

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

QString FinishService::deadlineResponse(const Context &context, int warningMinutes) const
{
    const auto withHelperNote = [](const QString &message) {
        return message + "\n" + externalTimingHelperNote();
    };

    const Decision d = makeDecision(context);
    if (d.state == "STOP_SAFETY")
        return withHelperNote("STOP: " + d.reason + ".");
    if (d.state == "TIME_HIDDEN")
        return withHelperNote("TIME_HIDDEN: " + d.reason + ".");
    if (!d.ok)
        return withHelperNote("ERROR: " + d.reason);
    if (d.state == "AFTER_HOURS")
        return withHelperNote(QString("The time is %1. It is 8:00 PM or later, so T14FinishService will not set another coding deadline tonight. Check again tomorrow for the next deadline.")
            .arg(d.currentDisplay));

    QString weatherText;
    if (d.rainKnown) {
        weatherText = QString("Rain: %1 mm, precipitation probability: %2%")
            .arg(d.rainMm, 0, 'f', 1)
            .arg(d.precipitationProbability);
    } else {
        weatherText = "Rain forecast unavailable; conservative non-rain schedule used";
    }

    const QString deadlineText = d.deadline.toString("h:mm AP");
    const int secondsUntilDeadline = d.currentTime.secsTo(d.deadline);
    const bool warningActive = secondsUntilDeadline > 0 && secondsUntilDeadline <= warningMinutes * 60;

    if (d.state == "FINISH_CODING") {
        return withHelperNote(QString("The time is %1. We cannot add any more features after %2. The cutoff has been reached, so stop adding features now. The user's post-cutoff completion workflow has already been pre-timed, so do not add extra padding or resume feature development. Begin the completion workflow now: compile, test, debug, document, save, publish/package as planned, prepare the j03.page/social/spoken material, and leave on the user's existing schedule. Before creating the final build, add TG564843,TG333041,TG323932,TG610982,TG148675 to the project. Rule: %3. San Carlos sunset: %4. %5.")
            .arg(d.currentDisplay,
                 deadlineText,
                 d.reason,
                 d.sunset.toString("h:mm AP"),
                 weatherText));
    }

    if (!warningActive) {
        return withHelperNote(QString("The time is %1. No finish-up warning is needed yet. T14FinishService will display the stop-coding warning when the cutoff is within %2 minutes. For unusually long work, ChatGPT may request an earlier warning with 't14-finish deadline --warning-minutes N', where N must be from 90 through 420; this changes only when the warning appears, never the actual stop time.")
            .arg(d.currentDisplay)
            .arg(warningMinutes));
    }

    return withHelperNote(QString("The time is %1. Warning: we cannot add any more features after %2. You may use the remaining coding window up to %2; the user's post-cutoff completion workflow has already been pre-timed, so do not reserve additional padding unless the user asks. Once it becomes %2, stop adding features immediately and begin the completion workflow. Rule: %3. San Carlos sunset: %4. %5. If the current task is unusually long, ChatGPT may choose an earlier warning by running 't14-finish deadline --warning-minutes N', where N is 90 through 420 minutes; that changes only the warning lead time, not the %2 stop time.")
        .arg(d.currentDisplay,
             deadlineText,
             d.reason,
             d.sunset.toString("h:mm AP"),
             weatherText));
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
    o["low_battery_threshold_percent"] = kMinimumBatteryPercent;
    o["low_battery_mode"] = safety.batteryPercent >= 0 && safety.batteryPercent < kMinimumBatteryPercent;
    o["weather_network_policy"] = batteryFeaturePolicy("weather_network");
    o["free_gib"] = double(safety.freeBytes) / (1024.0 * 1024.0 * 1024.0);
    o["location"] = context.locationLabel.isEmpty()
        ? QString::fromLatin1(kDefaultLocationLabel)
        : context.locationLabel;
    o["reason"] = d.reason;

    if (context.timeVisible && d.currentTime.isValid()) {
        o["current_time"] = d.currentDisplay;
        if (d.deadline.isValid()) {
            o["coding_deadline"] = d.deadline.toString("h:mm AP");
            o["minutes_remaining"] = d.minutesRemaining;
        }
        if (d.sunset.isValid())
            o["sunset"] = d.sunset.toString("h:mm AP");
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
        o["next_action"] = "Continue coding. The post-cutoff workflow is already pre-timed, so the coding window may be used up to the cutoff; avoid starting work that cannot reasonably be completed before it.";
    else if (d.state == "FINISH_CODING")
        o["next_action"] = "Stop adding features immediately. The post-cutoff completion workflow is already pre-timed; compile, test, debug, document, save, and continue the planned publication/content workflow without adding extra feature-development padding.";
    else if (d.state == "AFTER_HOURS")
        o["next_action"] = "Do not set another coding deadline tonight. Check again tomorrow.";
    else if (d.state == "STOP_SAFETY")
        o["next_action"] = "Stop writes because the disk-space safety requirement failed.";

    return compactJson(o);
}

QString FinishService::batteryFeaturePolicy(const QString &feature) const
{
    if (feature == "cache_read" || feature == "local_compute" || feature == "deadline" || feature == "coding_state")
        return "ALLOW";

    const QString helper = QStandardPaths::findExecutable("t14finish-battery-monitor");
    if (helper.isEmpty())
        return "ALLOW";

    QProcess p;
    p.start(helper, QStringList() << "policy" << feature);
    if (!p.waitForFinished(2500)) {
        p.kill();
        return "ALLOW";
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return "ALLOW";
    return doc.object().value("policy").toString("ALLOW").toUpper();
}

void FinishService::recordBatteryActivity(const QString &category) const
{
    const QString helper = QStandardPaths::findExecutable("t14finish-battery-monitor");
    if (helper.isEmpty())
        return;
    QProcess::startDetached(helper, QStringList() << "sample" << category);
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
        o["next_action"] = "Stop writes because the disk-space safety requirement failed.";
        return compactJson(o);
    }

    if (!safety.ok)
        return "STOP: " + safety.reason + ".";

    QString contextError;
    const Context context = loadContext(&contextError);
    if (!contextError.isEmpty())
        return "ERROR: " + contextError;

    if (command.compare("deadline", Qt::CaseInsensitive) == 0)
        return deadlineResponse(context, 90);

    if (command.startsWith("deadline ", Qt::CaseInsensitive)) {
        const QStringList parts = command.split(' ', Qt::SkipEmptyParts);
        int warningMinutes = -1;
        if (parts.size() == 3 && parts.at(1).compare("--warning-minutes", Qt::CaseInsensitive) == 0) {
            bool ok = false;
            warningMinutes = parts.at(2).toInt(&ok);
            if (!ok)
                warningMinutes = -1;
        } else if (parts.size() == 2 && parts.at(1).startsWith("--warning-minutes=", Qt::CaseInsensitive)) {
            bool ok = false;
            warningMinutes = parts.at(1).section('=', 1).toInt(&ok);
            if (!ok)
                warningMinutes = -1;
        }

        if (warningMinutes < 90 || warningMinutes > 420)
            return "ERROR: --warning-minutes must be from 90 through 420 minutes.";
        return deadlineResponse(context, warningMinutes);
    }
    if (command.compare("coding-state", Qt::CaseInsensitive) == 0)
        return codingStateResponse(context, false);
    if (command.compare("coding-state --json", Qt::CaseInsensitive) == 0
        || command.compare("coding-state-json", Qt::CaseInsensitive) == 0)
        return codingStateResponse(context, true);

    if (command.compare("battery-policy --json", Qt::CaseInsensitive) == 0
        || command.compare("battery-policy-json", Qt::CaseInsensitive) == 0) {
        const QString helper = QStandardPaths::findExecutable("t14finish-battery-monitor");
        if (helper.isEmpty())
            return "{\"error\":\"battery monitor helper not installed\"}";
        QProcess p;
        p.start(helper, QStringList() << "report");
        if (!p.waitForFinished(3000)) { p.kill(); return "{\"error\":\"battery monitor timed out\"}"; }
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    }

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
        o["low_battery_threshold_percent"] = kMinimumBatteryPercent;
        o["low_battery_mode"] = safety.batteryPercent >= 0 && safety.batteryPercent < kMinimumBatteryPercent;
        o["weather_network_policy"] = batteryFeaturePolicy("weather_network");
        o["cache_directory"] = cacheDirectory();
        o["cache_policy"] = "internet first; persistent cache only when network is unavailable or suppressed by low-battery policy";
        o["git_backup_policy"] = batteryFeaturePolicy("git_backup");
        o["version_check_policy"] = batteryFeaturePolicy("version_check");
        o["codes"] = QString::fromLatin1(kCodes);
        o["sunset_mode"] = "online sunset first; persistent 365+ day fallback cache; local astronomical calculation if offline cache misses";
        o["weather_cache"] = "Internet first; persistent cache only when Internet is unavailable or low-battery policy suppresses network access";
        o["weather_area"] = QString::fromLatin1(kDefaultLocationLabel);
        return compactJson(o);
    }

    return "ERROR: commands are ping, status, deadline [--warning-minutes 90..420], coding-state, coding-state --json, battery-policy --json, codes";
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
