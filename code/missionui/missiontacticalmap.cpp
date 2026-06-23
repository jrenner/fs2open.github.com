#include "missionui/missiontacticalmap.h"

#include "ai/ai.h"
#include "bmpman/bmpman.h"
#include "camera/photomode.h"
#include "controlconfig/controlsconfig.h"
#include "freespace.h"
#include "gamesequence/gamesequence.h"
#include "globalincs/alphacolors.h"
#include "graphics/2d.h"
#include "graphics/font.h"
#include "graphics/matrix.h"
#include "graphics/render.h"
#include "hud/hudmessage.h"
#include "iff_defs/iff_defs.h"
#include "io/cursor.h"
#include "io/key.h"
#include "io/mouse.h"
#include "localization/localize.h"
#include "mission/missionmessage.h"
#include "object/object.h"
#include "object/objectshield.h"
#include "playerman/player.h"
#include "ship/ship.h"
#include "sound/audiostr.h"
#include "ui/ui.h"
#include "weapon/weapon.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace
{
struct tactical_map_contact_id {
	int objnum = -1;
	int signature = -1;
};

struct tactical_map_rect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

struct tactical_map_contact {
	tactical_map_contact_id id;
	int shipnum = -1;
	int ship_class = -1;
	int team = -1;

	vec3d world_pos = vmd_zero_vector;
	SCP_string display_name;
	SCP_string class_name;
	SCP_string team_name;

	float hull_pct = 0.0f;
	float shield_pct = -1.0f;
	float distance_from_player = 0.0f;

	int radar_image_2d = -1;
	int radar_color_image_2d = -1;
	int radar_image_size = -1;

	color iff_color;

	int icon_x = 0;
	int icon_y = 0;
	int ground_x = 0;
	int ground_y = 0;
	int hit_radius = 8;

	bool is_player = false;
	bool is_current_target = false;
};

struct tactical_map_view {
	float zoom = 1.0f;
	float vertical_scale = 0.08f;
	vec3d origin = vmd_zero_vector;
	int pan_x = 0;
	int pan_y = 0;
	bool auto_fit_pending = true;
};

bool Tactical_map_active = false;
UI_WINDOW Tactical_map_window;
SCP_vector<tactical_map_contact> Tactical_map_contacts;
tactical_map_contact_id Selected_contact;
tactical_map_contact_id Hovered_contact;
tactical_map_view Tactical_map_view;

color Tactical_bg_color;
color Tactical_panel_color;
color Tactical_grid_minor_color;
color Tactical_grid_major_color;
color Tactical_grid_axis_color;
color Tactical_drop_line_color;
color Tactical_hover_color;
color Tactical_selected_color;
color Tactical_target_color;
color Tactical_text_dim_color;

bool Tactical_colors_initialized = false;

constexpr float MIN_ZOOM = 0.0025f;
constexpr float MAX_ZOOM = 8.0f;
constexpr int DEFAULT_ICON_SIZE = 12;
constexpr int MIN_HIT_RADIUS = 8;

bool valid_contact_id(const tactical_map_contact_id& id)
{
	return id.objnum >= 0 && id.objnum < MAX_OBJECTS && Objects[id.objnum].signature == id.signature &&
	       Objects[id.objnum].type == OBJ_SHIP && !Objects[id.objnum].flags[Object::Object_Flags::Should_be_dead];
}

bool same_contact(const tactical_map_contact_id& a, const tactical_map_contact_id& b)
{
	return a.objnum == b.objnum && a.signature == b.signature;
}

void clear_contact_id(tactical_map_contact_id& id)
{
	id.objnum = -1;
	id.signature = -1;
}

const tactical_map_contact* find_contact(const tactical_map_contact_id& id)
{
	if (id.objnum < 0) {
		return nullptr;
	}

	for (const auto& contact : Tactical_map_contacts) {
		if (same_contact(contact.id, id)) {
			return &contact;
		}
	}

	return nullptr;
}

void init_colors()
{
	if (Tactical_colors_initialized) {
		return;
	}

	gr_init_alphacolor(&Tactical_bg_color, 4, 8, 12, 255);
	gr_init_alphacolor(&Tactical_panel_color, 12, 22, 32, 255);
	gr_init_alphacolor(&Tactical_grid_minor_color, 30, 60, 70, 160);
	gr_init_alphacolor(&Tactical_grid_major_color, 58, 104, 120, 210);
	gr_init_alphacolor(&Tactical_grid_axis_color, 95, 160, 175, 230);
	gr_init_alphacolor(&Tactical_drop_line_color, 90, 115, 130, 175);
	gr_init_alphacolor(&Tactical_hover_color, 255, 255, 255, 255);
	gr_init_alphacolor(&Tactical_selected_color, 255, 220, 80, 255);
	gr_init_alphacolor(&Tactical_target_color, 120, 220, 255, 255);
	gr_init_alphacolor(&Tactical_text_dim_color, 150, 170, 180, 255);

	Tactical_colors_initialized = true;
}

float clamp_float(float value, float min_value, float max_value)
{
	return std::max(min_value, std::min(max_value, value));
}

bool point_in_rect(int x, int y, const tactical_map_rect& rect)
{
	return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

int get_contact_icon_size(const tactical_map_contact& contact)
{
	return contact.radar_image_size > 0 ? contact.radar_image_size : DEFAULT_ICON_SIZE * 2;
}

void get_layout(tactical_map_rect& title_rect, tactical_map_rect& map_rect, tactical_map_rect& info_rect, tactical_map_rect& help_rect)
{
	const int screen_w = gr_screen.max_w;
	const int screen_h = gr_screen.max_h;
	const int title_h = 34;
	const int help_h = 30;
	const int margin = 10;
	const int info_w = std::max(260, std::min(360, screen_w / 4));

	title_rect = {0, 0, screen_w, title_h};
	help_rect = {0, screen_h - help_h, screen_w, help_h};
	info_rect = {screen_w - info_w, title_h, info_w, screen_h - title_h - help_h};
	map_rect = {margin, title_h + margin, screen_w - info_w - margin * 2, screen_h - title_h - help_h - margin * 2};
}

void collect_contacts()
{
	Tactical_map_contacts.clear();

	for (auto objp = GET_FIRST(&obj_used_list); objp != END_OF_LIST(&obj_used_list); objp = GET_NEXT(objp)) {
		if (objp->type != OBJ_SHIP || objp->flags[Object::Object_Flags::Should_be_dead]) {
			continue;
		}

		const int shipnum = objp->instance;
		if (shipnum < 0 || shipnum >= MAX_SHIPS) {
			continue;
		}

		const auto& shipp = Ships[shipnum];
		if (shipp.ship_info_index < 0 || shipp.ship_info_index >= static_cast<int>(Ship_info.size())) {
			continue;
		}

		const auto& sip = Ship_info[shipp.ship_info_index];

		tactical_map_contact contact;
		contact.id.objnum = OBJ_INDEX(objp);
		contact.id.signature = objp->signature;
		contact.shipnum = shipnum;
		contact.ship_class = shipp.ship_info_index;
		contact.team = shipp.team;
		contact.world_pos = objp->pos;
		contact.display_name = shipp.get_display_name();
		contact.class_name = sip.get_display_name();
		contact.team_name = (shipp.team >= 0 && shipp.team < static_cast<int>(Iff_info.size())) ? Iff_info[shipp.team].iff_name : "Unknown";

		const float max_hull = sip.max_hull_strength;
		contact.hull_pct = max_hull > 0.0f ? clamp_float(objp->hull_strength / max_hull, 0.0f, 1.0f) : 0.0f;

		const float max_shield = shield_get_max_strength(objp);
		contact.shield_pct = max_shield > 0.0f ? clamp_float(shield_get_strength(objp) / max_shield, 0.0f, 1.0f) : -1.0f;

		if (Player_obj != nullptr) {
			contact.distance_from_player = vm_vec_dist(&objp->pos, &Player_obj->pos);
		}

		contact.radar_image_2d = sip.radar_image_2d_idx;
		contact.radar_color_image_2d = sip.radar_color_image_2d_idx;
		contact.radar_image_size = sip.radar_image_size;

		if (Player_ship != nullptr) {
			contact.iff_color = *iff_get_color_by_team_and_object(shipp.team, Player_ship->team, 1, objp);
		} else {
			contact.iff_color = Color_white;
		}

		contact.is_player = objp == Player_obj;
		contact.is_current_target = Player_ai != nullptr && Player_ai->target_objnum == contact.id.objnum;

		Tactical_map_contacts.push_back(contact);
	}
}

void auto_fit_contacts(const tactical_map_rect& map_rect)
{
	if (Tactical_map_contacts.empty()) {
		Tactical_map_view.origin = Player_obj != nullptr ? Player_obj->pos : vmd_zero_vector;
		Tactical_map_view.zoom = 1.0f;
		Tactical_map_view.pan_x = 0;
		Tactical_map_view.pan_y = 0;
		return;
	}

	float min_x = Tactical_map_contacts.front().world_pos.xyz.x;
	float max_x = min_x;
	float min_z = Tactical_map_contacts.front().world_pos.xyz.z;
	float max_z = min_z;
	float sum_y = 0.0f;

	for (const auto& contact : Tactical_map_contacts) {
		min_x = std::min(min_x, contact.world_pos.xyz.x);
		max_x = std::max(max_x, contact.world_pos.xyz.x);
		min_z = std::min(min_z, contact.world_pos.xyz.z);
		max_z = std::max(max_z, contact.world_pos.xyz.z);
		sum_y += contact.world_pos.xyz.y;
	}

	Tactical_map_view.origin.xyz.x = (min_x + max_x) * 0.5f;
	Tactical_map_view.origin.xyz.y = sum_y / static_cast<float>(Tactical_map_contacts.size());
	Tactical_map_view.origin.xyz.z = (min_z + max_z) * 0.5f;

	const float span_x = std::max(max_x - min_x, 100.0f);
	const float span_z = std::max(max_z - min_z, 100.0f);
	const float usable_w = std::max(100, map_rect.w - 80);
	const float usable_h = std::max(100, map_rect.h - 80);

	const float zoom_x = usable_w / (span_x * 1.15f);
	const float zoom_z = usable_h / (span_z * 1.15f);
	Tactical_map_view.zoom = clamp_float(std::min(zoom_x, zoom_z), MIN_ZOOM, MAX_ZOOM);
	Tactical_map_view.pan_x = 0;
	Tactical_map_view.pan_y = 0;
}

void project_contacts(const tactical_map_rect& map_rect)
{
	for (auto& contact : Tactical_map_contacts) {
		const float rel_x = contact.world_pos.xyz.x - Tactical_map_view.origin.xyz.x;
		const float rel_y = contact.world_pos.xyz.y - Tactical_map_view.origin.xyz.y;
		const float rel_z = contact.world_pos.xyz.z - Tactical_map_view.origin.xyz.z;

		contact.ground_x = map_rect.x + map_rect.w / 2 + Tactical_map_view.pan_x + fl2i(rel_x * Tactical_map_view.zoom);
		contact.ground_y = map_rect.y + map_rect.h / 2 + Tactical_map_view.pan_y - fl2i(rel_z * Tactical_map_view.zoom);
		contact.icon_x = contact.ground_x;
		contact.icon_y = contact.ground_y - fl2i(rel_y * Tactical_map_view.zoom * Tactical_map_view.vertical_scale);
		contact.hit_radius = std::max(MIN_HIT_RADIUS, get_contact_icon_size(contact) / 2 + 6);
	}
}

float choose_grid_spacing()
{
	static const float spacings[] = {50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2500.0f, 5000.0f, 10000.0f, 25000.0f, 50000.0f};
	const float target_world = 64.0f / std::max(Tactical_map_view.zoom, MIN_ZOOM);

	for (auto spacing : spacings) {
		if (spacing >= target_world) {
			return spacing;
		}
	}

	return spacings[sizeof(spacings) / sizeof(spacings[0]) - 1];
}

void draw_text_line(int x, int& y, const char* label, const char* value, const color* value_color = nullptr)
{
	gr_set_color_fast(&Tactical_text_dim_color);
	gr_printf_no_resize(x, y, "%s", label);

	if (value_color != nullptr) {
		gr_set_color_fast(value_color);
	} else {
		gr_set_color_fast(&Color_text_normal);
	}
	gr_printf_no_resize(x + 92, y, "%s", value);
	y += 18;
}

void draw_text_line(int x, int& y, const char* label, const SCP_string& value, const color* value_color = nullptr)
{
	draw_text_line(x, y, label, value.c_str(), value_color);
}

void draw_bitmap_icon(const tactical_map_contact& contact, int bitmap)
{
	int w = 0;
	int h = 0;
	if (bitmap < 0 || bm_get_info(bitmap, &w, &h) < 0 || w <= 0 || h <= 0) {
		return;
	}

	const int desired_size = get_contact_icon_size(contact);
	const float scale = clamp_float(static_cast<float>(desired_size) / static_cast<float>(std::max(w, h)), 0.15f, 2.5f);

	vec3d scale_vec = vmd_zero_vector;
	scale_vec.xyz.x = scale;
	scale_vec.xyz.y = scale;
	scale_vec.xyz.z = 1.0f;

	const int x = fl2i(static_cast<float>(contact.icon_x) / scale - static_cast<float>(w) * 0.5f);
	const int y = fl2i(static_cast<float>(contact.icon_y) / scale - static_cast<float>(h) * 0.5f);

	gr_push_scale_matrix(&scale_vec);
	gr_set_bitmap(bitmap, GR_ALPHABLEND_NONE, GR_BITBLT_MODE_NORMAL, 1.0f);
	gr_bitmap(x, y, GR_RESIZE_NONE);
	gr_pop_scale_matrix();
}

void draw_fallback_icon(const tactical_map_contact& contact)
{
	const int x = contact.icon_x;
	const int y = contact.icon_y;
	const int r = DEFAULT_ICON_SIZE / 2;

	gr_set_color_fast(&contact.iff_color);

	if (contact.is_player) {
		gr_line(x, y - r - 3, x + r + 3, y + r + 3, GR_RESIZE_NONE);
		gr_line(x, y - r - 3, x - r - 3, y + r + 3, GR_RESIZE_NONE);
		gr_line(x - r - 3, y + r + 3, x + r + 3, y + r + 3, GR_RESIZE_NONE);
	} else if (Ship_info[contact.ship_class].is_big_ship() || Ship_info[contact.ship_class].is_huge_ship()) {
		gr_rect(x - r - 3, y - r, (r + 3) * 2, r * 2, GR_RESIZE_NONE);
	} else {
		gr_line(x, y - r - 2, x + r + 2, y, GR_RESIZE_NONE);
		gr_line(x + r + 2, y, x, y + r + 2, GR_RESIZE_NONE);
		gr_line(x, y + r + 2, x - r - 2, y, GR_RESIZE_NONE);
		gr_line(x - r - 2, y, x, y - r - 2, GR_RESIZE_NONE);
	}
}

void draw_contact_icon(const tactical_map_contact& contact)
{
	if (contact.radar_image_2d >= 0) {
		draw_bitmap_icon(contact, contact.radar_image_2d);
	} else if (contact.radar_color_image_2d >= 0) {
		draw_bitmap_icon(contact, contact.radar_color_image_2d);
	} else {
		draw_fallback_icon(contact);
	}

	if (contact.is_current_target) {
		gr_set_color_fast(&Tactical_target_color);
		gr_circle(contact.icon_x, contact.icon_y, DEFAULT_ICON_SIZE * 2 + 8, GR_RESIZE_NONE);
	}

	if (same_contact(contact.id, Hovered_contact)) {
		gr_set_color_fast(&Tactical_hover_color);
		gr_circle(contact.icon_x, contact.icon_y, DEFAULT_ICON_SIZE * 2 + 3, GR_RESIZE_NONE);
	}

	if (same_contact(contact.id, Selected_contact)) {
		gr_set_color_fast(&Tactical_selected_color);
		gr_circle(contact.icon_x, contact.icon_y, DEFAULT_ICON_SIZE * 2 + 10, GR_RESIZE_NONE);
	}
}

void draw_contact_label(const tactical_map_contact& contact)
{
	int label_w = 0;
	int label_h = 0;
	gr_get_string_size(&label_w, &label_h, contact.display_name.c_str());

	gr_set_color_fast(&Color_text_normal);
	gr_printf_no_resize(contact.icon_x - label_w / 2,
		contact.icon_y - get_contact_icon_size(contact) / 2 - label_h - 3,
		"%s",
		contact.display_name.c_str());
}

void render_grid(const tactical_map_rect& map_rect)
{
	gr_set_clip(map_rect.x, map_rect.y, map_rect.w, map_rect.h, GR_RESIZE_NONE);

	gr_set_color_fast(&Tactical_panel_color);
	gr_rect(map_rect.x, map_rect.y, map_rect.w, map_rect.h, GR_RESIZE_NONE);

	const float spacing = choose_grid_spacing();
	const float center_world_x = Tactical_map_view.origin.xyz.x - static_cast<float>(Tactical_map_view.pan_x) / Tactical_map_view.zoom;
	const float center_world_z = Tactical_map_view.origin.xyz.z + static_cast<float>(Tactical_map_view.pan_y) / Tactical_map_view.zoom;
	const float half_w_world = static_cast<float>(map_rect.w) * 0.5f / Tactical_map_view.zoom;
	const float half_h_world = static_cast<float>(map_rect.h) * 0.5f / Tactical_map_view.zoom;

	const float min_x = center_world_x - half_w_world;
	const float max_x = center_world_x + half_w_world;
	const float min_z = center_world_z - half_h_world;
	const float max_z = center_world_z + half_h_world;

	int major_index = 0;
	for (float x = std::floor(min_x / spacing) * spacing; x <= max_x; x += spacing) {
		const int sx = map_rect.x + map_rect.w / 2 + Tactical_map_view.pan_x + fl2i((x - Tactical_map_view.origin.xyz.x) * Tactical_map_view.zoom);
		const bool is_axis = std::fabs(x) < spacing * 0.5f;
		const bool is_major = (major_index % 5) == 0;
		gr_set_color_fast(is_axis ? &Tactical_grid_axis_color : (is_major ? &Tactical_grid_major_color : &Tactical_grid_minor_color));
		gr_line(sx, map_rect.y, sx, map_rect.y + map_rect.h, GR_RESIZE_NONE);
		major_index++;
	}

	major_index = 0;
	for (float z = std::floor(min_z / spacing) * spacing; z <= max_z; z += spacing) {
		const int sy = map_rect.y + map_rect.h / 2 + Tactical_map_view.pan_y - fl2i((z - Tactical_map_view.origin.xyz.z) * Tactical_map_view.zoom);
		const bool is_axis = std::fabs(z) < spacing * 0.5f;
		const bool is_major = (major_index % 5) == 0;
		gr_set_color_fast(is_axis ? &Tactical_grid_axis_color : (is_major ? &Tactical_grid_major_color : &Tactical_grid_minor_color));
		gr_line(map_rect.x, sy, map_rect.x + map_rect.w, sy, GR_RESIZE_NONE);
		major_index++;
	}

	gr_set_color_fast(&Color_text_normal);
	gr_printf_no_resize(map_rect.x + 8, map_rect.y + 8, "Grid: %.0f m  Zoom: %.3fx", spacing, Tactical_map_view.zoom);

	gr_reset_clip();
}

void render_contacts(const tactical_map_rect& map_rect)
{
	gr_set_clip(map_rect.x, map_rect.y, map_rect.w, map_rect.h, GR_RESIZE_NONE);

	for (const auto& contact : Tactical_map_contacts) {
		gr_set_color_fast(same_contact(contact.id, Selected_contact) ? &Tactical_selected_color : &Tactical_drop_line_color);
		gr_line(contact.icon_x, contact.icon_y, contact.ground_x, contact.ground_y, GR_RESIZE_NONE);
		gr_line(contact.ground_x - 4, contact.ground_y - 4, contact.ground_x + 4, contact.ground_y + 4, GR_RESIZE_NONE);
		gr_line(contact.ground_x - 4, contact.ground_y + 4, contact.ground_x + 4, contact.ground_y - 4, GR_RESIZE_NONE);
	}

	for (const auto& contact : Tactical_map_contacts) {
		draw_contact_icon(contact);
	}

	for (const auto& contact : Tactical_map_contacts) {
		draw_contact_label(contact);
	}

	gr_reset_clip();
}

void render_title_bar(const tactical_map_rect& title_rect)
{
	gr_set_color_fast(&Tactical_panel_color);
	gr_rect(title_rect.x, title_rect.y, title_rect.w, title_rect.h, GR_RESIZE_NONE);
	font::set_font(font::FONT2);
	gr_set_color_fast(&Color_text_heading);
	gr_printf_no_resize(title_rect.x + 12, title_rect.y + 7, "%s", XSTR("Tactical Map", -1));
	font::set_font(font::FONT1);
	gr_set_color_fast(&Color_text_normal);
	gr_printf_no_resize(title_rect.w - 330, title_rect.y + 10, "%s", XSTR("Esc or bound key: Close", -1));
}

void render_help_bar(const tactical_map_rect& help_rect)
{
	gr_set_color_fast(&Tactical_panel_color);
	gr_rect(help_rect.x, help_rect.y, help_rect.w, help_rect.h, GR_RESIZE_NONE);
	gr_set_color_fast(&Color_text_normal);
	gr_printf_no_resize(help_rect.x + 12, help_rect.y + 8, "%s", XSTR("Left click: select/target   Right drag/arrows: pan   Wheel/+/-: zoom   R: reset", -1));
}

void render_info_panel(const tactical_map_rect& info_rect)
{
	gr_set_color_fast(&Tactical_panel_color);
	gr_rect(info_rect.x, info_rect.y, info_rect.w, info_rect.h, GR_RESIZE_NONE);

	int x = info_rect.x + 14;
	int y = info_rect.y + 14;

	font::set_font(font::FONT2);
	gr_set_color_fast(&Color_text_heading);
	gr_printf_no_resize(x, y, "%s", XSTR("Contact", -1));
	font::set_font(font::FONT1);
	y += 30;

	const tactical_map_contact* contact = find_contact(Selected_contact);
	if (contact == nullptr) {
		gr_set_color_fast(&Tactical_text_dim_color);
		gr_printf_no_resize(x, y, "%s", Tactical_map_contacts.empty() ? XSTR("No contacts", -1) : XSTR("Select a ship icon", -1));
		return;
	}

	gr_set_color_fast(&contact->iff_color);
	gr_printf_no_resize(x, y, "%s", contact->display_name.c_str());
	y += 24;

	draw_text_line(x, y, XSTR("Class:", -1), contact->class_name);
	draw_text_line(x, y, XSTR("IFF:", -1), contact->team_name, &contact->iff_color);

	char buffer[128];
	snprintf(buffer, sizeof(buffer), "%.0f%%", contact->hull_pct * 100.0f);
	draw_text_line(x, y, XSTR("Hull:", -1), buffer);

	if (contact->shield_pct >= 0.0f) {
		snprintf(buffer, sizeof(buffer), "%.0f%%", contact->shield_pct * 100.0f);
	} else {
		snprintf(buffer, sizeof(buffer), "%s", XSTR("N/A", -1));
	}
	draw_text_line(x, y, XSTR("Shields:", -1), buffer);

	snprintf(buffer, sizeof(buffer), "%.0f m", contact->distance_from_player);
	draw_text_line(x, y, XSTR("Distance:", -1), buffer);

	y += 10;
	gr_set_color_fast(&Color_text_heading);
	gr_printf_no_resize(x, y, "%s", XSTR("Position", -1));
	y += 22;

	snprintf(buffer, sizeof(buffer), "%.0f", contact->world_pos.xyz.x);
	draw_text_line(x, y, "X:", buffer);
	snprintf(buffer, sizeof(buffer), "%.0f", contact->world_pos.xyz.y);
	draw_text_line(x, y, "Y:", buffer);
	snprintf(buffer, sizeof(buffer), "%.0f", contact->world_pos.xyz.z);
	draw_text_line(x, y, "Z:", buffer);

	y += 10;
	if (contact->is_player) {
		draw_text_line(x, y, XSTR("Status:", -1), XSTR("Player", -1));
	} else if (contact->is_current_target) {
		draw_text_line(x, y, XSTR("Status:", -1), XSTR("Current target", -1));
	}
}

void render_hover_tooltip()
{
	const tactical_map_contact* contact = find_contact(Hovered_contact);
	if (contact == nullptr || same_contact(Hovered_contact, Selected_contact)) {
		return;
	}

	int mx = 0;
	int my = 0;
	mouse_get_pos(&mx, &my);

	int w = 0;
	int h = 0;
	gr_get_string_size(&w, &h, contact->display_name.c_str());
	gr_set_color_fast(&Tactical_panel_color);
	gr_rect(mx + 12, my + 12, w + 12, h + 8, GR_RESIZE_NONE);
	gr_set_color_fast(&contact->iff_color);
	gr_string(mx + 18, my + 16, contact->display_name.c_str(), GR_RESIZE_NONE);
}

void update_hover(const tactical_map_rect& map_rect)
{
	clear_contact_id(Hovered_contact);

	int mx = 0;
	int my = 0;
	mouse_get_pos(&mx, &my);
	if (!point_in_rect(mx, my, map_rect)) {
		return;
	}

	int best_dist_sq = INT_MAX;
	for (const auto& contact : Tactical_map_contacts) {
		const int dx = mx - contact.icon_x;
		const int dy = my - contact.icon_y;
		const int dist_sq = dx * dx + dy * dy;
		const int radius = contact.hit_radius;
		if (dist_sq <= radius * radius && dist_sq < best_dist_sq) {
			best_dist_sq = dist_sq;
			Hovered_contact = contact.id;
		}
	}
}

const tactical_map_contact* find_closest_contact(int x, int y)
{
	const tactical_map_contact* closest = nullptr;
	int best_dist_sq = INT_MAX;

	for (const auto& contact : Tactical_map_contacts) {
		const int dx = x - contact.icon_x;
		const int dy = y - contact.icon_y;
		const int dist_sq = dx * dx + dy * dy;
		if (dist_sq < best_dist_sq) {
			best_dist_sq = dist_sq;
			closest = &contact;
		}
	}

	return closest;
}

void target_selected_contact()
{
	if (!valid_contact_id(Selected_contact) || Player_ai == nullptr) {
		return;
	}

	set_target_objnum(Player_ai, Selected_contact.objnum);
	HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR("Target selected from tactical map", -1));
}

void handle_input(int key, const tactical_map_rect& map_rect)
{
	if (key == KEY_ESC || check_control(TACTICAL_MAP_TOGGLE, key)) {
		gameseq_post_event(GS_EVENT_PREVIOUS_STATE);
		return;
	}

	const int masked_key = key & KEY_MASK;
	const int pan_step = 32;

	switch (masked_key) {
		case KEY_R:
			Tactical_map_view.auto_fit_pending = true;
			break;

		case KEY_LEFT:
			Tactical_map_view.pan_x += pan_step;
			break;

		case KEY_RIGHT:
			Tactical_map_view.pan_x -= pan_step;
			break;

		case KEY_UP:
			Tactical_map_view.pan_y += pan_step;
			break;

		case KEY_DOWN:
			Tactical_map_view.pan_y -= pan_step;
			break;

		case KEY_EQUAL:
		case KEY_PADPLUS:
			Tactical_map_view.zoom = clamp_float(Tactical_map_view.zoom * 1.2f, MIN_ZOOM, MAX_ZOOM);
			break;

		case KEY_MINUS:
		case KEY_PADMINUS:
			Tactical_map_view.zoom = clamp_float(Tactical_map_view.zoom / 1.2f, MIN_ZOOM, MAX_ZOOM);
			break;

		case KEY_ENTER:
			if (find_contact(Selected_contact) != nullptr) {
				target_selected_contact();
			}
			break;
	}

	int wheel_x = 0;
	int wheel_y = 0;
	mouse_get_wheel_delta(&wheel_x, &wheel_y);
	if (wheel_y > 0) {
		Tactical_map_view.zoom = clamp_float(Tactical_map_view.zoom * 1.15f, MIN_ZOOM, MAX_ZOOM);
	} else if (wheel_y < 0) {
		Tactical_map_view.zoom = clamp_float(Tactical_map_view.zoom / 1.15f, MIN_ZOOM, MAX_ZOOM);
	}

	if (mouse_down(MOUSE_RIGHT_BUTTON)) {
		int dx = 0;
		int dy = 0;
		mouse_get_delta(&dx, &dy);
		Tactical_map_view.pan_x += dx;
		Tactical_map_view.pan_y += dy;
	}

	if (mouse_down_count(MOUSE_LEFT_BUTTON, 1) > 0) {
		int mx = 0;
		int my = 0;
		mouse_get_pos(&mx, &my);
		const auto closest_contact = point_in_rect(mx, my, map_rect) ? find_closest_contact(mx, my) : nullptr;
		if (closest_contact != nullptr) {
			Selected_contact = closest_contact->id;
			target_selected_contact();
		}
	}
}

void validate_selection()
{
	if (Selected_contact.objnum >= 0 && find_contact(Selected_contact) == nullptr) {
		clear_contact_id(Selected_contact);
	}
}
}

