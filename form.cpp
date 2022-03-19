#include "includes.h"


void Form::draw() {
	// get menu alphas,
	int alpha1 = g_menu.main.misc.alpha1.get();
	int alpha2 = g_menu.main.misc.alpha2.get();

	// opacity should reach 1 in 500 milliseconds.
	constexpr float frequency = 1.f / 0.5f;

	// the increment / decrement per frame.
	float step = frequency * g_csgo.m_globals->m_frametime;

	// if open		-> increment
	// if closed	-> decrement
	m_open ? m_opacity += step : m_opacity -= step;

	// clamp the opacity.
	math::clamp(m_opacity, 0.f, 1.f);

	m_alpha = 0xff * m_opacity;
	if (!m_alpha)
		return;

	// get gui color.
	Color color = g_gui.m_color;
	color.a() = m_alpha;

	// background.
	render::rect_filled(m_x, m_y, m_width, m_height, { 17, 17, 17, alpha1 });

	// border.
	render::rect(m_x, m_y, m_width, m_height, color);
	if (alpha1 > 240) {
		render::rect(m_x + 1, m_y + 1, m_width - 2, m_height - 2, { 60, 60, 60, m_alpha });
		render::rect(m_x + 2, m_y + 2, m_width - 4, m_height - 4, { 40, 40, 40, m_alpha });
		render::rect(m_x + 3, m_y + 3, m_width - 6, m_height - 6, { 40, 40, 40, m_alpha });
		render::rect(m_x + 4, m_y + 4, m_width - 8, m_height - 8, { 40, 40, 40, m_alpha });
		render::rect(m_x + 5, m_y + 5, m_width - 10, m_height - 10, { 60, 60, 60, m_alpha });
	}
	else
		render::rect(m_x + 1, m_y + 1, m_width - 2, m_height - 2, { 60, 60, 60, alpha1 + 10 });
		render::rect(m_x + 2, m_y + 2, m_width - 4, m_height - 4, { 40, 40, 40, alpha1 + 10 });
		render::rect(m_x + 3, m_y + 3, m_width - 6, m_height - 6, { 40, 40, 40, alpha1 + 10 });
		render::rect(m_x + 4, m_y + 4, m_width - 8, m_height - 8, { 40, 40, 40, alpha1 + 10 });
		render::rect(m_x + 5, m_y + 5, m_width - 10, m_height - 10, { 60, 60, 60, alpha1 + 10 });

	// draw tabs if we have any.
	if (!m_tabs.empty()) {
		// tabs background and border.
		Rect tabs_area = GetTabsRect();

		render::rect_filled(tabs_area.x, tabs_area.y, tabs_area.w, tabs_area.h, { 20, 20, 20, alpha2 });
		render::rect(tabs_area.x, tabs_area.y, tabs_area.w, tabs_area.h, { 0, 0, 0, alpha2 });
		render::rect(tabs_area.x + 1, tabs_area.y + 1, tabs_area.w - 2, tabs_area.h - 2, { 25, 26, 33, alpha2 });

		for (size_t i{}; i < m_tabs.size(); ++i) {
			const auto& t = m_tabs[i];

			render::menu_b.string(tabs_area.x + 10, tabs_area.y + 5 + (i * 16),
				t == m_active_tab ? color : Color{ 152, 152, 152, m_alpha },
				t->m_title);
		}

		// this tab has elements.
		if (!m_active_tab->m_elements.empty()) {
			// elements background and border.
			Rect el = GetElementsRect();

			render::rect_filled(el.x, el.y, el.w, el.h, { 20, 20, 20, alpha2 });
			render::rect(el.x, el.y, el.w, el.h, { 0, 0, 0, alpha2 });
			render::rect(el.x + 1, el.y + 1, el.w - 2, el.h - 2, { 25, 26, 33, alpha2 });

			std::string date = XOR(__DATE__);

			// iterate elements to display.
			for (const auto& e : m_active_tab->m_elements) {

				// draw the active element last.
				if (!e || (m_active_element && e == m_active_element))
					continue;

				if (!e->m_show)
					continue;

				// this element we dont draw.
				if (!(e->m_flags & ElementFlags::DRAW))
					continue;

				e->draw();
			}

			// we still have to draw one last fucker.
			if (m_active_element && m_active_element->m_show)
				m_active_element->draw();
		}
	}
}