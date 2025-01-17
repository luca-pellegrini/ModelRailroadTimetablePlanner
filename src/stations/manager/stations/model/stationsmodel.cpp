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

#include "stationsmodel.h"

#include "utils/delegates/sql/pageditemmodelhelper_impl.h"

#include <sqlite3pp/sqlite3pp.h>
using namespace sqlite3pp;

#include "stations/station_name_utils.h"

#include "app/session.h"

#include <QDebug>

// Error messages
static constexpr char errorNameAlreadyUsedText[] =
  QT_TRANSLATE_NOOP("StationsModel", "The name <b>%1</b> is already used by another station.<br>"
                                     "Please choose a different name for each station.");
static constexpr char errorShortNameAlreadyUsedText[] = QT_TRANSLATE_NOOP(
  "StationsModel", "The name <b>%1</b> is already used as short name for station <b>%2</b>.<br>"
                   "Please choose a different name for each station.");
static constexpr char errorNameSameShortNameText[] =
  QT_TRANSLATE_NOOP("StationsModel", "Name and short name cannot be equal (<b>%1</b>).");

static constexpr char errorPhoneSameNumberText[] = QT_TRANSLATE_NOOP(
  "StationsModel", "The phone number <b>%1</b> is already used by another station.<br>"
                   "Please choose a different phone number for each station.");

static constexpr char errorStationInUseText[] = QT_TRANSLATE_NOOP(
  "StationsModel", "Cannot delete <b>%1</b> station because it is still referenced.<br>"
                   "Please delete all jobs stopping here and remove the station from any line.");

StationsModel::StationsModel(sqlite3pp::database &db, QObject *parent) :
    BaseClass(500, db, parent)
{
    sortColumn = NameCol;
}

QVariant StationsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal)
    {
        switch (role)
        {
        case Qt::DisplayRole:
        {
            switch (section)
            {
            case NameCol:
                return tr("Name");
            case ShortNameCol:
                return tr("Short Name");
            case TypeCol:
                return tr("Type");
            case PhoneCol:
                return tr("Phone");
            }
            break;
        }
        case Qt::ToolTipRole:
        {
            switch (section)
            {
            case NameCol:
                return tr("You can filter by <b>Name</b> or <b>Short Name</b>");
            }
            break;
        }
        }
    }
    else if (role == Qt::DisplayRole)
    {
        return section + curPage * ItemsPerPage + 1;
    }

    return QAbstractTableModel::headerData(section, orientation, role);
}

QVariant StationsModel::data(const QModelIndex &idx, int role) const
{
    const int row = idx.row();
    if (!idx.isValid() || row >= curItemCount || idx.column() >= NCols)
        return QVariant();

    if (row < cacheFirstRow || row >= cacheFirstRow + cache.size())
    {
        // Fetch above or below current cache
        const_cast<StationsModel *>(this)->fetchRow(row);

        // Temporarily return null
        return role == Qt::DisplayRole ? QVariant("...") : QVariant();
    }

    const StationItem &item = cache.at(row - cacheFirstRow);

    switch (role)
    {
    case Qt::DisplayRole:
    {
        switch (idx.column())
        {
        case NameCol:
            return item.name;
        case ShortNameCol:
            return item.shortName;
        case TypeCol:
            return utils::StationUtils::name(item.type);
        case PhoneCol:
        {
            if (item.phone_number == -1)
                return QVariant(); // Null
            return item.phone_number;
        }
        }
        break;
    }
    case Qt::EditRole:
    {
        switch (idx.column())
        {
        case NameCol:
            return item.name;
        case ShortNameCol:
            return item.shortName;
        case TypeCol:
            return int(item.type);
        case PhoneCol:
            return item.phone_number;
        }
        break;
    }
    }

    return QVariant();
}

