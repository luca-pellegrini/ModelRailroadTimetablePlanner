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

#include "jobwriter.h"

#include "odtutils.h"
#include <QXmlStreamWriter>

#include "utils/rs_utils.h"
#include "utils/jobcategorystrings.h"

#include "jobs/jobsmanager/model/jobshelper.h"

#include <QDebug>

void writeJobSummary(QXmlStreamWriter &xml, const QString &from, const QString &dep,
                     const QString &to, const QString &arr, int axes)
{
    // Table 'job_summary'
    xml.writeStartElement("table:table");
    xml.writeAttribute("table:name", "job_summary");
    xml.writeAttribute("table:style-name", "job_5f_summary");

    xml.writeEmptyElement("table:table-column"); // A
    xml.writeAttribute("table:style-name", "job_5f_summary.A");

    xml.writeEmptyElement("table:table-column"); // B
    xml.writeAttribute("table:style-name", "job_5f_summary.B");

    xml.writeEmptyElement("table:table-column"); // C
    xml.writeAttribute("table:style-name", "job_5f_summary.C");

    xml.writeEmptyElement("table:table-column"); // D
    xml.writeAttribute("table:style-name", "job_5f_summary.D");

    // Row
    xml.writeStartElement("table:table-row");

    // Cells
    writeCell(xml, "job_5f_summary_cell", "P2", Odt::text(Odt::jobSummaryFrom));
    writeCell(xml, "job_5f_summary_cell", "P3", from);
    writeCell(xml, "job_5f_summary_cell", "P2", Odt::text(Odt::jobSummaryDep));
    writeCell(xml, "job_5f_summary_cell", "P3", dep);

    xml.writeEndElement(); // table-row

    // Row 2
    xml.writeStartElement("table:table-row");

    // Cells
    writeCell(xml, "job_5f_summary_cell", "P2", Odt::text(Odt::jobSummaryTo));
    writeCell(xml, "job_5f_summary_cell", "P3", to);
    writeCell(xml, "job_5f_summary_cell", "P2", Odt::text(Odt::jobSummaryArr));
    writeCell(xml, "job_5f_summary_cell", "P3", arr);

    xml.writeEndElement(); // table-row

    // Row 3
    xml.writeStartElement("table:table-row");

    // Cells
    writeCell(xml, "job_5f_summary_cell", "P2", Odt::text(Odt::jobSummaryAxes));
    writeCell(xml, "job_5f_summary_cell", "P3", QString::number(axes));
    writeCell(xml, "job_5f_summary_cell", "P2", QString());
    writeCell(xml, "job_5f_summary_cell", "P3", QString());

    xml.writeEndElement(); // table-row

    xml.writeEndElement(); // table:table END
}

JobWriter::JobWriter(database &db) :
    mDb(db),
    q_getJobStops(mDb, "SELECT stops.id,"
                       "stops.station_id,"
                       "stations.name,"
                       "stops.arrival,"
                       "stops.departure,"
                       "stops.type,"
                       "stops.description,"
                       "t1.name, t2.name,"
                       "g1.track_side, g2.track_side"
                       " FROM stops"
                       " JOIN stations ON stations.id=stops.station_id"
                       " LEFT JOIN station_gate_connections g1 ON g1.id=stops.in_gate_conn"
                       " LEFT JOIN station_gate_connections g2 ON g2.id=stops.out_gate_conn"
                       " LEFT JOIN station_tracks t1 ON t1.id=g1.track_id"
                       " LEFT JOIN station_tracks t2 ON t2.id=g2.track_id"
                       " WHERE stops.job_id=? ORDER BY stops.arrival"),

    q_getFirstStop(mDb, "SELECT stops.id, stations.name, MIN(stops.departure)"
                        " FROM stops"
                        " JOIN stations ON stations.id=stops.station_id"
                        " WHERE stops.job_id=?"),

    q_getLastStop(mDb, "SELECT stops.id, stations.name, MAX(stops.arrival)"
                       " FROM stops"
                       " JOIN stations ON stations.id=stops.station_id"
                       " WHERE stops.job_id=?"),

    q_initialJobAxes(mDb, "SELECT SUM(rs_models.axes)"
                          " FROM coupling"
                          " JOIN rs_list ON rs_list.id=coupling.rs_id"
                          " JOIN rs_models ON rs_models.id=rs_list.model_id"
                          " WHERE stop_id=?"),

    q_selectPassings(mDb, "SELECT stops.id,stops.job_id,jobs.category,"
                          "stops.arrival,stops.departure"
                          " FROM stops"
                          " JOIN jobs ON jobs.id=stops.job_id"
                          " WHERE stops.station_id=? AND stops.departure>=? AND stops.arrival<=? "
                          "AND stops.job_id<>?"),

    q_getStopCouplings(mDb, "SELECT coupling.rs_id,"
                            "rs_list.number,rs_models.name,rs_models.suffix,rs_models.type"
                            " FROM coupling"
                            " JOIN rs_list ON rs_list.id=coupling.rs_id"
                            " JOIN rs_models ON rs_models.id=rs_list.model_id"
                            " WHERE coupling.stop_id=? AND coupling.operation=?")
{
}

