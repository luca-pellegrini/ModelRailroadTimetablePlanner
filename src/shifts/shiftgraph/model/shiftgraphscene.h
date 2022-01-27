#ifndef SHIFTGRAPHSCENE_H
#define SHIFTGRAPHSCENE_H

#include "utils/scene/igraphscene.h"

#include <QVector>
#include <QHash>
#include <QTime>

#include "utils/types.h"

namespace sqlite3pp {
class database;
class query;
}

/*!
 * \brief Class to store shift information
 *
 * Reimplement IGraphScene for shift graph
 * Stores information to draw shift contents in a ShiftGraphView
 *
 * \sa ShiftGraphView
 */
class ShiftGraphScene : public IGraphScene
{
    Q_OBJECT
public:

    struct JobItem
    {
        JobEntry job;

        db_id fromStId = 0;
        db_id toStId = 0;

        QTime start;
        QTime end;
    };

    struct ShiftGraph
    {
        db_id shiftId;
        QString shiftName;

        QVector<JobItem> jobList;
    };

    ShiftGraphScene(sqlite3pp::database& db, QObject *parent = nullptr);

    virtual void renderContents(QPainter *painter, const QRectF& sceneRect) override;
    virtual void renderHeader(QPainter *painter, const QRectF& sceneRect,
                              Qt::Orientation orient, double scroll) override;

    void drawShifts(QPainter *painter, const QRectF& sceneRect);
    void drawHourLines(QPainter *painter, const QRectF &sceneRect);
    void drawShiftHeader(QPainter *painter, const QRectF& rect);
    void drawHourHeader(QPainter *painter, const QRectF &rect);

    /*!
     * \brief Get job at graph position
     *
     * \param scenePos Point in scene coordinates
     * \param outShiftName If job is found, gets filled with its shift name
     */
    JobItem getJobAt(const QPointF& scenePos, QString& outShiftName) const;

    inline QString getStationFullName(db_id stationId) const
    {
        auto st = m_stationCache.constFind(stationId);
        if(st == m_stationCache.constEnd())
            return QString();
        return st.value().name;
    }

public slots:
    bool loadShifts();

private slots:
    void updateShiftGraphOptions();

    void onShiftNameChanged(db_id shiftId);
    void onShiftRemoved(db_id shiftId);
    void onShiftJobsChanged(db_id shiftId);

    void onStationNameChanged(db_id stationId);

private:
    void recalcContentSize();

    bool loadShiftRow(ShiftGraph& shiftObj, sqlite3pp::query& q_getStName,
                      sqlite3pp::query& q_countJobs, sqlite3pp::query& q_getJobs);
    void loadStationName(db_id stationId, sqlite3pp::query& q_getStName);

    std::pair<int, int> lowerBound(db_id shiftId, const QString& name);

    static constexpr qreal MSEC_PER_HOUR = 1000 * 60 * 60;
    inline qreal jobPos(const QTime& t)
    {
        return t.msecsSinceStartOfDay() / MSEC_PER_HOUR * hourOffset + horizOffset;
    }

private:
    sqlite3pp::database& mDb;

    QVector<ShiftGraph> m_shifts;

    struct StationCache
    {
        //Full Name of station
        QString name;

        //Short Name if available or fallback to Full Name
        QString shortNameOrFallback;
    };

    QHash<db_id, StationCache> m_stationCache;

    //Options
    qreal hourOffset = 150;
    qreal shiftRowHeight = 50;
    qreal rowSpaceOffset = 10;

    qreal horizOffset = 50;
    qreal vertOffset = 20;

    bool hideSameStations = true;
};

#endif // SHIFTGRAPHSCENE_H