bool tactical_map_is_active()
{
	return Tactical_map_active;
}

void tactical_map_init()
{
	if (Tactical_map_active) {
		return;
	}

	Assert(!(Game_mode & GM_MULTIPLAYER));

	init_colors();

	weapon_pause_sounds();
	audiostream_pause_all();
	message_pause_all();

	Tactical_map_window.create(0, 0, gr_screen.max_w_unscaled, gr_screen.max_h_unscaled, 0);

	io::mouse::CursorManager::get()->pushStatus();
	io::mouse::CursorManager::get()->showCursor(true);

	clear_contact_id(Selected_contact);
	clear_contact_id(Hovered_contact);
	Tactical_map_view = tactical_map_view();
	Tactical_map_active = true;
}

void tactical_map_do(float /*frametime*/)
{
	if (!Tactical_map_active) {
		return;
	}

	Assert(!(Game_mode & GM_MULTIPLAYER));

	tactical_map_rect title_rect;
	tactical_map_rect map_rect;
	tactical_map_rect info_rect;
	tactical_map_rect help_rect;
	get_layout(title_rect, map_rect, info_rect, help_rect);

	const int key = Tactical_map_window.process(-1, 0) & ~KEY_DEBUGGED;

	collect_contacts();
	validate_selection();

	if (Tactical_map_view.auto_fit_pending) {
		auto_fit_contacts(map_rect);
		Tactical_map_view.auto_fit_pending = false;
	}

	project_contacts(map_rect);
	update_hover(map_rect);
	handle_input(key, map_rect);

	gr_reset_clip();
	gr_set_color_fast(&Tactical_bg_color);
	gr_rect(0, 0, gr_screen.max_w, gr_screen.max_h, GR_RESIZE_NONE);

	render_title_bar(title_rect);
	render_grid(map_rect);
	render_contacts(map_rect);
	render_info_panel(info_rect);
	render_hover_tooltip();
	render_help_bar(help_rect);

	Tactical_map_window.draw();
	gr_flip();
}

void tactical_map_close()
{
	if (!Tactical_map_active) {
		return;
	}

	Assert(!(Game_mode & GM_MULTIPLAYER));

	weapon_unpause_sounds();

	if (!game_is_photo_mode_active()) {
		message_resume_all();
		audiostream_unpause_all();
	}

	Tactical_map_window.destroy();
	game_flush();
	io::mouse::CursorManager::get()->popStatus();

	Tactical_map_contacts.clear();
	clear_contact_id(Selected_contact);
	clear_contact_id(Hovered_contact);
	Tactical_map_active = false;
}
