/*
 * SampleThumbnail.cpp
 *
 * Copyright (c) 2024 Khoi Dau <casboi86@gmail.com>
 * Copyright (c) 2024 Sotonye Atemie <sakertooth@gmail.com>
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

#include "SampleThumbnail.h"

#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>

#include "Sample.h"

namespace {
	constexpr auto MaxSampleThumbnailCacheSize = 32;
	constexpr auto AggregationPerZoomStep = 10;
}

namespace lmms {

SampleThumbnail::Thumbnail::Thumbnail(std::vector<Peak> peaks, double samplesPerPeak)
	: m_peaks(std::move(peaks))
	, m_samplesPerPeak(samplesPerPeak)
{
}

SampleThumbnail::Thumbnail::Thumbnail(const float* buffer, size_t size, size_t width)
	: m_peaks(width)
	, m_samplesPerPeak(std::max(static_cast<double>(size) / width, 1.0))
{
	for (auto peakIndex = std::size_t{0}; peakIndex < width; ++peakIndex)
	{
		const auto beginSample = buffer + static_cast<size_t>(std::floor(peakIndex * m_samplesPerPeak));
		const auto endSample = buffer + static_cast<size_t>(std::ceil((peakIndex + 1) * m_samplesPerPeak));
		const auto [min, max] = std::minmax_element(beginSample, endSample);
		m_peaks[peakIndex] = Peak{*min, *max};
	}
}

SampleThumbnail::Thumbnail SampleThumbnail::Thumbnail::zoomOut(float factor) const
{
	assert(factor >= 1 && "Invalid zoom out factor");

	auto peaks = std::vector<Peak>(m_peaks.size() / factor);
	for (auto peakIndex = std::size_t{0}; peakIndex < peaks.size(); ++peakIndex)
	{
		const auto beginAggregationAt = m_peaks.begin() + static_cast<size_t>(std::floor(peakIndex * factor));
		const auto endAggregationAt = m_peaks.begin() + static_cast<size_t>(std::ceil((peakIndex + 1) * factor));
		peaks[peakIndex] = std::accumulate(beginAggregationAt, endAggregationAt, Peak{});
	}

	return Thumbnail{std::move(peaks), m_samplesPerPeak * factor};
}

SampleThumbnail::SampleThumbnail(const Sample& sample)
	: m_buffer(sample.buffer())
{
	auto entry = SampleThumbnailEntry{sample.sampleFile(), QFileInfo{sample.sampleFile()}.lastModified()};
	if (!entry.filePath.isEmpty())
	{
		const auto it = s_sampleThumbnailCacheMap.find(entry);
		if (it != s_sampleThumbnailCacheMap.end())
		{
			m_thumbnailCache = it->second;
			return;
		}

		if (s_sampleThumbnailCacheMap.size() == MaxSampleThumbnailCacheSize)
		{
			const auto leastUsed = std::min_element(s_sampleThumbnailCacheMap.begin(), s_sampleThumbnailCacheMap.end(),
				[](const auto& a, const auto& b) { return a.second.use_count() < b.second.use_count(); });
			s_sampleThumbnailCacheMap.erase(leastUsed->first);
		}

		s_sampleThumbnailCacheMap[std::move(entry)] = m_thumbnailCache;
	}

	const auto flatBuffer = m_buffer->data()->data();
	const auto flatBufferSize = m_buffer->size() * DEFAULT_CHANNELS;
	m_thumbnailCache->emplace_back(flatBuffer, flatBufferSize, flatBufferSize / AggregationPerZoomStep);

	while (m_thumbnailCache->back().width() >= AggregationPerZoomStep)
	{
		auto zoomedOutThumbnail = m_thumbnailCache->back().zoomOut(AggregationPerZoomStep);
		m_thumbnailCache->emplace_back(std::move(zoomedOutThumbnail));
	}
}

void SampleThumbnail::visualize(VisualizeParameters parameters, QPainter& painter) const
{
	const auto& sampleRect = parameters.sampleRect;
	const auto& viewportRect = parameters.viewportRect.isNull() ? sampleRect : parameters.viewportRect;

	const auto renderRect = sampleRect.intersected(viewportRect);
	if (renderRect.isNull()) { return; }

	const auto sampleRange = parameters.sampleEnd - parameters.sampleStart;
	if (sampleRange <= 0.0f || sampleRange > 1.0f) { return; }

	const auto targetThumbnailWidth = static_cast<int>(sampleRect.width() / sampleRange);
	const auto finerThumbnail = std::find_if(m_thumbnailCache->rbegin(), m_thumbnailCache->rend(),
		[&](const auto& thumbnail) { return thumbnail.width() >= targetThumbnailWidth; });

	const auto useOriginalBuffer = finerThumbnail == m_thumbnailCache->rend();
	const auto drawOriginalBuffer = static_cast<size_t>(targetThumbnailWidth) == m_buffer->size();

	painter.save();
	painter.setRenderHint(QPainter::Antialiasing, true);

	const auto thumbnailBeginForward = std::max<int>(renderRect.x() - sampleRect.x(), parameters.sampleStart * targetThumbnailWidth);
	const auto thumbnailEndForward = std::max<int>(renderRect.x() + renderRect.width() - sampleRect.x(), parameters.sampleEnd * targetThumbnailWidth);
	const auto thumbnailBegin = parameters.reversed ? targetThumbnailWidth - thumbnailBeginForward - 1 : thumbnailBeginForward;
	const auto thumbnailEnd = parameters.reversed ? targetThumbnailWidth - thumbnailEndForward : thumbnailEndForward;
	const auto advanceThumbnailBy = parameters.reversed ? -1 : 1;

	const auto finerThumbnailWidth = useOriginalBuffer ? m_buffer->size() : finerThumbnail->width();
	const auto finerThumbnailScaleFactor = static_cast<double>(finerThumbnailWidth) / targetThumbnailWidth;
	const auto yScale = renderRect.height() / 2 * parameters.amplification;
	const auto centerY = static_cast<qreal>(renderRect.center().y());

	// Collect peak data into paths
	QPainterPath topPath;
	QPainterPath bottomPath;
	bool firstPoint = true;

	for (auto x = renderRect.x(), i = thumbnailBegin; x < renderRect.x() + renderRect.width() && i != thumbnailEnd;
		++x, i += advanceThumbnailBy)
	{
		qreal yMin, yMax;

		if (useOriginalBuffer && drawOriginalBuffer)
		{
			const auto value = m_buffer->data()->data()[i];
			yMin = centerY - value * yScale;
			yMax = yMin;
		}
		else
		{
			const auto beginIndex = std::clamp<size_t>(std::floor(i * finerThumbnailScaleFactor), 0, finerThumbnail->width() - 1);
			const auto endIndex = std::clamp<size_t>(std::ceil((i + 1) * finerThumbnailScaleFactor), 0, finerThumbnail->width() - 1);

			auto minPeak = 0.f;
			auto maxPeak = 0.f;

			if (useOriginalBuffer)
			{
				const auto flatBuffer = m_buffer->data()->data();
				const auto [min, max] = std::minmax_element(flatBuffer + beginIndex, flatBuffer + endIndex);
				minPeak = *min;
				maxPeak = *max;
			}
			else
			{
				const auto beginAggregationAt = finerThumbnail->data() + beginIndex;
				const auto endAggregationAt = finerThumbnail->data() + endIndex;
				const auto peak = std::accumulate(beginAggregationAt, endAggregationAt, Thumbnail::Peak{});
				minPeak = peak.min;
				maxPeak = peak.max;
			}

			yMin = centerY - minPeak * yScale;  // bottom of waveform (min peak → below center)
			yMax = centerY - maxPeak * yScale;   // top of waveform (max peak → above center)
		}

		if (firstPoint)
		{
			topPath.moveTo(x, yMax);
			bottomPath.moveTo(x, yMin);
			firstPoint = false;
		}
		else
		{
			topPath.lineTo(x, yMax);
			bottomPath.lineTo(x, yMin);
		}
	}

	if (firstPoint) { painter.restore(); return; }

	// Use a neutral dark color for the waveform (independent of clip bg color)
	const QColor waveColor(0x2A, 0x2A, 0x2A);

	// Draw center line
	QColor centerLineColor = waveColor;
	centerLineColor.setAlpha(50);
	painter.setPen(QPen(centerLineColor, 0.5));
	painter.drawLine(QPointF(renderRect.x(), centerY), QPointF(renderRect.x() + renderRect.width(), centerY));

	// Build closed fill shape: top path forward + bottom path reversed
	QPainterPath fillPath = topPath;
	// Reverse bottomPath by extracting points
	const int elemCount = bottomPath.elementCount();
	for (int e = elemCount - 1; e >= 0; --e)
	{
		const auto elem = bottomPath.elementAt(e);
		fillPath.lineTo(elem.x, elem.y);
	}
	fillPath.closeSubpath();

	// Gradient fill: alpha 140 at peaks → alpha 60 at center
	QLinearGradient grad(0, renderRect.y(), 0, renderRect.y() + renderRect.height());
	QColor fillTop = waveColor; fillTop.setAlpha(140);
	QColor fillMid = waveColor; fillMid.setAlpha(60);
	QColor fillBot = waveColor; fillBot.setAlpha(140);
	grad.setColorAt(0.0, fillTop);
	grad.setColorAt(0.5, fillMid);
	grad.setColorAt(1.0, fillBot);

	painter.setPen(Qt::NoPen);
	painter.setBrush(grad);
	painter.drawPath(fillPath);

	// Outline stroke on top and bottom envelopes
	QColor outlineColor = waveColor;
	outlineColor.setAlpha(180);
	painter.setPen(QPen(outlineColor, 0.8));
	painter.setBrush(Qt::NoBrush);
	painter.drawPath(topPath);
	painter.drawPath(bottomPath);

	painter.restore();
}

} // namespace lmms
