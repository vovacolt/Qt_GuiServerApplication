#include "serverbackend.h"
#include <QThread>

ServerBackend::ServerBackend(QObject *parent) : QObject(parent), m_server(nullptr)
{
}

ServerBackend::~ServerBackend()
{
    stopServer();
}

void ServerBackend::startServer()
{
    m_server = new QTcpServer(this);

    connect(m_server, &QTcpServer::newConnection, this, &ServerBackend::onNewConnection);

    if (m_server->listen(QHostAddress::Any, SERVER_PORT))
    {
        emit logMessage(tr("The server is running on port: ") + QString::number(SERVER_PORT));
    }
    else
    {
        emit logMessage(tr("Server startup error: ") + m_server->errorString());
    }
}

void ServerBackend::stopServer()
{
    if (m_server)
    {
        for(auto ctx : m_clients)
        {
            ctx->socket->close();
            delete ctx;
        }

        m_clients.clear();
        m_server->close();
        delete m_server;

        m_server = nullptr;
    }
}

void ServerBackend::onNewConnection()
{
    QTcpSocket* socket = m_server->nextPendingConnection();
    qintptr id = socket->socketDescriptor();

    ClientContext *ctx = new ClientContext
    {
        socket->peerAddress().toString(),
        socket->peerPort(),
        socket
    };

    m_clients.insert(id, ctx);

    connect(socket, &QTcpSocket::readyRead, this, &ServerBackend::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &ServerBackend::onClientDisconnected);

    emit clientConnected(id, ctx->ip);
    emit logMessage(tr("New client: ") + ctx->ip);

    QJsonObject response;
    response[KEY_TYPE] = PacketType::HANDSHAKE;
    response["status"] = "Connected";
    socket->write(packJson(response));
}

void ServerBackend::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

    if (!socket)
    {
       return;
    }

    QDataStream in(socket);
    in.setVersion(QDataStream::Qt_6_5);

    while (true)
    {
        if (socket->bytesAvailable() < sizeof(quint32))
        {
            break;
        }

        // Read the packet size, but don't move the position until we're sure the data arrived
        QByteArray header = socket->peek(sizeof(quint32));
        QDataStream headerStream(header);
        headerStream.setVersion(QDataStream::Qt_6_5);
        quint32 blockSize;
        headerStream >> blockSize;

        // Waiting for more data
        if (socket->bytesAvailable() < (quint32)sizeof(quint32) + blockSize)
        {
            break;
        }
        // Data is ready

        // Skipping the size header
        quint32 dummy;
        in >> dummy;

        QByteArray data;
        data.resize(blockSize);
        in.readRawData(data.data(), blockSize);

        QJsonDocument doc = QJsonDocument::fromJson(data.data());

        if (doc.isObject())
        {
            processJson(socket->socketDescriptor(), doc.object());
        }
    }
}

void ServerBackend::processJson(qintptr id, const QJsonObject& json)
{
    QString type = json[KEY_TYPE].toString();
    emit dataReceived(id, type, json);
}

void ServerBackend::onClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

    if (!socket)
    {
        return;
    }

    qintptr id = socket->socketDescriptor();
    qintptr foundId = -1;

    for(auto it = m_clients.begin(); it != m_clients.end(); ++it)
    {
        if(it.value()->socket == socket)
        {
            foundId = it.key();
            break;
        }
    }

    if (foundId != -1)
    {
        emit clientDisconnected(foundId);
        emit logMessage(tr("Client disconnected: ") + m_clients[foundId]->ip);
        delete m_clients[foundId];
        m_clients.remove(foundId);
    }

    socket->deleteLater();
}

void ServerBackend::sendCommandToAll(const QString& commandType)
{
    QJsonObject cmd;
    cmd[KEY_TYPE] = commandType;
    QByteArray packet = packJson(cmd);

    for(auto ctx : m_clients)
    {
        ctx->socket->write(packet);
    }

    emit logMessage(tr("Command sent to everyone: ") + commandType);
}
