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

#include "rscouplinginterface.h"

#include <QMessageBox>

#include <QApplication>

#include <QDebug>

#include "stopmodel.h"
#include "utils/rs_utils.h"

#include <QElapsedTimer>

RSCouplingInterface::RSCouplingInterface(database &db, QObject *parent) :
    QObject(parent),
    stopsModel(nullptr),
    mDb(db),
    q_deleteCoupling(mDb, "DELETE FROM coupling WHERE stop_id=? AND rs_id=?"),
    q_addCoupling(mDb, "INSERT INTO"
                       " coupling(stop_id,rs_id,operation)"
                       " VALUES(?, ?, ?)")
{
}

void RSCouplingInterface::loadCouplings(StopModel *model, db_id stopId, db_id jobId, QTime arr)
{
    stopsModel = model;

    m_stopId   = stopId;
    m_jobId    = jobId;
    arrival    = arr;

    coupled.clear();
    uncoupled.clear();

    query q(mDb, "SELECT rs_id, operation FROM coupling WHERE stop_id=?");
    q.bind(1, m_stopId);

    for (auto rs : q)
    {
        db_id rsId = rs.get<db_id>(0);
        RsOp op    = RsOp(rs.get<int>(1));

        if (op == RsOp::Coupled)
            coupled.append(rsId);
        else
            uncoupled.append(rsId);
    }
}

bool RSCouplingInterface::contains(db_id rsId, RsOp op) const
{
    if (op == RsOp::Coupled)
        return coupled.contains(rsId);
    else
        return uncoupled.contains(rsId);
}