bool StationsModel::setData(const QModelIndex &idx, const QVariant &value, int role)
{
    const int row = idx.row();
    if (!idx.isValid() || row >= curItemCount || idx.column() >= NCols || row < cacheFirstRow
        || row >= cacheFirstRow + cache.size() || role != Qt::EditRole)
        return false; // Not fetched yet or invalid

    StationItem &item = cache[row - cacheFirstRow];
    QModelIndex first = idx;
    QModelIndex last  = idx;

    switch (idx.column())
    {
    case NameCol:
    {
        if (!setName(item, value.toString()))
            return false;
        break;
    }
    case ShortNameCol:
    {
        if (!setShortName(item, value.toString()))
            return false;
        break;
    }
    case TypeCol:
    {
        bool ok = false;
        int val = value.toInt(&ok);
        if (!ok || !setType(item, val))
            return false;
        break;
    }
    case PhoneCol:
    {
        bool ok    = false;
        qint64 val = value.toLongLong(&ok);
        if (!ok)
            val = -1;
        if (!setPhoneNumber(item, val))
            return false;
        break;
    }
    }

    emit dataChanged(first, last);

    return true;
}

Qt::ItemFlags StationsModel::flags(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags f = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;
    if (idx.row() < cacheFirstRow || idx.row() >= cacheFirstRow + cache.size())
        return f; // Not fetched yet

    f.setFlag(Qt::ItemIsEditable);

    return f;
}

qint64 StationsModel::recalcTotalItemCount()
{
    query q(mDb);
    buildQuery(q, 0, 0, false);

    q.step();
    const qint64 count = q.getRows().get<qint64>(0);
    return count;
}

void StationsModel::buildQuery(sqlite3pp::query &q, int sortCol, int offset, bool fullData)
{
    QByteArray sql;
    if (fullData)
        sql = "SELECT id,name,short_name,type,phone_number FROM stations";
    else
        sql = "SELECT COUNT(1) FROM stations";

    bool whereClauseAdded  = false;

    bool phoneFilterIsNull = m_phoneFilter.startsWith(nullFilterStr, Qt::CaseInsensitive);
    if (!m_phoneFilter.isEmpty())
    {
        if (phoneFilterIsNull)
            sql.append(" WHERE phone_number IS NULL");
        else
            sql.append(" WHERE phone_number LIKE ?3");
        whereClauseAdded = true;
    }

    if (!m_nameFilter.isEmpty())
    {
        if (whereClauseAdded)
            sql.append(" AND ");
        else
            sql.append(" WHERE ");
        sql.append("(name LIKE ?4 OR short_name LIKE ?4)");
    }

    if (fullData)
    {
        // Apply sorting
        const char *sortColExpr = nullptr;
        switch (sortCol)
        {
        case NameCol:
        {
            sortColExpr = "name"; // Order by 1 column, no where clause
            break;
        }
        case TypeCol:
        {
            sortColExpr = "type,name";
            break;
        }
        }

        sql += " ORDER BY ";
        sql += sortColExpr;

        sql += " LIMIT ?1";
        if (offset)
            sql += " OFFSET ?2";
    }

    q.prepare(sql);

    if (fullData)
    {
        // Apply offset and batch size
        q.bind(1, BatchSize);
        if (offset)
            q.bind(2, offset);
    }

    // Apply filters
    QByteArray phoneFilter;
    if (!m_phoneFilter.isEmpty() && !phoneFilterIsNull)
    {
        phoneFilter.reserve(m_phoneFilter.size() + 2);
        phoneFilter.append('%');
        phoneFilter.append(m_phoneFilter.toUtf8());
        phoneFilter.append('%');
        sqlite3_bind_text(q.stmt(), 3, phoneFilter, phoneFilter.size(), SQLITE_STATIC);
    }

    QByteArray nameFilter;
    if (!m_nameFilter.isEmpty())
    {
        nameFilter.reserve(m_nameFilter.size() + 2);
        nameFilter.append('%');
        nameFilter.append(m_nameFilter.toUtf8());
        nameFilter.append('%');
        sqlite3_bind_text(q.stmt(), 4, nameFilter, nameFilter.size(), SQLITE_STATIC);
    }
}

void StationsModel::setSortingColumn(int col)
{
    if (sortColumn == col || (col != NameCol && col != TypeCol))
        return;

    clearCache();
    sortColumn        = col;

    QModelIndex first = index(0, 0);
    QModelIndex last  = index(curItemCount - 1, NCols - 1);
    emit dataChanged(first, last);
}

std::pair<QString, IPagedItemModel::FilterFlags> StationsModel::getFilterAtCol(int col)
{
    switch (col)
    {
    case NameCol:
        return {m_nameFilter, FilterFlag::BasicFiltering};
    case PhoneCol:
        return {m_phoneFilter, FilterFlags(FilterFlag::BasicFiltering | FilterFlag::ExplicitNULL)};
    }

    return {QString(), FilterFlag::NoFiltering};
}

