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

#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QSettings>
#include <QScopedPointer>

#include <QFont>

#include "type_utils.h"

#include "info.h"

// FIXME: check if all settings are wired to SettingsDialog

#define FIELD_GET(name, str, type, val)                                                   \
    type get##name()                                                                      \
    {                                                                                     \
        if (m_settings)                                                                   \
            return utils::fromVariant<type>(m_settings->value(QStringLiteral(str), val)); \
        return val;                                                                       \
    }

#define FIELD_SET(name, str, type)                                                   \
    void set##name(utils::const_ref_t<type>::Type v)                                 \
    {                                                                                \
        if (!m_settings)                                                             \
            return;                                                                  \
        return m_settings->setValue(QStringLiteral(str), utils::toVariant<type>(v)); \
    }

#define FIELD(name, str, type, val) \
    FIELD_GET(name, str, type, val) \
    FIELD_SET(name, str, type)

#define FONT_FIELD(name, str, val)      \
    QFont get##name()                   \
    {                                   \
        return getFontHelper(str, val); \
    }                                   \
    void set##name(const QFont &f)      \
    {                                   \
        return setFontHelper(str, f);   \
    }

class MRTPSettings : public QObject
{
    Q_OBJECT
public:
    explicit MRTPSettings(QObject *parent = nullptr);

    void loadSettings(const QString &fileName);

    void saveSettings();
    void restoreDefaultSettings();

    // General
    FIELD(Language, "language", QLocale, QLocale(QLocale::English))
    FIELD(RecentFiles, "recent_files", QStringList, QStringList())

    // Job Graph
    FIELD(HorizontalOffset, "job_graph/horizontal_offset", int, 50)
    FIELD(VerticalOffset, "job_graph/vertical_offset", int, 50)
    FIELD(HourOffset, "job_graph/hour_offset", int, 100)
    FIELD(StationOffset, "job_graph/station_offset", int, 150)
    FIELD(PlatformOffset, "job_graph/platform_offset", int, 20)

    FIELD(PlatformLineWidth, "job_graph/platf_line_width", int, 2)
    FIELD(HourLineWidth, "job_graph/hour_line_width", int, 2)
    FIELD(JobLineWidth, "job_graph/job_line_width", int, 6)

    FIELD(HourLineColor, "job_graph/hour_line_color", QColor, QColor(Qt::black))
    FIELD(HourTextColor, "job_graph/hour_text_color", QColor, QColor(Qt::green))
    FIELD(StationTextColor, "job_graph/station_text_color", QColor, QColor(Qt::red))
    FIELD(MainPlatfColor, "job_graph/main_platf_color", QColor, QColor(Qt::magenta))
    FIELD(DepotPlatfColor, "job_graph/depot_platf_color", QColor, QColor(Qt::darkGray))

    FIELD(JobLabelFontSize, "job_graph/job_label_font_size", qreal, 12.0)

    FIELD(FollowSelectionOnGraphChange, "job_graph/follow_selection_on_graph_change", bool, true)
    FIELD(SyncSelectionOnAllGraphs, "job_graph/sync_job_selection", bool, true)

    // Job Colors
    QColor getCategoryColor(int category);
    void setCategoryColor(int category, const QColor &color);

    // Stops
    FIELD(AutoInsertTransits, "job_editor/auto_insert_transits", bool, true)
    FIELD(AutoShiftLastStopCouplings, "job_editor/auto_shift_couplings", bool, true)
    FIELD(AutoUncoupleAtLastStop, "job_editor/auto_uncouple_at_last_stop", bool, true)
    int getDefaultStopMins(int category);
    void setDefaultStopMins(int category, int mins);

    // Shift Graph
    FIELD(ShiftHourOffset, "shift_graph/hour_offset", double, 150.0)
    FIELD(ShiftHorizOffset, "shift_graph/horiz_offset", double, 50.0)
    FIELD(ShiftVertOffset, "shift_graph/vert_offset", double, 35.0)
    FIELD(ShiftJobRowHeight, "shift_graph/job_row_height", double, 70.0)
    FIELD(ShiftJobRowSpace, "shift_graph/job_row_space", double, 4.0)
    FIELD(ShiftHideSameStations, "shift_graph/hide_same_stations", bool, true)

    // RollingStock
    FIELD(RemoveMergedSourceModel, "rollingstock/remove_merged_source_model", bool, false)
    FIELD(RemoveMergedSourceOwner, "rollingstock/remove_merged_source_owner", bool, false)
    FIELD(ShowCouplingLegend, "rollingstock/show_coupling_legend", bool, false)

    // RS Import
    FIELD(ODSFirstRow, "rs_import/first_row", int, 3)
    FIELD(ODSNumCol, "rs_import/num_column", int, 1)
    FIELD(ODSNameCol, "rs_import/model_column", int, 3)

    // Sheet export ODT, NOTE: header/footer can be overriden by session specific values
    FIELD(SheetHeader, "sheet_export/header", QString, QString())
    FIELD(SheetFooter, "sheet_export/footer", QString,
          QStringLiteral("Generated by %1").arg(AppDisplayName))
    FIELD(SheetStoreLocationDateInMeta, "sheet_export/location_date_in_meta", bool, true)

    // Background Tasks
    FIELD(CheckRSWhenOpeningDB, "background_tasks/check_rs_at_startup", bool, true)
    FIELD(CheckRSOnJobEdit, "background_tasks/check_rs_on_job_edited", bool, true)
    FIELD(CheckCrossingWhenOpeningDB, "background_tasks/check_crossing_at_startup", bool, true)
    FIELD(CheckCrossingOnJobEdit, "background_tasks/check_crossing_on_job_edited", bool, true)

signals:
    void jobColorsChanged();
    void jobGraphOptionsChanged();
    void shiftGraphOptionsChanged();
    void stopOptionsChanged();

private:
    QFont getFontHelper(const QString &baseKey, QFont defFont);
    void setFontHelper(const QString &baseKey, const QFont &f);
    QScopedPointer<QSettings> m_settings;
};

#endif // APPSETTINGS_H
