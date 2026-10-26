#include "main_window.h"
#include "native_audit.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QFile>
#include <QJsonDocument>

int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setOrganizationName("MotorTestPlatform");app.setApplicationName("NativeQt");app.setFont(QFont("Microsoft YaHei UI",9));
    app.setApplicationVersion(PLATFORM_VERSION);
    app.setStyleSheet("QWidget{color:#203b4d;font-size:13px;} QMainWindow{background:#f0f4f6;} QLabel#title{font-size:23px;font-weight:600;} QLabel#state{background:#e4f0f1;border-left:4px solid #167b83;padding:10px;} QLabel#readout{background:white;padding:12px;font-size:16px;} QPushButton{background:white;border:1px solid #c6d3dc;border-radius:4px;padding:7px 12px;} QPushButton:hover{background:#e7f2f3;} QPushButton:disabled{color:#8c9ba7;background:#f3f5f6;} QComboBox{padding:6px;background:white;border:1px solid #c6d3dc;} QTabWidget::pane{background:white;border:1px solid #d9e2e8;} QTabBar::tab{padding:10px 20px;} QTabBar::tab:selected{background:white;border-bottom:3px solid #167b83;} QGroupBox{border:1px solid #d9e2e8;border-radius:4px;margin-top:10px;padding-top:8px;} QGroupBox::title{subcontrol-origin:margin;left:10px;} QTableWidget{background:white;gridline-color:#e3e9ee;}");
    QCommandLineParser args;args.addHelpOption();
    args.addVersionOption();
    args.addOption(QCommandLineOption("replay","Open an existing JSONL without connecting serial","path"));
    args.addOption(QCommandLineOption("self-test","Offline startup check; requires --replay"));
    args.addOption(QCommandLineOption("report","New self-test report path","path"));
    args.addOption(QCommandLineOption("screenshot","Save offline startup screenshot","path"));
    args.addOption(QCommandLineOption("size","Offline window check width,height","size"));
    args.addOption(QCommandLineOption("page","Initial page: monitor, tasks, history or maintenance (never starts a task)","page"));
    args.addOption(QCommandLineOption("audit-log","Offline raw native journal audit; requires --report; never connects hardware","path"));
    args.process(app);
    if(args.isSet("audit-log")) {
        if(!args.isSet("report")){qCritical("--audit-log requires a new --report path");return 4;}
        NativeAudit audit;QString error;audit.load(args.value("audit-log"),error);
        const auto bytes=QJsonDocument(audit.report()).toJson();QFile file(args.value("report"));
        if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)||file.write(bytes)!=bytes.size()||!file.flush())return 4;
        const auto evidence=audit.report()["evidence"].toString();return evidence=="complete"?0:evidence=="partial"||evidence=="unsupported_format"||evidence=="not_applicable"?2:3;
    }
    MainWindow window;QString error;
    if(args.isSet("size")) {const auto size=args.value("size").split(',');if(size.size()==2)window.resize(size[0].toInt(),size[1].toInt());}
    if(args.isSet("replay")&&!window.loadHistory(args.value("replay"),error)) {qCritical("%s",qPrintable(error));return 2;}
    if(args.isSet("page"))window.selectPage(args.value("page")=="tasks"?1:args.value("page")=="history"?2:args.value("page")=="maintenance"?3:0);
    window.show();
    if(args.isSet("self-test")) {QTimer::singleShot(800,&app,[&]{app.exit(window.selfTest(args.value("report"),args.value("screenshot"))?0:3);});}
    return app.exec();
}
