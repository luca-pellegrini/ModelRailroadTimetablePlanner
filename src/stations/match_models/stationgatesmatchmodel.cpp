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

#include "stationgatesmatchmodel.h"

#include "stations/station_name_utils.h"

#include <QBrush>

using namespace sqlite3pp;

StationGatesMatchModel::StationGatesMatchModel(sqlite3pp::database &db, QObject *parent) :
    ISqlFKMatchModel(parent),
    mDb(db),
    q_getMatches(mDb),
    m_stationId(0),
    m_excludeSegmentId(0),
    m_markConnectedGates(false),
    m_showOnlySegments(false)
{
}

QVariant StationGatesMatchModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= size)
        return QVariant();

    const bool emptyRow =
      hasEmptyRow && (idx.row() == ItemCount || (size < ItemCount + 2 && idx.row() == size - 1));
    const bool ellipsesRow = idx.row() == (ItemCount - 1 - (hasEmptyRow ? 0 : 1));

    switch (role)
    {
    case Qt::DisplayRole:
    {
        if (emptyRow)
        {
            return ISqlFKMatchModel::tr("Empty");
        }
        else if (ellipsesRow)
        {
            return ellipsesString;
        }

        return getNameAtRow(idx.row());
    }
    case Qt::ToolTipRole:
    {
        if (!emptyRow && !ellipsesRow)
        {
            QString tip =
              tr("Gate <b>%1</b> is %2")
                .arg(items[idx.row()].gateLetter, utils::StationUtils::name(items[idx.row()].side));
            if (m_markConnectedGates)
            {
                QString state;
                db_id segId = items[idx.row()].segmentId;
                if (segId)
                {
                    state = tr("Segment: <b>%1</b>").arg(items[idx.row()].segmentName);
                    if (segId == m_excludeSegmentId)
                        state.append(tr("<br>Current"));
                }
                else
                {
                    state = tr("Not connected");
                }

                // New line, then append
                tip.append("<br>");
                tip.append(state);
            }
            return tip;
        }
        break;
    }
    case Qt::FontRole:
    {
        if (emptyRow)
        {
            return boldFont();
        }
        break;
    }
    case Qt::TextAlignmentRole:
    {
        if (!emptyRow && !ellipsesRow && !m_showOnlySegments)
            return Qt::AlignRight + Qt::AlignVCenter; // Segments will be Left aligned
        break;
    }
    case Qt::BackgroundRole:
    {
        if (!emptyRow && !ellipsesRow && m_markConnectedGates)
        {
            db_id segId = items[idx.row()].segmentId;
            if (segId && segId != m_excludeSegmentId)
            {
                // Cyan if gate is connected to a segment
                return QBrush(Qt::cyan);
            }
        }
        break;
    }
    case Qt::DecorationRole:
    {
        if (!emptyRow && !ellipsesRow)
        {
            QColor color;
            if (items[idx.row()].type.testFlag(utils::GateType::Bidirectional))
                break; // Default color

            if (items[idx.row()].type.testFlag(utils::GateType::Entrance))
                color = Qt::green; // Entrance only
            else if (items[idx.row()].type.testFlag(utils::GateType::Exit))
                color = Qt::red; // Exit only
            return color;
        }
        break;
    }
    }

    return QVariant();
}

void StationGatesMatchModel::autoSuggest(const QString &text)
{
    mQuery.clear();
    if (!text.isEmpty())
    {
        mQuery.clear();
        mQuery.reserve(text.size() + 2);
        mQuery.append('%');
        mQuery.append(text.toUtf8());
        mQuery.append('%');
    }

    refreshData();
}

void StationGatesMatchModel::refreshData()
{
    if (!mDb.db())
        return;

    beginResetModel();

    char emptyQuery = '%';

    if (mQuery.isEmpty())
        sqlite3_bind_text(q_getMatches.stmt(), 1, &emptyQuery, 1, SQLITE_STATIC);
    else
        sqlite3_bind_text(q_getMatches.stmt(), 1, mQuery, mQuery.size(), SQLITE_STATIC);

    q_getMatches.bind(2, m_stationId);

    auto end = q_getMatches.end();
    auto it  = q_getMatches.begin();
    int i    = 0;
    for (; i < ItemCount && it != end; i++)
    {
        auto track             = *it;
        items[i].gateId        = track.get<db_id>(0);
        items[i].outTrackCount = track.get<int>(1);
        items[i].type          = utils::GateType(track.get<int>(2));
        items[i].gateLetter    = sqlite3_column_text(q_getMatches.stmt(), 3)[0];
        items[i].side          = utils::Side(track.get<int>(4));

        if (m_markConnectedGates)
        {
            items[i].segmentId = track.get<db_id>(5);
            if (m_showOnlySegments && !items[i].segmentId)
            {
                // Skip this gate because it is not connected to a segment
                i--;  // Overwrite with new item
                ++it; // Step query to next record
                continue;
            }

            items[i].segmentName     = track.get<QString>(6);
            const db_id inGateId     = track.get<db_id>(7);
            items[i].segmentReversed = (inGateId != items[i].gateId);
        }
        else
        {
            items[i].segmentId = 0;
            items[i].segmentName.clear();
            items[i].segmentReversed = false;
        }

        ++it;
    }

    size = i;
    if (hasEmptyRow)
        size++; // Items + Empty

    if (it != end)
    {
        // There would be still rows, show Ellipses
        size++; // Items + Empty + Ellispses
    }

    q_getMatches.reset();
    endResetModel();

    emit resultsReady(false);
}