bool RSCouplingInterface::coupleRS(db_id rsId, const QString &rsName, bool on,
                                   bool checkTractionType)
{
    stopsModel->startStopsEditing();
    stopsModel->markRsToUpdate(rsId);

    if (on)
    {
        if (coupled.contains(rsId))
        {
            qWarning() << "Error already checked:" << rsId;
            return true;
        }

        db_id jobId = 0;

        query q_RS_lastOp(mDb, "SELECT MAX(stops.arrival), coupling.operation, stops.job_id"
                               " FROM stops"
                               " JOIN coupling"
                               " ON coupling.stop_id=stops.id"
                               " AND coupling.rs_id=?"
                               " AND stops.arrival<?");
        q_RS_lastOp.bind(1, rsId);
        q_RS_lastOp.bind(2, arrival);
        int ret         = q_RS_lastOp.step();

        bool isOccupied = false; // No Op means RS is turned off in a depot so it isn't occupied
        if (ret == SQLITE_ROW)
        {
            auto row       = q_RS_lastOp.getRows();
            RsOp operation = RsOp(row.get<int>(1)); // Get last operation
            jobId          = row.get<db_id>(2);
            isOccupied     = (operation == RsOp::Coupled);
        }

        if (isOccupied)
        {
            if (jobId == m_jobId)
            {
                qWarning() << "Error while adding coupling op. Stop:" << m_stopId << "Rs:" << rsId
                           << "Already coupled by this job:" << m_jobId;

                QMessageBox::warning(qApp->activeWindow(), tr("Error"),
                                     tr("Error while adding coupling operation.\n"
                                        "Rollingstock %1 is already coupled by this job (%2)")
                                       .arg(rsName)
                                       .arg(m_jobId),
                                     QMessageBox::Ok);
                return false;
            }
            else
            {
                qWarning() << "Error while adding coupling op. Stop:" << m_stopId << "Rs:" << rsId
                           << "Occupied by this job:" << jobId;

                int but =
                  QMessageBox::warning(qApp->activeWindow(), tr("Error"),
                                       tr("Error while adding coupling operation.\n"
                                          "Rollingstock %1 is already coupled to another job (%2)\n"
                                          "Do you still want to couple it?")
                                         .arg(rsName)
                                         .arg(jobId),
                                       QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

                if (but == QMessageBox::No)
                    return false; // Abort
            }
        }

        if (checkTractionType && !stopsModel->isRailwayElectrifiedAfterStop(m_stopId))
        {
            // Query RS type
            query q_getRSType(mDb, "SELECT rs_models.type,rs_models.sub_type"
                                   " FROM rs_list"
                                   " JOIN rs_models ON rs_models.id=rs_list.model_id"
                                   " WHERE rs_list.id=?");
            q_getRSType.bind(1, rsId);
            if (q_getRSType.step() != SQLITE_ROW)
            {
                qWarning() << "RS seems to not exist, ID:" << rsId;
            }

            auto rs                 = q_getRSType.getRows();
            RsType type             = RsType(rs.get<int>(0));
            RsEngineSubType subType = RsEngineSubType(rs.get<int>(1));

            if (type == RsType::Engine && subType == RsEngineSubType::Electric)
            {
                int but = QMessageBox::warning(
                  qApp->activeWindow(), tr("Warning"),
                  tr("Rollingstock %1 is an Electric engine but the line is not electrified\n"
                     "This engine will not be albe to move a train.\n"
                     "Do you still want to couple it?")
                    .arg(rsName),
                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (but == QMessageBox::No)
                    return false; // Cancel coupling operation
            }
        }

        q_addCoupling.bind(1, m_stopId);
        q_addCoupling.bind(2, rsId);
        q_addCoupling.bind(3, int(RsOp::Coupled));
        ret = q_addCoupling.execute();
        q_addCoupling.reset();

        if (ret != SQLITE_OK)
        {
            qWarning() << "Error while adding coupling op. Stop:" << m_stopId << "Rs:" << rsId
                       << "Op: Coupled "
                       << "Ret:" << ret << mDb.error_msg();
            return false;
        }

        coupled.append(rsId);

        // Check if there is a next coupling operation in the same job
        query q(mDb, "SELECT s2.id, s2.arrival, s2.station_id, stations.name"
                     " FROM coupling"
                     " JOIN stops s2 ON s2.id=coupling.stop_id"
                     " JOIN stops s1 ON s1.id=?"
                     " JOIN stations ON stations.id=s2.station_id"
                     " WHERE coupling.rs_id=? AND coupling.operation=? AND s1.job_id=s2.job_id AND "
                     "s1.arrival < s2.arrival");
        q.bind(1, m_stopId);
        q.bind(2, rsId);
        q.bind(3, int(RsOp::Coupled));

        if (q.step() == SQLITE_ROW)
        {
            auto r         = q.getRows();
            db_id stopId   = r.get<db_id>(0);
            QTime arr      = r.get<QTime>(1);
            db_id stId     = r.get<db_id>(2);
            QString stName = r.get<QString>(3);

            qDebug() << "Found coupling, RS:" << rsId << "Stop:" << stopId << "St:" << stId << arr;

            int but =
              QMessageBox::question(qApp->activeWindow(), tr("Delete coupling?"),
                                    tr("You couple %1 also in a next stop in %2 at %3.\n"
                                       "Do you want to remove the other coupling operation?")
                                      .arg(rsName, stName, arr.toString("HH:mm")),
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (but == QMessageBox::Yes)
            {
                qDebug() << "Deleting coupling";

                q_deleteCoupling.bind(1, stopId);
                q_deleteCoupling.bind(2, rsId);
                ret = q_deleteCoupling.execute();
                q_deleteCoupling.reset();

                if (ret != SQLITE_OK)
                {
                    qWarning() << "Error while deleting next coupling op. Stop:" << stopId
                               << "Rs:" << rsId << "Op: Uncoupled "
                               << "Ret:" << ret << mDb.error_msg();
                }
            }
            else
            {
                qDebug() << "Keeping couple";
            }
        }
    }
    else
    {
        int row = coupled.indexOf(rsId);
        if (row == -1)
            return false;

        q_deleteCoupling.bind(1, m_stopId);
        q_deleteCoupling.bind(2, rsId);
        int ret = q_deleteCoupling.execute();
        q_deleteCoupling.reset();

        if (ret != SQLITE_OK)
        {
            qWarning() << "Error while deleting coupling op. Stop:" << m_stopId << "Rs:" << rsId
                       << "Op: Coupled "
                       << "Ret:" << ret << mDb.error_msg();
            return false;
        }

        coupled.removeAt(row);

        // Check if there is a next uncoupling operation
        query q(mDb, "SELECT s2.id, MIN(s2.arrival), s2.station_id, stations.name"
                     " FROM coupling"
                     " JOIN stops s2 ON s2.id=coupling.stop_id"
                     " JOIN stops s1 ON s1.id=?"
                     " JOIN stations ON stations.id=s2.station_id"
                     " WHERE coupling.rs_id=? AND coupling.operation=? AND s2.arrival > s1.arrival "
                     "AND s2.job_id=s1.job_id");
        q.bind(1, m_stopId);
        q.bind(2, rsId);
        q.bind(3, int(RsOp::Uncoupled));

        if (q.step() == SQLITE_ROW && q.getRows().column_type(0) != SQLITE_NULL)
        {
            auto r         = q.getRows();
            db_id stopId   = r.get<db_id>(0);
            QTime arr      = r.get<QTime>(1);
            db_id stId     = r.get<db_id>(2);
            QString stName = r.get<QString>(3);

            qDebug() << "Found uncoupling, RS:" << rsId << "Stop:" << stopId << "St:" << stId
                     << arr;

            int but = QMessageBox::question(
              qApp->activeWindow(), tr("Delete uncoupling?"),
              tr("You don't couple %1 anymore.\n"
                 "Do you want to remove also the uncoupling operation in %2 at %3?")
                .arg(rsName, stName, arr.toString("HH:mm")),
              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (but == QMessageBox::Yes)
            {
                qDebug() << "Deleting coupling";

                q_deleteCoupling.bind(1, stopId);
                q_deleteCoupling.bind(2, rsId);
                ret = q_deleteCoupling.execute();
                q_deleteCoupling.reset();

                if (ret != SQLITE_OK)
                {
                    qWarning() << "Error while deleting next uncoupling op. Stop:" << stopId
                               << "Rs:" << rsId << "Op: Uncoupled "
                               << "Ret:" << ret << mDb.error_msg();
                }
            }
            else
            {
                qDebug() << "Keeping couple";
            }
        }
    }

    return true;
}

bool RSCouplingInterface::uncoupleRS(db_id rsId, const QString &rsName, bool on)
{
    stopsModel->startStopsEditing();
    stopsModel->markRsToUpdate(rsId);

    if (on)
    {
        if (uncoupled.contains(rsId))
        {
            qWarning() << "Error already checked:" << rsId;
            return true;
        }

        q_addCoupling.bind(1, m_stopId);
        q_addCoupling.bind(2, rsId);
        q_addCoupling.bind(3, int(RsOp::Uncoupled));
        int ret = q_addCoupling.execute();
        q_addCoupling.reset();

        if (ret != SQLITE_OK)
        {
            qWarning() << "Error while adding coupling op. Stop:" << m_stopId << "Rs:" << rsId
                       << "Op: Uncoupled "
                       << "Ret:" << ret << mDb.error_msg();
            return false;
        }

        uncoupled.append(rsId);

        // Check if there is a next uncoupling operation
        query q(mDb, "SELECT s2.id, MIN(s2.arrival), s2.station_id, stations.name"
                     " FROM coupling"
                     " JOIN stops s2 ON s2.id=coupling.stop_id"
                     " JOIN stops s1 ON s1.id=?"
                     " JOIN stations ON stations.id=s2.station_id"
                     " WHERE coupling.rs_id=? AND coupling.operation=? AND s2.arrival > s1.arrival "
                     "AND s2.job_id=s1.job_id");
        q.bind(1, m_stopId);
        q.bind(2, rsId);
        q.bind(3, int(RsOp::Uncoupled));

        if (q.step() == SQLITE_ROW && q.getRows().column_type(0) != SQLITE_NULL)
        {
            auto r         = q.getRows();
            db_id stopId   = r.get<db_id>(0);
            QTime arr      = r.get<QTime>(1);
            db_id stId     = r.get<db_id>(2);
            QString stName = r.get<QString>(3);

            qDebug() << "Found uncoupling, RS:" << rsId << "Stop:" << stopId << "St:" << stId
                     << arr;

            int but =
              QMessageBox::question(qApp->activeWindow(), tr("Delete uncoupling?"),
                                    tr("You uncouple %1 also in %2 at %3.\n"
                                       "Do you want to remove the other uncoupling operation?")
                                      .arg(rsName, stName, arr.toString("HH:mm")),
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (but == QMessageBox::Yes)
            {
                qDebug() << "Deleting coupling";

                q_deleteCoupling.bind(1, stopId);
                q_deleteCoupling.bind(2, rsId);
                ret = q_deleteCoupling.execute();
                q_deleteCoupling.reset();

                if (ret != SQLITE_OK)
                {
                    qWarning() << "Error while deleting next uncoupling op. Stop:" << stopId
                               << "Rs:" << rsId << "Op: Uncoupled "
                               << "Ret:" << ret << mDb.error_msg();
                }
            }
            else
            {
                qDebug() << "Keeping couple";
            }
        }
    }
    else
    {
        int row = uncoupled.indexOf(rsId);
        if (row == -1)
            return false;

        q_deleteCoupling.bind(1, m_stopId);
        q_deleteCoupling.bind(2, rsId);
        int ret = q_deleteCoupling.execute();
        q_deleteCoupling.reset();

        if (ret != SQLITE_OK)
        {
            qWarning() << "Error while deleting coupling op. Stop:" << m_stopId << "Rs:" << rsId
                       << "Op: Uncoupled "
                       << "Ret:" << ret << mDb.error_msg();
            return false;
        }

        uncoupled.removeAt(row);
    }

    return true;
}

int RSCouplingInterface::importRSFromJob(db_id otherStopId)
{
    query q_getUncoupled(mDb, "SELECT coupling.rs_id, rs_list.number,"
                              " rs_models.name, rs_models.suffix, rs_models.type"
                              " FROM coupling"
                              " JOIN rs_list ON rs_list.id=coupling.rs_id"
                              " JOIN rs_models ON rs_models.id=rs_list.model_id"
                              " WHERE coupling.stop_id=? AND coupling.operation=0");
    q_getUncoupled.bind(1, otherStopId);

    int count            = 0;
    bool lineElectrified = stopsModel->isRailwayElectrifiedAfterStop(m_stopId);

    QElapsedTimer timer;
    timer.start();

    for (auto rs : q_getUncoupled)
    {
        db_id rsId       = rs.get<db_id>(0);

        int number       = rs.get<int>(1);
        int modelNameLen = sqlite3_column_bytes(q_getUncoupled.stmt(), 2);
        const char *modelName =
          reinterpret_cast<char const *>(sqlite3_column_text(q_getUncoupled.stmt(), 2));

        int modelSuffixLen = sqlite3_column_bytes(q_getUncoupled.stmt(), 3);
        const char *modelSuffix =
          reinterpret_cast<char const *>(sqlite3_column_text(q_getUncoupled.stmt(), 3));
        RsType rsType  = RsType(rs.get<int>(4));

        QString rsName = rs_utils::formatNameRef(modelName, modelNameLen, number, modelSuffix,
                                                 modelSuffixLen, rsType);

        // TODO: optimize work
        if (coupleRS(rsId, rsName, true, !lineElectrified))
            count++;

        if (timer.elapsed() > 10000)
        {
            // After 10 seconds, give opportunity to stop
            int ret = QMessageBox::question(
              qApp->activeWindow(), tr("Continue Importation?"),
              tr("Rollingstock importation is taking more time than expected.\n"
                 "Do you want to continue?"));

            if (ret == QMessageBox::No)
                return count; // Abort here

            timer.restart(); // Count again
        }
    }

    return count;
}

bool RSCouplingInterface::hasEngineAfterStop(bool *isElectricOnNonElectrifiedLine)
{
    query q_hasEngine(mDb, "SELECT coupling.rs_id,MAX(rs_models.sub_type),MAX(stops.arrival)"
                           " FROM stops"
                           " JOIN coupling ON coupling.stop_id=stops.id"
                           " JOIN rs_list ON rs_list.id=coupling.rs_id"
                           " JOIN rs_models ON rs_models.id=rs_list.model_id"
                           " WHERE stops.job_id=? AND stops.arrival<=? AND rs_models.type=0"
                           " GROUP BY coupling.rs_id"
                           " HAVING coupling.operation=1"
                           " LIMIT 1");
    q_hasEngine.bind(1, m_jobId);
    q_hasEngine.bind(2, arrival);
    if (q_hasEngine.step() != SQLITE_ROW)
        return false; // No engine

    if (isElectricOnNonElectrifiedLine)
    {
        RsEngineSubType subType = RsEngineSubType(q_hasEngine.getRows().get<int>(1));
        *isElectricOnNonElectrifiedLine =
          (subType == RsEngineSubType::Electric) && (!isRailwayElectrified());
    }
    return true;
}

bool RSCouplingInterface::isRailwayElectrified() const
{
    return stopsModel->isRailwayElectrifiedAfterStop(m_stopId);
}

db_id RSCouplingInterface::getJobId() const
{
    return m_jobId;
}
