#include "serial_endpoint.h"

QtSerialEndpoint::QtSerialEndpoint(QObject *parent):SerialEndpoint(parent) {
    serial.setReadBufferSize(8192);
    connect(&serial,&QSerialPort::readyRead,this,&SerialEndpoint::readyRead);
    connect(&serial,&QSerialPort::errorOccurred,this,[this](QSerialPort::SerialPortError error){
        if(error!=QSerialPort::NoError&&serial.isOpen()&&!closing) emit failed(serial.errorString());
    });
}
bool QtSerialEndpoint::open(const QString &name,qint32 baud,QString &error) {
    if(serial.isOpen()) {error=QStringLiteral("端口已连接");return false;}
    serial.setPortName(name);serial.setBaudRate(baud);serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);
    if(!serial.open(QIODevice::ReadWrite)) {error=serial.errorString();return false;}
    return true;
}
void QtSerialEndpoint::close() {closing=true;serial.close();closing=false;}
bool QtSerialEndpoint::isOpen() const {return serial.isOpen();}
qint64 QtSerialEndpoint::write(const QByteArray &raw) {
    // Qt 6.8 has no setWriteBufferSize; enforce a bound before queuing.
    if(raw.isEmpty()||raw.size()>8192||serial.bytesToWrite()+raw.size()>8192)return -1;
    return serial.write(raw);
}
QByteArray QtSerialEndpoint::readAll() {return serial.readAll();}
