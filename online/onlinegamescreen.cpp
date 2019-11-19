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
#include "onlinegamescreen.h"
#include "ui_onlinegamescreen.h"

#include <the-libs_global.h>
#include <QIcon>
#include <QRandomGenerator>
#include "game/gametile.h"
#include "game/gameover.h"
#include "game/congratulation.h"
#include <pauseoverlay.h>
//#include "pausescreen.h"
#include <musicengine.h>
#include <QUrl>
#include <QShortcut>
#include <tpromise.h>
#include <focusbarrier.h>
#include <discordintegration.h>
#include "onlinecontroller.h"

struct OnlineGameScreenPrivate {
    QVector<GameTile*> tiles;

    int width = 0;
    int mines = 0;

    int remainingTileCount = 0;
    int minesRemaining = 0;

    QDateTime startDateTime;
    QTimer* dateTimeTimer;

    QPushButton* focusPreventer;
};

OnlineGameScreen::OnlineGameScreen(QWidget *parent) :
    AbstractGameScreen(parent),
    ui(new Ui::OnlineGameScreen)
{
    ui->setupUi(this);
    d = new OnlineGameScreenPrivate();

    ui->gamepadHud->setButtonText(QGamepadManager::ButtonA, tr("Reveal"));
    ui->gamepadHud->setButtonText(QGamepadManager::ButtonX, tr("Flag"));

    ui->gamepadHud->setButtonAction(QGamepadManager::ButtonA, [=] {
        GameTile* currentTile = this->currentTile();
        if (currentTile != nullptr) currentTile->revealOrSweep();
    });
    ui->gamepadHud->setButtonAction(QGamepadManager::ButtonX, [=] {
        GameTile* currentTile = this->currentTile();
        if (currentTile != nullptr) currentTile->toggleFlagStatus();
    });

    d->dateTimeTimer = new QTimer();
    d->dateTimeTimer->setInterval(1000);
    connect(d->dateTimeTimer, &QTimer::timeout, this, &OnlineGameScreen::updateTimer);
    d->dateTimeTimer->start();

    QBoxLayout* layout = new QBoxLayout(QBoxLayout::TopToBottom, this);
    FocusBarrier* bar1 = new FocusBarrier(this);
    FocusBarrier* bar2 = new FocusBarrier(this);
    d->focusPreventer = new QPushButton();
    bar1->setBounceWidget(d->focusPreventer);
    bar2->setBounceWidget(d->focusPreventer);
    layout->addWidget(bar1);
    layout->addWidget(d->focusPreventer);
    layout->addWidget(bar2);
    layout->setGeometry(QRect(-50, -50, 5, 5));
    layout->setParent(this);

    connect(OnlineController::instance(), &OnlineController::jsonMessage, this, [=](QJsonDocument doc) {
        QJsonObject obj = doc.object();
        QString type = obj.value("type").toString();
        if (type == "boardSetup") {
            startGame(obj.value("width").toInt(), obj.value("height").toInt(), obj.value("mines").toInt());
        } else if (type == "tileUpdate") {
            if (d->tiles.count() > obj.value("tile").toInt()) {
                GameTile* t = d->tiles.at(obj.value("tile").toInt());
                t->setRemoteParameters(obj);
            }
        }
    });
}

OnlineGameScreen::~OnlineGameScreen()
{
    delete ui;
    delete d;
}

bool OnlineGameScreen::hasGameStarted()
{
    return true;
}

bool OnlineGameScreen::isGameOver()
{
    return false;
}

QSize OnlineGameScreen::gameArea()
{
    return ui->gameArea->size();
}

GameTile*OnlineGameScreen::tileAt(QPoint location)
{
    return d->tiles.at(pointToIndex(location));
}

GameTile*OnlineGameScreen::currentTile()
{
    for (GameTile* tile : d->tiles) {
        if (tile->hasFocus()) return tile;
    }
    return nullptr;
}

QSize OnlineGameScreen::boardDimensions()
{
    return QSize(d->width, d->tiles.count() / d->width);
}

void OnlineGameScreen::revealedTile()
{
    //noop
}

void OnlineGameScreen::flagChanged(bool didFlag)
{
    if (didFlag) {
        d->minesRemaining--;
    } else {
        d->minesRemaining++;
    }

    ui->minesRemainingLabel->setText(QString::number(d->minesRemaining));
}

QPoint OnlineGameScreen::indexToPoint(int index)
{
    return QPoint(index % d->width, index / d->width);
}

int OnlineGameScreen::pointToIndex(QPoint point)
{
    return point.y() * d->width + point.x();
}

void OnlineGameScreen::resizeEvent(QResizeEvent*event)
{
    resizeTiles();
}

