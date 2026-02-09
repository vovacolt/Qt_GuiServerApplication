#ifndef SERVERBACKEND_H
#define SERVERBACKEND_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include <QMutex>
#include "../common/networkhelpers.h"

// Structure for storing client context
struct ClientContext
{
    QString ip;
    int port;
    QTcpSocket* socket;

    // Buffering for fragmentation handling
    QByteArray buffer;

    // Timeout & Flood control
    qint64 lastActiveTime;
    qint64 lastFloodCheckTime;
    int requestsInCurrentSecond;

    ClientContext()
        : socket(nullptr)
        , lastActiveTime(0)
        , lastFloodCheckTime(0)
        , requestsInCurrentSecond(0) {}
};

class ServerBackend : public QObject
{
    Q_OBJECT

public:
    explicit ServerBackend(QObject *parent = nullptr);
    ~ServerBackend();

public slots:
    void startServer();
    void stopServer();
    void sendCommandToAll(const QString& command);

signals:
    void logMessage(const QString& msg);
    void clientConnected(qintptr socketDescriptor, QString ip);
    void clientDisconnected(qintptr socketDescriptor);
    void dataReceived(qintptr socketDescriptor, QString type, QJsonObject data);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();
    void onCleanupTimerTick();

private:
    void processJson(qintptr id, const QJsonObject& doc);
    void disconnectClient(qintptr id, const QString& reason);

private:
    QTcpServer* m_server;
    QMap<qintptr, ClientContext*> m_clients;
    QTimer* m_cleanupTimer;
    QMutex m_mutex;

};

#endif // SERVERBACKEND_H
