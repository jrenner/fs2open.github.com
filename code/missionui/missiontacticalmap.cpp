#include "missionui/missiontacticalmap.h"

#include "ai/ai.h"
#include "bmpman/bmpman.h"
#include "camera/photomode.h"
#include "controlconfig/controlsconfig.h"
#include "freespace.h"
#include "gamesequence/gamesequence.h"
#include "gamesnd/gamesnd.h"
#include "globalincs/alphacolors.h"
#include "graphics/2d.h"
#include "graphics/font.h"
#include "graphics/generic.h"
#include "graphics/matrix.h"
#include "graphics/render.h"
#include "hud/hudmessage.h"
#include "hud/hudsquadmsg.h"
#include "hud/hudtarget.h"
#include "iff_defs/iff_defs.h"
#include "io/cursor.h"
#include "io/key.h"
#include "io/mouse.h"
#include "localization/localize.h"
#include "mission/missionbriefcommon.h"
#include "mission/missionmessage.h"
#include "mission/missionparse.h"
#include "object/object.h"
#include "object/objectshield.h"
#include "playerman/player.h"
#include "ship/ship.h"
#include "ship/subsysdamage.h"
#include "sound/audiostr.h"
#include "ui/ui.h"
#include "weapon/weapon.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <iterator>

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

enum class tactical_recipient_type {
	AllFighters,
	Wing,
};

struct tactical_recipient_button {
	tactical_map_rect rect;
	tactical_recipient_type type = tactical_recipient_type::AllFighters;
	int wingnum = -1;
	int cycle_delta = 0;
	SCP_string label;
	bool selected = false;
	bool active = true;
};

