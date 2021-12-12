#ifndef STATIONPLANMODEL_H
#define STATIONPLANMODEL_H

#include <QAbstractTableModel>

#include <QTime>

#include <sqlite3pp/sqlite3pp.h>

#include "utils/types.h"

typedef struct StPlanItem_
{
    db_id stopId;
    db_id jobId;
    QTime arrival;
    QTime departure;
    QString platform;

    QString description;

    JobCategory cat;

    enum class ItemType : qint8
    {
        Normal = 0,
        Departure,
        Transit
    };
    ItemType type;
} StPlanItem;

class StationPlanModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Columns
    {
        Arrival = 0,
        Departure,
        Platform,
        Job,
        Notes,
        NCols
    };
    StationPlanModel(sqlite3pp::database& db, QObject *parent = nullptr);

    // Header:
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Basic functionality:
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;

    void clear();

    void loadPlan(db_id stId);

    std::pair<db_id, db_id> getJobAndStopId(int row) const;

private:
    QVector<StPlanItem> m_data;

    sqlite3pp::database& mDb;
    sqlite3pp::query q_countPlanItems;
    sqlite3pp::query q_selectPlan;
};

#endif // STATIONPLANMODEL_H
