// SPDX-License-Identifier: GPL-2.0
#include "profile-widget/diveeventitem.h"
#include "profile-widget/divecartesianaxis.h"
#include "profile-widget/divepixmapcache.h"
#include "profile-widget/animationfunctions.h"
#include "core/event.h"
#include "core/eventname.h"
#include "core/format.h"
#include "core/profile.h"
#include "core/gettextfromc.h"
#include "core/sample.h"
#include "core/subsurface-string.h"

#define DEPTH_NOT_FOUND (-2342)

static int depthAtTime(const plot_info &pi, duration_t time);

DiveEventItem::DiveEventItem(const struct dive *d, struct event *ev, struct gasmix lastgasmix,
			     const plot_info &pi, DiveCartesianAxis *hAxis, DiveCartesianAxis *vAxis,
			     int speed, const DivePixmaps &pixmaps, QGraphicsItem *parent) : DivePixmapItem(parent),
	vAxis(vAxis),
	hAxis(hAxis),
	ev(ev),
	dive(d),
	depth(depthAtTime(pi, ev->time))
{
	setFlag(ItemIgnoresTransformations);

	setupPixmap(lastgasmix, pixmaps);
	setupToolTipString(lastgasmix);
	recalculatePos();
}

DiveEventItem::~DiveEventItem()
{
}

const struct event *DiveEventItem::getEvent() const
{
	return ev;
}

struct event *DiveEventItem::getEventMutable()
{
	return ev;
}

void DiveEventItem::setupPixmap(struct gasmix lastgasmix, const DivePixmaps &pixmaps)
{
	if (empty_string(ev->name)) {
		setPixmap(pixmaps.warning);
	} else if (same_string_caseinsensitive(ev->name, "modechange")) {
		if (ev->value == 0)
			setPixmap(pixmaps.bailout);
		else
			setPixmap(pixmaps.onCCRLoop);
	} else if (ev->type == SAMPLE_EVENT_BOOKMARK) {
		setPixmap(pixmaps.bookmark);
		setOffset(QPointF(0.0, -pixmap().height()));
	} else if (event_is_gaschange(ev)) {
		struct gasmix mix = get_gasmix_from_event(dive, ev);
		struct icd_data icd_data;
		bool icd = isobaric_counterdiffusion(lastgasmix, mix, &icd_data);
		if (mix.he.permille) {
			if (icd)
				setPixmap(pixmaps.gaschangeTrimixICD);
			else
				setPixmap(pixmaps.gaschangeTrimix);
		} else if (gasmix_is_air(mix)) {
			if (icd)
				setPixmap(pixmaps.gaschangeAirICD);
			else
				setPixmap(pixmaps.gaschangeAir);
		} else if (mix.o2.permille == 1000) {
			if (icd)
				setPixmap(pixmaps.gaschangeOxygenICD);
			else
				setPixmap(pixmaps.gaschangeOxygen);
		} else {
			if (icd)
				setPixmap(pixmaps.gaschangeEANICD);
			else
				setPixmap(pixmaps.gaschangeEAN);
		}
#ifdef SAMPLE_FLAGS_SEVERITY_SHIFT
	} else if ((((ev->flags & SAMPLE_FLAGS_SEVERITY_MASK) >> SAMPLE_FLAGS_SEVERITY_SHIFT) == 1) ||
		    // those are useless internals of the dive computer
#else
	} else if (
#endif
		   same_string_caseinsensitive(ev->name, "heading") ||
		   (same_string_caseinsensitive(ev->name, "SP change") && ev->time.seconds == 0)) {
		// 2 cases:
		// a) some dive computers have heading in every sample
		// b) at t=0 we might have an "SP change" to indicate dive type
		// in both cases we want to get the right data into the tooltip but don't want the visual clutter
		// so set an "almost invisible" pixmap (a narrow but somewhat tall, basically transparent pixmap)
		// that allows tooltips to work when we don't want to show a specific
		// pixmap for an event, but want to show the event value in the tooltip
		setPixmap(pixmaps.transparent);
#ifdef SAMPLE_FLAGS_SEVERITY_SHIFT
	} else if (((ev->flags & SAMPLE_FLAGS_SEVERITY_MASK) >> SAMPLE_FLAGS_SEVERITY_SHIFT) == 2) {
		setPixmap(pixmaps.info);
	} else if (((ev->flags & SAMPLE_FLAGS_SEVERITY_MASK) >> SAMPLE_FLAGS_SEVERITY_SHIFT) == 3) {
		setPixmap(pixmaps.warning);
	} else if (((ev->flags & SAMPLE_FLAGS_SEVERITY_MASK) >> SAMPLE_FLAGS_SEVERITY_SHIFT) == 4) {
		setPixmap(pixmaps.violation);
#endif
	} else if (same_string_caseinsensitive(ev->name, "violation") || // generic libdivecomputer
		   same_string_caseinsensitive(ev->name, "Safety stop violation")  || // the rest are from the Uemis downloader
		   same_string_caseinsensitive(ev->name, "pO₂ ascend alarm")  ||
		   same_string_caseinsensitive(ev->name, "RGT alert")  ||
		   same_string_caseinsensitive(ev->name, "Dive time alert")  ||
		   same_string_caseinsensitive(ev->name, "Low battery alert")  ||
		   same_string_caseinsensitive(ev->name, "Speed alarm")) {
		setPixmap(pixmaps.violation);
	} else if (same_string_caseinsensitive(ev->name, "non stop time") || // generic libdivecomputer
		   same_string_caseinsensitive(ev->name, "safety stop") ||
		   same_string_caseinsensitive(ev->name, "safety stop (voluntary)") ||
		   same_string_caseinsensitive(ev->name, "Tank change suggested") || // Uemis downloader
		   same_string_caseinsensitive(ev->name, "Marker")) {
		setPixmap(pixmaps.info);
	} else {
		// we should do some guessing based on the type / name of the event;
		// for now they all get the warning icon
		setPixmap(pixmaps.warning);
	}
}