QString StationGatesMatchModel::getName(db_id id) const
{
    if (!mDb.db())
        return QString();

    query q(mDb, "SELECT name FROM station_gates WHERE id=?");
    q.bind(1, id);
    if (q.step() != SQLITE_ROW)
        return QString();

    QString str = q.getRows().get<QString>(0);
    if (m_showOnlySegments)
    {
        q.prepare("SELECT name FROM railway_segments WHERE in_gate_id=?1 OR out_gate_id=?1");
        q.bind(1, id);
        if (q.step() == SQLITE_ROW)
        {
            str.append(QLatin1String(": "));
            str.append(q.getRows().get<QString>(0));
        }
    }
    return str;
}

db_id StationGatesMatchModel::getIdAtRow(int row) const
{
    return items[row].gateId;
}

QString StationGatesMatchModel::getNameAtRow(int row) const
{
    QString str = items[row].gateLetter;
    if (m_showOnlySegments)
    {
        str.reserve(3 + items[row].segmentName.size());
        str.append(QLatin1String(": "));
        str.append(items[row].segmentName);
    }
    return str;
}

void StationGatesMatchModel::setFilter(db_id stationId, bool markConnectedGates,
                                       db_id excludeSegmentId, bool showOnlySegments)
{
    m_stationId          = stationId;
    m_markConnectedGates = markConnectedGates;
    m_excludeSegmentId   = m_markConnectedGates ? excludeSegmentId : 0;
    m_showOnlySegments   = showOnlySegments;

    QByteArray sql       = "SELECT g.id,g.out_track_count,g.type,g.name,g.side";
    if (m_markConnectedGates)
    {
        sql += ",s.id,s.name,s.in_gate_id";
    }
    sql += " FROM station_gates g";
    if (m_markConnectedGates)
    {
        sql += " LEFT JOIN railway_segments s ON s.in_gate_id=g.id OR s.out_gate_id=g.id";
    }

    sql += " WHERE g.station_id=?2 AND g.name LIKE ?1";

    sql += " ORDER BY g.side,g.name";

    q_getMatches.prepare(sql);

    refreshData();
}

int StationGatesMatchModel::getOutTrackCount(db_id gateId) const
{
    for (const GateItem &item : items)
    {
        if (item.gateId == gateId)
            return item.outTrackCount;
    }
    return 0;
}

utils::Side StationGatesMatchModel::getGateSide(db_id gateId) const
{
    for (const GateItem &item : items)
    {
        if (item.gateId == gateId)
            return item.side;
    }
    return utils::Side::West;
}

db_id StationGatesMatchModel::getSegmentIdAtRow(int row) const
{
    if (row < 0 || row >= size)
        return 0;
    return items[row].segmentId;
}

db_id StationGatesMatchModel::isSegmentReversedAtRow(int row) const
{
    if (row < 0 || row >= size)
        return 0;
    return items[row].segmentReversed;
}

int StationGatesMatchModel::getGateTrackCount(db_id gateId) const
{
    if (!mDb.db())
        return 0;

    query q(mDb, "SELECT out_track_count FROM station_gates WHERE id=?");
    q.bind(1, gateId);
    if (q.step() != SQLITE_ROW)
        return 0;
    return q.getRows().get<int>(0);
}

StationGatesMatchFactory::StationGatesMatchFactory(database &db, QObject *parent) :
    IMatchModelFactory(parent),
    m_stationId(0),
    m_excludeSegmentId(0),
    markConnectedGates(false),
    mDb(db)
{
}

ISqlFKMatchModel *StationGatesMatchFactory::createModel()
{
    StationGatesMatchModel *m = new StationGatesMatchModel(mDb);
    m->setFilter(m_stationId, markConnectedGates, m_excludeSegmentId);
    return m;
}
