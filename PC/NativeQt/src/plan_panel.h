#pragma once
#include "plan_controller.h"
#include <QWidget>
#include <QLineEdit>
#include <QTableWidget>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>

class PlanPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlanPanel(PlanController &controller,QWidget *parent=nullptr);
    bool loadDraft(const QString &path,QString &error);
private:
    PlanController &controller;
    QLineEdit *profilePath,*planId,*purpose;
    QTableWidget *steps,*results;
    QLabel *validation,*execution;
    QCheckBox *motionConfirmed;
    QPushButton *startButton;
    QLabel *auditStatus;
    QJsonObject auditedReport;
    QByteArray profile;
    void addStep(const QJsonObject &step={});
    QByteArray draftBytes() const;
    bool readProfile(QString &error);
    std::optional<PlanSnapshot> validate();
    void updateResults();
    void save(bool report);
    void auditRecord(bool chooseFile);
    void exportAudit();
};