void JobWriter::writeJobAutomaticStyles(QXmlStreamWriter &xml)
{
    // job_summary columns
    writeColumnStyle(xml, "job_5f_summary.A", "1.60cm");
    writeColumnStyle(xml, "job_5f_summary.B", "8.30cm");
    writeColumnStyle(xml, "job_5f_summary.C", "2.90cm");
    writeColumnStyle(xml, "job_5f_summary.D", "4.20cm");

    // job_stops columns
    writeColumnStyle(xml, "job_5f_stops.A", "2.60cm"); // Station      (IT: Stazione)
    writeColumnStyle(xml, "job_5f_stops.B", "1.60cm"); // Arrival      (IT: Arrivo)
    writeColumnStyle(xml, "job_5f_stops.C", "2.10cm"); // Departure    (IT: Partenza)
    writeColumnStyle(xml, "job_5f_stops.D", "1.cm");   // Platorm 'Platf' (IT: Binario 'Bin')
    writeColumnStyle(xml, "job_5f_stops.E", "3.00cm"); // Rollingstock (IT: Rotabili)
    writeColumnStyle(xml, "job_5f_stops.F", "2.30cm"); // Crossings
    writeColumnStyle(xml, "job_5f_stops.G", "2.30cm"); // Passings
    writeColumnStyle(xml, "job_5f_stops.H", "3.20cm"); // Description  (IT: Note)

    /* Style: job_5f_stops.A1
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on left, top, bottom sides
     * Padding: 0.030cm all sides except bottom
     * padding-bottom: 0.15cm
     * Vertical Align: middle
     *
     * Usage:
     *  - job_5f_stops table: top left/middle cells (except top right which has H1 style)
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "job_5f_stops.A1");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding-left", "0.030cm");
    xml.writeAttribute("fo:padding-right", "0.030cm");
    xml.writeAttribute("fo:padding-top", "0.030cm");
    xml.writeAttribute("fo:padding-bottom", "0.15cm");
    xml.writeAttribute("fo:border-left", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-right", "none");
    xml.writeAttribute("fo:border-top", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-bottom", "0.05pt solid #000000");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: job_5f_stops.H1
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on all sides
     * Padding: 0.030cm all sides except bottom
     * padding-bottom: 0.15cm
     * Vertical Align: middle
     *
     * Usage:
     *  - job_5f_stops table: top right cell
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "job_5f_stops.H1");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding-left", "0.030cm");
    xml.writeAttribute("fo:padding-right", "0.030cm");
    xml.writeAttribute("fo:padding-top", "0.030cm");
    xml.writeAttribute("fo:padding-bottom", "0.15cm");
    xml.writeAttribute("fo:border", "0.05pt solid #000000");
    xml.writeAttribute("style:vertical-align", "middle");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: job_5f_stops.A2
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on left and bottom sides
     * Padding: 0.049cm all sides
     * Vertical Align: middle
     *
     * Usage:
     *  - job_5f_stops table: right and middle cells from second row to last row
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "job_5f_stops.A2");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border-left", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-right", "none");
    xml.writeAttribute("fo:border-top", "none");
    xml.writeAttribute("fo:border-bottom", "0.05pt solid #000000");
    xml.writeAttribute("style:vertical-align", "middle");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: job_5f_stops.H2
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on left, right and bottom sides
     * Padding: 0.049cm all sides
     * Vertical Align: middle
     *
     * Usage:
     *  - job_5f_stops table: left cells from second row to last row
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "job_5f_stops.H2");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border-left", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-right", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-top", "none");
    xml.writeAttribute("fo:border-bottom", "0.05pt solid #000000");
    xml.writeAttribute("style:vertical-align", "middle");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    // job_5f_asset columns
    writeColumnStyle(xml, "job_5f_asset.A", "3.0cm");
    writeColumnStyle(xml, "job_5f_asset.B", "14.0cm");

    /* Style: job_5f_asset.A1
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on left, top, bottom sides
     * Padding: 0.049cm
     * Vertical Align: middle
     *
     * Usage:
     *  - job_asset table: top left cell
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "job_5f_asset.A1");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border-left", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-right", "none");
    xml.writeAttribute("fo:border-top", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-bottom", "0.05pt solid #000000");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: job_5f_asset.B1
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on all sides
     * Padding: 0.049cm
     * Vertical Align: middle
     *
     * Usage:
     *  - job_asset table: top right cell
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "job_5f_asset.B1");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border", "0.05pt solid #000000");
    xml.writeAttribute("style:vertical-align", "middle");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: job_5f_asset.A2
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on right and bottom sides
     * Padding: 0.049cm
     * Vertical Align: middle
     *
     * Usage:
     *  - job_asset table: bottom left cell
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "job_5f_asset.A2");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border-left", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-right", "none");
    xml.writeAttribute("fo:border-top", "none");
    xml.writeAttribute("fo:border-bottom", "0.05pt solid #000000");
    xml.writeAttribute("style:vertical-align", "middle");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: job_5f_asset.B2
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 all sides except top
     * Padding: 0.049cm
     * Vertical Align: middle
     *
     * Usage:
     *  - job_asset table: bottom left cell
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "job_5f_asset.B2");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border-left", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-right", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-top", "none");
    xml.writeAttribute("fo:border-bottom", "0.05pt solid #000000");
    xml.writeAttribute("style:vertical-align", "middle");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style
}

void JobWriter::writeJobStyles(QXmlStreamWriter &xml)
{
    /* Style: job_5f_summary
     *
     * Type:         table
     * Display name: job_summary
     * Align:        left
     * Width:        8.0cm
     *
     * Usage:
     *  - job_summary table: displays summary information about the job
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table");
    xml.writeAttribute("style:name", "job_5f_summary");
    xml.writeAttribute("style:display-name", "job_summary");
    xml.writeStartElement("style:table-properties");
    xml.writeAttribute("style:shadow", "none");
    xml.writeAttribute("table:align", "left");
    xml.writeAttribute("style:width", "8.0cm");
    xml.writeEndElement(); // style:table-properties
    xml.writeEndElement(); // style

    /* Style: job_5f_summary_cell
     *
     * Type: table-cell
     * Border: none
     * Padding: 0.097cm
     *
     * Usage:
     *  - job_summary table: do not show borders so we fake text layout in a invisible table grid
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "job_5f_summary_cell");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:border", "none");
    xml.writeAttribute("fo:padding", "0.097cm");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: job_5f_stops
     *
     * Type:         table
     * Display name: job_stops
     * Align:        left
     * Width:        16.0cm
     *
     * Usage:
     *  - job_stops table: displays job stops
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table");
    xml.writeAttribute("style:name", "job_5f_stops");
    xml.writeAttribute("style:display-name", "job_stops");
    xml.writeStartElement("style:table-properties");
    xml.writeAttribute("table:align", "left");
    xml.writeAttribute("style:width", "16.0cm");

    xml.writeEndElement(); // style:table-properties
    xml.writeEndElement(); // style

    /* Style: job_5f_asset
     *
     * Type:         table
     * Display name: job_asset
     * Align:        left
     * Width:        16.0cm
     *
     * Usage:
     *  - job_stops table: displays job rollingstock asset summary
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table");
    xml.writeAttribute("style:name", "job_5f_asset");
    xml.writeAttribute("style:display-name", "job_asset");
    xml.writeStartElement("style:table-properties");
    xml.writeAttribute("table:align", "left");
    xml.writeAttribute("style:width", "16.0cm");

    xml.writeEndElement(); // style:table-properties
    xml.writeEndElement(); // style

    /* Style P2
     * type:        paragraph
     * text-align:  start
     * font-size:   16pt
     * font-weight: bold
     *
     * Usages:
     * - job_summary: summary title fields
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "paragraph");
    xml.writeAttribute("style:name", "P2");

    xml.writeStartElement("style:paragraph-properties");
    xml.writeAttribute("fo:text-align", "start");
    xml.writeAttribute("style:justify-single-word", "false");
    xml.writeEndElement(); // style:paragraph-properties

    xml.writeStartElement("style:text-properties");
    xml.writeAttribute("fo:font-size", "16pt");
    xml.writeAttribute("fo:font-weight", "bold");
    xml.writeEndElement(); // style:text-properties

    xml.writeEndElement(); // style:style

    /* Style P3
     * type: paragraph
     * text-align: start
     * font-size: 16pt
     *
     * Description
     *  Like P2 but not bold
     *
     * Usages:
     * - job_summary: summary value fields
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "paragraph");
    xml.writeAttribute("style:name", "P3");

    xml.writeStartElement("style:paragraph-properties");
    xml.writeAttribute("fo:text-align", "start");
    xml.writeAttribute("style:justify-single-word", "false");
    xml.writeEndElement(); // style:paragraph-properties

    xml.writeStartElement("style:text-properties");
    xml.writeAttribute("fo:font-size", "16pt");
    xml.writeEndElement(); // style:text-properties

    xml.writeEndElement(); // style:style

    /* Style P5
     * type: paragraph
     * text-align: center
     * font-size: 12pt
     *
     * Description:
     *  Like P4 but not bold
     *
     * Usages:
     * - job_stops: stop cell text for normal stops and transit
     * Rollingstock/Crossings/Passings/Description
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "paragraph");
    xml.writeAttribute("style:name", "P5");

    xml.writeStartElement("style:paragraph-properties");
    xml.writeAttribute("fo:text-align", "center");
    xml.writeAttribute("style:justify-single-word", "false");
    xml.writeEndElement(); // style:paragraph-properties

    xml.writeStartElement("style:text-properties");
    xml.writeAttribute("fo:font-size", "12pt");
    xml.writeEndElement(); // style:text-properties

    xml.writeEndElement(); // style:style

    /* Style P6
     * type: paragraph
     * text-align: center
     * font-size: 12pt
     * font-style: italic
     *
     * Description:
     *  Like P5 but Italic
     *  (P4 + Italic, not bold)
     *
     * Usages:
     * - job_stops: stop cell text for transit stops except for
     * Rollingstock/Crossings/Passings/Description columns which have P5
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "paragraph");
    xml.writeAttribute("style:name", "P6");

    xml.writeStartElement("style:paragraph-properties");
    xml.writeAttribute("fo:text-align", "center");
    xml.writeAttribute("style:justify-single-word", "false");
    xml.writeEndElement(); // style:paragraph-properties

    xml.writeStartElement("style:text-properties");
    xml.writeAttribute("fo:font-size", "12pt");
    xml.writeAttribute("fo:font-style", "italic");
    xml.writeEndElement(); // style:text-properties

    xml.writeEndElement(); // style:style

    // stile interruzione di pagina
    // TODO: quando useremo 'Page master style' vedere se vanno in conflitto
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "paragraph");
    xml.writeAttribute("style:name", "interruzione");

    xml.writeStartElement("style:paragraph-properties");
    xml.writeAttribute("fo:text-align", "start");
    xml.writeAttribute("fo:break-after", "page");
    xml.writeEndElement(); // style:paragraph-properties

    xml.writeStartElement("style:text-properties");
    xml.writeAttribute("fo:font-size", "1pt");
    xml.writeEndElement(); // style:text-properties

    xml.writeEndElement(); // style:style
}

void JobWriter::writeJob(QXmlStreamWriter &xml, db_id jobId, JobCategory jobCat)
{
    query q_getRSInfo(mDb, "SELECT rs_list.number,rs_models.name,rs_models.suffix,rs_models.type"
                           " FROM rs_list"
                           " LEFT JOIN rs_models ON rs_models.id=rs_list.model_id"
                           " WHERE rs_list.id=?");

    QList<QPair<QString, QList<db_id>>> stopsRS;

    // Title
    xml.writeStartElement("text:p");
    xml.writeAttribute("text:style-name", "P1");
    xml.writeCharacters(JobCategoryName::jobNameSpaced(jobId, jobCat));
    xml.writeEndElement();

    // Vertical space
    xml.writeStartElement("text:p");
    xml.writeAttribute("text:style-name", "P1");
    xml.writeEndElement();

    db_id firstStopId = 0;
    db_id lastStopId  = 0;

    QTime start, end;
    QString fromStation, toStation;
    int axesCount = 0;

    // Job summary
    q_getFirstStop.bind(1, jobId);
    if (q_getFirstStop.step() == SQLITE_ROW
        && q_getFirstStop.getRows().column_type(0) != SQLITE_NULL)
    {
        auto r      = q_getFirstStop.getRows();

        firstStopId = r.get<db_id>(0);
        fromStation = r.get<QString>(1);
        start       = r.get<QTime>(2);

        q_initialJobAxes.bind(1, firstStopId);
        q_initialJobAxes.step();
        axesCount = q_initialJobAxes.getRows().get<int>(0);
        q_initialJobAxes.reset();
    }
    q_getFirstStop.reset();

    q_getLastStop.bind(1, jobId);
    if (q_getLastStop.step() == SQLITE_ROW && q_getLastStop.getRows().column_type(0) != SQLITE_NULL)
    {
        auto r     = q_getLastStop.getRows();

        lastStopId = r.get<db_id>(0);
        toStation  = r.get<QString>(1);
        end        = r.get<QTime>(2);
    }
    q_getLastStop.reset();

    if (firstStopId && lastStopId)
    {
        writeJobSummary(xml, fromStation, start.toString("HH:mm"), toStation, end.toString("HH:mm"),
                        axesCount);
    }
    else
    {
        qDebug() << "Error: getting first/last stations names FAILED\n"
                 << mDb.error_code() << mDb.error_msg();
        const QString err = QLatin1String("err");
        writeJobSummary(xml, err, err, err, err, 0);
    }

    // Vertical space
    xml.writeStartElement("text:p");
    xml.writeAttribute("text:style-name", "P1");
    xml.writeEndElement();

    // Table 'job_stops'
    xml.writeStartElement("table:table");
    xml.writeAttribute("table:name", "job_stops");
    xml.writeAttribute("table:style-name", "job_5f_stops");

    xml.writeEmptyElement("table:table-column"); // Station
    xml.writeAttribute("table:style-name", "job_5f_stops.A");

    xml.writeEmptyElement("table:table-column"); // Arrival
    xml.writeAttribute("table:style-name", "job_5f_stops.B");

    xml.writeEmptyElement("table:table-column"); // Departure
    xml.writeAttribute("table:style-name", "job_5f_stops.C");

    xml.writeEmptyElement("table:table-column"); // Platform (Platf)
    xml.writeAttribute("table:style-name", "job_5f_stops.D");

    xml.writeEmptyElement("table:table-column"); // Rollingstock
    xml.writeAttribute("table:style-name", "job_5f_stops.E");

    xml.writeEmptyElement("table:table-column"); // Crossings
    xml.writeAttribute("table:style-name", "job_5f_stops.F");

    xml.writeEmptyElement("table:table-column"); // Passings
    xml.writeAttribute("table:style-name", "job_5f_stops.G");

    xml.writeEmptyElement("table:table-column"); // Description
    xml.writeAttribute("table:style-name", "job_5f_stops.H");

    // Row 1 (Heading)
    xml.writeStartElement("table:table-header-rows");
    xml.writeStartElement("table:table-row");

    const QString P4_Style = "P4";
    writeCell(xml, "job_5f_stops.A1", P4_Style, Odt::text(Odt::station));
    writeCell(xml, "job_5f_stops.A1", P4_Style, Odt::text(Odt::arrival));
    writeCell(xml, "job_5f_stops.A1", P4_Style, Odt::text(Odt::departure));
    writeCell(xml, "job_5f_stops.A1", P4_Style, Odt::text(Odt::jobStopPlatf));
    writeCell(xml, "job_5f_stops.A1", P4_Style, Odt::text(Odt::rollingstock));
    writeCell(xml, "job_5f_stops.A1", P4_Style, Odt::text(Odt::jobStopCross));
    writeCell(xml, "job_5f_stops.A1", P4_Style, Odt::text(Odt::jobStopPassings));
    writeCell(xml, "job_5f_stops.H1", P4_Style, Odt::text(Odt::notes)); // Description

    xml.writeEndElement(); // end of row
    xml.writeEndElement(); // header section

    QList<db_id> rsAsset;

    const QString P5_style = "P5";

    // Fill stops table
    q_getJobStops.bind(1, jobId);
    for (auto stop : q_getJobStops)
    {
        db_id stopId        = stop.get<db_id>(0);
        db_id stationId     = stop.get<db_id>(1);
        QString stationName = stop.get<QString>(2);
        QTime arr           = stop.get<QTime>(3);
        QTime dep           = stop.get<QTime>(4);
        const int stopType  = stop.get<int>(5);
        QString descr       = stop.get<QString>(6);

        QString trackName   = stop.get<QString>(7);
        if (trackName.isEmpty())
            trackName = stop.get<QString>(8); // Use out gate to get track name

        utils::Side entranceSide = utils::Side(stop.get<int>(9));
        utils::Side exitSide     = utils::Side(stop.get<int>(10));

        if (entranceSide == exitSide && stop.column_type(9) != SQLITE_NULL
            && stop.column_type(10) != SQLITE_NULL)
        {
            // Train enters and leaves from same track side, add reversal to description
            QString descr2 = Odt::text(Odt::jobReverseDirection);
            if (!descr.isEmpty())
                descr2.append('\n'); // Separate from manually set description
            descr2.append(descr);
            descr = descr2;
        }

        const bool isTransit = stopType == 1;

        qDebug() << "(Loop) Job:" << jobId << "Stop:" << stopId;

        xml.writeStartElement("table:table-row"); // start new row

        const QString styleName = isTransit ? "P6" : P5_style; // If it's transit use italic style

        // Station
        writeCell(xml, "job_5f_stops.A2", styleName, stationName);

        // Arrival
        writeCell(xml, "job_5f_stops.A2", styleName,
                  stopId == firstStopId ? QString() : arr.toString("HH:mm"));

        // Departure
        // If it's transit then and arrival is equal to departure (should be always but if is
        // different show both to warn user about the error) then show only arrival
        writeCell(xml, "job_5f_stops.A2", styleName,
                  (stopId == lastStopId || (isTransit && arr == dep)) ? QString()
                                                                      : dep.toString("HH:mm"));

        // Platform
        writeCell(xml, "job_5f_stops.A2", styleName, trackName);

        // Rollingstock
        sqlite3_stmt *stmt = q_getStopCouplings.stmt();
        writeCellListStart(xml, "job_5f_stops.A2", P5_style);

        // Coupled rollingstock
        bool firstCoupRow = true;
        q_getStopCouplings.bind(1, stopId);
        q_getStopCouplings.bind(2, int(RsOp::Coupled));
        for (auto coup : q_getStopCouplings)
        {
            db_id rsId = coup.get<db_id>(0);
            rsAsset.append(rsId);

            int number              = coup.get<int>(1);
            int modelNameLen        = sqlite3_column_bytes(stmt, 2);
            const char *modelName   = reinterpret_cast<char const *>(sqlite3_column_text(stmt, 2));

            int modelSuffixLen      = sqlite3_column_bytes(stmt, 3);
            const char *modelSuffix = reinterpret_cast<char const *>(sqlite3_column_text(stmt, 3));
            RsType type             = RsType(sqlite3_column_int(stmt, 4));

            const QString rsName    = rs_utils::formatNameRef(modelName, modelNameLen, number,
                                                              modelSuffix, modelSuffixLen, type);

            if (firstCoupRow)
            {
                firstCoupRow = false;
                // Use bold font
                xml.writeStartElement("text:span");
                xml.writeAttribute("text:style-name", "T1");
                xml.writeCharacters(Odt::text(Odt::CoupledAbbr));
                xml.writeEndElement(); // test:span
            }

            xml.writeEmptyElement("text:line-break");
            xml.writeCharacters(rsName);
        }
        q_getStopCouplings.reset();

        // Unoupled rollingstock
        bool firstUncoupRow = true;
        q_getStopCouplings.bind(1, stopId);
        q_getStopCouplings.bind(2, int(RsOp::Uncoupled));
        for (auto coup : q_getStopCouplings)
        {
            db_id rsId = coup.get<db_id>(0);
            rsAsset.removeAll(rsId);

            int number              = coup.get<int>(1);
            int modelNameLen        = sqlite3_column_bytes(stmt, 2);
            const char *modelName   = reinterpret_cast<char const *>(sqlite3_column_text(stmt, 2));

            int modelSuffixLen      = sqlite3_column_bytes(stmt, 3);
            const char *modelSuffix = reinterpret_cast<char const *>(sqlite3_column_text(stmt, 3));
            RsType type             = RsType(sqlite3_column_int(stmt, 4));

            const QString rsName    = rs_utils::formatNameRef(modelName, modelNameLen, number,
                                                              modelSuffix, modelSuffixLen, type);

            if (firstUncoupRow)
            {
                if (!firstCoupRow) // Not first row, there were coupled rs
                    xml.writeEmptyElement("text:line-break"); // Separate from coupled
                firstUncoupRow = false;
                // Use bold font
                xml.writeStartElement("text:span");
                xml.writeAttribute("text:style-name", "T1");
                xml.writeCharacters(Odt::text(Odt::UncoupledAbbr));
                xml.writeEndElement(); // test:span
            }

            xml.writeEmptyElement("text:line-break");
            xml.writeCharacters(rsName);
        }
        q_getStopCouplings.reset();
        writeCellListEnd(xml);

        stopsRS.append({stationName, rsAsset});

        // Crossings / Passings

        JobStopDirectionHelper dirHelper(mDb);

        utils::Side myDir = dirHelper.getStopOutSide(stopId);

        QVector<JobEntry> passings;

        q_selectPassings.bind(1, stationId);
        q_selectPassings.bind(2, arr);
        q_selectPassings.bind(3, dep);
        q_selectPassings.bind(4, jobId);

        // Incroci
        firstCoupRow = true;
        writeCellListStart(xml, "job_5f_stops.A2", P5_style);
        for (auto pass : q_selectPassings)
        {
            db_id otherStopId       = pass.get<db_id>(0);
            db_id otherJobId        = pass.get<db_id>(1);
            JobCategory otherJobCat = JobCategory(pass.get<int>(2));

            // QTime otherArr = pass.get<QTime>(3);
            // QTime otherDep = pass.get<QTime>(4);

            utils::Side otherDir = dirHelper.getStopOutSide(otherStopId);

            if (myDir == otherDir)
                passings.append({otherJobId, otherJobCat});
            else
            {
                if (firstCoupRow)
                    firstCoupRow = false;
                else
                    xml.writeEmptyElement("text:line-break");
                xml.writeCharacters(JobCategoryName::jobName(otherJobId, otherJobCat));
            }
        }
        q_selectPassings.reset();
        writeCellListEnd(xml);

        // Passings
        firstCoupRow = true;
        writeCellListStart(xml, "job_5f_stops.A2", P5_style);
        for (auto entry : passings)
        {
            if (firstCoupRow)
                firstCoupRow = false;
            else
                xml.writeEmptyElement("text:line-break");
            xml.writeCharacters(JobCategoryName::jobName(entry.jobId, entry.category));
        }
        writeCellListEnd(xml);

        // Description
        writeCellListStart(xml, "job_5f_stops.H2", P5_style);
        if (!descr.isEmpty())
        {
            // Split in lines
            int lastIdx = 0;
            while (true)
            {
                int idx      = descr.indexOf('\n', lastIdx);
                QString line = descr.mid(lastIdx, idx == -1 ? idx : idx - lastIdx);
                xml.writeCharacters(line.simplified());
                if (idx < 0)
                    break; // Last line
                lastIdx = idx + 1;
                xml.writeEmptyElement("text:line-break");
            }
        }
        writeCellListEnd(xml);

        xml.writeEndElement(); // end of row
    }
    q_getJobStops.reset();

    xml.writeEndElement(); // table:table END

    // text:p as separator
    xml.writeStartElement("text:p");
    xml.writeAttribute("text:style-name", "P1");
    xml.writeEndElement();

    // Table 'job_asset'
    xml.writeStartElement("table:table");
    xml.writeAttribute("table:name", "job_asset");
    xml.writeAttribute("table:style-name", "job_5f_asset");

    xml.writeEmptyElement("table:table-column"); // Stazione
    xml.writeAttribute("table:style-name", "job_5f_asset.A");

    xml.writeEmptyElement("table:table-column"); // Assetto
    xml.writeAttribute("table:style-name", "job_5f_asset.B");

    // Duplicate second-last asset to last stop because last stop would be always empty
    if (stopsRS.size() >= 2)
    {
        int i                 = stopsRS.size() - 2; // Get second-last (IT: penultima fermata)
        stopsRS[i + 1].second = stopsRS[i].second;
    }
    else
    {
        // Error!
        qWarning() << __FUNCTION__ << "At least 2 stops required!";
    }

    bool firstRow = true;
    for (auto &s : qAsConst(stopsRS))
    {
        xml.writeStartElement("table:table-row"); // start new row

        writeCell(xml, firstRow ? "job_5f_asset.A1" : "job_5f_asset.A2", P5_style, s.first);

        writeCellListStart(xml, firstRow ? "job_5f_asset.B1" : "job_5f_asset.B2", P5_style);
        for (int i = 0; i < s.second.size(); i++)
        {
            q_getRSInfo.reset();
            q_getRSInfo.bind(1, s.second.at(i));
            int ret = q_getRSInfo.step();
            if (ret != SQLITE_ROW)
            {
                // Error: RS does not exist!
                continue;
            }

            sqlite3_stmt *stmt      = q_getRSInfo.stmt();
            int number              = sqlite3_column_int(stmt, 0);
            int modelNameLen        = sqlite3_column_bytes(stmt, 1);
            const char *modelName   = reinterpret_cast<char const *>(sqlite3_column_text(stmt, 1));

            int modelSuffixLen      = sqlite3_column_bytes(stmt, 2);
            const char *modelSuffix = reinterpret_cast<char const *>(sqlite3_column_text(stmt, 2));
            RsType type             = RsType(sqlite3_column_int(stmt, 3));

            const QString name      = rs_utils::formatNameRef(modelName, modelNameLen, number,
                                                              modelSuffix, modelSuffixLen, type);

            xml.writeCharacters(name);
            if (i < s.second.size() - 1)
                xml.writeCharacters(" + ");
        }
        writeCellListEnd(xml);

        xml.writeEndElement(); // end of row

        if (firstRow)
            firstRow = false;
    }

    xml.writeEndElement();

    // Interruzione pagina TODO: see style 'interruzione'
    xml.writeStartElement("text:p");
    xml.writeAttribute("text:style-name", "interruzione");
    xml.writeEndElement();
}
