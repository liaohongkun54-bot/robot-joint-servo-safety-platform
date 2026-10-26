#include "main_window.h"
#include "plan_panel.h"
#include "serial_panel.h"
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QFileDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonDocument>
#include <QMessageBox>
#include <QScrollArea>
#include <QSerialPortInfo>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <QDir>
#include <QDialog>
#include <QMenuBar>
#include <QTextBrowser>

namespace {
QString hex32(qint64 n) {return QString("0x%1").arg(quint32(n),8,16,QChar('0'));}
QString safety(int s) {const QStringList names{"INIT","WAIT_FEEDBACK","ONLINE","ACTIVE","SAFE","RECOVERING","FAULT"};return s>=0&&s<names.size()?names[s]:QString::number(s);}
QWidget *chartControls(TrendPlot *chart) {
    auto *row=new QWidget;auto *layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
    const QStringList names{QStringLiteral("转速"),QStringLiteral("反馈电流"),QStringLiteral("编码器"),QStringLiteral("温度")};
    for(int i=0;i<4;++i) {auto *box=new QCheckBox(names[i]);box->setChecked(true);layout->addWidget(box);QObject::connect(box,&QCheckBox::toggled,chart,[chart,i](bool on){chart->setChannel(i,on);});}
    layout->addStretch();auto *window=new QComboBox;for(int s:{1,5,10,30,60})window->addItem(QString("%1 s").arg(s),s);window->setCurrentIndex(2);layout->addWidget(window);QObject::connect(window,&QComboBox::currentIndexChanged,chart,[chart,window]{chart->setWindow(window->currentData().toInt());});
    auto *follow=new QPushButton(QStringLiteral("回到最新 / 自动缩放"));layout->addWidget(follow);QObject::connect(follow,&QPushButton::clicked,chart,&TrendPlot::followLatest);return row;
}
QString sampleText(const Wire::Sample &s,const QString &prefix) {
    if(!s.fresh)return prefix+QStringLiteral(" · 反馈失效，数值不可用");
    return prefix+QStringLiteral(" · 转速 %1 rpm    电流 %2 raw    编码器 %3 raw    温度 %4 °C    转矩 — Nm").arg(s.speed).arg(s.current).arg(s.encoder).arg(s.temperature);
}
}
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),controller(link,this) {
    setupUi();
}
MainWindow::MainWindow(SerialEndpoint &endpoint,const QString &directory,QWidget *parent):QMainWindow(parent),link(endpoint,directory),controller(link,this) {
    setupUi();
}
void MainWindow::setupUi() {
    setWindowTitle(QStringLiteral("机器人关节伺服驱动与安全控制平台"));resize(1280,900);setMinimumSize(840,620);
    auto *help=menuBar()->addMenu(QStringLiteral("帮助"));auto *guide=help->addAction(QStringLiteral("使用说明书"));guide->setShortcut(QKeySequence::HelpContents);
    connect(guide,&QAction::triggered,this,[this]{QFile file(QApplication::applicationDirPath()+"/docs/USER_GUIDE.md");if(!file.open(QIODevice::ReadOnly)||file.size()>131072){QMessageBox::warning(this,QStringLiteral("说明书未打开"),QStringLiteral("请保留完整发布目录中的docs/USER_GUIDE.md"));return;}auto *dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(QStringLiteral("平台使用说明书"));dialog->resize(900,700);auto *box=new QVBoxLayout(dialog);auto *text=new QTextBrowser; text->setMarkdown(QString::fromUtf8(file.readAll()));box->addWidget(text);dialog->show();});
    auto *about=help->addAction(QStringLiteral("关于"));connect(about,&QAction::triggered,this,[this]{QMessageBox::about(this,QStringLiteral("关于"),QStringLiteral("机器人关节伺服驱动与安全控制平台 %1\nC++17 · Qt %2 · QCustomPlot 2.1.1\n功能范围、使用步骤和验证状态见F1说明书。").arg(QApplication::applicationVersion(),qVersion()));});
    auto *root=new QWidget;auto *layout=new QVBoxLayout(root);layout->setContentsMargins(16,10,16,10);layout->setSpacing(8);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(root);setCentralWidget(scroll);
    auto *title=new QLabel(QStringLiteral("机器人关节伺服驱动与安全控制平台"));title->setObjectName("title");auto *heading=new QHBoxLayout;heading->addWidget(title);heading->addStretch();purpose=new QComboBox;purpose->setObjectName("connectionPurpose");purpose->addItems({QStringLiteral("平台任务与监测"),QStringLiteral("通用串口维护")});heading->addWidget(purpose);layout->addLayout(heading);
    auto *row=new QHBoxLayout;ports=new QComboBox;ports->setMinimumWidth(210);baud=new QComboBox;for(int b:{9600,19200,38400,57600,115200,230400,460800,921600})baud->addItem(QString::number(b),b);baud->setCurrentText("115200");
    auto *refresh=new QPushButton(QStringLiteral("刷新端口"));connectButton=new QPushButton(QStringLiteral("连接"));connection=new QLabel(QStringLiteral("未连接"));
    ports->setObjectName("serialPorts");connectButton->setObjectName("connectPort");
    row->addWidget(new QLabel(QStringLiteral("端口")));row->addWidget(ports);row->addWidget(refresh);row->addWidget(new QLabel(QStringLiteral("波特率")));row->addWidget(baud);row->addWidget(new QLabel("8N1"));row->addWidget(connectButton);row->addStretch();row->addWidget(connection);layout->addLayout(row);
    auto refreshPorts=[this]{if(link.isOpen())return;ports->clear();for(const auto &p:QSerialPortInfo::availablePorts())ports->addItem(p.portName()+"  "+p.description(),p.portName());};refreshPorts();connect(refresh,&QPushButton::clicked,this,refreshPorts);
    mcu=new QLabel(QStringLiteral("MCU状态待查询 · 输出许可未知"));mcu->setObjectName("state");mcu->setWordWrap(true);layout->addWidget(mcu);
    auto *sessionRow=new QHBoxLayout;auto *openSession=new QPushButton(QStringLiteral("建立操作会话")),*stopMotion=new QPushButton(QStringLiteral("停止 / 撤销许可")),*clearFault=new QPushButton(QStringLiteral("申请故障复位")),*closeSession=new QPushButton(QStringLiteral("关闭操作会话")),*refreshState=new QPushButton(QStringLiteral("刷新状态"));auto *sessionState=new QLabel(QStringLiteral("会话未建立 · 无任务许可"));sessionState->setWordWrap(true);
    for(auto *b:{openSession,stopMotion,clearFault,closeSession,refreshState})sessionRow->addWidget(b);
    sessionRow->addWidget(sessionState,1);layout->addLayout(sessionRow);sessionState->setMinimumWidth(90);sessionState->setToolTip(QStringLiteral("建立操作会话不会使能电机。任务需精确的一次性许可。"));
    for(auto *b:{openSession,stopMotion,clearFault,closeSession,refreshState})b->setEnabled(false);
    connect(&link,&MonitorLink::connectionChanged,refreshState,[this,refreshState](bool on){refreshState->setEnabled(on&&link.purpose()==MonitorLink::Purpose::Platform);});
    connect(openSession,&QPushButton::clicked,this,[this]{if(!controller.openSession())QMessageBox::information(this,QStringLiteral("会话未建立"),QStringLiteral("请先连接、读取设备，并等待当前查询结束；输出状态未核实需先刷新状态。"));});
    connect(stopMotion,&QPushButton::clicked,&controller,&PlanController::stop);connect(clearFault,&QPushButton::clicked,this,[this]{if(!controller.clearFault())QMessageBox::information(this,QStringLiteral("未发送复位"),QStringLiteral("需要有效操作会话，且当前没有执行中的计划或待处理控制请求。"));});connect(closeSession,&QPushButton::clicked,&controller,&PlanController::closeSession);connect(refreshState,&QPushButton::clicked,&controller,&PlanController::refreshStatus);
    connect(&controller,&PlanController::changed,this,[this,sessionState,openSession,clearFault,closeSession,stopMotion]{sessionState->setText(controller.sessionActive()?QStringLiteral("操作会话已建立"):QStringLiteral("操作会话未建立"));openSession->setEnabled(link.isOpen()&&link.purpose()==MonitorLink::Purpose::Platform&&!controller.running()&&!controller.sessionActive());clearFault->setEnabled(controller.sessionActive()&&!controller.running());closeSession->setEnabled(controller.sessionActive()&&!controller.running());stopMotion->setEnabled(controller.sessionActive());});
    auto *tabs=new QTabWidget;layout->addWidget(tabs,1);
    auto *monitor=new QWidget;auto *monitorLayout=new QVBoxLayout(monitor);device=new QLabel(QStringLiteral("设备信息未读取"));device->setWordWrap(true);monitorLayout->addWidget(device);
    auto *actions=new QHBoxLayout;auto *info=new QPushButton(QStringLiteral("读取设备")),*status=new QPushButton(QStringLiteral("刷新状态"));capture=new QPushButton(QStringLiteral("开始采集"));actions->addWidget(info);actions->addWidget(status);actions->addWidget(capture);actions->addStretch();actions->addWidget(new QLabel(QStringLiteral("CAN1 · 1 Mbps · C620 ID1")));monitorLayout->addLayout(actions);
    readout=new QLabel(QStringLiteral("转速 — rpm    电流 — raw    编码器 — raw    温度 — °C    转矩 — Nm"));readout->setObjectName("readout");readout->setWordWrap(true);monitorLayout->addWidget(readout);
    liveChart=new TrendPlot;monitorLayout->addWidget(chartControls(liveChart));monitorLayout->addWidget(liveChart,1);tabs->addTab(monitor,QStringLiteral("设备监测"));
    auto *replay=new QWidget;auto *replayLayout=new QVBoxLayout(replay);auto *files=new QHBoxLayout;auto *load=new QPushButton(QStringLiteral("打开实验记录")),*csv=new QPushButton(QStringLiteral("导出反馈CSV")),*report=new QPushButton(QStringLiteral("导出核对摘要"));files->addWidget(load);files->addWidget(csv);files->addWidget(report);files->addStretch();replayLayout->addLayout(files);
    historyInfo=new QLabel(QStringLiteral("选择现有JSONL记录，从原始收发字节关联真实样本"));historyInfo->setWordWrap(true);replayLayout->addWidget(historyInfo);
    auto *transport=new QHBoxLayout;play=new QPushButton(QStringLiteral("播放"));auto *prev=new QPushButton(QStringLiteral("上一点")),*next=new QPushButton(QStringLiteral("下一点"));progress=new QSlider(Qt::Horizontal);progress->setRange(0,0);auto *speed=new QComboBox;for(int n:{2,10,20})speed->addItem(QStringLiteral("%1 点/秒").arg(n),n);speed->setCurrentIndex(1);transport->addWidget(play);transport->addWidget(prev);transport->addWidget(next);transport->addWidget(progress,1);transport->addWidget(speed);replayLayout->addLayout(transport);
    historyChart=new TrendPlot;replayLayout->addWidget(chartControls(historyChart));replayLayout->addWidget(historyChart,1);tabs->addTab(replay,QStringLiteral("记录回放"));
    auto *plans=new PlanPanel(controller);tabs->insertTab(1,plans,QStringLiteral("测试任务"));
    cursor=new QLabel(QStringLiteral("点击曲线读取采样点 · 各通道独立纵轴与单位"));cursor->setWordWrap(true);layout->addWidget(cursor);
    auto *maintenance=new QTabWidget;auto *serialPanel=new SerialPanel(link);maintenance->addTab(serialPanel,QStringLiteral("串口工具"));auto *detailTabs=new QTabWidget;events=new QTableWidget(0,4);events->setHorizontalHeaderLabels({QStringLiteral("类型"),QStringLiteral("请求号"),QStringLiteral("MCU tick/ms"),QStringLiteral("回执详情")});events->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);events->setEditTriggers(QAbstractItemView::NoEditTriggers);diagnostics=new QPlainTextEdit;diagnostics->setReadOnly(true);diagnostics->document()->setMaximumBlockCount(300);detailTabs->addTab(events,QStringLiteral("关联回执"));detailTabs->addTab(diagnostics,QStringLiteral("通信事件"));maintenance->addTab(detailTabs,QStringLiteral("诊断记录"));tabs->addTab(maintenance,QStringLiteral("设备维护"));
    connect(purpose,&QComboBox::currentIndexChanged,this,[this,serialPanel,tabs]{if(link.setPurpose(purpose->currentIndex()==0?MonitorLink::Purpose::Platform:MonitorLink::Purpose::SerialTool)){serialPanel->refreshConnection();if(purpose->currentIndex()==1)tabs->setCurrentIndex(3);}});
    connect(tabs,&QTabWidget::currentChanged,this,[this](int page){cursor->setVisible(page==0||page==2);});
    statusBar()->showMessage(QStringLiteral("请选择设备并连接，或打开已有记录查看测试结果"));
    connect(connectButton,&QPushButton::clicked,this,[this]{if(link.isOpen()){link.close();return;}QString error;if(ports->currentData().toString().isEmpty()||!link.open(ports->currentData().toString(),baud->currentData().toInt(),error))QMessageBox::warning(this,QStringLiteral("连接未建立"),error.isEmpty()?QStringLiteral("请选择端口"):error);});
    connect(&link,&MonitorLink::connectionChanged,this,[this,info,status](bool on){const bool platform=link.purpose()==MonitorLink::Purpose::Platform;connectButton->setText(on?QStringLiteral("断开"):QStringLiteral("连接"));ports->setEnabled(!on);baud->setEnabled(!on);purpose->setEnabled(!on);info->setEnabled(on&&platform);status->setEnabled(on&&platform);capture->setEnabled(on&&platform&&capabilityTelemetry);connection->setText(on?QStringLiteral("串口已连接"):QStringLiteral("未连接"));statusAge.invalidate();sampleAge.invalidate();mcu->setText(!on?QStringLiteral("连接已断开 · 当前许可未知"):platform?QStringLiteral("MCU状态待查询"):QStringLiteral("通用串口维护 · 不解析MCU状态"));if(!on){capabilityTelemetry=false;capturing=false;capture->setText(QStringLiteral("开始采集"));readout->setText(QStringLiteral("采集已停止 · 历史曲线保留"));}else{liveSamples.clear();liveChart->setSamples({});device->setText(QStringLiteral("设备信息未读取"));if(platform)QTimer::singleShot(0,this,[this]{link.query(6);});}});
    info->setEnabled(false);status->setEnabled(false);capture->setEnabled(false);
    connect(info,&QPushButton::clicked,this,[this]{link.query(6);});connect(status,&QPushButton::clicked,&controller,&PlanController::refreshStatus);
    connect(capture,&QPushButton::clicked,this,[this]{link.setCapture(!link.isCapturing());});
    connect(&link,&MonitorLink::captureChanged,this,[this](bool on){capturing=on;capture->setText(on?QStringLiteral("停止采集"):QStringLiteral("开始采集"));});
    connect(&link,&MonitorLink::frameReceived,this,&MainWindow::onFrame);
    connect(&controller,&PlanController::frameReceived,this,&MainWindow::onFrame);
    connect(&controller,&PlanController::message,this,[this](const QString &text){diagnostics->appendPlainText(text);statusBar()->showMessage(text,10000);});
    connect(&link,&MonitorLink::message,this,[this](const QString &text){diagnostics->appendPlainText(text);statusBar()->showMessage(text);if(text.contains(QStringLiteral("停止采集"))||text.contains(QStringLiteral("采集已停止"))){capturing=false;capture->setText(QStringLiteral("开始采集"));}});
    connect(liveChart,&TrendPlot::selected,cursor,&QLabel::setText);connect(historyChart,&TrendPlot::selected,cursor,&QLabel::setText);
    connect(load,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("打开实验记录"),{},"JSONL (*.jsonl)");if(path.isEmpty())return;QString error;if(!loadHistory(path,error))QMessageBox::warning(this,QStringLiteral("记录未载入"),error);});
    connect(csv,&QPushButton::clicked,this,&MainWindow::exportCsv);connect(report,&QPushButton::clicked,this,&MainWindow::exportReport);
    connect(progress,&QSlider::valueChanged,this,&MainWindow::updatePlayback);
    connect(prev,&QPushButton::clicked,this,[this]{replayTimer.stop();play->setText(QStringLiteral("播放"));progress->setValue(progress->value()-1);});connect(next,&QPushButton::clicked,this,[this]{replayTimer.stop();play->setText(QStringLiteral("播放"));progress->setValue(progress->value()+1);});
    connect(play,&QPushButton::clicked,this,[this,speed]{if(replayTimer.isActive()){replayTimer.stop();play->setText(QStringLiteral("播放"));}else if(!history.samples.isEmpty()){if(progress->value()==progress->maximum())progress->setValue(0);replayTimer.start(1000/speed->currentData().toInt());play->setText(QStringLiteral("暂停"));}});
    connect(speed,&QComboBox::currentIndexChanged,this,[this,speed]{if(replayTimer.isActive())replayTimer.setInterval(1000/speed->currentData().toInt());});
    connect(&replayTimer,&QTimer::timeout,this,[this]{if(progress->value()<progress->maximum())progress->setValue(progress->value()+1);if(progress->value()>=progress->maximum()){replayTimer.stop();play->setText(QStringLiteral("播放"));}});
    freshnessTimer.setInterval(100);connect(&freshnessTimer,&QTimer::timeout,this,[this]{if(statusAge.isValid())mcu->setText(statusText+(statusAge.elapsed()>=1000?QStringLiteral(" · 快照已过期，当前许可未知"):QStringLiteral(" · 状态有效")));if(sampleAge.isValid()&&sampleAge.elapsed()>500)readout->setText(QStringLiteral("实时反馈已过期 · 历史曲线保留"));});freshnessTimer.start();
    statusRefresh.setInterval(500);connect(&statusRefresh,&QTimer::timeout,this,[this]{if(link.isOpen()&&link.purpose()==MonitorLink::Purpose::Platform&&!link.isCapturing()&&!controller.running()&&!controller.report()["awaiting_final_status"].toBool()&&statusAge.isValid()&&statusAge.elapsed()<1000)controller.refreshStatus();});statusRefresh.start();
}
void MainWindow::onFrame(const Wire::Frame &f) {
    const auto &o=f.fields;
    if(f.type==6) {const auto version=quint32(o["firmware_version"].toInteger());const auto uid=QString("%1%2%3").arg(o["uid_word0"].toInteger(),8,16,QChar('0')).arg(o["uid_word1"].toInteger(),8,16,QChar('0')).arg(o["uid_word2"].toInteger(),8,16,QChar('0'));device->setText(QStringLiteral("C620 ID1 · 控制板固件 %1.%2.%3").arg(version>>16).arg((version>>8)&255).arg(version&255));device->setToolTip(QStringLiteral("设备自报UID %1 · 身份未认证").arg(uid.toUpper()));capabilityTelemetry=(o["feature_mask"].toInt()&2)!=0;capture->setEnabled(link.isOpen()&&capabilityTelemetry);if(!controller.running()&&!controller.sessionActive())QTimer::singleShot(0,this,[this]{link.query(4);});}
    if(f.type==4) {statusText=QStringLiteral("MCU %1 · 输出许可 %2 · 活动任务 %3 · 故障 %4 · 记录 %5/64").arg(safety(o["safety_state"].toInt())).arg(o["output_permitted"].toInt()).arg(o["motion_active"].toInt()).arg(hex32(o["safety_fault_code"].toInteger())).arg(o["record_count"].toInt());statusAge.restart();mcu->setText(statusText);}
    if(f.type==5) {auto s=Wire::Sample::fromFrame(f);liveSamples.push_back(s);if(liveSamples.size()>6000)liveSamples.removeFirst();liveChart->setSamples(liveSamples);readout->setText(sampleText(s,QStringLiteral("实时反馈")));sampleAge.restart();}
}
bool MainWindow::loadHistory(const QString &path,QString &error) {
    if(controller.running()||controller.sessionActive()){error=QStringLiteral("请在测试结束并关闭操作会话后载入历史记录");return false;}
    replayTimer.stop();play->setText(QStringLiteral("播放"));History loaded;
    if(!loaded.load(path,error)) {history=History{};historyChart->setSamples({});events->setRowCount(0);progress->setRange(0,0);historyInfo->setText(error);return false;}
    history=std::move(loaded);events->setRowCount(0);
    for(const auto &f:history.frames) {const int row=events->rowCount();if(row>=1000)break;events->insertRow(row);const QStringList cells{Wire::kind(f.type),QString::number(f.fields["request_id"].toInteger()),QString::number(f.fields.value("timestamp_ms").toInteger(f.fields["sample_tick_ms"].toInteger())),QString::fromUtf8(QJsonDocument(f.fields).toJson(QJsonDocument::Compact))};for(int i=0;i<4;++i)events->setItem(row,i,new QTableWidgetItem(cells[i]));}
    const auto summary=history.summary();historyInfo->setText(QStringLiteral("%1 · 关联帧 %2 · 样本 %3（新鲜%4）· %5 · 异常帧%6 / 未关联%7\nSHA-256 %8").arg(QFileInfo(path).fileName()).arg(history.frames.size()).arg(history.samples.size()).arg(summary["fresh_samples"].toInt()).arg(history.closed?QStringLiteral("日志有结束记录"):QStringLiteral("开放日志前缀")).arg(history.invalid).arg(history.unmatched).arg(history.sha256));
    progress->setRange(0,qMax(0,int(history.samples.size())-1));progress->setValue(progress->maximum());updatePlayback(progress->value());
    auto *tabs=findChild<QTabWidget*>();if(tabs)tabs->setCurrentIndex(2);return true;
}
void MainWindow::updatePlayback(int position) {if(history.samples.isEmpty()){historyChart->setSamples({});return;}const int at=qBound(0,position,int(history.samples.size())-1);historyChart->setSamples(history.samples.first(at+1));cursor->setText(sampleText(history.samples[at],QStringLiteral("历史采样 %1/%2 · MCU %3 ms").arg(at+1).arg(history.samples.size()).arg(history.samples[at].tick)));}
void MainWindow::exportCsv() {
    if(history.samples.isEmpty()) return;
    const auto path=QFileDialog::getSaveFileName(this,QStringLiteral("导出关联反馈"),{},"CSV (*.csv)");
    if(path.isEmpty()) return;
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)){QMessageBox::warning(this,QStringLiteral("未导出"),QStringLiteral("文件已存在或无法创建，请使用新文件名"));return;}
    QTextStream out(&file);out<<"request_id,mcu_tick_ms,last_rx_tick_ms,encoder_raw,speed_rpm,current_raw,temperature_c,fresh,source_sha256\n";for(const auto &s:history.samples)out<<s.request<<','<<s.tick<<','<<s.lastRx<<','<<s.encoder<<','<<s.speed<<','<<s.current<<','<<s.temperature<<','<<int(s.fresh)<<','<<history.sha256<<'\n';out.flush();if(out.status()!=QTextStream::Ok||!file.flush())QMessageBox::warning(this,QStringLiteral("导出失败"),file.errorString());
}
void MainWindow::exportReport() {if(history.frames.isEmpty())return;const auto path=QFileDialog::getSaveFileName(this,QStringLiteral("导出核对摘要"),{},"JSON (*.json)");if(path.isEmpty())return;QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)){QMessageBox::warning(this,QStringLiteral("未导出"),QStringLiteral("文件已存在或无法创建"));return;}const auto raw=QJsonDocument(history.summary()).toJson();if(file.write(raw)!=raw.size()||!file.flush())QMessageBox::warning(this,QStringLiteral("导出失败"),file.errorString());}
bool MainWindow::selfTest(const QString &report,const QString &screenshot) {
    const auto *planSteps=findChild<QTableWidget*>("planSteps");const auto *planResults=findChild<QTableWidget*>("planResults");
    const bool ok=!link.isOpen()&&!controller.sessionActive()&&!controller.running()&&planSteps&&planSteps->rowCount()==1&&planResults&&planResults->rowCount()==0&&history.samples.size()>0&&historyChart->plot()->graphCount()==4&&historyChart->sampleCount()==history.samples.size();
    if(!screenshot.isEmpty()&&!grab().save(screenshot))return false;
    if(!report.isEmpty()){QFile file(report);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return false;auto summary=history.summary();summary["status"]=ok?"passed":"failed";summary["serial_opened"]=link.isOpen();summary["plot_library"]="QCustomPlot 2.1.1";summary["qt_version"]=qVersion();summary["implementation"]="C++17 Qt Widgets";file.write(QJsonDocument(summary).toJson());}
    return ok;
}
void MainWindow::selectPage(int index) {if(auto *tabs=findChild<QTabWidget*>())tabs->setCurrentIndex(qBound(0,index,3));}