void OnlineGameScreen::resizeTiles()
{
    int targetHeight = qMax(SC_DPI(50), static_cast<int>(this->height() * 0.05));
    int fontHeight = targetHeight - 18;

    QFont fnt = ui->hudWidget->font();
    fnt.setPixelSize(fontHeight);
    ui->hudWidget->setFont(fnt);

    QSize iconSize(fontHeight, fontHeight);
    ui->mineIcon->setPixmap(QIcon(":/tiles/mine.svg").pixmap(iconSize));
    ui->timeIcon->setPixmap(QIcon(":/tiles/clock.svg").pixmap(iconSize));

    emit boardResized();
}

void OnlineGameScreen::setup()
{
    //Clear out the tiles
    for (GameTile* tile : d->tiles) {
        ui->gameGrid->removeWidget(tile);
        tile->deleteLater();
    }
    d->tiles.clear();

}

void OnlineGameScreen::finishSetup()
{
    ui->minesRemainingLabel->setText(QString::number(d->minesRemaining));

    d->tiles.first()->setFocus();
    this->setFocusProxy(d->tiles.first());

    DiscordIntegration::instance()->setPresence({
        {"state", tr("Online Game")},
        {"details", tr("Cooperative: %1×%2 board with %n mines", nullptr, d->mines).arg(d->width).arg(boardDimensions().height())},
        {"startTimestamp", QDateTime::currentDateTimeUtc()}
    });

    updateTimer();
    resizeTiles();

    MusicEngine::setBackgroundMusic("crypto");
    MusicEngine::playBackgroundMusic();
}

void OnlineGameScreen::startGame(int width, int height, int mines)
{
    d->width = width;
    d->mines = mines;

    //Ensure that the number of mines is valid for this game
    if (mines > width * height - 1) mines = width * height - 1;

    setup();

    //Create new tiles
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            GameTile* tile = new GameTile(this, x, y);
            tile->setIsRemote(true);
            ui->gameGrid->addWidget(tile, y, x);
            d->tiles.append(tile);
            connect(tile, &GameTile::currentTileChanged, this, &OnlineGameScreen::currentTileChanged);

            connect(tile, &GameTile::revealTile, this, [=] {
                OnlineController::instance()->sendJsonO({
                    {"type", "boardAction"},
                    {"action", "reveal"},
                    {"tile", width * y + x}
                });
            });
            connect(tile, &GameTile::flagTile, this, [=] {
                OnlineController::instance()->sendJsonO({
                    {"type", "boardAction"},
                    {"action", "flag"},
                    {"tile", width * y + x}
                });
            });
            connect(tile, &GameTile::sweepTile, this, [=] {
                OnlineController::instance()->sendJsonO({
                    {"type", "boardAction"},
                    {"action", "sweep"},
                    {"tile", width * y + x}
                });
            });
        }
    }

    d->startDateTime = QDateTime::currentDateTimeUtc();

    d->remainingTileCount = width * height - mines;
    d->minesRemaining = mines + 1;
    flagChanged(true);

    MusicEngine::playSoundEffect(MusicEngine::Selection);

    finishSetup();
}

void OnlineGameScreen::currentTileChanged()
{
    GameTile* tile = this->currentTile();
    if (tile != nullptr) {
        //Update button text accordingly
        QString buttonA = "";
        QString buttonX = "";

        switch (tile->state()) {
            case GameTile::Idle:
                buttonA = tr("Reveal");
                buttonX = tr("Flag");
                break;
            case GameTile::Flagged:
                buttonX = tr("Mark");
                break;
            case GameTile::Marked:
                buttonX = tr("Unflag");
                break;
            case GameTile::Revealed:
                buttonA = tr("Sweep");
        }

        if (buttonA == "") {
            ui->gamepadHud->removeText(QGamepadManager::ButtonA);
        } else {
            ui->gamepadHud->setButtonText(QGamepadManager::ButtonA, buttonA);
        }

        if (buttonX == "") {
            ui->gamepadHud->removeText(QGamepadManager::ButtonX);
        } else {
            ui->gamepadHud->setButtonText(QGamepadManager::ButtonX, buttonX);
        }
    }
}

void OnlineGameScreen::updateTimer()
{
//    if (d->showDateTime) {
        QString seconds = QString::number(d->startDateTime.secsTo(QDateTime::currentDateTimeUtc()));
        QString display = seconds.rightJustified(3, QLatin1Char('0'), true);
        ui->timeLabel->setText(display);
//    } else {
//        ui->timeLabel->setText("XXX");
//    }
}

void OnlineGameScreen::distributeMines(QPoint clickLocation)
{
    //noop
}

void OnlineGameScreen::performGameOver()
{
    //noop
}
