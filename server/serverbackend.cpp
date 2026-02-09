#include "serverbackend.h"
#include <QThread>
#include <QDateTime>
#include <QtConcurrent>
#include <QTimer>

ServerBackend::ServerBackend(QObject *parent)
    : QObject(parent)
    , m_server(nullptr)
    , m_cleanupTimer(new QTimer(this))
{
    connect(m_cleanupTimer, &QTimer::timeout, this, &ServerBackend::onCleanupTimerTick);
}

ServerBackend::~ServerBackend()
{
    stopServer();
}

void ServerBackend::startServer()
{
    if (m_server)
    {
      return;
    }

    m_server = new QTcpServer(this);

    connect(m_server, &QTcpServer::newConnection, this, &ServerBackend::onNewConnection);

    if (m_server->listen(QHostAddress::Any, SERVER_PORT))
    {
        emit logMessage(tr("Server running on port: ") + QString::number(SERVER_PORT));
        m_cleanupTimer->start(5000);
    }
    else
    {
        emit logMessage(tr("Startup error: ") + m_server->errorString());
    }
}

void ServerBackend::stopServer()
{
    m_cleanupTimer->stop();

    if (m_server)
    {
        // Safe cleanup
        auto keys = m_clients.keys();
        for(auto id : keys)
        {
            disconnectClient(id, "Server shutdown");
        }

        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}

void ServerBackend::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections())
    {
        QTcpSocket* socket = m_server->nextPendingConnection();

        socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

        qintptr id = socket->socketDescriptor();

        QHostAddress clientAddress = socket->peerAddress();

        QString ipString = (clientAddress.protocol() == QAbstractSocket::IPv6Protocol &&
                            QHostAddress(clientAddress.toIPv4Address()).protocol() == QAbstractSocket::IPv4Protocol)
                            ? QHostAddress(clientAddress.toIPv4Address()).toString()
                            : clientAddress.toString();

        ClientContext *ctx = new ClientContext();
        ctx->ip = ipString;
        ctx->port = socket->peerPort();
        ctx->socket = socket;
        ctx->lastActiveTime = QDateTime::currentMSecsSinceEpoch();
        ctx->lastFloodCheckTime = ctx->lastActiveTime;

        m_clients.insert(id, ctx);

        connect(socket, &QTcpSocket::readyRead, this, &ServerBackend::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ServerBackend::onClientDisconnected);

        emit clientConnected(id, QString("%1:%2").arg(ctx->ip).arg(ctx->port));
        emit logMessage(tr("New client: ") + QString("%1:%2").arg(ctx->ip).arg(ctx->port));

        QJsonObject response;
        response[KEY_TYPE] = PacketType::HANDSHAKE;
        response["status"] = "Connected";
        socket->write(packJson(response));
    }
}

void ServerBackend::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());

    if (!socket)
    {
       return;
    }

    qintptr id = socket->socketDescriptor();

    if (!m_clients.contains(id))
    {
       return;
    }

    ClientContext *ctx = m_clients[id];

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    ctx->lastActiveTime = now;

    ctx->buffer.append(socket->readAll());

    while (true)
    {
        // We need at least 4 bytes for the size header
        if (ctx->buffer.size() < (qsizetype)sizeof(quint32))
        {
            break;
        }

        QDataStream in(ctx->buffer);
        in.setVersion(QDataStream::Qt_6_5);

        quint32 blockSize;
        in >> blockSize;

        if (blockSize > MAX_PACKET_SIZE)
        {
            disconnectClient(id, "Packet size exceeded limit");
            return;
        }

        // Wait until the full packet is in the buffer
        if (ctx->buffer.size() < (qsizetype)(sizeof(quint32) + blockSize))
        {
            break;
        }

        // Remove header from stream/buffer
        ctx->buffer.remove(0, sizeof(quint32));

        // Extract data
        QByteArray data = ctx->buffer.left(blockSize);
        ctx->buffer.remove(0, blockSize);
        // FIX: JSON Error handling

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

        if (parseError.error != QJsonParseError::NoError)
        {
            emit logMessage(tr("JSON Parse Error from %1: %2").arg(ctx->ip, parseError.errorString()));
            continue; // Skip bad packet, but keep connection
        }

        if (doc.isObject())
        {
            if (now - ctx->lastFloodCheckTime > 1000)
            {
                ctx->lastFloodCheckTime = now;
                ctx->requestsInCurrentSecond = 0;
            }

            ctx->requestsInCurrentSecond++;
            if (ctx->requestsInCurrentSecond > MAX_REQUESTS_PER_SECOND)
            {
                disconnectClient(id, "Flood detected");
                return;
            }

            QJsonObject obj = doc.object();
            QtConcurrent::run([this, id, obj](){this->processJson(id, obj);});
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

    if (m_clients.contains(id))
    {
        emit clientDisconnected(id);
        emit logMessage(tr("Client disconnected: ") + m_clients[id]->ip);

        delete m_clients[id];
        m_clients.remove(id);
    }

    socket->deleteLater();
}

void ServerBackend::disconnectClient(qintptr id, const QString& reason)
{
    if (m_clients.contains(id))
    {
        QTcpSocket* socket = m_clients[id]->socket;
        emit logMessage(tr("Forcing disconnect client %1. Reason: %2").arg(id).arg(reason));
        socket->disconnectFromHost();
        m_clients.remove(id);
    }
}

void ServerBackend::onCleanupTimerTick()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto keys = m_clients.keys();

    for (auto id : keys)
    {
        ClientContext* ctx = m_clients[id];

        // Timeout check
        if (now - ctx->lastActiveTime > CLIENT_TIMEOUT_MS)
        {
            disconnectClient(id, "Timeout (No data received)");
            continue;
        }

        // Backpressure check (Write buffer overflow)
        if (ctx->socket->bytesToWrite() > MAX_PENDING_WRITE_BYTES)
        {
            disconnectClient(id, "Write buffer overflow (Slow consumer)");
        }
    }
}

void ServerBackend::sendCommandToAll(const QString& commandType)
{
    QJsonObject cmd;
    cmd[KEY_TYPE] = commandType;
    QByteArray packet = packJson(cmd);

    for(auto ctx : m_clients)
    {
        // Simple backpressure check before adding more data
        if (ctx->socket->state() == QAbstractSocket::ConnectedState &&
            ctx->socket->bytesToWrite() <= MAX_PENDING_WRITE_BYTES)
        {
            ctx->socket->write(packet);
        }
    }

    emit logMessage(tr("Command broadcast: ") + commandType);
}