void DiveEventItem::setupToolTipString(struct gasmix lastgasmix)
{
	// we display the event on screen - so translate
	QString name = gettextFromC::tr(ev->name);
	int value = ev->value;
	int type = ev->type;

	if (event_is_gaschange(ev)) {
		struct icd_data icd_data;
		struct gasmix mix = get_gasmix_from_event(dive, ev);
		name += ": ";
		name += gasname(mix);

		/* Do we have an explicit cylinder index?  Show it. */
		if (ev->gas.index >= 0)
			name += tr(" (cyl. %1)").arg(ev->gas.index + 1);
		bool icd = isobaric_counterdiffusion(lastgasmix, mix, &icd_data);
		if (icd_data.dHe < 0) {
			name += qasprintf_loc("\n%s %s:%+.3g%% %s:%+.3g%%%s%+.3g%%",
					      qPrintable(tr("ICD")),
					      qPrintable(tr("ΔHe")), icd_data.dHe / 10.0,
					      qPrintable(tr("ΔN₂")), icd_data.dN2 / 10.0,
					      icd ? ">" : "<", lrint(-icd_data.dHe / 5.0) / 10.0);
		}
	} else if (same_string(ev->name, "modechange")) {
		name += QString(": %1").arg(gettextFromC::tr(divemode_text_ui[ev->value]));
	} else if (value) {
		if (type == SAMPLE_EVENT_PO2 && same_string(ev->name, "SP change")) {
			name += QString(": %1bar").arg((double)value / 1000, 0, 'f', 1);
		} else if (type == SAMPLE_EVENT_CEILING && same_string(ev->name, "planned waypoint above ceiling")) {
			const char *depth_unit;
			double depth_value = get_depth_units(value*1000, NULL, &depth_unit);
			name += QString(": %1%2").arg((int) round(depth_value)).arg(depth_unit);
		} else {
			name += QString(": %1").arg(value);
		}
	} else if (type == SAMPLE_EVENT_PO2 && same_string(ev->name, "SP change")) {
		// this is a bad idea - we are abusing an existing event type that is supposed to
		// warn of high or low pO₂ and are turning it into a setpoint change event
		name += ":\n" + tr("Manual switch to OC");
	} else {
		name += ev->flags & SAMPLE_FLAGS_BEGIN ? tr(" begin", "Starts with space!") :
								    ev->flags & SAMPLE_FLAGS_END ? tr(" end", "Starts with space!") : "";
	}
	setToolTip(QString("<img height=\"16\" src=\":status-warning-icon\">&nbsp;  ") + name);
}

void DiveEventItem::eventVisibilityChanged(const QString&, bool)
{
	//WARN: lookslike we should implement this.
}

static int depthAtTime(const plot_info &pi, duration_t time)
{
	// Do a binary search for the timestamp
	auto it = std::lower_bound(pi.entry, pi.entry + pi.nr, time,
				   [](const plot_data &d1, duration_t t) { return d1.sec < t.seconds; });
	if (it == pi.entry + pi.nr || it->sec != time.seconds) {
		qWarning("can't find a spot in the dataModel");
		return DEPTH_NOT_FOUND;
	}
	return it->depth;
}

bool DiveEventItem::isInteresting(const struct dive *d, const struct divecomputer *dc,
				  const struct event *ev, const plot_info &pi,
				  int firstSecond, int lastSecond)
{
	/*
	 * Ignore items outside of plot range
	 */
	if (ev->time.seconds < firstSecond || ev->time.seconds >= lastSecond)
		return false;

	/*
	 * Some gas change events are special. Some dive computers just tell us the initial gas this way.
	 * Don't bother showing those
	 */
	const struct sample *first_sample = &dc->sample[0];
	if (!strcmp(ev->name, "gaschange") &&
	    (ev->time.seconds == 0 ||
	     (first_sample && ev->time.seconds == first_sample->time.seconds) ||
	     depthAtTime(pi, ev->time) < SURFACE_THRESHOLD))
		return false;

	/*
	 * Some divecomputers give "surface" events that just aren't interesting.
	 * Like at the beginning or very end of a dive. Well, duh.
	 */
	if (!strcmp(ev->name, "surface")) {
		int time = ev->time.seconds;
		if (time <= 30 || time + 30 >= (int)dc->duration.seconds)
			return false;
	}
	return true;
}

bool DiveEventItem::shouldBeHidden()
{
	return is_event_hidden(ev->name, ev->flags);
}

void DiveEventItem::recalculatePos()
{
	if (!ev)
		return;

	if (depth == DEPTH_NOT_FOUND) {
		hide();
		return;
	}
	setVisible(!shouldBeHidden());
	double x = hAxis->posAtValue(ev->time.seconds);
	double y = vAxis->posAtValue(depth);
	setPos(x, y);
}