struct tactical_command_button {
	tactical_map_rect rect;
	int command = NO_ORDER_ITEM;
	int hotkey = -1;
	SCP_string label;
	bool active = false;
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
SCP_vector<tactical_recipient_button> Tactical_recipient_buttons;
SCP_vector<tactical_command_button> Tactical_command_buttons;
SCP_vector<generic_anim*> Tactical_loaded_briefing_anims;
tactical_map_contact_id Selected_contact;
tactical_map_contact_id Hovered_contact;
tactical_map_view Tactical_map_view;
tactical_recipient_type Selected_recipient_type = tactical_recipient_type::AllFighters;
int Selected_recipient_wing = -1;

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
color Tactical_label_bg_color;
color Tactical_button_color;
color Tactical_button_selected_color;
color Tactical_button_disabled_color;

bool Tactical_colors_initialized = false;

constexpr float MIN_ZOOM = 0.0025f;
constexpr float MAX_ZOOM = 8.0f;
constexpr int DEFAULT_ICON_SIZE = 12;
constexpr int MIN_HIT_RADIUS = 8;
constexpr int COMMAND_ROW_H = 22;
constexpr int COMMAND_ROW_GAP = 4;

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
	gr_init_alphacolor(&Tactical_label_bg_color, 2, 6, 8, 190);
	gr_init_alphacolor(&Tactical_button_color, 20, 38, 50, 230);
	gr_init_alphacolor(&Tactical_button_selected_color, 46, 76, 90, 245);
	gr_init_alphacolor(&Tactical_button_disabled_color, 14, 22, 28, 180);

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

bool contact_can_be_targeted(const tactical_map_contact& contact)
{
	return !contact.is_player && valid_contact_id(contact.id);
}

bool ship_can_receive_tactical_orders(int shipnum)
{
	if (shipnum < 0 || shipnum >= MAX_SHIPS || Player_ship == nullptr) {
		return false;
	}

	const auto& shipp = Ships[shipnum];
	if (shipp.objnum < 0 || shipp.objnum >= MAX_OBJECTS) {
		return false;
	}

	auto objp = &Objects[shipp.objnum];
	if (objp->type != OBJ_SHIP || objp->instance != shipnum || objp == Player_obj || is_instructor(objp)) {
		return false;
	}

	if (shipp.ship_info_index < 0 || shipp.ship_info_index >= static_cast<int>(Ship_info.size())) {
		return false;
	}

	if (shipp.team != Player_ship->team || shipp.is_dying_or_departing() || shipp.orders_accepted.empty()) {
		return false;
	}

	if (objp->flags[Object::Object_Flags::Player_ship] && !(Game_mode & GM_MULTIPLAYER)) {
		return false;
	}

	const auto& sip = Ship_info[shipp.ship_info_index];
	if (sip.class_type < 0 || !Ship_types[sip.class_type].flags[Ship::Type_Info_Flags::AI_accept_player_orders]) {
		return false;
	}

	if (The_mission.ai_profile->flags[AI::Profile_Flags::Check_comms_for_non_player_ships] && hud_communications_state(&shipp) != COMM_OK) {
		return false;
	}

	return true;
}

bool wing_can_receive_tactical_orders(int wingnum)
{
	if (wingnum < 0 || wingnum >= Num_wings) {
		return false;
	}

	const auto& wing = Wings[wingnum];
	if (wing.flags[Ship::Wing_Flags::Gone, Ship::Wing_Flags::Departing] || wing.current_count <= 0) {
		return false;
	}

	for (int idx = 0; idx < wing.current_count; idx++) {
		if (ship_can_receive_tactical_orders(wing.ship_index[idx])) {
			return true;
		}
	}

	return false;
}

void add_wing_recipient(SCP_vector<int>& wingnums, int wingnum)
{
	if (!wing_can_receive_tactical_orders(wingnum)) {
		return;
	}

	if (std::find(wingnums.begin(), wingnums.end(), wingnum) == wingnums.end()) {
		wingnums.push_back(wingnum);
	}
}

SCP_vector<int> get_tactical_order_wings()
{
	SCP_vector<int> wingnums;

	for (int idx = 0; idx < MAX_STARTING_WINGS; idx++) {
		add_wing_recipient(wingnums, Starting_wings[idx]);
	}

	for (int wingnum = 0; wingnum < Num_wings; wingnum++) {
		add_wing_recipient(wingnums, wingnum);
	}

	return wingnums;
}

void normalize_selected_recipient()
{
	const auto wingnums = get_tactical_order_wings();
	if (Selected_recipient_type == tactical_recipient_type::Wing &&
	    std::find(wingnums.begin(), wingnums.end(), Selected_recipient_wing) == wingnums.end()) {
		Selected_recipient_type = tactical_recipient_type::AllFighters;
		Selected_recipient_wing = -1;
	}
}

int get_selected_recipient_index()
{
	if (Selected_recipient_type == tactical_recipient_type::AllFighters) {
		return 0;
	}

	const auto wingnums = get_tactical_order_wings();
	const auto wing_iter = std::find(wingnums.begin(), wingnums.end(), Selected_recipient_wing);
	if (wing_iter == wingnums.end()) {
		return 0;
	}

	return 1 + static_cast<int>(std::distance(wingnums.begin(), wing_iter));
}

void select_recipient_by_index(int index)
{
	const auto wingnums = get_tactical_order_wings();
	const int recipient_count = 1 + static_cast<int>(wingnums.size());
	if (recipient_count <= 0) {
		Selected_recipient_type = tactical_recipient_type::AllFighters;
		Selected_recipient_wing = -1;
		return;
	}

	index = (index % recipient_count + recipient_count) % recipient_count;
	if (index == 0) {
		Selected_recipient_type = tactical_recipient_type::AllFighters;
		Selected_recipient_wing = -1;
		return;
	}

	Selected_recipient_type = tactical_recipient_type::Wing;
	Selected_recipient_wing = wingnums[index - 1];
}

SCP_string get_selected_recipient_label()
{
	if (Selected_recipient_type == tactical_recipient_type::Wing && Selected_recipient_wing >= 0 && Selected_recipient_wing < Num_wings) {
		return Wings[Selected_recipient_wing].get_display_name();
	}

	return XSTR("All fighters", -1);
}

int briefing_icon_type_for_contact(const tactical_map_contact& contact)
{
	const auto& sip = Ship_info[contact.ship_class];

	if (contact.is_player) {
		return sip.flags[Ship::Info_Flags::Bomber] ? ICON_BOMBER_PLAYER : ICON_FIGHTER_PLAYER;
	}

	if (sip.flags[Ship::Info_Flags::Support]) {
		return ICON_SUPPORT_SHIP;
	} else if (sip.flags[Ship::Info_Flags::Bomber]) {
		return ICON_BOMBER;
	} else if (sip.flags[Ship::Info_Flags::Fighter]) {
		return ICON_FIGHTER;
	} else if (sip.flags[Ship::Info_Flags::Knossos_device]) {
		return ICON_KNOSSOS_DEVICE;
	} else if (sip.flags[Ship::Info_Flags::Supercap]) {
		return ICON_SUPERCAP;
	} else if (sip.flags[Ship::Info_Flags::Capital]) {
		return ICON_CAPITAL;
	} else if (sip.flags[Ship::Info_Flags::Corvette]) {
		return ICON_CORVETTE;
	} else if (sip.flags[Ship::Info_Flags::Cruiser]) {
		return ICON_CRUISER;
	} else if (sip.flags[Ship::Info_Flags::Gas_miner]) {
		return ICON_GAS_MINER;
	} else if (sip.flags[Ship::Info_Flags::Awacs] || sip.flags[Ship::Info_Flags::Has_awacs]) {
		return ICON_AWACS;
	} else if (sip.flags[Ship::Info_Flags::Sentrygun]) {
		return ICON_SENTRYGUN;
	} else if (sip.flags[Ship::Info_Flags::Transport]) {
		return ICON_TRANSPORT;
	} else if (sip.flags[Ship::Info_Flags::Freighter]) {
		return ICON_FREIGHTER_NO_CARGO;
	} else if (sip.flags[Ship::Info_Flags::Cargo]) {
		return ICON_CARGO;
	}

	return ICON_UNKNOWN;
}

int get_briefing_icon_bitmap(const tactical_map_contact& contact, bool selected)
{
	brief_icon icon = {};
	icon.ship_class = contact.ship_class;
	icon.type = briefing_icon_type_for_contact(contact);
	icon.team = contact.team;

	auto bii = brief_get_icon_info(&icon);
	if (bii == nullptr) {
		return -1;
	}

	if (bii->regular.first_frame < 0) {
		if (generic_anim_load(&bii->regular) < 0) {
			return -1;
		}

		if (std::find(Tactical_loaded_briefing_anims.begin(), Tactical_loaded_briefing_anims.end(), &bii->regular) ==
		    Tactical_loaded_briefing_anims.end()) {
			Tactical_loaded_briefing_anims.push_back(&bii->regular);
		}
	}

	if (selected && bii->regular.num_frames > 1) {
		return bii->regular.first_frame + 1;
	}

	return bii->regular.first_frame;
}

int get_contact_icon_size(const tactical_map_contact& contact)
{
	if (Ship_info[contact.ship_class].is_huge_ship()) {
		return DEFAULT_ICON_SIZE * 3;
	} else if (Ship_info[contact.ship_class].is_big_ship()) {
		return DEFAULT_ICON_SIZE * 2 + 8;
	}

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

void draw_bitmap_icon(const tactical_map_contact& contact, int bitmap, bool use_iff_filter)
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
	if (use_iff_filter) {
		gr_set_color_fast(&contact.iff_color);
	}
	gr_set_bitmap(bitmap, use_iff_filter ? GR_ALPHABLEND_FILTER : GR_ALPHABLEND_NONE, GR_BITBLT_MODE_NORMAL, 1.0f);
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
	const bool selected = same_contact(contact.id, Selected_contact);
	const int briefing_bitmap = get_briefing_icon_bitmap(contact, selected);

	if (briefing_bitmap >= 0) {
		draw_bitmap_icon(contact, briefing_bitmap, true);
	} else if (contact.radar_image_2d >= 0) {
		draw_bitmap_icon(contact, contact.radar_image_2d, false);
	} else if (contact.radar_color_image_2d >= 0) {
		draw_bitmap_icon(contact, contact.radar_color_image_2d, true);
	} else {
		draw_fallback_icon(contact);
	}

	if (contact.is_current_target) {
		gr_set_color_fast(&Tactical_target_color);
		gr_circle(contact.icon_x, contact.icon_y, get_contact_icon_size(contact) + 14, GR_RESIZE_NONE);
	}

	if (same_contact(contact.id, Hovered_contact)) {
		gr_set_color_fast(&Tactical_hover_color);
		gr_circle(contact.icon_x, contact.icon_y, get_contact_icon_size(contact) + 8, GR_RESIZE_NONE);
	}

	if (selected) {
		gr_set_color_fast(&Tactical_selected_color);
		gr_circle(contact.icon_x, contact.icon_y, get_contact_icon_size(contact) + 18, GR_RESIZE_NONE);
	}
}

void draw_contact_label(const tactical_map_contact& contact, const tactical_map_rect& map_rect)
{
	int label_w = 0;
	int label_h = 0;
	gr_get_string_size(&label_w, &label_h, contact.display_name.c_str());

	const int label_x = std::max(map_rect.x + 3, std::min(contact.icon_x - label_w / 2, map_rect.x + map_rect.w - label_w - 3));
	const int label_y = std::max(map_rect.y + 3, contact.icon_y - get_contact_icon_size(contact) / 2 - label_h - 5);

	gr_set_color_fast(&Tactical_label_bg_color);
	gr_rect(label_x - 3, label_y - 2, label_w + 6, label_h + 4, GR_RESIZE_NONE);
	gr_set_color_fast(&Color_text_normal);
	gr_printf_no_resize(label_x, label_y, "%s", contact.display_name.c_str());
}

bool command_applies_to_selected_target(int command, const tactical_map_contact* contact, bool is_wing_order)
{
	if (contact == nullptr || !contact_can_be_targeted(*contact) || Player_ship == nullptr || Player_ai == nullptr) {
		return false;
	}

	switch (command) {
		case ATTACK_TARGET_ITEM:
		case IGNORE_TARGET_ITEM:
		case DISABLE_TARGET_ITEM:
		case DISARM_TARGET_ITEM:
		case PROTECT_TARGET_ITEM:
			break;

		default:
			return false;
	}

	auto target_ai = *Player_ai;
	target_ai.target_objnum = contact->id.objnum;
	return hud_squadmsg_is_target_order_valid(static_cast<size_t>(command), &target_ai, is_wing_order);
}

bool wing_accepts_tactical_command(int wingnum, int command, const tactical_map_contact* contact)
{
	if (!wing_can_receive_tactical_orders(wingnum) || !command_applies_to_selected_target(command, contact, true)) {
		return false;
	}

	const auto& wing = Wings[wingnum];
	if (wing.special_ship < 0 || wing.special_ship >= wing.current_count) {
		return false;
	}

	const int shipnum = wing.ship_index[wing.special_ship];
	if (!ship_can_receive_tactical_orders(shipnum)) {
		return false;
	}

	const auto& shipp = Ships[shipnum];
	if (shipp.objnum < 0 || shipp.objnum >= MAX_OBJECTS || Objects[shipp.objnum].type != OBJ_SHIP) {
		return false;
	}

	if (!shipp.orders_accepted.contains(command) || !hud_squadmsg_ship_order_valid(shipnum, command)) {
		return false;
	}

	if (command == ATTACK_TARGET_ITEM && contact != nullptr && contact->shipnum >= 0 && Ships[contact->shipnum].wingnum == wingnum) {
		return false;
	}

	return true;
}

bool all_fighters_wing_accepts_tactical_command(int wingnum, int command, const tactical_map_contact* contact)
{
	if (wingnum < 0 || wingnum >= Num_wings || !command_applies_to_selected_target(command, contact, true)) {
		return false;
	}

	const auto& wing = Wings[wingnum];
	if (wing.flags[Ship::Wing_Flags::Gone, Ship::Wing_Flags::Departing] || wing.current_count <= 0) {
		return false;
	}

	if (wing.special_ship < 0 || wing.special_ship >= wing.current_count) {
		return false;
	}

	const int shipnum = wing.ship_index[wing.special_ship];
	if (!ship_can_receive_tactical_orders(shipnum)) {
		return false;
	}

	const auto& shipp = Ships[shipnum];
	if (shipp.objnum < 0 || shipp.objnum >= MAX_OBJECTS || Objects[shipp.objnum].type != OBJ_SHIP) {
		return false;
	}

	if (wing.special_ship_ship_info_index < 0 || wing.special_ship_ship_info_index >= static_cast<int>(Ship_info.size())) {
		return false;
	}

	if (Player_ship == nullptr || shipp.team != Player_ship->team || !Ship_info[wing.special_ship_ship_info_index].is_fighter_bomber()) {
		return false;
	}

	if (!shipp.orders_accepted.contains(command) || !hud_squadmsg_ship_order_valid(shipnum, command)) {
		return false;
	}

	if (command == ATTACK_TARGET_ITEM && contact != nullptr && contact->shipnum >= 0 && Ships[contact->shipnum].wingnum == wingnum) {
		return false;
	}

	return true;
}

bool all_fighters_ship_accepts_tactical_command(int shipnum, int command, const tactical_map_contact* contact)
{
	if (shipnum < 0 || shipnum >= MAX_SHIPS || !command_applies_to_selected_target(command, contact, false)) {
		return false;
	}

	if (!ship_can_receive_tactical_orders(shipnum)) {
		return false;
	}

	const auto& shipp = Ships[shipnum];
	if (shipp.objnum < 0 || shipp.objnum >= MAX_OBJECTS || Objects[shipp.objnum].type != OBJ_SHIP) {
		return false;
	}

	if (shipp.wingnum != -1) {
		return false;
	}

	if (shipp.ship_info_index < 0 || shipp.ship_info_index >= static_cast<int>(Ship_info.size()) ||
	    !Ship_info[shipp.ship_info_index].is_fighter_bomber()) {
		return false;
	}

	if (!shipp.orders_accepted.contains(command) || !hud_squadmsg_ship_order_valid(shipnum, command)) {
		return false;
	}

	if (command == PROTECT_TARGET_ITEM && contact != nullptr && contact->id.objnum == shipp.objnum) {
		return false;
	}

	return true;
}

bool all_fighters_accept_tactical_command(int command, const tactical_map_contact* contact)
{
	for (int wingnum = 0; wingnum < Num_wings; wingnum++) {
		if (all_fighters_wing_accepts_tactical_command(wingnum, command, contact)) {
			return true;
		}
	}

	for (auto so : list_range(&Ship_obj_list)) {
		const auto objp = &Objects[so->objnum];
		if (objp->flags[Object::Object_Flags::Should_be_dead] || objp->type != OBJ_SHIP) {
			continue;
		}

		if (all_fighters_ship_accepts_tactical_command(objp->instance, command, contact)) {
			return true;
		}
	}

	return false;
}

bool selected_recipient_accepts_tactical_command(int command, const tactical_map_contact* contact)
{
	if (Selected_recipient_type == tactical_recipient_type::Wing) {
		return wing_accepts_tactical_command(Selected_recipient_wing, command, contact);
	}

	return all_fighters_accept_tactical_command(command, contact);
}

void build_panel_buttons(const tactical_map_rect& info_rect)
{
	Tactical_recipient_buttons.clear();
	Tactical_command_buttons.clear();

	const int x = info_rect.x + 14;
	const int w = info_rect.w - 28;
	const int command_count = 5;
	const int panel_h = 22 + 18 + COMMAND_ROW_H + 18 + 18 + command_count * (COMMAND_ROW_H + COMMAND_ROW_GAP);
	int y = std::max(info_rect.y + 14, info_rect.y + info_rect.h - panel_h - 14);

	const int cycle_w = 30;
	const int recipient_w = std::max(60, w - cycle_w * 2 - COMMAND_ROW_GAP * 2);
	const bool can_cycle_recipients = !get_tactical_order_wings().empty();

	tactical_recipient_button prev_button;
	prev_button.rect = {x, y, cycle_w, COMMAND_ROW_H};
	prev_button.cycle_delta = -1;
	prev_button.label = "<";
	prev_button.active = can_cycle_recipients;
	Tactical_recipient_buttons.push_back(prev_button);

	tactical_recipient_button current_button;
	current_button.rect = {x + cycle_w + COMMAND_ROW_GAP, y, recipient_w, COMMAND_ROW_H};
	current_button.type = Selected_recipient_type;
	current_button.wingnum = Selected_recipient_wing;
	current_button.cycle_delta = 1;
	current_button.label = get_selected_recipient_label();
	current_button.selected = true;
	Tactical_recipient_buttons.push_back(current_button);

	tactical_recipient_button next_button;
	next_button.rect = {x + cycle_w + COMMAND_ROW_GAP + recipient_w + COMMAND_ROW_GAP, y, cycle_w, COMMAND_ROW_H};
	next_button.cycle_delta = 1;
	next_button.label = ">";
	next_button.active = can_cycle_recipients;
	Tactical_recipient_buttons.push_back(next_button);

	y += COMMAND_ROW_H + 36;
	const tactical_map_contact* contact = find_contact(Selected_contact);
	const struct {
		int command;
		int hotkey;
		const char* key_text;
	} command_defs[] = {
		{ATTACK_TARGET_ITEM, KEY_A, "A"},
		{PROTECT_TARGET_ITEM, KEY_P, "P"},
		{DISABLE_TARGET_ITEM, KEY_E, "E"},
		{DISARM_TARGET_ITEM, KEY_M, "M"},
		{IGNORE_TARGET_ITEM, KEY_I, "I"},
	};

	for (const auto& def : command_defs) {
		tactical_command_button button;
		button.rect = {x, y, w, COMMAND_ROW_H};
		button.command = def.command;
		button.hotkey = def.hotkey;
		button.label = SCP_string(def.key_text) + "  " + Player_orders[def.command].localized_name;
		button.active = selected_recipient_accepts_tactical_command(def.command, contact);
		Tactical_command_buttons.push_back(button);
		y += COMMAND_ROW_H + COMMAND_ROW_GAP;
	}
}

void draw_panel_button(const tactical_map_rect& rect, const char* text, bool selected, bool active)
{
	char fit_text[256];
	strcpy_s(fit_text, text);
	font::force_fit_string(fit_text, sizeof(fit_text), std::max(8, rect.w - 16));

	gr_set_color_fast(active ? (selected ? &Tactical_button_selected_color : &Tactical_button_color) : &Tactical_button_disabled_color);
	gr_rect(rect.x, rect.y, rect.w, rect.h, GR_RESIZE_NONE);
	gr_set_color_fast(active ? (selected ? &Tactical_selected_color : &Color_text_normal) : &Tactical_text_dim_color);
	gr_printf_no_resize(rect.x + 8, rect.y + 5, "%s", fit_text);
}

void render_command_panel(const tactical_map_rect& info_rect)
{
	const int x = info_rect.x + 14;
	int y = !Tactical_recipient_buttons.empty() ? Tactical_recipient_buttons.front().rect.y - 26 : info_rect.y + info_rect.h - 300;

	gr_set_color_fast(&Color_text_heading);
	gr_printf_no_resize(x, y, "%s", XSTR("Orders", -1));
	y += 22;

	gr_set_color_fast(&Tactical_text_dim_color);
	gr_printf_no_resize(x, y, "%s", XSTR("Recipient", -1));

	for (const auto& button : Tactical_recipient_buttons) {
		draw_panel_button(button.rect, button.label.c_str(), button.selected, button.active);
	}

	const int command_header_y = Tactical_command_buttons.empty() ? y + 28 : Tactical_command_buttons.front().rect.y - 22;
	gr_set_color_fast(&Tactical_text_dim_color);
	gr_printf_no_resize(x, command_header_y, "%s", XSTR("Command", -1));

	for (const auto& button : Tactical_command_buttons) {
		draw_panel_button(button.rect, button.label.c_str(), false, button.active);
	}
}

bool issue_tactical_command(int command)
{
	const auto contact = find_contact(Selected_contact);
	if (!selected_recipient_accepts_tactical_command(command, contact)) {
		gamesnd_play_error_beep();
		return false;
	}

	if (hud_communications_state(Player_ship) != COMM_OK) {
		HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR("Messaging is restricted due to communications damage", 331));
		gamesnd_play_error_beep();
		return false;
	}

