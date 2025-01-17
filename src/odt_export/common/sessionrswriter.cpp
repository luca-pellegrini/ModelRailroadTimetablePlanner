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

#include "sessionrswriter.h"

#include "utils/jobcategorystrings.h"
#include "utils/rs_utils.h"
#include "odtutils.h"

#include <QXmlStreamWriter>

SessionRSWriter::SessionRSWriter(database &db, SessionRSMode mode, SessionRSOrder order) :
    lastParentId(0),
    mDb(db),
    q_getSessionRS(mDb),
    q_getParentName(mDb),
    m_mode(mode),
    m_order(order)
{
    // TODO: fetch departure instead of arrival for start session
    const auto sql = QStringLiteral(
      "SELECT %1,"
      " %2, %3, %4,"
      " rs_list.id, rs_list.number, rs_models.name, rs_models.suffix, rs_models.type,"
      " t1.name,t2.name,"
      " stops.job_id, jobs.category, coupling.operation"
      " FROM rs_list"
      " JOIN coupling ON coupling.rs_id=rs_list.id"
      " JOIN stops ON stops.id=coupling.stop_id"
      " JOIN jobs ON jobs.id=stops.job_id"
      " JOIN rs_models ON rs_models.id=rs_list.model_id"
      " LEFT JOIN station_gate_connections g1 ON g1.id=stops.in_gate_conn"
      " LEFT JOIN station_gate_connections g2 ON g2.id=stops.out_gate_conn"
      " LEFT JOIN station_tracks t1 ON t1.id=g1.track_id"
      " LEFT JOIN station_tracks t2 ON t2.id=g2.track_id"
      " JOIN %5"
      " GROUP BY rs_list.id"
      " ORDER BY %6, stops.arrival, stops.job_id, rs_list.model_id");

    QString temp = sql.arg(m_mode == SessionRSMode::StartOfSession ? "MIN(stops.arrival)"
                                                                   : "MAX(stops.departure)");
    if (m_order == SessionRSOrder::ByStation)
    {
        temp = temp.arg("rs_list.owner_id", "rs_owners.name", "stops.station_id",
                        "rs_owners ON rs_owners.id=rs_list.owner_id", "stops.station_id");

        q_getParentName.prepare("SELECT name FROM stations WHERE id=?");
    }
    else
    {
        temp = temp.arg("stops.station_id", "stations.name", "rs_list.owner_id",
                        "stations ON stations.id=stops.station_id", "rs_list.owner_id");

        q_getParentName.prepare("SELECT name FROM rs_owners WHERE id=?");
    }

    QByteArray query = temp.toUtf8();

    q_getSessionRS.prepare(query.constData());
}

