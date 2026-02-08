#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QApplication>
#include <QTranslator>

#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , settings(qApp->applicationDirPath() + "/config.ini", QSettings::IniFormat)
{
    // Initializing the interface from a .ui file
    ui->setupUi(this);

    ui->m_clientTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->m_dataTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    // Connecting the combo box to the slot
    connect(ui->m_langCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onLanguageChanged);

    // Setting the initial language
    ui->m_langCombo->setCurrentIndex(0);

    m_serverThread = new QThread(this);
    m_backend = new ServerBackend();
    m_backend->moveToThread(m_serverThread);

    // Backend to UI connections
    connect(m_serverThread, &QThread::started, m_backend, &ServerBackend::startServer);
    connect(m_serverThread, &QThread::finished, m_backend, &QObject::deleteLater);

    connect(m_backend, &ServerBackend::logMessage, this, &MainWindow::updateLog);
    connect(m_backend, &ServerBackend::clientConnected, this, &MainWindow::addClientToTable);
    connect(m_backend, &ServerBackend::clientDisconnected, this, &MainWindow::removeClientFromTable);
    connect(m_backend, &ServerBackend::dataReceived, this, &MainWindow::updateDataTable);

    connect(this, &MainWindow::sendCommandRequested, m_backend, &ServerBackend::sendCommandToAll);

    // Handling button events
    connect(ui->m_btnStartClients, &QPushButton::clicked, this, &MainWindow::onStartBtnClicked);
    connect(ui->m_btnStopClients, &QPushButton::clicked, this, &MainWindow::onStopBtnClicked);

    m_serverThread->start();
}

MainWindow::~MainWindow()
{
    m_serverThread->quit();
    m_serverThread->wait();

    delete ui;
}

void MainWindow::onLanguageChanged(int index)
{
    qApp->removeTranslator(&m_translator);

    bool loaded = false;

    if (index == 0)
    {
        ui->retranslateUi(this);
    }

    if (index == 1)
    {
        loaded = m_translator.load(":/i18n/Qt_GuiServerApplication_ru_RU.qm");
    }

    if (loaded)
    {
        qApp->installTranslator(&m_translator);
        ui->retranslateUi(this);
    }

    updateLog(index == 1 ? "Язык изменен на Русский" : "Language changed to English");
}

void MainWindow::updateLog(const QString& msg)
{
    QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
    ui->m_logEdit->append(QString("[%1] %2").arg(time, msg));
}

void MainWindow::addClientToTable(qintptr id, QString ip)
{
    int row = ui->m_clientTable->rowCount();

    ui->m_clientTable->insertRow(row);
    ui->m_clientTable->setItem(row, 0, new QTableWidgetItem(QString::number(id)));
    ui->m_clientTable->setItem(row, 1, new QTableWidgetItem(ip));
    ui->m_clientTable->setItem(row, 2, new QTableWidgetItem("Connected"));

    ui->m_clientTable->item(row, 2)->setForeground(QColor(0x00ff00));
}

void MainWindow::removeClientFromTable(qintptr id)
{
    for(int i = 0; i < ui->m_clientTable->rowCount(); ++i)
    {
        if(ui->m_clientTable->item(i, 0)->text() == QString::number(id))
        {
            ui->m_clientTable->removeRow(i);
            break;
        }
    }
}

void MainWindow::updateDataTable(qintptr id, QString type, QJsonObject data)
{
    int row = ui->m_dataTable->rowCount();
    ui->m_dataTable->insertRow(row);

    // Content parsing
    QString contentStr;

    if (type == PacketType::NETWORK_METRICS)
    {
        contentStr = QString("BW: %1, Lat: %2, Loss: %3")
            .arg(data["bandwidth"].toDouble())
            .arg(data["latency"].toDouble())
            .arg(data["packet_loss"].toDouble());
    }
    else if (type == PacketType::DEVICE_STATUS)
    {
        contentStr = QString("CPU: %1%, Mem: %2%, Up: %3s")
            .arg(data["cpu_usage"].toInt())
            .arg(data["memory_usage"].toInt())
            .arg(data["uptime"].toInt());
    }
    else if (type == PacketType::LOG)
    {
        contentStr = QString("%1: %2")
            .arg(data["severity"].toString())
            .arg(data["message"].toString());
    }

    ui->m_dataTable->setItem(row, 0, new QTableWidgetItem(QString::number(id)));
    ui->m_dataTable->setItem(row, 1, new QTableWidgetItem(type));
    ui->m_dataTable->setItem(row, 2, new QTableWidgetItem(contentStr));
    ui->m_dataTable->setItem(row, 3, new QTableWidgetItem(QDateTime::currentDateTime().toString("HH:mm:ss.zzz")));

    ui->m_dataTable->scrollToBottom();

    // Checking thresholds
    checkThresholds(id, type, data);
}

void MainWindow::checkThresholds(qintptr id, const QString& type, const QJsonObject& data)
{
    // Read the threshold from the config
    // Group "threshold"
    // "80" is the default value if nothing is found in the config
    int threshold = settings.value("thresholds/cpu_overloaded", 80).toInt();

    // Configuration example: if CPU > threshold, write WARNING in the log
    if (type == PacketType::DEVICE_STATUS)
    {
        if (data["cpu_usage"].toInt() > threshold)
        {
            updateLog(QString(tr("WARNING: Client %1 is overloaded! CPU: %2%")).arg(id).arg(data["cpu_usage"].toInt()));
        }
    }
}

void MainWindow::onStartBtnClicked()
{
    emit sendCommandRequested(PacketType::COMMAND_START);
    updateLog(tr("Command: The launch has been sent to all clients"));
}

void MainWindow::onStopBtnClicked()
{
    emit sendCommandRequested(PacketType::COMMAND_STOP);
    updateLog(tr("Command: Stop sent to all clients"));
}