	set_target_objnum(Player_ai, contact->id.objnum);

	if (Selected_recipient_type == tactical_recipient_type::Wing) {
		hud_squadmsg_send_wing_command(Selected_recipient_wing, command, 1);
	} else if (!hud_squadmsg_send_to_all_fighters(command)) {
		gamesnd_play_error_beep();
		return false;
	}

	HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR("Tactical order issued", -1));
	return true;
}

void cycle_tactical_recipient(int delta = 1)
{
	select_recipient_by_index(get_selected_recipient_index() + delta);
}

bool handle_panel_click(int mx, int my)
{
	for (const auto& button : Tactical_recipient_buttons) {
		if (point_in_rect(mx, my, button.rect) && button.active) {
			if (button.cycle_delta != 0) {
				cycle_tactical_recipient(button.cycle_delta);
			} else {
				Selected_recipient_type = button.type;
				Selected_recipient_wing = button.wingnum;
			}
			return true;
		}
	}

	for (const auto& button : Tactical_command_buttons) {
		if (point_in_rect(mx, my, button.rect)) {
			if (button.active) {
				issue_tactical_command(button.command);
			} else {
				gamesnd_play_error_beep();
			}
			return true;
		}
	}

	return false;
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
		draw_contact_label(contact, map_rect);
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
	gr_printf_no_resize(help_rect.x + 12, help_rect.y + 8, "%s", XSTR("Left click: select/target/order   Tab: recipient   A/P/E/M/I: orders   Right drag/arrows: pan   Wheel/+/-: zoom   R: reset", -1));
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
		render_command_panel(info_rect);
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

	render_command_panel(info_rect);
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

const tactical_map_contact* find_closest_targetable_contact(int x, int y)
{
	const tactical_map_contact* closest = nullptr;
	int best_dist_sq = INT_MAX;

	for (const auto& contact : Tactical_map_contacts) {
		if (!contact_can_be_targeted(contact)) {
			continue;
		}

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

	const auto contact = find_contact(Selected_contact);
	if (contact == nullptr || !contact_can_be_targeted(*contact)) {
		gamesnd_play_error_beep();
		return;
	}

	set_target_objnum(Player_ai, Selected_contact.objnum);
	HUD_sourced_printf(HUD_SOURCE_HIDDEN, "%s", XSTR("Target selected from tactical map", -1));
}

void handle_input(int key, const tactical_map_rect& map_rect, const tactical_map_rect& info_rect)
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

		case KEY_TAB:
			cycle_tactical_recipient();
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

		case KEY_A:
		case KEY_P:
		case KEY_E:
		case KEY_M:
		case KEY_I:
			for (const auto& button : Tactical_command_buttons) {
				if (button.hotkey == masked_key) {
					issue_tactical_command(button.command);
					break;
				}
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

		if (point_in_rect(mx, my, info_rect) && handle_panel_click(mx, my)) {
			return;
		}

		const auto closest_contact = point_in_rect(mx, my, map_rect) ? find_closest_targetable_contact(mx, my) : nullptr;
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
	Selected_recipient_type = tactical_recipient_type::AllFighters;
	Selected_recipient_wing = -1;
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
	normalize_selected_recipient();

	if (Tactical_map_view.auto_fit_pending) {
		auto_fit_contacts(map_rect);
		Tactical_map_view.auto_fit_pending = false;
	}

	project_contacts(map_rect);
	update_hover(map_rect);
	build_panel_buttons(info_rect);
	handle_input(key, map_rect, info_rect);

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
	Tactical_recipient_buttons.clear();
	Tactical_command_buttons.clear();
	for (auto anim : Tactical_loaded_briefing_anims) {
		if (anim != nullptr && anim->first_frame >= 0) {
			bm_unload(anim->first_frame);
			anim->first_frame = -1;
		}
	}
	Tactical_loaded_briefing_anims.clear();
	clear_contact_id(Selected_contact);
	clear_contact_id(Hovered_contact);
	Tactical_map_active = false;
}
