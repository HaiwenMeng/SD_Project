#include "core/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QTextStream>

Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::Logger(QObject *parent)
    : QObject(parent)
{
}

bool Logger::open(const QString &logFilePath, QString *errorMessage)
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_file.close();
    }

    QFileInfo fileInfo(logFilePath);
    QDir dir(fileInfo.absolutePath());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create log directory: %1").arg(dir.absolutePath());
        }
        return false;
    }

    m_file.setFileName(logFilePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open log file: %1").arg(m_file.errorString());
        }
        return false;
    }
    return true;
}

void Logger::close()
{
    QMutexLocker locker(&m_mutex);
    if (m_file.isOpen()) {
        m_file.close();
    }
}

void Logger::log(Logger::Level level, const QString &message)
{
    const QString line = QStringLiteral("[%1] [%2] %3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
        .arg(levelName(level))
        .arg(message);

    {
        QMutexLocker locker(&m_mutex);
        if (m_file.isOpen()) {
            QTextStream stream(&m_file);
            stream << line << '\n';
            stream.flush();
        }
    }

    emit messageLogged(line);
}

QString Logger::levelName(Logger::Level level) const
{
    switch (level) {
    case Info:
        return QStringLiteral("INFO");
    case Warning:
        return QStringLiteral("WARN");
    case Error:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("UNKNOWN");
}
