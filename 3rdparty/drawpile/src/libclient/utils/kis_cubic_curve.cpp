// Copyright (c) 2005 C. Boemann <cbo@boemann.dk>
// Copyright (c) 2009 Dmitry Kazakov <dimula73@gmail.com>
// Copyright (c) 2010 Cyrille Berger <cberger@cberger.net>
// SPDX-License-Identifier: GPL-2.0-or-later
extern "C" {
#include <dpcommon/curve.h>
}
#include "libclient/utils/kis_cubic_curve.h"
#include <QStringList>

DpCubicCurve::DpCubicCurve()
	: m_points({{0.0, 0.0}, {1.0, 1.0}})
{
}

DpCubicCurve::DpCubicCurve(const QList<QPointF> &points)
	: m_points(points)
{
	keepSorted();
}

DpCubicCurve::DpCubicCurve(const DpCubicCurve &other)
	: m_points(other.m_points)
	, m_curve(DP_curve_incref_nullable(other.m_curve))
{
}

DpCubicCurve::DpCubicCurve(DpCubicCurve &&other)
{
	std::swap(m_points, other.m_points);
	std::swap(m_curve, other.m_curve);
}

DpCubicCurve &DpCubicCurve::operator=(const DpCubicCurve &other)
{
	if(this != &other) {
		m_points = other.m_points;
		DP_curve_decref_nullable(m_curve);
		m_curve = DP_curve_incref_nullable(other.m_curve);
	}
	return *this;
}

DpCubicCurve &DpCubicCurve::operator=(DpCubicCurve &&other)
{
	std::swap(m_points, other.m_points);
	DP_curve_decref_nullable(m_curve);
	m_curve = other.m_curve;
	other.m_curve = nullptr;
	return *this;
}

DpCubicCurve::~DpCubicCurve()
{
	DP_curve_decref_nullable(m_curve);
}

bool DpCubicCurve::operator==(const DpCubicCurve &other) const
{
	return (m_curve && m_curve == other.m_curve) || m_points == other.m_points;
}

qreal DpCubicCurve::value(qreal x) const
{
	if(!m_curve) {
		m_curve = makeCurve(m_points);
	}
	return DP_curve_value_at(m_curve, x);
}

void DpCubicCurve::setPoints(const QList<QPointF> &points)
{
	m_points = points;
	invalidate();
}

void DpCubicCurve::setPoint(int idx, const QPointF &point)
{
	m_points[idx] = point;
	keepSorted();
	invalidate();
}

int DpCubicCurve::addPoint(const QPointF &point)
{
	m_points.append(point);
	keepSorted();
	invalidate();
	return m_points.indexOf(point);
}

void DpCubicCurve::removePoint(int idx)
{
	m_points.removeAt(idx);
	invalidate();
}

QString DpCubicCurve::toString() const
{
	QString sCurve;
	if(m_points.size() > 0) {
		for(const QPointF &pair : m_points) {
			sCurve += QString::number(pair.x());
			sCurve += ',';
			sCurve += QString::number(pair.y());
			sCurve += ';';
		}
	}
	return sCurve;
}

void DpCubicCurve::fromString(const QString &string)
{
	QStringList data = string.split(';');
	QList<QPointF> points;
	for(const QString &pair : data) {
		if(pair.indexOf(',') > -1) {
			QPointF p;
			p.rx() = qBound(0.0, pair.section(',', 0, 0).toDouble(), 1.0);
			p.ry() = qBound(0.0, pair.section(',', 1, 1).toDouble(), 1.0);
			points.append(p);
		}
	}

	if(points.size() < 2) {
		points = {{0.0, 0.0}, {1.0, 1.0}};
	}

	setPoints(points);
}

DP_Curve *DpCubicCurve::makeCurve(const QList<QPointF> &points)
{
	return DP_curve_new(
		DP_CURVE_TYPE_CUBIC, points.size(), &DpCubicCurve::getPointCallback,
		const_cast<QList<QPointF> *>(&points));
}

void DpCubicCurve::transferIntoFloat(float *out, int size) const
{
	qreal end = 1.0 / (size - 1);
	for(int i = 0; i < size; ++i) {
		out[i] = value(i * end);
	}
}

static bool pointLessThan(const QPointF &a, const QPointF &b)
{
	return a.x() < b.x();
}

void DpCubicCurve::keepSorted()
{
	std::stable_sort(m_points.begin(), m_points.end(), pointLessThan);
}

void DpCubicCurve::invalidate()
{
	if(m_curve) {
		DP_curve_decref(m_curve);
		m_curve = nullptr;
	}
}

void DpCubicCurve::getPointCallback(
	void *user, int i, double *out_x, double *out_y)
{
	const QList<QPointF> &points = *static_cast<const QList<QPointF> *>(user);
	QPointF point = points[i];
	*out_x = point.x();
	*out_y = point.y();
}
