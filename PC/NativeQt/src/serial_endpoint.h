#pragma once
#include <QObject>
#include <QSerialPort>

// Caller owns injected endpoints; MonitorLink owns its production endpoint.
class SerialEndpoint : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool open(const QString &name, qint32 baud, QString &error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual qint64 write(const QByteArray &raw) = 0;
    virtual QByteArray readAll() = 0;
signals:
    void readyRead();
    void failed(const QString &error);
};

class QtSerialEndpoint final : public SerialEndpoint {
    Q_OBJECT
public:
    explicit QtSerialEndpoint(QObject *parent=nullptr);
    bool open(const QString &name, qint32 baud, QString &error) override;
    void close() override;
    bool isOpen() const override;
    qint64 write(const QByteArray &raw) override;
    QByteArray readAll() override;
private:
    QSerialPort serial;
    bool closing=false;
};
