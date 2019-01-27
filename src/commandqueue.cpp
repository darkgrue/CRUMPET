/*
 *   Copyright 2019 Dan Leinir Turthra Jensen <admin@leinir.dk>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 3, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public License
 *   along with this program; if not, see <https://www.gnu.org/licenses/>
 */

#include "commandqueue.h"
#include "btconnectionmanager.h"

#include <QTimer>

class CommandQueue::Private
{
public:
    Private(CommandQueue* qq, BTConnectionManager* connectionManager)
        : q(qq)
        , connectionManager(connectionManager)
    {}
    ~Private() {}

    CommandQueue* q;

    // These commands are owned by TailCommandModel, do not delete them
    TailCommandModel::CommandInfoList commands;
    TailCommandModel::CommandInfoList ourCommands;
    BTConnectionManager* connectionManager;

    QTimer* popTimer;

    void pop()
    {
        // only pop if there's commands left
        if(commands.count() > 0) {
//             q->beginRemoveRows(QModelIndex(), 0, 0);
            TailCommandModel::CommandInfo* command = commands.takeFirst();
//             q->endRemoveRows();

            // Command can be empty if it's a pause (possibly others as well,
            // though not yet, but just never send an empty command)
            if(!command->command.isEmpty()) {
                connectionManager->sendMessage(command->command);
            }

            popTimer->start(command->duration + command->minimumCooldown);

            // ourCommands holds the things we create in the queue itself,
            // which is mostly pauses. Those we will need to manage the
            // memory of ourselves, and so when we take those, get rid of
            // them entirely.
            if(ourCommands.contains(command)) {
                ourCommands.removeAll(command);
                delete command;
            }

            emit q->countChanged();
        }
    }
};

CommandQueue::CommandQueue(BTConnectionManager* connectionManager)
    : CommandQueueProxySource(connectionManager)
    , d(new Private(this, connectionManager))
{
    d->popTimer = new QTimer(this);
    d->popTimer->setSingleShot(true);
    connect(d->popTimer, &QTimer::timeout, this, [this](){ d->pop(); });
}

CommandQueue::~CommandQueue()
{
    delete d;
}

QHash<int, QByteArray> CommandQueue::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[Name] = "name";
    roles[Command] = "command";
    roles[IsRunning] = "isRunning";
    roles[Category] = "category";
    roles[Duration] = "duration";
    roles[MinimumCooldown] = "minimumCooldown";
    return roles;
}

QVariant CommandQueue::data(const QModelIndex& index, int role) const
{
    QVariant value;
    if(index.isValid() && index.row() > -1 && index.row() < d->commands.count()) {
        TailCommandModel::CommandInfo* command = d->commands.at(index.row());
        switch(role) {
            case Name:
                value = command->name;
                break;
            case Command:
                value = command->command;
                break;
            case IsRunning:
                value = command->isRunning;
                break;
            case Category:
                value = command->category;
                break;
            case Duration:
                value = command->duration;
                break;
            case MinimumCooldown:
                value = command->minimumCooldown;
                break;
            default:
                break;
        }
    };
    return value;
}

int CommandQueue::rowCount(const QModelIndex& parent) const
{
    if(parent.isValid()) {
        return 0;
    }
    return d->commands.count();
}

int CommandQueue::count() const
{
    return d->commands.count();
}

void CommandQueue::clear()
{
    // Before doing anything else, ensure the timer doesn't suddenly pick stuff
    // out from underneath us. Stop all functio and let's do the thing.
    d->popTimer->stop();
//     beginResetModel();
    d->commands.clear();
//     endResetModel();
    emit countChanged();
}

void CommandQueue::pushPause(int durationMilliseconds)
{
    TailCommandModel::CommandInfo* command = new TailCommandModel::CommandInfo;
    command->name = "Pause";
    command->duration = durationMilliseconds;
    d->ourCommands.append(command);

    d->commands.append(command);
    emit countChanged();

    // If we have just pushed a command and the timer is not currently running,
    // let's fire one off now!
    if(!d->popTimer->isActive()) {
        d->pop();
    }
}

void CommandQueue::pushCommand(int tailCommandModelIndex)
{
//     beginInsertRows(QModelIndex(), d->commands.count(), d->commands.count());
    TailCommandModel::CommandInfo* command = qobject_cast<TailCommandModel*>(d->connectionManager->commandModel())->getCommand(tailCommandModelIndex);
    qDebug() << "Command to push" << command;
    d->commands.append(command);
//     endInsertRows();
    emit countChanged();

    // If we have just pushed a command and the timer is not currently running,
    // let's fire one off now!
    if(!d->popTimer->isActive()) {
        d->pop();
    }
}

void CommandQueue::pushCommands(TailCommandModel::CommandInfoList commands)
{
    if(commands.count() > 0) {
//         beginInsertRows(QModelIndex(), d->commands.count(), d->commands.count() + commands.count() - 1);
        d->commands.append(commands);
//         endInsertRows();
        emit countChanged();

        // If we have just pushed some commands and the timer is not currently
        // running, let's fire one off now!
        if(!d->popTimer->isActive()) {
            d->pop();
        }
    }
}

void CommandQueue::removeEntry(int index)
{
//     beginRemoveRows(QModelIndex(), index, index);
    d->commands.removeAt(index);
//     endRemoveRows();
    emit countChanged();
}

void CommandQueue::swapEntries(int swapThis, int withThis)
{
    if(swapThis >= 0 && swapThis < d->commands.count() && withThis >= 0 && withThis < d->commands.count()) {
//         QModelIndex idx1 = createIndex(swapThis, 0);
//         QModelIndex idx2 = createIndex(withThis, 0);
        TailCommandModel::CommandInfo* with = d->commands.takeAt(withThis);
        TailCommandModel::CommandInfo* swap = d->commands.takeAt(swapThis);
        d->commands.insert(swapThis, with);
        d->commands.insert(withThis, swap);
//         dataChanged(idx1, idx1);
//         dataChanged(idx2, idx2);
    }
}

void CommandQueue::moveEntryUp(int index)
{
    swapEntries(index, index + 1);
}

void CommandQueue::moveEntryDown(int index)
{
    swapEntries(index - 1, index);
}
