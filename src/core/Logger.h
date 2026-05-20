#pragma once

#include <QObject>
#include <QFile>
#include <QMutex>
#include <QString>

class Logger : public QObject
{
    Q_OBJECT

public:
    enum Level
    {
        Info,
        Warning,
        Error
    };

    static Logger &instance();

    bool open(const QString &logFilePath, QString *errorMessage = nullptr);
    void close();
    void log(Level level, const QString &message);

signals:
    void messageLogged(const QString &message);

private:
    explicit Logger(QObject *parent = nullptr);
    QString levelName(Level level) const;

    QFile m_file;
    QMutex m_mutex;
};
