/*****************************************************************************
 * painter.h
 * May 15, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <promeki/namespace.h>
#include <promeki/paintengine.h>

PROMEKI_NAMESPACE_BEGIN

class Painter {
	public:	
		Painter(PaintEngine *engine = NULL);
		~Painter();

		void setEngine(MPaintEngine *val);
		
		MColor getColor(const QPoint &pt) const;
		MColor getColor(int x, int y) const;
		
		void setColor(const MColor &color);
		void setColor(float r, float g, float b, float a = 0.0);

		void drawPoint(const QPoint &pt);
		void drawPoint(int x, int y);
		void drawPoints(const QPoint *pts, int count);

		void drawLine(const QLine &line);
		void drawLine(const QPoint &pt1, const QPoint &pt2);
		void drawLine(int x1, int y1, int x2, int y2);
		void drawLines(const QLine *lines, int count);

		void drawRect(const QRect &rect);
		void drawRect(const QPoint &topLeft, const QPoint &botRight);
		void drawRect(const QPoint &topLeft, const QSize &size);
		void drawRect(int x, int y, int width, int height);

		void drawFilledRect(const QRect &rect);
		void drawFilledRect(const QPoint &topLeft, const QPoint &botRight);
		void drawFilledRect(const QPoint &topLeft, const QSize &size);
		void drawFilledRect(int x, int y, int width, int height);

		void drawCircle(const QPoint &pt, int radius);
		void drawCircle(int x, int y, int radius);

		void drawFilledCircle(const QPoint &pt, int radius);
		void drawFilledCircle(int x, int y, int radius);

		void drawEllipse(const QPoint &center, const QSize &size);
		void drawEllipse(int x, int y, int width, int height);

		void drawFilledEllipse(const QPoint &center, const QSize &size);
		void drawFilledEllipse(int x, int y, int width, int height);

	private:
		MPaintEngine *engine;
};

inline Painter::Painter(MPaintEngine *e) {
	setEngine(e);
}

inline Painter::~Painter() {

}

inline void Painter::setEngine(MPaintEngine *val) {
	engine = val;
	return;
}

inline MColor Painter::getColor(const QPoint &pt) const {
	return engine->getColor(pt);
}

inline MColor Painter::getColor(int x, int y) const {
	return getColor(QPoint(x, y));
}

inline void Painter::setColor(const MColor &c) {
	engine->setColor(c);
	return;
}

inline void Painter::setColor(float r, float g, float b, float a) {
	setColor(MColor(r, g, b, a));
	return;
}

inline void Painter::drawPoint(const QPoint &pt) {
	engine->drawPoints(&pt, 1);
	return;
}

inline void Painter::drawPoint(int x, int y) {
	drawPoint(QPoint(x, y));
	return;
}

inline void Painter::drawPoints(const QPoint *pts, int ct) {
	engine->drawPoints(pts, ct);
	return;
}

inline void Painter::drawLine(const QLine &line) {
	engine->drawLines(&line, 1);
	return;
}

inline void Painter::drawLine(const QPoint &pt1, const QPoint &pt2) {
	drawLine(QLine(pt1, pt2));
	return;
}

inline void Painter::drawLine(int x1, int y1, int x2, int y2) {
	drawLine(QLine(x1, y1, x2, y2));
	return;
}

inline void Painter::drawLines(const QLine *lines, int count) {
	engine->drawLines(lines, count);
	return;
}

inline void Painter::drawRect(const QRect &rect) {
	engine->drawRect(rect);
	return;
}

inline void Painter::drawRect(const QPoint &topLeft, const QPoint &botRight) {
	drawRect(QRect(topLeft, botRight));
	return;
}

inline void Painter::drawRect(const QPoint &topLeft, const QSize &size) {
	drawRect(QRect(topLeft, size));
	return;
}

inline void Painter::drawRect(int x, int y, int width, int height) {
	drawRect(QRect(x, y, width, height));
	return;
}

inline void Painter::drawFilledRect(const QRect &rect) {
	engine->drawFilledRect(rect);
	return;
}

inline void Painter::drawFilledRect(const QPoint &topLeft, const QPoint &botRight) {
	drawFilledRect(QRect(topLeft, botRight));
	return;
}

inline void Painter::drawFilledRect(const QPoint &topLeft, const QSize &size) {
	drawFilledRect(QRect(topLeft, size));
	return;
}

inline void Painter::drawFilledRect(int x, int y, int width, int height) {
	drawFilledRect(QRect(x, y, width, height));
	return;
}

inline void Painter::drawCircle(const QPoint &pt, int radius) {
	engine->drawCircle(pt, radius);
	return;
}

inline void Painter::drawCircle(int x, int y, int radius) {
	drawCircle(QPoint(x, y), radius);
	return;
}

inline void Painter::drawFilledCircle(const QPoint &pt, int radius) {
	engine->drawFilledCircle(pt, radius);
	return;
}

inline void Painter::drawFilledCircle(int x, int y, int radius) {
	drawFilledCircle(QPoint(x, y), radius);
	return;
}

inline void Painter::drawEllipse(const QPoint &center, const QSize &size) {
	engine->drawEllipse(center, size);
	return;
}

inline void Painter::drawEllipse(int x, int y, int width, int height) {
	drawEllipse(QPoint(x, y), QSize(width, height));
	return;
}

inline void Painter::drawFilledEllipse(const QPoint &center, const QSize &size) {
	engine->drawFilledEllipse(center, size);
	return;
}

inline void Painter::drawFilledEllipse(int x, int y, int width, int height) {
	drawFilledEllipse(QPoint(x, y), QSize(width, height));
	return;
}

PROMEKI_NAMESPACE_END

