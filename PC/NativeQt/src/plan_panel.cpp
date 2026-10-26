#include "plan_panel.h"
#include "native_audit.h"
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QMessageBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QString label(const QString &value) {
    static const QHash<QString,QString> labels{{"feedback_readonly",QStringLiteral("只读反馈")},{"zero_output",QStringLiteral("零输出")},{"unarmed_rejection",QStringLiteral("未授权拒绝")},{"limited_motion",QStringLiteral("受限动作")},{"deadline_stop",QStringLiteral("到期停止")},{"passed",QStringLiteral("通过")},{"failed",QStringLiteral("失败")},{"uncertain",QStringLiteral("不确定")},{"cancelled",QStringLiteral("已中止排程")},{"not_executed",QStringLiteral("未执行")},{"succeeded",QStringLiteral("计划通过")},{"waiting_result",QStringLiteral("等待任务接入")},{"waiting_stop",QStringLiteral("执行中，等待停止回执")},{"waiting_feedback",QStringLiteral("等待反馈")},{"waiting_info",QStringLiteral("读取设备信息")},{"waiting_status",QStringLiteral("核对MCU状态")}};
    return labels.value(value,value);
}
}
PlanPanel::PlanPanel(PlanController &c,QWidget *parent):QWidget(parent),controller(c) {
    auto *layout=new QVBoxLayout(this);auto *tabs=new QTabWidget;layout->addWidget(tabs);
    auto *editor=new QWidget;auto *editLayout=new QVBoxLayout(editor);tabs->addTab(editor,QStringLiteral("配置计划"));
    auto *profileRow=new QHBoxLayout;profilePath=new QLineEdit(QApplication::applicationDirPath()+"/profiles/c620_m3508_lab_v1.json");profilePath->setReadOnly(true);auto *choose=new QPushButton(QStringLiteral("设备能力文件"));profileRow->addWidget(choose);profileRow->addWidget(profilePath,1);editLayout->addLayout(profileRow);
    auto *identity=new QHBoxLayout;planId=new QLineEdit("platform_test");purpose=new QLineEdit(QStringLiteral("平台通信、任务与停止验证"));identity->addWidget(new QLabel(QStringLiteral("计划ID")));identity->addWidget(planId);identity->addWidget(new QLabel(QStringLiteral("目的")));identity->addWidget(purpose,1);editLayout->addLayout(identity);
    steps=new QTableWidget(0,6);steps->setObjectName("planSteps");steps->setHorizontalHeaderLabels({QStringLiteral("步骤ID"),QStringLiteral("测试类型"),QStringLiteral("控制模式"),QStringLiteral("命令 / raw"),QStringLiteral("期限 / ms"),QStringLiteral("轮次")});steps->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);steps->setMinimumHeight(220);editLayout->addWidget(steps,1);
    auto *buttons=new QHBoxLayout;auto *add=new QPushButton(QStringLiteral("添加步骤")),*remove=new QPushButton(QStringLiteral("删除所选")),*load=new QPushButton(QStringLiteral("载入草案")),*saveDraft=new QPushButton(QStringLiteral("另存草案")),*check=new QPushButton(QStringLiteral("校验计划"));for(auto *b:{add,remove,load,saveDraft,check})buttons->addWidget(b);editLayout->addLayout(buttons);
    validation=new QLabel(QStringLiteral("命令使用 C620 raw，非安培、转速或机械角度；按能力文件和现有MCU上限双重校验。"));validation->setWordWrap(true);editLayout->addWidget(validation);
    auto *runner=new QWidget;auto *runLayout=new QVBoxLayout(runner);tabs->addTab(runner,QStringLiteral("执行与结果"));
    motionConfirmed=new QCheckBox(QStringLiteral("本次运动：已核对供电、共地、CAN线序、限流、机械固定、急停和快速断电条件"));runLayout->addWidget(motionConfirmed);
    auto *runButtons=new QHBoxLayout;startButton=new QPushButton(QStringLiteral("启动计划"));auto *cancel=new QPushButton(QStringLiteral("停止后续排程")),*exportReport=new QPushButton(QStringLiteral("导出执行结果"));runButtons->addWidget(startButton);runButtons->addWidget(cancel);runButtons->addWidget(exportReport);runLayout->addLayout(runButtons);
    execution=new QLabel(QStringLiteral("尚未启动。停止后续排程不等同于停止当前动作；独立停止使用顶部按钮。"));execution->setWordWrap(true);runLayout->addWidget(execution);
    auto *auditButtons=new QHBoxLayout;auto *auditCurrent=new QPushButton(QStringLiteral("核对本次原始记录")),*auditFile=new QPushButton(QStringLiteral("核对历史记录")),*auditExport=new QPushButton(QStringLiteral("导出核对报告"));auditButtons->addWidget(auditCurrent);auditButtons->addWidget(auditFile);auditButtons->addWidget(auditExport);runLayout->addLayout(auditButtons);
    auditStatus=new QLabel(QStringLiteral("尚未核对原始记录。核对针对文件快照，执行结果与记录完整性分别判断。"));auditStatus->setWordWrap(true);runLayout->addWidget(auditStatus);
    connect(auditCurrent,&QPushButton::clicked,this,[this]{auditRecord(false);});connect(auditFile,&QPushButton::clicked,this,[this]{auditRecord(true);});connect(auditExport,&QPushButton::clicked,this,&PlanPanel::exportAudit);
    connect(&controller,&PlanController::changed,this,[this,auditCurrent,auditFile,auditExport]{const bool available=!controller.running()&&!controller.sessionActive();for(auto *button:{auditCurrent,auditFile,auditExport})button->setEnabled(available);});
    results=new QTableWidget(0,5);results->setObjectName("planResults");results->setHorizontalHeaderLabels({QStringLiteral("步骤 / 轮次"),QStringLiteral("测试"),QStringLiteral("请求号"),QStringLiteral("结果"),QStringLiteral("回执 / 原因")});results->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);results->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);results->setEditTriggers(QAbstractItemView::NoEditTriggers);runLayout->addWidget(results,1);
    connect(add,&QPushButton::clicked,this,[this]{addStep();});connect(remove,&QPushButton::clicked,this,[this]{if(steps->currentRow()>=0)steps->removeRow(steps->currentRow());});
    connect(choose,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("选择设备能力描述"),{},"JSON (*.json)");if(path.isEmpty())return;profilePath->setText(path);profile.clear();QString e;if(!readProfile(e))validation->setText(e);});
    connect(load,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("载入测试草案"),{},"JSON (*.json)");if(path.isEmpty())return;QString error;if(!loadDraft(path,error))QMessageBox::warning(this,QStringLiteral("草案未载入"),error);});
    connect(saveDraft,&QPushButton::clicked,this,[this]{save(false);});connect(exportReport,&QPushButton::clicked,this,[this]{save(true);});connect(check,&QPushButton::clicked,this,[this]{validate();});
    connect(startButton,&QPushButton::clicked,this,[this]{const auto p=validate();if(!p)return;const bool confirmed=motionConfirmed->isChecked();motionConfirmed->setChecked(false);QString error;if(!controller.start(*p,confirmed,error))QMessageBox::warning(this,QStringLiteral("计划未启动"),error);});
    connect(cancel,&QPushButton::clicked,&controller,&PlanController::cancelScheduling);connect(&controller,&PlanController::changed,this,&PlanPanel::updateResults);
    addStep();
}
bool PlanPanel::readProfile(QString &error) {
    QFile f(profilePath->text());if(!f.open(QIODevice::ReadOnly)){error=f.errorString();return false;}
    if(f.size()>131072){error=QStringLiteral("能力文件超过128 KiB");return false;}profile=f.readAll();return true;
}
void PlanPanel::addStep(const QJsonObject &s) {
    if(steps->rowCount()>=16)return;
    const int row=steps->rowCount();steps->insertRow(row);
    steps->setItem(row,0,new QTableWidgetItem(s["step_id"].toString(QString("step_%1").arg(row+1))));
    auto *test=new QComboBox;for(const QString id:{"feedback_readonly","zero_output","unarmed_rejection","limited_motion","deadline_stop"})test->addItem(label(id),id);test->setCurrentIndex(qMax(0,test->findData(s["test_id"].toString())));steps->setCellWidget(row,1,test);
    auto *mode=new QComboBox;mode->addItem(QStringLiteral("受限电流（≤100ms）"),"bounded_current_test");mode->addItem(QStringLiteral("固定+1000 / 1000ms"),"fixed_positive_run");mode->setCurrentIndex(qMax(0,mode->findData(s["mode_id"].toString())));steps->setCellWidget(row,2,mode);
    auto *command=new QSpinBox;command->setRange(-1000,1000);command->setValue(s["command_value"].toInt());steps->setCellWidget(row,3,command);
    auto *duration=new QSpinBox;duration->setRange(1,1000);duration->setValue(s["duration_ms"].toInt(100));steps->setCellWidget(row,4,duration);
    auto *attempts=new QSpinBox;attempts->setRange(1,16);attempts->setValue(s["attempts"].toInt(1));steps->setCellWidget(row,5,attempts);
    auto visibility=[test,mode,command,duration]{const bool ro=test->currentData()=="feedback_readonly";mode->setEnabled(!ro);command->setEnabled(!ro);duration->setEnabled(!ro);};visibility();connect(test,&QComboBox::currentIndexChanged,this,visibility);
}
QByteArray PlanPanel::draftBytes() const {
    QJsonArray rows;
    for(int row=0;row<steps->rowCount();++row) {
        const auto test=qobject_cast<QComboBox*>(steps->cellWidget(row,1))->currentData().toString();const bool ro=test=="feedback_readonly";
        rows.append(QJsonObject{{"step_id",steps->item(row,0)->text()},{"test_id",test},{"mode_id",ro?QJsonValue::Null:QJsonValue(qobject_cast<QComboBox*>(steps->cellWidget(row,2))->currentData().toString())},{"command_value",ro?QJsonValue::Null:QJsonValue(qobject_cast<QSpinBox*>(steps->cellWidget(row,3))->value())},{"duration_ms",ro?0:qobject_cast<QSpinBox*>(steps->cellWidget(row,4))->value()},{"attempts",qobject_cast<QSpinBox*>(steps->cellWidget(row,5))->value()}});
    }
    return QJsonDocument(QJsonObject{{"schema_version",1},{"status","draft_not_executable"},{"plan_id",planId->text()},{"profile_id",QJsonDocument::fromJson(profile).object()["profile_id"]},{"profile_sha256",QString::fromLatin1(QCryptographicHash::hash(profile,QCryptographicHash::Sha256).toHex()).toUpper()},{"purpose",purpose->text()},{"steps",rows}}).toJson();
}
std::optional<PlanSnapshot> PlanPanel::validate() {
    QString error;if(!readProfile(error)){validation->setText(error);return {};}
    auto p=PlanSnapshot::compile(draftBytes(),profile,error);validation->setText(p?QStringLiteral("计划有效 · %1轮 · 最坏记录预留 %2/64 · %3").arg(p->actions().size()).arg(p->reservedRecords()).arg(p->hasMotion()?QStringLiteral("包含运动，需要本次确认和操作会话"):QStringLiteral("无非零动作")):QStringLiteral("计划无效：")+error);return p;
}
bool PlanPanel::loadDraft(const QString &path,QString &error) {
    if(!readProfile(error))return false;
    QFile f(path);if(!f.open(QIODevice::ReadOnly)){error=f.errorString();return false;}if(f.size()>65536){error="draft exceeds 64 KiB";return false;}
    const auto raw=f.readAll();auto p=PlanSnapshot::compile(raw,profile,error);if(!p)return false;
    const auto d=QJsonDocument::fromJson(raw).object();planId->setText(d["plan_id"].toString());purpose->setText(d["purpose"].toString());steps->setRowCount(0);for(const auto &s:d["steps"].toArray())addStep(s.toObject());motionConfirmed->setChecked(false);validate();return true;
}
void PlanPanel::updateResults() {
    const auto report=controller.report();const auto rows=report["actions"].toArray();results->setRowCount(int(rows.size()));
    for(int i=0;i<rows.size();++i){const auto r=rows[i].toObject();const QStringList cells{r["step_id"].toString()+" / "+QString::number(r["attempt"].toInt()),label(r["test_id"].toString()),r.contains("request_id")?QString::number(r["request_id"].toInteger()):QStringLiteral("—"),label(r["outcome"].toString()),r["detail"].toString()};for(int col=0;col<cells.size();++col)results->setItem(i,col,new QTableWidgetItem(cells[col]));}
    if(report.contains("state"))execution->setText(label(report["state"].toString())+" · "+report["detail"].toString()+(report["awaiting_final_status"].toBool()?QStringLiteral(" · 正在核对最终MCU状态"):controller.running()?QStringLiteral(" · 正在执行，保留独立停止入口"):report["stop_state_unverified"].toBool()?QStringLiteral(" · 输出状态未核实，请刷新状态"):QStringLiteral(" · 许可已撤销、无活动任务；机械状态需现场观察")));
    startButton->setEnabled(!controller.running()&&!report["awaiting_final_status"].toBool());
}
void PlanPanel::save(bool report) {
    QByteArray bytes;
    if(report){auto r=controller.report();if(!r.contains("actions"))return;r["evidence_scope"]="native_live_state_not_independent_raw_audit";bytes=QJsonDocument(r).toJson();}
    else {auto p=validate();if(!p)return;bytes=p->draftBytes();}
    const auto path=QFileDialog::getSaveFileName(this,report?QStringLiteral("导出本次执行结果"):QStringLiteral("保存测试草案"),{},"JSON (*.json)");if(path.isEmpty())return;
    QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::NewOnly)||f.write(bytes)!=bytes.size()||!f.flush())QMessageBox::warning(this,QStringLiteral("未保存"),QStringLiteral("文件已存在或写入失败，请使用新文件名"));
}
void PlanPanel::auditRecord(bool chooseFile) {
    if(controller.running()||controller.sessionActive()){auditStatus->setText(QStringLiteral("请在测试结束并关闭操作会话后核对记录。"));return;}
    const auto path=chooseFile?QFileDialog::getOpenFileName(this,QStringLiteral("选择平台原始记录"),{},"JSONL (*.jsonl)"):controller.report()["raw_journal"].toString();
    if(path.isEmpty())return;
    NativeAudit audit;QString error;const bool ok=audit.load(path,error);auditedReport=audit.report();
    const auto evidence=auditedReport["evidence"].toString();
    QString state=evidence=="complete"?QStringLiteral("原始记录完整，结果已重算"):evidence=="partial"?QStringLiteral("记录不完整，不能作为完整验收证据"):evidence=="not_applicable"?QStringLiteral("该记录未包含测试计划，计划核对不适用"):evidence=="unsupported_format"?QStringLiteral("该记录格式缺少严格重算所需输入"):QStringLiteral("核对未通过：")+error;
    QStringList states;for(const auto &v:auditedReport["plans"].toArray()){const auto p=v.toObject();states.append(p["plan_id"].toString()+"："+label(p["state"].toString()));}
    auditStatus->setText(QFileInfo(path).fileName()+" · "+state+(states.isEmpty()?QString{}:"\n"+states.join(QStringLiteral("；")))+QStringLiteral("\n核对的是当前文件快照；机械停稳由现场观察判断。"));
    auditStatus->setToolTip(QStringLiteral("SHA-256：")+auditedReport["source_sha256"].toString());
    if(!ok)auditedReport["error"]=error;
}
void PlanPanel::exportAudit() {
    if(controller.running()||controller.sessionActive())return;
    if(auditedReport.isEmpty()){auditStatus->setText(QStringLiteral("请先核对原始记录。"));return;}
    const auto path=QFileDialog::getSaveFileName(this,QStringLiteral("导出原始记录核对报告"),{},"JSON (*.json)");if(path.isEmpty())return;
    const auto bytes=QJsonDocument(auditedReport).toJson();QFile file(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)||file.write(bytes)!=bytes.size()||!file.flush())QMessageBox::warning(this,QStringLiteral("未导出"),QStringLiteral("文件已存在或写入失败，请使用新文件名。"));
}
