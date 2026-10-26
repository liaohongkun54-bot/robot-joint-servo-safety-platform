#pragma once
#include "monitor_link.h"
#include "serial_text.h"
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>

class SerialPanel : public QWidget {
    Q_OBJECT
public:
    explicit SerialPanel(MonitorLink &link,QWidget *parent=nullptr);
    void refreshConnection();
private:
    MonitorLink &link;
    QComboBox *sendFormat,*sendEncoding,*ending,*receiveFormat,*receiveEncoding;
    QPlainTextEdit *input,*received=nullptr,*transmitted=nullptr;
    QLabel *hint,*preview,*counts,*recordPath;
    QPushButton *send;
    QByteArray rx,tx;
    quint64 rxCount=0,txCount=0;
    QTimer viewRefresh;
    void updatePreview();
    void updateViews();
};
