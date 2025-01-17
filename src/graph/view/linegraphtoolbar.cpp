/*
 * ModelRailroadTimetablePlanner
 * Copyright 2016-2023, Filippo Gentile
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "linegraphtoolbar.h"

#include "graph/model/linegraphscene.h"

#include "graph/view/linegraphselectionwidget.h"

#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

#include "utils/delegates/sql/customcompletionlineedit.h"

#include "stations/match_models/stationsmatchmodel.h"
#include "stations/match_models/railwaysegmentmatchmodel.h"
#include "stations/match_models/linesmatchmodel.h"

#include "app/session.h"

#include <QEvent>

#include <QHBoxLayout>

LineGraphToolbar::LineGraphToolbar(QWidget *parent) :
    QWidget(parent),
    m_scene(nullptr),
    mZoom(100)
{
    QHBoxLayout *lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    selectionWidget = new LineGraphSelectionWidget;
    connect(selectionWidget, &LineGraphSelectionWidget::graphChanged, this,
            &LineGraphToolbar::onWidgetGraphChanged);
    lay->addWidget(selectionWidget);

    redrawBut = new QPushButton(tr("Redraw"));
    connect(redrawBut, &QPushButton::clicked, this, &LineGraphToolbar::requestRedraw);
    lay->addWidget(redrawBut);

    zoomSlider = new QSlider(Qt::Horizontal);
    zoomSlider->setRange(25, 400);
    zoomSlider->setTickPosition(QSlider::TicksBelow);
    zoomSlider->setTickInterval(50);
    zoomSlider->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    zoomSlider->setValue(mZoom);
    zoomSlider->setToolTip(tr("Double click to reset zoom"));
    connect(zoomSlider, &QSlider::valueChanged, this, &LineGraphToolbar::updateZoomLevel);
    lay->addWidget(zoomSlider);

    zoomSpinBox = new QSpinBox;
    zoomSpinBox->setRange(25, 400);
    zoomSpinBox->setValue(mZoom);
    zoomSpinBox->setSuffix(QChar('%'));
    connect(zoomSpinBox, qOverload<int>(&QSpinBox::valueChanged), this,
            &LineGraphToolbar::updateZoomLevel);
    lay->addWidget(zoomSpinBox);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    // Accept focus events by click
    setFocusPolicy(Qt::ClickFocus);

    // Install event filter to catch focus events on children widgets
    for (QObject *child : selectionWidget->children())
    {
        if (child->isWidgetType())
            child->installEventFilter(this);
    }

    // Install event filter on Zoom Slider to catch double click
    zoomSlider->installEventFilter(this);
}

LineGraphToolbar::~LineGraphToolbar()
{
}

void LineGraphToolbar::setScene(LineGraphScene *scene)
{
    if (m_scene)
    {
        disconnect(m_scene, &LineGraphScene::graphChanged, this,
                   &LineGraphToolbar::onSceneGraphChanged);
        disconnect(m_scene, &QObject::destroyed, this, &LineGraphToolbar::onSceneDestroyed);
    }
    m_scene = scene;
    if (m_scene)
    {
        connect(m_scene, &LineGraphScene::graphChanged, this,
                &LineGraphToolbar::onSceneGraphChanged);
        connect(m_scene, &QObject::destroyed, this, &LineGraphToolbar::onSceneDestroyed);
    }
}

bool LineGraphToolbar::eventFilter(QObject *watched, QEvent *ev)
{
    if (ev->type() == QEvent::FocusIn)
    {
        // If any of our child widgets receives focus, activate our scene
        if (m_scene)
            m_scene->activateScene();
    }

    if (watched == zoomSlider && ev->type() == QEvent::MouseButtonDblClick)
    {
        // Zoom Slider was double clicked, reset zoom level to 100
        updateZoomLevel(100);
    }

    return QWidget::eventFilter(watched, ev);
}

void LineGraphToolbar::resetToolbarToScene()
{
    LineGraphType type = LineGraphType::NoGraph;
    db_id objectId     = 0;
    QString name;

    if (m_scene)
    {
        type     = m_scene->getGraphType();
        objectId = m_scene->getGraphObjectId();
        name     = m_scene->getGraphObjectName();
    }

    selectionWidget->setGraphType(type);
    selectionWidget->setObjectId(objectId, name);
}

void LineGraphToolbar::updateZoomLevel(int zoom)
{
    if (mZoom == zoom)
        return;

    mZoom = zoom;

    zoomSlider->setValue(mZoom);
    zoomSpinBox->setValue(mZoom);

    emit requestZoom(mZoom);
}

void LineGraphToolbar::onWidgetGraphChanged(int type, db_id objectId)
{
    LineGraphType graphType = LineGraphType(type);
    if (graphType == LineGraphType::NoGraph)
        objectId = 0;

    if (graphType != LineGraphType::NoGraph && !objectId)
        return; // User is still selecting an object

    if (m_scene)
        m_scene->loadGraph(objectId, graphType);
}

void LineGraphToolbar::onSceneGraphChanged(int type, db_id objectId)
{
    selectionWidget->setGraphType(LineGraphType(type));

    QString name;
    if (m_scene && m_scene->getGraphObjectId() == objectId)
        name = m_scene->getGraphObjectName();
    selectionWidget->setObjectId(objectId, name);
}

void LineGraphToolbar::onSceneDestroyed()
{
    m_scene = nullptr;
    resetToolbarToScene(); // Clear UI
}

void LineGraphToolbar::focusInEvent(QFocusEvent *e)
{
    if (m_scene)
        m_scene->activateScene();

    QWidget::focusInEvent(e);
}
