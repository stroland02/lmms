/*
 * TrackGrip.cpp - Grip that can be used to move tracks
 *
 * Copyright (c) 2024- Michael Gregorius
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#include "TrackGrip.h"

#include "embed.h"
#include "Track.h"

#include <QPainter>
#include <QMouseEvent>


namespace lmms::gui
{

constexpr int c_gripWidth = 14;
constexpr qreal c_dotRadius = 1.5;
constexpr int c_dotSpacingY = 5;
constexpr int c_dotSpacingX = 5;

TrackGrip::TrackGrip(Track* track, QWidget* parent) :
	QWidget(parent),
	m_track(track)
{
	setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
	setCursor(Qt::OpenHandCursor);
	setFixedWidth(c_gripWidth);
}

void TrackGrip::mousePressEvent(QMouseEvent* m)
{
	m->accept();

	m_isGrabbed = true;
	setCursor(Qt::ClosedHandCursor);

	emit grabbed();

	update();
}

void TrackGrip::mouseReleaseEvent(QMouseEvent* m)
{
	m->accept();

	m_isGrabbed = false;
	setCursor(Qt::OpenHandCursor);

	emit released();

	update();
}

void TrackGrip::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);

	// Determine background color
	const auto trackColor = m_track->color();
	const auto muted = m_track->getMutedModel()->value();

	QColor bg;
	if (trackColor.has_value() && !muted)
	{
		bg = trackColor.value();
		p.fillRect(rect(), bg);
	}
	else
	{
		// Use parent widget's palette background
		bg = palette().color(QPalette::Window);
	}

	// Check luminance to pick dot color: light bg → dark dots, dark bg → light dots
	const qreal luminance = 0.299 * bg.redF() + 0.587 * bg.greenF() + 0.114 * bg.blueF();
	const QColor dotColor = luminance > 0.5 ? QColor(0x88, 0x88, 0x88) : QColor(0xcc, 0xcc, 0xcc);

	// Slightly dim dots when grabbed
	QColor drawColor = dotColor;
	if (m_isGrabbed)
	{
		drawColor.setAlphaF(0.6f);
	}

	p.setPen(Qt::NoPen);
	p.setBrush(drawColor);

	// Draw 2-column dot grid centered in the widget
	const int cx = width() / 2;
	const int col1 = cx - c_dotSpacingX / 2;
	const int col2 = cx + c_dotSpacingX / 2;

	const int startY = 6;
	const int endY = height() - 4;

	for (int y = startY; y < endY; y += c_dotSpacingY)
	{
		p.drawEllipse(QPointF(col1, y), c_dotRadius, c_dotRadius);
		p.drawEllipse(QPointF(col2, y), c_dotRadius, c_dotRadius);
	}
}

} // namespace lmms::gui
