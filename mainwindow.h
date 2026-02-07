#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QJsonObject>
#include <QTranslator>

#include "server/serverbackend.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
    class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

signals:
    void startServerRequested();
    void sendCommandRequested(QString cmd);

private:
    void checkThresholds(qintptr id, const QString& type, const QJsonObject& data);

private slots:
    void updateLog(const QString& msg);
    void addClientToTable(qintptr id, QString ip);
    void removeClientFromTable(qintptr id);
    void updateDataTable(qintptr id, QString type, QJsonObject data);

    void onStartBtnClicked();
    void onStopBtnClicked();

    void onLanguageChanged(int index);

private:
    Ui::MainWindow *ui;
    QThread *m_serverThread;
    ServerBackend *m_backend;
    QTranslator m_translator;

};

#endif // MAINWINDOW_H
