#include "serial_panel.h"
#include <QDateTime>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSplitter>
#include <QVBoxLayout>

namespace {
void formats(QComboBox *box){box->addItems({QStringLiteral("文本"),"HEX","DEC","BIN","OCT"});}
SerialText::Format format(QComboBox *box){return static_cast<SerialText::Format>(box->currentIndex());}
SerialText::Encoding encoding(QComboBox *box){return static_cast<SerialText::Encoding>(box->currentIndex());}
}
SerialPanel::SerialPanel(MonitorLink &connection,QWidget *parent):QWidget(parent),link(connection) {
    auto *layout=new QVBoxLayout(this);layout->setSpacing(12);
    hint=new QLabel;hint->setWordWrap(true);layout->addWidget(hint);
    auto *sendBox=new QGroupBox(QStringLiteral("手动发送"));auto *sendLayout=new QVBoxLayout(sendBox);
    auto *options=new QHBoxLayout;sendFormat=new QComboBox;formats(sendFormat);sendFormat->setCurrentIndex(1);sendEncoding=new QComboBox;sendEncoding->addItems({"UTF-8","Latin-1"});ending=new QComboBox;ending->addItems({QStringLiteral("无结束符"),"LF","CR","CRLF"});
    options->addWidget(new QLabel(QStringLiteral("格式")));options->addWidget(sendFormat);options->addWidget(sendEncoding);options->addWidget(ending);options->addStretch();send=new QPushButton(QStringLiteral("发送一次"));send->setObjectName("serialSend");auto *clearInput=new QPushButton(QStringLiteral("清空输入"));options->addWidget(clearInput);options->addWidget(send);sendLayout->addLayout(options);
    input=new QPlainTextEdit;input->setObjectName("serialInput");input->setPlaceholderText(QStringLiteral("HEX示例：01 0A FF 或 010AFF；其他进制用空白分隔字节"));input->setMaximumHeight(110);input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));sendLayout->addWidget(input);
    preview=new QLabel;preview->setWordWrap(true);preview->setTextInteractionFlags(Qt::TextSelectableByMouse);sendLayout->addWidget(preview);layout->addWidget(sendBox);
    auto *receiveOptions=new QHBoxLayout;receiveFormat=new QComboBox;formats(receiveFormat);receiveFormat->setCurrentIndex(1);receiveEncoding=new QComboBox;receiveEncoding->addItems({"UTF-8","Latin-1"});receiveOptions->addWidget(new QLabel(QStringLiteral("显示格式")));receiveOptions->addWidget(receiveFormat);receiveOptions->addWidget(receiveEncoding);counts=new QLabel;receiveOptions->addWidget(counts,1);auto *clearView=new QPushButton(QStringLiteral("清空显示"));receiveOptions->addWidget(clearView);layout->addLayout(receiveOptions);
    auto *splitter=new QSplitter(Qt::Horizontal);
    for(const auto &title:{QStringLiteral("接收数据"),QStringLiteral("已排队发送")}) {
        auto *box=new QGroupBox(title);auto *boxLayout=new QVBoxLayout(box);auto *view=new QPlainTextEdit;view->setReadOnly(true);view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));view->setMinimumHeight(160);view->document()->setMaximumBlockCount(600);boxLayout->addWidget(view);splitter->addWidget(box);
        if(!received)received=view;else transmitted=view;
    }
    received->setObjectName("serialReceived");transmitted->setObjectName("serialTransmitted");layout->addWidget(splitter,1);
    recordPath=new QLabel;recordPath->setWordWrap(true);recordPath->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(recordPath);
    auto *scope=new QLabel(QStringLiteral("显示保留最近16 KiB；原始记录持续保存。驱动排队成功不代表设备已执行。"));scope->setWordWrap(true);layout->addWidget(scope);
    connect(input,&QPlainTextEdit::textChanged,this,[this]{if(input->toPlainText().size()>16384){input->setPlainText(input->toPlainText().first(16384));}updatePreview();});
    for(auto *box:{sendFormat,sendEncoding,ending})connect(box,&QComboBox::currentIndexChanged,this,&SerialPanel::updatePreview);
    for(auto *box:{receiveFormat,receiveEncoding})connect(box,&QComboBox::currentIndexChanged,this,&SerialPanel::updateViews);
    connect(clearInput,&QPushButton::clicked,input,&QPlainTextEdit::clear);
    connect(clearView,&QPushButton::clicked,this,[this]{rx.clear();tx.clear();rxCount=txCount=0;updateViews();});
    connect(send,&QPushButton::clicked,this,[this]{QByteArray raw;QString error;if(!SerialText::encode(input->toPlainText(),format(sendFormat),encoding(sendEncoding),static_cast<SerialText::Ending>(ending->currentIndex()),raw,error))return;if(link.sendRaw(raw)!=raw.size())hint->setText(QStringLiteral("未完整发送；请查看连接状态与原始记录，未自动重发"));});
    connect(&link,&MonitorLink::connectionChanged,this,[this](bool on){if(on){rx.clear();tx.clear();rxCount=txCount=0;updateViews();}refreshConnection();});
    viewRefresh.setSingleShot(true);viewRefresh.setInterval(50);connect(&viewRefresh,&QTimer::timeout,this,&SerialPanel::updateViews);
    connect(&link,&MonitorLink::rawReceived,this,[this](const QByteArray &raw){rxCount+=quint64(raw.size());rx=(rx+raw).last(16384);if(!viewRefresh.isActive())viewRefresh.start();});
    connect(&link,&MonitorLink::rawTransmitted,this,[this](const QByteArray &raw){txCount+=quint64(raw.size());tx=(tx+raw).last(16384);if(!viewRefresh.isActive())viewRefresh.start();});
    refreshConnection();updateViews();
}
void SerialPanel::refreshConnection() {
    const bool manual=link.purpose()==MonitorLink::Purpose::SerialTool;
    hint->setText(!link.isOpen()?QStringLiteral("先在连接栏选择“通用串口维护”再连接，才能手动发送。"):(manual?QStringLiteral("通用串口维护 · 平台任务、状态查询和操作会话已停用"):QStringLiteral("平台连接 · 可查看收发；手动发送需断开后切换用途")));
    recordPath->setText(link.journalPath().isEmpty()?QStringLiteral("尚无原始记录"):QStringLiteral("原始记录：%1").arg(link.journalPath()));updatePreview();
}
void SerialPanel::updatePreview() {
    sendEncoding->setEnabled(sendFormat->currentIndex()==0);QByteArray raw;QString error;
    const bool valid=SerialText::encode(input->toPlainText(),format(sendFormat),encoding(sendEncoding),static_cast<SerialText::Ending>(ending->currentIndex()),raw,error);
    preview->setText(valid?QStringLiteral("%1 B · %2%3").arg(raw.size()).arg(QString::fromLatin1(raw.first(64).toHex(' ')).toUpper()).arg(raw.size()>64?QStringLiteral(" …"):QString{}):error);
    send->setEnabled(valid&&link.isOpen()&&link.purpose()==MonitorLink::Purpose::SerialTool);
}
void SerialPanel::updateViews() {
    receiveEncoding->setEnabled(receiveFormat->currentIndex()==0);
    received->setPlainText(SerialText::display(rx,format(receiveFormat),encoding(receiveEncoding)));transmitted->setPlainText(SerialText::display(tx,format(receiveFormat),encoding(receiveEncoding)));
    counts->setText(QStringLiteral("RX %1 B · TX %2 B").arg(rxCount).arg(txCount));
}