bool StationsModel::setFilterAtCol(int col, const QString &str)
{
    const bool isNull = str.startsWith(nullFilterStr, Qt::CaseInsensitive);

    switch (col)
    {
    case NameCol:
    {
        if (isNull)
            return false; // Cannot have NULL Name
        m_nameFilter = str;
        break;
    }
    case PhoneCol:
    {
        m_phoneFilter = str;
        break;
    }
    default:
        return false;
    }

    emit filterChanged();
    return true;
}

bool StationsModel::addStation(const QString &name, db_id *outStationId)
{
    if (name.isEmpty())
        return false;

    command q_newStation(mDb, "INSERT INTO stations(id,name,short_name,type,phone_number,svg_data)"
                              " VALUES (NULL, ?, NULL, 0, NULL, NULL)");
    q_newStation.bind(1, name);

    sqlite3_mutex *mutex = sqlite3_db_mutex(mDb.db());
    sqlite3_mutex_enter(mutex);
    int ret         = q_newStation.execute();
    db_id stationId = mDb.last_insert_rowid();
    sqlite3_mutex_leave(mutex);
    q_newStation.reset();

    if ((ret != SQLITE_OK && ret != SQLITE_DONE) || stationId == 0)
    {
        // Error
        if (outStationId)
            *outStationId = 0;

        if (ret == SQLITE_CONSTRAINT_UNIQUE)
        {
            emit modelError(tr(errorNameAlreadyUsedText).arg(name));
        }
        else
        {
            emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        }
        return false;
    }

    if (outStationId)
        *outStationId = stationId;

    // Clear filters
    m_nameFilter.clear();
    m_nameFilter.squeeze();
    m_phoneFilter.clear();
    m_phoneFilter.squeeze();
    emit filterChanged();

    refreshData(); // Recalc row count
    setSortingColumn(NameCol);
    switchToPage(0); // Reset to first page and so it is shown as first row

    return true;
}

bool StationsModel::removeStation(db_id stationId)
{
    command q_removeStation(mDb, "DELETE FROM stations WHERE id=?");

    q_removeStation.bind(1, stationId);
    int ret = q_removeStation.execute();
    q_removeStation.reset();

    if (ret != SQLITE_OK)
    {
        if (ret == SQLITE_CONSTRAINT_FOREIGNKEY || ret == SQLITE_CONSTRAINT_TRIGGER)
        {
            // TODO: show more information to the user, like where it's still referenced
            query q(mDb, "SELECT name FROM stations WHERE id=?");
            q.bind(1, stationId);
            if (q.step() == SQLITE_ROW)
            {
                const QString name = q.getRows().get<QString>(0);
                emit modelError(tr(errorStationInUseText).arg(name));
            }
        }
        else
        {
            emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        }

        return false;
    }

    emit Session->stationRemoved(stationId);

    refreshData(); // Recalc row count

    return true;
}

void StationsModel::internalFetch(int first, int sortCol, int /*valRow*/, const QVariant & /*val*/)
{
    query q(mDb);

    int offset = first + curPage * ItemsPerPage;

    qDebug() << "Fetching:" << first << "Offset:" << offset;

    buildQuery(q, sortCol, offset, true);

    QVector<StationItem> vec(BatchSize);

    auto it        = q.begin();
    const auto end = q.end();

    int i          = 0;
    for (; it != end; ++it)
    {
        auto r            = *it;
        StationItem &item = vec[i];
        item.stationId    = r.get<db_id>(0);
        item.name         = r.get<QString>(1);
        item.shortName    = r.get<QString>(2);
        item.type         = utils::StationType(r.get<int>(3));
        if (r.column_type(4) == SQLITE_NULL)
            item.phone_number = -1;
        else
            item.phone_number = r.get<qint64>(4);

        i += 1;
    }

    if (i < BatchSize)
        vec.remove(i, BatchSize - i);

    postResult(vec, first);
}

