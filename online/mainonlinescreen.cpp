/****************************************
 *
 *   INSERT-PROJECT-NAME-HERE - INSERT-GENERIC-NAME-HERE
 *   Copyright (C) 2019 Victor Tran
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * *************************************/
#include "mainonlinescreen.h"
#include "ui_mainonlinescreen.h"

#include <online/onlineapi.h>
#include <online/onlinewebsocket.h>
#include <questionoverlay.h>
#include <discordintegration.h>
#include "onlinecontroller.h"
#include "onlinejoinscreen.h"

struct MainOnlineScreenPrivate {

};

MainOnlineScreen::MainOnlineScreen(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MainOnlineScreen)
{
    ui->setupUi(this);
    d = new MainOnlineScreenPrivate();

    connect(ui->menuPage, &OnlineMenuScreen::quitOnline, this, [=] {
        OnlineController::instance()->close();
    });

    connect(OnlineController::instance(), &OnlineController::disconnected, this, [=](int closeCode) {
        QString error;
        switch (closeCode) {
            case QWebSocketProtocol::CloseCodeNormal:
                break;
            case QWebSocketProtocol::CloseCodeGoingAway:
                error = tr("You've been disconnected because the server is about to undergo maintenance.");
                break;
            case QWebSocketProtocol::CloseCodeProtocolError:
                error = tr("You'be been disconnected from the server because there was a communication error.");
                break;
            case QWebSocketProtocol::CloseCodeDatatypeNotSupported:
            case QWebSocketProtocol::CloseCodeReserved1004:
            case QWebSocketProtocol::CloseCodeMissingStatusCode:
            case QWebSocketProtocol::CloseCodeWrongDatatype:
            case QWebSocketProtocol::CloseCodePolicyViolated:
            case QWebSocketProtocol::CloseCodeTooMuchData:
            case QWebSocketProtocol::CloseCodeAbnormalDisconnection:
            default:
                error = tr("You'be been disconnected from the server.");
        }

        if (error.isEmpty()) {
            emit quitOnline();
        } else {
            QuestionOverlay* question = new QuestionOverlay(this);
            question->setIcon(QMessageBox::Critical);
            question->setTitle(tr("Error"));
            question->setText(error);
            question->setButtons(QMessageBox::Ok);

            auto handler = [=] {
                question->deleteLater();
                emit quitOnline();
            };

            connect(question, &QuestionOverlay::accepted, this, handler);
            connect(question, &QuestionOverlay::rejected, this, handler);
        }
    });

    connect(OnlineController::instance(), &OnlineController::jsonMessage, this, [=](QJsonDocument doc) {
        QJsonObject object = doc.object();
        QString type = object.value("type").toString();
        if (type == "stateChange") {
            QString state = object.value("newState").toString();
            if (state == "idle") {
                ui->stackedWidget->setCurrentWidget(ui->menuPage);

                DiscordIntegration::instance()->setPresence({
                    {"state", tr("Idle")},
                    {"details", tr("Main Menu")}
                });
            } else if (state == "lobby") {
                ui->stackedWidget->setCurrentWidget(ui->lobbyPage);
            }
        } else if (type == "availableRoomsReply") {
            OnlineJoinScreen* joinScreen = new OnlineJoinScreen(object, this);
        }
    });
}

MainOnlineScreen::~MainOnlineScreen()
{
    delete d;
    delete ui;
}

void MainOnlineScreen::connectToOnline()
{
    ui->stackedWidget->setCurrentWidget(ui->connectingPage);
    OnlineApi::instance()->play("EntertainingMines", "1.0", this)->then([=](OnlineWebSocket* ws) {
        ui->stackedWidget->setCurrentWidget(ui->menuPage);
        ui->menuPage->setFocus();

        OnlineController::instance()->attachToWebSocket(ws);
    })->error([=](QString error) {
        QuestionOverlay* question = new QuestionOverlay(this);
        question->setIcon(QMessageBox::Critical);
        question->setTitle(tr("Error"));
        question->setText(OnlineApi::errorFromPromiseRejection(error));
        question->setButtons(QMessageBox::Ok);

        auto handler = [=] {
            question->deleteLater();
            emit quitOnline();
        };

        connect(question, &QuestionOverlay::accepted, this, handler);
        connect(question, &QuestionOverlay::rejected, this, handler);
    });
}
