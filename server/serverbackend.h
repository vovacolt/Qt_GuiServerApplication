#ifndef SERVERBACKEND_H
#define SERVERBACKEND_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QMap>
#include "../common/networkhelpers.h"

// Structure for storing client context
struct ClientContext
{
    QString ip;
    int port;
    QTcpSocket* socket;
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

private:
    void processJson(qintptr id, const QJsonObject& doc);

private:
    QTcpServer *m_server;
    QMap<qintptr, ClientContext*> m_clients;
    QMap<QTcpSocket*, QByteArray> m_buffers;

};

#endif // SERVERBACKEND_H