bool StationsModel::setName(StationsModel::StationItem &item, const QString &val)
{
    const QString name = val.simplified();
    if (name.isEmpty() || item.name == name)
        return false;

    // TODO: check non allowed characters

    query q(mDb, "SELECT id,name FROM stations WHERE short_name=?");
    q.bind(1, name);
    if (q.step() == SQLITE_ROW)
    {
        db_id stId = q.getRows().get<db_id>(0);
        if (stId == item.stationId)
        {
            emit modelError(tr(errorNameSameShortNameText).arg(name));
        }
        else
        {
            const QString otherShortName = q.getRows().get<QString>(1);
            emit modelError(tr(errorShortNameAlreadyUsedText).arg(name, otherShortName));
        }
        return false;
    }

    q.prepare("UPDATE stations SET name=? WHERE id=?");
    q.bind(1, name);
    q.bind(2, item.stationId);
    int ret = q.step();
    if (ret != SQLITE_OK && ret != SQLITE_DONE)
    {
        if (ret == SQLITE_CONSTRAINT_UNIQUE)
        {
            emit modelError(tr(errorNameAlreadyUsedText).arg(name));
        }
        else
        {
            emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        }
        return false;
    }

    item.name = name;

    emit Session->stationNameChanged(item.stationId);

    // This row has now changed position so we need to invalidate cache
    // HACK: we emit dataChanged for this index (that doesn't exist anymore)
    // but the view will trigger fetching at same scroll position so it is enough
    cache.clear();
    cacheFirstRow = 0;

    return true;
}

bool StationsModel::setShortName(StationsModel::StationItem &item, const QString &val)
{
    const QString shortName = val.simplified();
    if (item.shortName == shortName)
        return false;

    // TODO: check non allowed characters

    query q(mDb, "SELECT id,name FROM stations WHERE name=?");
    q.bind(1, shortName);
    if (q.step() == SQLITE_ROW)
    {
        db_id stId = q.getRows().get<db_id>(0);
        if (stId == item.stationId)
        {
            emit modelError(tr(errorNameSameShortNameText).arg(shortName));
        }
        else
        {
            const QString otherName = q.getRows().get<QString>(1);
            emit modelError(tr(errorShortNameAlreadyUsedText).arg(shortName, otherName));
        }
        return false;
    }

    q.prepare("UPDATE stations SET short_name=? WHERE id=?");
    if (shortName.isEmpty())
        q.bind(1); // Bind NULL
    else
        q.bind(1, shortName);
    q.bind(2, item.stationId);
    int ret = q.step();
    if (ret != SQLITE_OK && ret != SQLITE_DONE)
    {
        if (ret == SQLITE_CONSTRAINT_UNIQUE)
        {
            emit modelError(tr(errorShortNameAlreadyUsedText).arg(shortName, QString()));
        }
        else
        {
            emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        }
        return false;
    }

    item.shortName = shortName;
    emit Session->stationNameChanged(item.stationId);

    return true;
}

bool StationsModel::setType(StationsModel::StationItem &item, int val)
{
    utils::StationType type = utils::StationType(val);
    if (val < 0 || type >= utils::StationType::NTypes || item.type == type)
        return false;

    query q(mDb, "UPDATE stations SET type=? WHERE id=?");
    q.bind(1, val);
    q.bind(2, item.stationId);
    int ret = q.step();
    if (ret != SQLITE_OK && ret != SQLITE_DONE)
    {
        emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        return false;
    }

    item.type = type;

    if (sortColumn == TypeCol)
    {
        // This row has now changed position so we need to invalidate cache
        // HACK: we emit dataChanged for this index (that doesn't exist anymore)
        // but the view will trigger fetching at same scroll position so it is enough
        cache.clear();
        cacheFirstRow = 0;
    }

    return true;
}

bool StationsModel::setPhoneNumber(StationsModel::StationItem &item, qint64 val)
{
    if (item.phone_number == val)
        return false;

    // TODO: better handling of NULL (= remove phone number)
    // TODO: maybe is better TEXT type for phone number
    if (val < 0)
        val = -1;

    query q(mDb, "UPDATE stations SET phone_number=? WHERE id=?");
    if (val == -1)
        q.bind(1); // Bind NULL
    else
        q.bind(1, val);
    q.bind(2, item.stationId);
    int ret = q.step();
    if (ret != SQLITE_OK && ret != SQLITE_DONE)
    {
        if (ret == SQLITE_CONSTRAINT_UNIQUE)
        {
            emit modelError(tr(errorPhoneSameNumberText).arg(val));
        }
        else
        {
            emit modelError(tr("Error: %1").arg(mDb.error_msg()));
        }
        return false;
    }

    item.phone_number = val;

    return true;
}