void SessionRSWriter::writeStyles(QXmlStreamWriter &xml)
{
    /* Style P5           FIXME: merge with JobWriter and StationWriter
     * type: paragraph
     * text-align: center
     * font-size: 12pt
     * font-name:   Liberation Sans
     *
     * Description:
     *  Like P4 but not bold, and Sans Serif
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
    xml.writeAttribute("style:font-name", "Liberation Sans");
    xml.writeAttribute("fo:font-size", "12pt");
    xml.writeEndElement(); // style:text-properties

    xml.writeEndElement(); // style:style

    // rs_table Table
    /* Style: rs_5f_table
     *
     * Type:         table
     * Display name: rollingstock
     * Align:        left
     * Width:        16.0cm
     *
     * Usage:
     *  - SessionRSWriter: main table for Rollingstock Owners/Stations
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table");
    xml.writeAttribute("style:name", "rs_5f_table");
    xml.writeAttribute("style:display-name", "rollingstock");
    xml.writeStartElement("style:table-properties");
    xml.writeAttribute("style:shadow", "none");
    xml.writeAttribute("table:align", "left");
    xml.writeAttribute("style:width", "16.0cm");
    xml.writeEndElement(); // style:table-properties
    xml.writeEndElement(); // style

    // rs_table columns
    writeColumnStyle(xml, "rs_5f_table.A", "3.00cm"); // RS Name
    writeColumnStyle(xml, "rs_5f_table.B", "4.45cm"); // Job
    writeColumnStyle(xml, "rs_5f_table.C", "2.21cm"); // Platf
    writeColumnStyle(xml, "rs_5f_table.D", "3.17cm"); // Departure or Arrival
    writeColumnStyle(xml, "rs_5f_table.E", "4.00cm"); // Station or Owner

    /* Style: rs_5f_table.A1
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on left, top, bottom sides
     * Padding: 0.049cm all sides
     *
     * Usage:
     *  - rs_5f_table table: top left/middle cells (except top right which has E1 style)
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "rs_5f_table.A1");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border-left", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-right", "none");
    xml.writeAttribute("fo:border-top", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-bottom", "0.05pt solid #000000");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: rs_5f_table.E1
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on all sides
     * Padding: 0.049cm all sides
     *
     * Usage:
     *  - rs_5f_table table: top right cell
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "rs_5f_table.E1");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border", "0.05pt solid #000000");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: rs_5f_table.A2
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on left and bottom sides
     * Padding: 0.049cm all sides
     *
     * Usage:
     *  - rs_5f_table table: right and middle cells from second row to last row
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "rs_5f_table.A2");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border-left", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-right", "none");
    xml.writeAttribute("fo:border-top", "none");
    xml.writeAttribute("fo:border-bottom", "0.05pt solid #000000");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style

    /* Style: rs_5f_table.E2
     *
     * Type: table-cell
     * Border: 0.05pt solid #000000 on left, right and bottom sides
     * Padding: 0.049cm all sides
     *
     * Usage:
     *  - rs_5f_table table: left cells from second row to last row
     */
    xml.writeStartElement("style:style");
    xml.writeAttribute("style:family", "table-cell");
    xml.writeAttribute("style:name", "rs_5f_table.E2");

    xml.writeStartElement("style:table-cell-properties");
    xml.writeAttribute("fo:padding", "0.049cm");
    xml.writeAttribute("fo:border-left", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-right", "0.05pt solid #000000");
    xml.writeAttribute("fo:border-top", "none");
    xml.writeAttribute("fo:border-bottom", "0.05pt solid #000000");
    xml.writeEndElement(); // style:table-cell-properties
    xml.writeEndElement(); // style
}

db_id SessionRSWriter::writeTable(QXmlStreamWriter &xml, const QString &parentName)
{
    // Table '???_table' where ??? is the station/owner name without spaces

    QString tableName = parentName;
    tableName.replace(' ', '_'); // Replace spaces with underscores
    tableName.append("_table");

    xml.writeStartElement("table:table");
    xml.writeAttribute("table:name", tableName);
    xml.writeAttribute("table:style-name", "rs_5f_table");

    // Columns
    xml.writeEmptyElement("table:table-column"); // A - RS Name
    xml.writeAttribute("table:style-name", "rs_5f_table.A");

    xml.writeEmptyElement("table:table-column"); // B - Job
    xml.writeAttribute("table:style-name", "rs_5f_table.B");

    xml.writeEmptyElement("table:table-column"); // C - Platf
    xml.writeAttribute("table:style-name", "rs_5f_table.C");

    xml.writeEmptyElement("table:table-column"); // D - Departure
    xml.writeAttribute("table:style-name", "rs_5f_table.D");

    xml.writeEmptyElement("table:table-column"); // E - Station or Owner
    xml.writeAttribute("table:style-name", "rs_5f_table.E");

    // Row 1 (Heading)
    xml.writeStartElement("table:table-header-rows");
    xml.writeStartElement("table:table-row");

    const QString P4_style = QStringLiteral("P4");
    const QString P5_style = QStringLiteral("P5");
    // Cells (column names, headings)
    writeCell(xml, "rs_5f_table.A1", P4_style, Odt::text(Odt::rollingstock));
    writeCell(xml, "rs_5f_table.A1", P4_style, Odt::text(Odt::jobNr));
    writeCell(xml, "rs_5f_table.A1", P4_style, Odt::text(Odt::jobStopPlatf));
    writeCell(xml, "rs_5f_table.A1", P4_style,
              Odt::text(m_mode == SessionRSMode::StartOfSession ? Odt::departure : Odt::arrival));
    writeCell(xml, "rs_5f_table.E1", P4_style,
              Odt::text(m_order == SessionRSOrder::ByStation ? Odt::genericRSOwner : Odt::station));

    xml.writeEndElement(); // end of row
    xml.writeEndElement(); // header section

    // Fill the table
    for (; it != q_getSessionRS.end(); ++it)
    {
        auto rs    = *it;
        QTime time = rs.get<QTime>(0); // Departure or arrival

        // db_id stationOrOwnerId = rs.get<db_id>(1);
        QString name   = rs.get<QString>(2); // Name of the station or owner
        db_id parentId = rs.get<db_id>(3);   // ownerOrStation (opposite of stationOrOwner)

        // db_id rsId = rs.get<db_id>(4);
        int number              = rs.get<int>(5);
        sqlite3_stmt *stmt      = q_getSessionRS.stmt();
        int modelNameLen        = sqlite3_column_bytes(stmt, 6);
        const char *modelName   = reinterpret_cast<char const *>(sqlite3_column_text(stmt, 6));

        int modelSuffixLen      = sqlite3_column_bytes(stmt, 7);
        const char *modelSuffix = reinterpret_cast<char const *>(sqlite3_column_text(stmt, 7));
        RsType type             = RsType(rs.get<int>(8));

        QString rsName   = rs_utils::formatNameRef(modelName, modelNameLen, number, modelSuffix,
                                                   modelSuffixLen, type);

        QString platform = rs.get<QString>(9);
        if (platform.isEmpty())
            platform = rs.get<QString>(10); // Use out gate to get track name

        db_id jobId        = rs.get<db_id>(11);
        JobCategory jobCat = JobCategory(rs.get<int>(12));

        if (parentId != lastParentId)
        {
            xml.writeEndElement(); // table:table
            return parentId;
        }

        xml.writeStartElement("table:table-row"); // start new row

        writeCell(xml, "rs_5f_table.A2", P5_style, rsName);
        writeCell(xml, "rs_5f_table.A2", P5_style, JobCategoryName::jobName(jobId, jobCat));
        writeCell(xml, "rs_5f_table.A2", P5_style, platform);
        writeCell(xml, "rs_5f_table.A2", P5_style, time.toString("HH:mm"));
        writeCell(xml, "rs_5f_table.E2", P5_style, name);

        xml.writeEndElement(); // end of row
    }

    xml.writeEndElement(); // table:table

    return 0; // End of document, no more tables
}

void SessionRSWriter::writeContent(QXmlStreamWriter &xml)
{
    it = q_getSessionRS.begin();
    if (it == q_getSessionRS.end())
        return;

    lastParentId = (*it).get<db_id>(3);
    while (lastParentId)
    {
        q_getParentName.bind(1, lastParentId);
        q_getParentName.step();
        QString name = q_getParentName.getRows().get<QString>(0);
        q_getParentName.reset();

        // Write Station or Rollingstock Owner name
        xml.writeStartElement("text:p");
        xml.writeAttribute("text:style-name", "P1");
        xml.writeCharacters(name);
        xml.writeEndElement();

        lastParentId = writeTable(xml, name);

        // Add some space
        xml.writeStartElement("text:p");
        xml.writeAttribute("text:style-name", "P1");
        xml.writeEndElement();
    }
}

QString SessionRSWriter::generateTitle() const
{
    QString title =
      Odt::text(Odt::rsSessionTitle)
        .arg(Odt::text(m_order == SessionRSOrder::ByStation ? Odt::genericRSOwner : Odt::station),
             Odt::text(m_mode == SessionRSMode::StartOfSession ? Odt::rsSessionStart
                                                               : Odt::rsSessionEnd));
    return title;
}
