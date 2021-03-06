/*
 * createobjecttool.cpp
 * Copyright 2010-2011, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of Tiled.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "createobjecttool.h"

#include "addremovemapobject.h"
#include "addremovetileset.h"
#include "map.h"
#include "mapdocument.h"
#include "mapobject.h"
#include "mapobjectitem.h"
#include "maprenderer.h"
#include "mapscene.h"
#include "objectgroup.h"
#include "objectgroupitem.h"
#include "objectselectiontool.h"
#include "snaphelper.h"
#include "tile.h"
#include "toolmanager.h"
#include "utils.h"

#include <QApplication>
#include <QKeyEvent>
#include <QPalette>

using namespace Tiled;
using namespace Tiled::Internal;

CreateObjectTool::CreateObjectTool(QObject *parent)
    : AbstractObjectTool(QString(),
                         QIcon(),
                         QKeySequence(),
                         parent)
    , mNewMapObjectItem(nullptr)
    , mNewMapObjectGroup(new ObjectGroup)
    , mObjectGroupItem(new ObjectGroupItem(mNewMapObjectGroup.get()))
{
    mObjectGroupItem->setZValue(10000); // same as the BrushItem
}

CreateObjectTool::~CreateObjectTool()
{
}

void CreateObjectTool::activate(MapScene *scene)
{
    AbstractObjectTool::activate(scene);
    scene->addItem(mObjectGroupItem.get());
}

void CreateObjectTool::deactivate(MapScene *scene)
{
    if (mNewMapObjectItem)
        cancelNewMapObject();

    scene->removeItem(mObjectGroupItem.get());
    AbstractObjectTool::deactivate(scene);
}

void CreateObjectTool::keyPressed(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (mNewMapObjectItem) {
            finishNewMapObject();
            return;
        }
        break;
    case Qt::Key_Escape:
        if (mNewMapObjectItem) {
            cancelNewMapObject();
        } else {
            // If we're not currently creating a new object, switch to object selection tool
            toolManager()->selectTool(toolManager()->findTool<ObjectSelectionTool>());
        }
        return;
    }

    AbstractObjectTool::keyPressed(event);
}

void CreateObjectTool::mouseEntered()
{
}

void CreateObjectTool::mouseMoved(const QPointF &pos,
                                  Qt::KeyboardModifiers modifiers)
{
    AbstractObjectTool::mouseMoved(pos, modifiers);

    if (mNewMapObjectItem) {
        QPointF offset = mNewMapObjectItem->mapObject()->objectGroup()->totalOffset();
        mouseMovedWhileCreatingObject(pos - offset, modifiers);
    }
}

/**
 * Default implementation starts a new object on left mouse button, and cancels
 * object creation on right mouse button.
 */
void CreateObjectTool::mousePressed(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        if (mNewMapObjectItem)
            cancelNewMapObject();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        AbstractObjectTool::mousePressed(event);
        return;
    }

    ObjectGroup *objectGroup = currentObjectGroup();
    if (!objectGroup || !objectGroup->isVisible())
        return;

    const MapRenderer *renderer = mapDocument()->renderer();
    const QPointF offsetPos = event->scenePos() - objectGroup->totalOffset();

    QPointF pixelCoords = renderer->screenToPixelCoords(offsetPos);
    SnapHelper(renderer, event->modifiers()).snap(pixelCoords);

    if (startNewMapObject(pixelCoords, objectGroup))
        mouseMovedWhileCreatingObject(offsetPos, event->modifiers());
}

/**
 * Default implementation finishes object placement upon release.
 */
void CreateObjectTool::mouseReleased(QGraphicsSceneMouseEvent *)
{
    if (mNewMapObjectItem)
        finishNewMapObject();
}

bool CreateObjectTool::startNewMapObject(const QPointF &pos,
                                         ObjectGroup *objectGroup)
{
    Q_ASSERT(!mNewMapObjectItem);

    if (!objectGroup->isUnlocked())
        return false;

    MapObject *newMapObject = createNewMapObject();
    if (!newMapObject)
        return false;

    newMapObject->setPosition(pos);

    mNewMapObjectGroup->addObject(newMapObject);

    mNewMapObjectGroup->setColor(objectGroup->color());
    mNewMapObjectGroup->setOffset(objectGroup->totalOffset());

    mObjectGroupItem->setPos(mNewMapObjectGroup->offset());

    mNewMapObjectItem = new MapObjectItem(newMapObject, mapDocument(), mObjectGroupItem.get());

    return true;
}

/**
 * Deletes the new map object item, and returns its map object.
 */
MapObject *CreateObjectTool::clearNewMapObjectItem()
{
    Q_ASSERT(mNewMapObjectItem);

    MapObject *newMapObject = mNewMapObjectItem->mapObject();

    mNewMapObjectGroup->removeObject(newMapObject);

    delete mNewMapObjectItem;
    mNewMapObjectItem = nullptr;

    return newMapObject;
}

void CreateObjectTool::cancelNewMapObject()
{
    MapObject *newMapObject = clearNewMapObjectItem();
    delete newMapObject;
}

void CreateObjectTool::finishNewMapObject()
{
    Q_ASSERT(mNewMapObjectItem);

    ObjectGroup *objectGroup = currentObjectGroup();
    if (!objectGroup) {
        cancelNewMapObject();
        return;
    }

    MapObject *newMapObject = clearNewMapObjectItem();

    auto addObjectCommand = new AddMapObject(mapDocument(),
                                             objectGroup,
                                             newMapObject);

    if (Tileset *tileset = newMapObject->cell().tileset()) {
        SharedTileset sharedTileset = tileset->sharedPointer();

        // Make sure this tileset is part of the map
        if (!mapDocument()->map()->tilesets().contains(sharedTileset))
            new AddTileset(mapDocument(), sharedTileset, addObjectCommand);
    }

    mapDocument()->undoStack()->push(addObjectCommand);

    mapDocument()->setSelectedObjects(QList<MapObject*>() << newMapObject);
}

/**
 * Default implementation simply synchronizes the position of the new object
 * with the mouse position.
 */
void CreateObjectTool::mouseMovedWhileCreatingObject(const QPointF &pos, Qt::KeyboardModifiers modifiers)
{
    const MapRenderer *renderer = mapDocument()->renderer();

    QPointF pixelCoords = renderer->screenToPixelCoords(pos);
    SnapHelper(renderer, modifiers).snap(pixelCoords);

    mNewMapObjectItem->mapObject()->setPosition(pixelCoords);
    mNewMapObjectItem->syncWithMapObject();
}
