// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2010-2013 blue42u, Jonathon Anderson <anderjon@umail.iu.edu>
// Copyright (C) 2010-2013 kwolekr, Ryan Kwolek <kwolekr@minetest.net>

#include "hud.h"
#include <string>
#include <iostream>
#include <cmath>
#include <unordered_set>
#include "settings.h"
#include "util/numeric.h"
#include "log.h"
#include "client.h"
#include "inventory.h"
#include "shader.h"
#include "localplayer.h"
#include "camera.h"
#include "fontengine.h"
#include "guiscalingfilter.h"
#include "mesh.h"
#include "client/renderingengine.h"
#include "client/minimap.h"
#include "client/texturesource.h"
#include "gui/touchcontrols.h"
#include "util/enriched_string.h"
#include "irrlicht_changes/CGUITTFont.h"
#include "gui/drawItemStack.h"
#include <ICameraSceneNode.h>
#include <IMesh.h>

#define OBJECT_CROSSHAIR_LINE_SIZE 8
#define CROSSHAIR_LINE_SIZE 10

static void setting_changed_callback(const std::string &name, void *data)
{
	static_cast<Hud*>(data)->readScalingSetting();
}

Hud::Hud(Client *client, LocalPlayer *player,
		Inventory *inventory)
{
	driver            = RenderingEngine::get_video_driver();
	this->client      = client;
	this->player      = player;
	this->inventory   = inventory;

	readScalingSetting();
	g_settings->registerChangedCallback("dpi_change_notifier", setting_changed_callback, this);
	g_settings->registerChangedCallback("display_density_factor", setting_changed_callback, this);
	g_settings->registerChangedCallback("hud_scaling", setting_changed_callback, this);

	for (auto &hbar_color : hbar_colors)
		hbar_color = video::SColor(255, 255, 255, 255);

	tsrc = client->getTextureSource();

	v3f crosshair_color = g_settings->getV3F("crosshair_color").value_or(v3f());
	u32 cross_r = rangelim(myround(crosshair_color.X), 0, 255);
	u32 cross_g = rangelim(myround(crosshair_color.Y), 0, 255);
	u32 cross_b = rangelim(myround(crosshair_color.Z), 0, 255);
	u32 cross_a = rangelim(g_settings->getS32("crosshair_alpha"), 0, 255);
	crosshair_argb = video::SColor(cross_a, cross_r, cross_g, cross_b);

	v3f selectionbox_color = g_settings->getV3F("selectionbox_color").value_or(v3f());
	u32 sbox_r = rangelim(myround(selectionbox_color.X), 0, 255);
	u32 sbox_g = rangelim(myround(selectionbox_color.Y), 0, 255);
	u32 sbox_b = rangelim(myround(selectionbox_color.Z), 0, 255);
	selectionbox_argb = video::SColor(255, sbox_r, sbox_g, sbox_b);

	use_crosshair_image = tsrc->isKnownSourceImage("crosshair.png");
	use_object_crosshair_image = tsrc->isKnownSourceImage("object_crosshair.png");

	m_selection_boxes.clear();
	m_halo_boxes.clear();

	std::string mode_setting = g_settings->get("node_highlighting");

	if (mode_setting == "halo") {
		m_mode = HIGHLIGHT_HALO;
	} else if (mode_setting == "none") {
		m_mode = HIGHLIGHT_NONE;
	} else {
		m_mode = HIGHLIGHT_BOX;
	}

	// Initialize m_selection_material
	IShaderSource *shdrsrc = client->getShaderSource();
	if (m_mode == HIGHLIGHT_HALO) {
		auto shader_id = shdrsrc->getShaderRaw("selection_shader", true);
		m_selection_material.MaterialType = shdrsrc->getShaderInfo(shader_id).material;
	} else {
		m_selection_material.MaterialType = video::EMT_SOLID;
	}

	if (m_mode == HIGHLIGHT_BOX) {
		m_selection_material.Thickness =
			rangelim(g_settings->getS16("selectionbox_width"), 1, 5);
	} else if (m_mode == HIGHLIGHT_HALO) {
		m_selection_material.setTexture(0, tsrc->getTextureForMesh("halo.png"));
		m_selection_material.BackfaceCulling = true;
	} else {
		m_selection_material.MaterialType = video::EMT_SOLID;
	}

	// Initialize m_block_bounds_material
	m_block_bounds_material.MaterialType = video::EMT_SOLID;
	m_block_bounds_material.Thickness =
			rangelim(g_settings->getS16("selectionbox_width"), 1, 5);

	// Prepare mesh for compass drawing
	m_rotation_mesh_buffer.reset(new scene::SMeshBuffer());
	auto *b = m_rotation_mesh_buffer.get();
	auto &vertices = b->Vertices->Data;
	auto &indices = b->Indices->Data;
	vertices.resize(4);
	indices.resize(6);

	video::SColor white(255, 255, 255, 255);
	v3f normal(0.f, 0.f, 1.f);

	vertices[0] = video::S3DVertex(v3f(-1.f, -1.f, 0.f), normal, white, v2f(0.f, 1.f));
	vertices[1] = video::S3DVertex(v3f(-1.f,  1.f, 0.f), normal, white, v2f(0.f, 0.f));
	vertices[2] = video::S3DVertex(v3f( 1.f,  1.f, 0.f), normal, white, v2f(1.f, 0.f));
	vertices[3] = video::S3DVertex(v3f( 1.f, -1.f, 0.f), normal, white, v2f(1.f, 1.f));

	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 2;
	indices[3] = 2;
	indices[4] = 3;
	indices[5] = 0;

	b->getMaterial().MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
	b->setHardwareMappingHint(scene::EHM_STATIC);

#if IS_VOPI_ENGINE
	m_hud_padding = g_settings->getU16("hud_hotbar_bottom_margin");
#endif
}

void Hud::readScalingSetting()
{
	m_hud_scaling      = g_settings->getFloat("hud_scaling", 0.5f, 20.0f);
	m_scale_factor     = m_hud_scaling * RenderingEngine::getDisplayDensity();
	m_hotbar_imagesize = std::floor(HOTBAR_IMAGE_SIZE *
		RenderingEngine::getDisplayDensity() + 0.5f);
	m_hotbar_imagesize *= m_hud_scaling;
	m_padding = m_hotbar_imagesize / 12;
}

Hud::~Hud()
{
	g_settings->deregisterAllChangedCallbacks(this);

	if (m_selection_mesh)
		m_selection_mesh->drop();
}

void Hud::drawItem(const ItemStack &item, const core::rect<s32>& rect,
		bool selected)
{
	if (selected) {
		/* draw highlighting around selected item */
		if (use_hotbar_selected_image) {
			core::rect<s32> imgrect2 = rect;
			imgrect2.UpperLeftCorner.X  -= (m_padding*2);
			imgrect2.UpperLeftCorner.Y  -= (m_padding*2);
			imgrect2.LowerRightCorner.X += (m_padding*2);
			imgrect2.LowerRightCorner.Y += (m_padding*2);
				video::ITexture *texture = tsrc->getTexture(hotbar_selected_image);
				core::dimension2di imgsize(texture->getOriginalSize());
			draw2DImageFilterScaled(driver, texture, imgrect2,
					core::rect<s32>(core::position2d<s32>(0,0), imgsize),
					NULL, hbar_colors, true);
		} else {
			video::SColor c_outside(255,255,0,0);
			//video::SColor c_outside(255,0,0,0);
			//video::SColor c_inside(255,192,192,192);
			s32 x1 = rect.UpperLeftCorner.X;
			s32 y1 = rect.UpperLeftCorner.Y;
			s32 x2 = rect.LowerRightCorner.X;
			s32 y2 = rect.LowerRightCorner.Y;
			// Black base borders
			driver->draw2DRectangle(c_outside,
				core::rect<s32>(
				v2s32(x1 - m_padding, y1 - m_padding),
				v2s32(x2 + m_padding, y1)
				), NULL);
			driver->draw2DRectangle(c_outside,
				core::rect<s32>(
				v2s32(x1 - m_padding, y2),
				v2s32(x2 + m_padding, y2 + m_padding)
				), NULL);
			driver->draw2DRectangle(c_outside,
				core::rect<s32>(
				v2s32(x1 - m_padding, y1),
					v2s32(x1, y2)
				), NULL);
			driver->draw2DRectangle(c_outside,
				core::rect<s32>(
					v2s32(x2, y1),
				v2s32(x2 + m_padding, y2)
				), NULL);
			/*// Light inside borders
			driver->draw2DRectangle(c_inside,
				core::rect<s32>(
					v2s32(x1 - padding/2, y1 - padding/2),
					v2s32(x2 + padding/2, y1)
				), NULL);
			driver->draw2DRectangle(c_inside,
				core::rect<s32>(
					v2s32(x1 - padding/2, y2),
					v2s32(x2 + padding/2, y2 + padding/2)
				), NULL);
			driver->draw2DRectangle(c_inside,
				core::rect<s32>(
					v2s32(x1 - padding/2, y1),
					v2s32(x1, y2)
				), NULL);
			driver->draw2DRectangle(c_inside,
				core::rect<s32>(
					v2s32(x2, y1),
					v2s32(x2 + padding/2, y2)
				), NULL);
			*/
		}
	}

	video::SColor bgcolor2(128, 0, 0, 0);
	if (!use_hotbar_image)
		driver->draw2DRectangle(bgcolor2, rect, NULL);
	drawItemStack(driver, g_fontengine->getFont(), item, rect, NULL,
		client, selected ? IT_ROT_SELECTED : IT_ROT_NONE);
}

// NOTE: selectitem = 0 -> no selected; selectitem is 1-based
// mainlist can be NULL, but draw the frame anyway.
void Hud::drawItems(v2s32 screen_pos, v2s32 screen_offset, s32 itemcount, v2f alignment,
		s32 inv_offset, InventoryList *mainlist, u16 selectitem, u16 direction,
		bool is_hotbar)
{
	s32 height  = m_hotbar_imagesize + m_padding * 2;
	s32 width   = (itemcount - inv_offset) * (m_hotbar_imagesize + m_padding * 2);

	if (direction == HUD_DIR_TOP_BOTTOM || direction == HUD_DIR_BOTTOM_TOP) {
		s32 tmp = height;
		height = width;
		width = tmp;
	}

	// Position: screen_pos + screen_offset + alignment
	v2s32 pos(screen_offset.X * m_scale_factor, screen_offset.Y * m_scale_factor);
	pos += screen_pos;
	pos.X += (alignment.X - 1.0f) * (width * 0.5f);
	pos.Y += (alignment.Y - 1.0f) * (height * 0.5f);

	// Store hotbar_image in member variable, used by drawItem()
	if (hotbar_image != player->hotbar_image) {
		hotbar_image = player->hotbar_image;
		use_hotbar_image = !hotbar_image.empty();
	}

	// Store hotbar_selected_image in member variable, used by drawItem()
	if (hotbar_selected_image != player->hotbar_selected_image) {
		hotbar_selected_image = player->hotbar_selected_image;
		use_hotbar_selected_image = !hotbar_selected_image.empty();
	}

	// draw customized item background
	if (use_hotbar_image) {
		core::rect<s32> imgrect2(-m_padding/2, -m_padding/2,
			width+m_padding/2, height+m_padding/2);
		core::rect<s32> rect2 = imgrect2 + pos;
		video::ITexture *texture = tsrc->getTexture(hotbar_image);
		core::dimension2di imgsize(texture->getOriginalSize());
		draw2DImageFilterScaled(driver, texture, rect2,
			core::rect<s32>(core::position2d<s32>(0,0), imgsize),
			NULL, hbar_colors, true);
	}

	// Draw items
	core::rect<s32> imgrect(0, 0, m_hotbar_imagesize, m_hotbar_imagesize);
	const s32 list_max = std::min(itemcount, (s32) (mainlist ? mainlist->getSize() : 0 ));
	for (s32 i = inv_offset; i < list_max; i++) {
		s32 fullimglen = m_hotbar_imagesize + m_padding * 2;

		v2s32 steppos;
		switch (direction) {
		case HUD_DIR_RIGHT_LEFT:
			steppos = v2s32(m_padding + (list_max - 1 - i - inv_offset) * fullimglen, m_padding);
			break;
		case HUD_DIR_TOP_BOTTOM:
			steppos = v2s32(m_padding, m_padding + (i - inv_offset) * fullimglen);
			break;
		case HUD_DIR_BOTTOM_TOP:
			steppos = v2s32(m_padding, m_padding + (list_max - 1 - i - inv_offset) * fullimglen);
			break;
		default:
			steppos = v2s32(m_padding + (i - inv_offset) * fullimglen, m_padding);
			break;
		}

		core::rect<s32> item_rect = imgrect + pos + steppos;

		drawItem(mainlist->getItem(i), item_rect, (i + 1) == selectitem);

		if (is_hotbar && g_touchcontrols)
			g_touchcontrols->registerHotbarRect(i, item_rect);
	}
}

bool Hud::hasElementOfType(HudElementType type)
{
	for (HudElement *e : player->getHudElements()) {
		if (e && e->type == type)
			return true;
	}
	return false;
}

// Calculates screen position of waypoint. Returns true if waypoint is visible (in front of the player), else false.
bool Hud::calculateScreenPos(const v3s16 &camera_offset, HudElement *e, v2s32 *pos)
{
	v3f w_pos = e->world_pos * BS;
	scene::ICameraSceneNode* camera =
		client->getSceneManager()->getActiveCamera();
	w_pos -= intToFloat(camera_offset, BS);
	core::matrix4 trans = camera->getProjectionMatrix();
	trans *= camera->getViewMatrix();
	f32 transformed_pos[4] = { w_pos.X, w_pos.Y, w_pos.Z, 1.0f };
	trans.multiplyWith1x4Matrix(transformed_pos);
	if (transformed_pos[3] < 0)
		return false;
	f32 zDiv = transformed_pos[3] == 0.0f ? 1.0f :
		core::reciprocal(transformed_pos[3]);
	pos->X = m_screensize.X * (0.5 * transformed_pos[0] * zDiv + 0.5);
	pos->Y = m_screensize.Y * (0.5 - transformed_pos[1] * zDiv * 0.5);
	return true;
}

#if IS_VOPI_ENGINE
core::rect<s32> Hud::getImageElementRect(const HudElement *e, v2s32 pos) const
{
	video::ITexture *texture = tsrc->getTexture(e->text);
	core::dimension2di imgsize = texture
			? core::dimension2di(texture->getOriginalSize())
			: core::dimension2di(0, 0);

	// Anchor above the hotbar: replace the vertical reference with the hotbar's
	// top edge (or the screen bottom when the hotbar is hidden). x / alignment /
	// offset then apply as usual, so the element sits a fixed gap above the
	// hotbar on every device regardless of DPI / hud_scaling.
	if (e->anchor_above_hotbar) {
		pos.Y = (player->hud_flags & HUD_FLAG_HOTBAR_VISIBLE)
				? m_hotbar_top_y : (s32) m_screensize.Y;
	}

	v2s32 dstsize(imgsize.Width * e->scale.X * m_scale_factor,
			imgsize.Height * e->scale.Y * m_scale_factor);
	if (e->scale.X < 0)
		dstsize.X = m_screensize.X * (e->scale.X * -0.01);
	if (e->scale.Y < 0)
		dstsize.Y = m_screensize.Y * (e->scale.Y * -0.01);
	// Explicit size for 9-slice images. Use width/height (not getArea, which
	// is zero for a valid 1-D middle rect — kept consistent with the draw-time
	// 9-slice dispatch below).
	if ((e->middle.getWidth() != 0 || e->middle.getHeight() != 0) && e->size.X > 0 && e->size.Y > 0) {
		dstsize.X = e->size.X * m_scale_factor;
		dstsize.Y = e->size.Y * m_scale_factor;
	}
	v2s32 offset((e->align.X - 1.0) * dstsize.X / 2,
			(e->align.Y - 1.0) * dstsize.Y / 2);
	core::rect<s32> rect(0, 0, dstsize.X, dstsize.Y);
	rect += pos + offset + v2s32(e->offset.X * m_scale_factor,
			e->offset.Y * m_scale_factor);
	return rect;
}

std::vector<std::pair<u32, core::rect<s32>>> Hud::getTouchableHudRects()
{
	std::vector<std::pair<u32, core::rect<s32>>> out;
	const auto &elems = player->getHudElements();
	for (u32 i = 0; i < elems.size(); i++) {
		HudElement *e = elems[i];
		if (e && e->type == HUD_ELEM_IMAGE && e->touchable) {
			v2s32 pos(floor(e->pos.X * (float) m_screensize.X + 0.5),
					floor(e->pos.Y * (float) m_screensize.Y + 0.5));
			core::rect<s32> rect = getImageElementRect(e, pos);
			// A touchable button whose texture can't be resolved collapses to
			// a zero-size rect and is silently unclickable. Warn once per name
			// so a mod author's typo doesn't look like a dead button.
			if (rect.getArea() == 0) {
				static std::unordered_set<std::string> warned;
				if (warned.insert(e->text).second)
					warningstream << "HUD: touchable image '" << e->text
							<< "' has no resolvable texture; button is unclickable"
							<< std::endl;
			}
			out.emplace_back(i, rect);
		}
	}
	return out;
}
#endif

#if IS_VOPI_ENGINE
// Greedily word-wrap one already-newline-free EnrichedString to `wrap_px` pixels
// using the real font, preserving per-character colors. Appends the resulting
// visual lines to `out`. A single word wider than wrap_px is kept whole (it will
// overflow) rather than split mid-word.
static void wrapEnrichedLine(const EnrichedString &line, gui::IGUIFont *font,
		s32 wrap_px, std::vector<EnrichedString> &out)
{
	const std::wstring &ws = line.getString();
	const size_t n = ws.size();
	if (n == 0) {
		out.push_back(line);
		return;
	}

	// Emit [start, end) with trailing spaces trimmed: they neither render nor
	// should inflate the reported block width.
	auto emit = [&](size_t start, size_t end) {
		while (end > start && ws[end - 1] == L' ')
			end--;
		out.push_back(line.substr(start, end - start));
	};

	size_t seg_start = 0; // first char of the visual line currently being built
	size_t i = 0;
	s32 cur_w = 0;        // pixel width of [seg_start, i)

	while (i < n) {
		// One token = a run of non-spaces (the word) plus any trailing spaces.
		const size_t word_start = i;
		while (i < n && ws[i] != L' ')
			i++;
		while (i < n && ws[i] == L' ')
			i++;
		const EnrichedString tok = line.substr(word_start, i - word_start);
		const s32 tok_w = (s32) font->getDimension(tok.c_str()).Width;

		if (word_start == seg_start) {
			// First token on the line: always keep it, even if it overflows.
			cur_w = tok_w;
		} else if (cur_w + tok_w <= wrap_px) {
			cur_w += tok_w;
		} else {
			// Doesn't fit: flush the current visual line, start a new one here.
			emit(seg_start, word_start);
			seg_start = word_start;
			cur_w = tok_w;
		}
	}
	emit(seg_start, n);
}

// Render a HUD_ELEM_TEXT that has max_width > 0: wrap the text in the real font,
// draw it line by line, and stash the measured block size (in logical pixels) on
// the element so Game::processUserInput can report it back to server-side Lua.
static void drawWrappedHudText(HudElement *e, gui::IGUIFont *textfont,
		gui::CGUITTFont *ttfont, const EnrichedString &text,
		const video::SColor &color, v2s32 pos, float scale_factor)
{
	// Clamp the float product BEFORE the s32 cast: max_width is server-sent and
	// unbounded, and casting an out-of-range float to s32 is undefined behavior.
	// A malicious server can bypass the Lua-read clamp via a crafted HUDADD, so
	// this client-side guard at the actual cast site is the real protection.
	const s32 wrap_px = std::max<s32>(1, (s32) rangelim(e->max_width * scale_factor, 0.0f, 32767.0f));

	// Strip carriage returns first: the font backends treat '\r' as a hard line
	// break (and getNextLine only splits on '\n'), which would desync wrapping
	// from measurement. Only allocate a copy when a '\r' is actually present.
	const EnrichedString *src = &text;
	EnrichedString sanitized;
	if (text.getString().find(L'\r') != std::wstring::npos) {
		for (size_t k = 0; k < text.size(); k++) {
			if (text.getString()[k] != L'\r')
				sanitized.addChar(text, k);
		}
		src = &sanitized;
	}

	// Wrap every explicit (newline-delimited) line to the pixel width.
	std::vector<EnrichedString> lines;
	size_t str_pos = 0;
	while (str_pos < src->size())
		wrapEnrichedLine(src->getNextLine(&str_pos), textfont, wrap_px, lines);
	if (lines.empty())
		lines.push_back(*src);

	// Extra gap between wrapped lines (logical px → device px; may be negative).
	const s32 line_spacing = (s32) rangelim(e->line_spacing * scale_factor, -32767.0f, 32767.0f);

	// Measure each wrapped line ONCE (device px) and reuse the dimensions for both
	// the block size and the per-line draw below — no redundant getDimension passes.
	std::vector<core::dimension2d<u32>> dims;
	dims.reserve(lines.size());
	s32 block_w = 0, block_h = 0;
	for (size_t k = 0; k < lines.size(); k++) {
		const core::dimension2d<u32> ls = textfont->getDimension(lines[k].c_str());
		dims.push_back(ls);
		if ((s32) ls.Width > block_w)
			block_w = ls.Width;
		block_h += (s32) ls.Height;
		if (k + 1 < lines.size())
			block_h += line_spacing;
	}
	if (block_h < 0)
		block_h = 0;

	const core::rect<s32> rect(0, 0, wrap_px, block_h);
	v2s32 offset(0, (e->align.Y - 1.0) * (block_h / 2));
	const v2s32 offs((s32) (e->offset.X * scale_factor),
			(s32) (e->offset.Y * scale_factor));

	for (size_t k = 0; k < lines.size(); k++) {
		const core::dimension2d<u32> &ls = dims[k];
		const v2s32 line_offset((e->align.X - 1.0) * ((s32) ls.Width / 2), 0);
		if (ttfont)
			ttfont->draw(lines[k], rect + pos + offset + offs + line_offset);
		else
			textfont->draw(lines[k].c_str(), rect + pos + offset + offs + line_offset, color);
		offset.Y += (s32) ls.Height + line_spacing;
	}

	// Report the measured size in logical pixels (device px / scale_factor) back
	// to the mod. ceil() so the background box never clips the text; flag a change
	// only when it actually differs to avoid spamming the fields channel. The
	// reported width may be up to ~1px larger than max_width due to rounding.
	const v2s32 logical((s32) ceil(block_w / scale_factor),
			(s32) ceil(block_h / scale_factor));
	if (logical != e->measured_size) {
		e->measured_size = logical;
		e->measured_dirty = true;
	}
}
#endif

#if IS_VOPI_ENGINE
void Hud::drawLuaElements(const v3s16 &camera_offset, s16 z_index_min, s16 z_index_max)
#else
void Hud::drawLuaElements(const v3s16 &camera_offset)
#endif
{
	const u32 text_height = g_fontengine->getTextHeight();
	gui::IGUIFont *const font = g_fontengine->getFont();

	std::vector<HudElement*> elems;

	elems.reserve(player->getHudElements().size());
	for (HudElement *e : player->getHudElements()) {
#if IS_VOPI_ENGINE
		if (e && e->z_index >= z_index_min && e->z_index <= z_index_max)
			elems.push_back(e);
#else
		if (e)
			elems.push_back(e);
#endif
	}

	// Add builtin elements if the server doesn't send them.
	// Declared here such that they have the same lifetime as the elems vector.
	// Builtin fallbacks have an implicit z_index of 0, so they're only
	// included when the requested range covers 0 (default / "under formspec" pass).
	HudElement minimap;
	HudElement hotbar;
#if IS_VOPI_ENGINE
	const bool include_builtins = (z_index_min <= 0 && z_index_max >= 0);
#else
	constexpr bool include_builtins = true;
#endif
	if (include_builtins && client->getProtoVersion() < 44 && (player->hud_flags & HUD_FLAG_MINIMAP_VISIBLE)) {
		minimap = {HUD_ELEM_MINIMAP, v2f(1, 0), "", v2f(), "", 0 , 0, 0, v2f(-1, 1),
				v2f(-10, 10), v3f(), v2f(256.0f, 256.0f), 0, "", 0};
		elems.push_back(&minimap);
	}
	if (include_builtins && client->getProtoVersion() < 46 && player->hud_flags & HUD_FLAG_HOTBAR_VISIBLE) {
		hotbar = {HUD_ELEM_HOTBAR, v2f(0.5, 1), "", v2f(), "", 0 , 0, 0, v2f(0, -1),
				v2f(0, -4), v3f(), v2f(), 0, "", 0};
		elems.push_back(&hotbar);
	}

	// Reorder by Z-index for rendering
	// Note: we don't guarantee rendering in ID order, but it used to work so let's keep it.
	std::stable_sort(elems.begin(), elems.end(), [] (HudElement *l, HudElement *r) {
		return l->z_index < r->z_index;
	});

#if IS_VOPI_ENGINE
	// Resolve the held touchable button (by id) to a live element this frame, so
	// no server-removable pointer is held across the client-event pump.
	HudElement *pressed_elem = nullptr;
	if (m_pressed_touchable_id) {
		const auto &hels = player->getHudElements();
		if (*m_pressed_touchable_id < hels.size())
			pressed_elem = hels[*m_pressed_touchable_id];
	}
#endif

	for (HudElement *e : elems) {

		v2s32 pos(floor(e->pos.X * (float) m_screensize.X + 0.5),
				floor(e->pos.Y * (float) m_screensize.Y + 0.5));
#if IS_VOPI_ENGINE
		// Anchor above the hotbar for ALL element types (text labels included),
		// not just images. getImageElementRect re-applies the same override for
		// the IMAGE path (idempotent) and for hit-testing via getTouchableHudRects.
		if (e->anchor_above_hotbar) {
			pos.Y = (player->hud_flags & HUD_FLAG_HOTBAR_VISIBLE)
					? m_hotbar_top_y : (s32) m_screensize.Y;
		}
#endif
		switch (e->type) {
			case HUD_ELEM_TEXT: {
				unsigned int font_size = g_fontengine->getDefaultFontSize();

				if (e->size.X > 0)
					font_size *= e->size.X;

#if defined(__ANDROID__) || defined(__IOS__)
				// The text size on Android is not proportional with the actual scaling
				// FIXME: why do we have such a weird unportable hack??
				if (font_size > 3 && e->offset.X < -20)
					font_size -= 3;
#endif
				auto textfont = g_fontengine->getFont(FontSpec(font_size,
					(e->style & HUD_STYLE_MONO) ? FM_Mono : FM_Unspecified,
					e->style & HUD_STYLE_BOLD, e->style & HUD_STYLE_ITALIC));

				gui::CGUITTFont *ttfont = nullptr;
				if (textfont->getType() == gui::EGFT_CUSTOM)
					ttfont = static_cast<gui::CGUITTFont *>(textfont);

				u32 num = e->number;
				u8 alpha = (num >> 24) & 0xFF;
				if (alpha == 0)
					alpha = 0xFF; // Backwards compatibility

				video::SColor color = video::SColor(alpha,
						(num >> 16) & 0xFF,
						(num >> 8)  & 0xFF,
						(num >> 0)  & 0xFF);

				EnrichedString text(unescape_string(utf8_to_wide(e->text)), color);

#if IS_VOPI_ENGINE
				// VOPI: client-side word wrapping to a pixel width. The client owns
				// the layout (real font + DPI) and reports the measured size back to
				// Lua; everything else (background, stacking) stays mod-side.
				if (e->max_width > 0) {
					drawWrappedHudText(e, textfont, ttfont, text, color, pos,
							m_scale_factor);
					break;
				}
#endif
				core::dimension2d<u32> textsize = textfont->getDimension(text.c_str());

				v2s32 offset(0, (e->align.Y - 1.0) * (textsize.Height / 2));
				core::rect<s32> size(0, 0, e->scale.X * m_scale_factor,
						text_height * e->scale.Y * m_scale_factor);
				v2s32 offs(e->offset.X * m_scale_factor,
						e->offset.Y * m_scale_factor);

				// Draw each line
				// See also: GUIFormSpecMenu::parseLabel
				size_t str_pos = 0;
				while (str_pos < text.size()) {
					EnrichedString line = text.getNextLine(&str_pos);

					core::dimension2d<u32> linesize = textfont->getDimension(line.c_str());
					v2s32 line_offset((e->align.X - 1.0) * (linesize.Width / 2), 0);
					if (ttfont)
						ttfont->draw(line, size + pos + offset + offs + line_offset);
					else
						textfont->draw(line.c_str(), size + pos + offset + offs + line_offset, color);
					offset.Y += linesize.Height;
				}
				break; }
			case HUD_ELEM_STATBAR: {
				v2s32 offs(e->offset.X, e->offset.Y);
				drawStatbar(pos, HUD_CORNER_UPPER, e->dir, e->text, e->text2,
					e->number, e->item, offs, e->size);
				break; }
			case HUD_ELEM_INVENTORY: {
				InventoryList *inv = inventory->getList(e->text);
				if (!inv)
					warningstream << "HUD: Unknown inventory list. name=" << e->text << std::endl;
				drawItems(pos, v2s32(e->offset.X, e->offset.Y), e->number, e->align, 0,
					inv, e->item, e->dir, false);
				break; }
			case HUD_ELEM_WAYPOINT: {
				if (!calculateScreenPos(camera_offset, e, &pos))
					break;

				pos += v2s32(e->offset.X, e->offset.Y);
				video::SColor color(255, (e->number >> 16) & 0xFF,
										 (e->number >> 8)  & 0xFF,
										 (e->number >> 0)  & 0xFF);
				std::wstring text = unescape_translate(utf8_to_wide(e->name));
				const std::string &unit = e->text;
				// Waypoints reuse the item field to store precision,
				// item = precision + 1 and item = 0 <=> precision = 10 for backwards compatibility.
				// Also see `push_hud_element`.
				u32 item = e->item;
				float precision = (item == 0) ? 10.0f : (item - 1.f);
				bool draw_precision = precision > 0;

				core::rect<s32> bounds(0, 0, font->getDimension(text.c_str()).Width, (draw_precision ? 2:1) * text_height);
				pos.Y += (e->align.Y - 1.0) * bounds.getHeight() / 2;
				bounds += pos;
				font->draw(text.c_str(), bounds + v2s32((e->align.X - 1.0) * bounds.getWidth() / 2, 0), color);
				if (draw_precision) {
					std::ostringstream os;
					v3f p_pos = player->getPosition() / BS;
					float distance = std::floor(precision * p_pos.getDistanceFrom(e->world_pos)) / precision;
					os << distance << unit;
					text = unescape_translate(utf8_to_wide(os.str()));
					bounds.LowerRightCorner.X = bounds.UpperLeftCorner.X + font->getDimension(text.c_str()).Width;
					font->draw(text.c_str(), bounds + v2s32((e->align.X - 1.0f) * bounds.getWidth() / 2, text_height), color);
				}
				break; }
			case HUD_ELEM_IMAGE_WAYPOINT: {
				if (!calculateScreenPos(camera_offset, e, &pos))
					break;
				[[fallthrough]];
			}
			case HUD_ELEM_IMAGE: {
#if IS_VOPI_ENGINE
				// Tappable buttons draw their pressed_texture while held.
				const std::string &img_name = (e->touchable &&
						e == pressed_elem && !e->pressed_text.empty())
						? e->pressed_text : e->text;
				video::ITexture *texture = tsrc->getTexture(img_name);
#else
				video::ITexture *texture = tsrc->getTexture(e->text);
#endif
				if (!texture)
					continue;

				const video::SColor color(255, 255, 255, 255);
				const video::SColor colors[] = {color, color, color, color};
				core::dimension2di imgsize(texture->getOriginalSize());
#if IS_VOPI_ENGINE
				// Single source of truth for the on-screen rect (shared with
				// getImageElementRect / getTouchableHudRects for hit-testing).
				core::rect<s32> rect = getImageElementRect(e, pos);
#else
				v2s32 dstsize(imgsize.Width * e->scale.X * m_scale_factor,
				              imgsize.Height * e->scale.Y * m_scale_factor);
				if (e->scale.X < 0)
					dstsize.X = m_screensize.X * (e->scale.X * -0.01);
				if (e->scale.Y < 0)
					dstsize.Y = m_screensize.Y * (e->scale.Y * -0.01);
				v2s32 offset((e->align.X - 1.0) * dstsize.X / 2,
				             (e->align.Y - 1.0) * dstsize.Y / 2);
				core::rect<s32> rect(0, 0, dstsize.X, dstsize.Y);
				rect += pos + offset + v2s32(e->offset.X * m_scale_factor,
				                             e->offset.Y * m_scale_factor);
#endif
				core::rect<s32> srcrect(core::position2d<s32>(0, 0), imgsize);
#if IS_VOPI_ENGINE
				if (e->middle.getWidth() != 0 || e->middle.getHeight() != 0) {
				  draw2DImage9Slice(driver, texture, rect, srcrect, e->middle, NULL, colors, e->middle_scale);
				} else {
				  draw2DImageFilterScaled(driver, texture, rect, srcrect, NULL, colors, true);
				}
#else
				draw2DImageFilterScaled(driver, texture, rect,
				  srcrect, NULL, colors, true);
#endif
				break; }
			case HUD_ELEM_COMPASS: {
				video::ITexture *texture = tsrc->getTexture(e->text);
				if (!texture)
					continue;

				// Positionning :
				v2s32 dstsize(e->size.X, e->size.Y);
				if (e->size.X < 0)
					dstsize.X = m_screensize.X * (e->size.X * -0.01);
				if (e->size.Y < 0)
					dstsize.Y = m_screensize.Y * (e->size.Y * -0.01);

				if (dstsize.X <= 0 || dstsize.Y <= 0)
					return; // Avoid zero divides

				// Angle according to camera view
				scene::ICameraSceneNode *cam = client->getSceneManager()->getActiveCamera();
				v3f fore = cam->getAbsoluteTransformation()
						.rotateAndScaleVect(v3f(0.f, 0.f, 1.f));
				int angle = - fore.getHorizontalAngle().Y;

				// Limit angle and ajust with given offset
				angle = (angle + (int)e->number) % 360;

				core::rect<s32> dstrect(0, 0, dstsize.X, dstsize.Y);
				dstrect += pos + v2s32(
								(e->align.X - 1.0) * dstsize.X / 2,
								(e->align.Y - 1.0) * dstsize.Y / 2) +
						v2s32(e->offset.X * m_hud_scaling, e->offset.Y * m_hud_scaling);

				switch (e->dir) {
				case HUD_COMPASS_ROTATE:
					drawCompassRotate(e, texture, dstrect, angle);
					break;
				case HUD_COMPASS_ROTATE_REVERSE:
					drawCompassRotate(e, texture, dstrect, -angle);
					break;
				case HUD_COMPASS_TRANSLATE:
					drawCompassTranslate(e, texture, dstrect, angle);
					break;
				case HUD_COMPASS_TRANSLATE_REVERSE:
					drawCompassTranslate(e, texture, dstrect, -angle);
					break;
				default:
					break;
				}
				break; }
			case HUD_ELEM_MINIMAP: {
				if (!client->getMinimap())
					break;
				// Draw a minimap of size "size"
				v2s32 dstsize(e->size.X * m_scale_factor,
				              e->size.Y * m_scale_factor);

				// Only one percentage is supported to avoid distortion.
				if (e->size.X < 0)
					dstsize.X = dstsize.Y = m_screensize.X * (e->size.X * -0.01);
				else if (e->size.Y < 0)
					dstsize.X = dstsize.Y = m_screensize.Y * (e->size.Y * -0.01);

				if (dstsize.X <= 0 || dstsize.Y <= 0)
					return;

				v2s32 offset((e->align.X - 1.0) * dstsize.X / 2,
				             (e->align.Y - 1.0) * dstsize.Y / 2);
				core::rect<s32> rect(0, 0, dstsize.X, dstsize.Y);
				rect += pos + offset + v2s32(e->offset.X * m_scale_factor,
				                             e->offset.Y * m_scale_factor);
				client->getMinimap()->drawMinimap(rect);
				break; }
			case HUD_ELEM_HOTBAR: {
				drawHotbar(pos, e->offset, e->dir, e->align);
				break; }
			default:
				infostream << "Hud::drawLuaElements: ignoring drawform " << e->type
					<< " due to unrecognized type" << std::endl;
		}
	}
}

void Hud::drawCompassTranslate(HudElement *e, video::ITexture *texture,
		const core::rect<s32> &rect, int angle)
{
	const video::SColor color(255, 255, 255, 255);
	const video::SColor colors[] = {color, color, color, color};

	// Compute source image scaling
	core::dimension2di imgsize(texture->getOriginalSize());
	core::rect<s32> srcrect(0, 0, imgsize.Width, imgsize.Height);

	v2s32 dstsize(rect.getHeight() * e->scale.X * imgsize.Width / imgsize.Height,
			rect.getHeight() * e->scale.Y);

	// Avoid infinite loop
	if (dstsize.X <= 0 || dstsize.Y <= 0)
		return;

	core::rect<s32> tgtrect(0, 0, dstsize.X, dstsize.Y);
	tgtrect +=  v2s32(
				(rect.getWidth() - dstsize.X) / 2,
				(rect.getHeight() - dstsize.Y) / 2) +
			rect.UpperLeftCorner;

	int offset = angle * dstsize.X / 360;

	tgtrect += v2s32(offset, 0);

	// Repeat image as much as needed
	while (tgtrect.UpperLeftCorner.X > rect.UpperLeftCorner.X)
		tgtrect -= v2s32(dstsize.X, 0);

	draw2DImageFilterScaled(driver, texture, tgtrect, srcrect, &rect, colors, true);
	tgtrect += v2s32(dstsize.X, 0);

	while (tgtrect.UpperLeftCorner.X < rect.LowerRightCorner.X) {
		draw2DImageFilterScaled(driver, texture, tgtrect, srcrect, &rect, colors, true);
		tgtrect += v2s32(dstsize.X, 0);
	}
}

void Hud::drawCompassRotate(HudElement *e, video::ITexture *texture,
		const core::rect<s32> &rect, int angle)
{
	core::rect<s32> oldViewPort = driver->getViewPort();
	core::matrix4 oldProjMat = driver->getTransform(video::ETS_PROJECTION);
	core::matrix4 oldViewMat = driver->getTransform(video::ETS_VIEW);

	core::matrix4 Matrix;
	Matrix.makeIdentity();
	Matrix.setRotationDegrees(v3f(0.f, 0.f, angle));

	driver->setViewPort(rect);
	driver->setTransform(video::ETS_PROJECTION, core::matrix4());
	driver->setTransform(video::ETS_VIEW, core::matrix4());
	driver->setTransform(video::ETS_WORLD, Matrix);

	auto &material = m_rotation_mesh_buffer->getMaterial();
	material.TextureLayers[0].Texture = texture;
	driver->setMaterial(material);
	driver->drawMeshBuffer(m_rotation_mesh_buffer.get());

	driver->setTransform(video::ETS_WORLD, core::matrix4());
	driver->setTransform(video::ETS_VIEW, oldViewMat);
	driver->setTransform(video::ETS_PROJECTION, oldProjMat);

	// restore the view area
	driver->setViewPort(oldViewPort);
}

void Hud::drawStatbar(v2s32 pos, u16 corner, u16 drawdir,
		const std::string &texture, const std::string &bgtexture,
		s32 count, s32 maxcount, v2s32 offset, v2f size)
{
	const video::SColor color(255, 255, 255, 255);
	const video::SColor colors[] = {color, color, color, color};

	video::ITexture *stat_texture = tsrc->getTexture(texture);
	if (!stat_texture)
		return;

	video::ITexture *stat_texture_bg = nullptr;
	if (!bgtexture.empty()) {
		stat_texture_bg = tsrc->getTexture(bgtexture);
	}

	core::dimension2di srcd(stat_texture->getOriginalSize());
	core::dimension2di dstd;
	if (size == v2f()) {
		dstd = srcd;
		dstd.Height *= m_scale_factor;
		dstd.Width  *= m_scale_factor;
		offset.X *= m_scale_factor;
		offset.Y *= m_scale_factor;
	} else {
		dstd.Height = size.Y * m_scale_factor;
		dstd.Width  = size.X * m_scale_factor;
		offset.X *= m_scale_factor;
		offset.Y *= m_scale_factor;
	}

	v2s32 p = pos;
	if (corner & HUD_CORNER_LOWER)
		p -= dstd.Height;

	p += offset;

	v2s32 steppos;
	switch (drawdir) {
		case HUD_DIR_RIGHT_LEFT:
			steppos = v2s32(-1, 0);
			break;
		case HUD_DIR_TOP_BOTTOM:
			steppos = v2s32(0, 1);
			break;
		case HUD_DIR_BOTTOM_TOP:
			steppos = v2s32(0, -1);
			break;
		default:
			// From left to right
			steppos = v2s32(1, 0);
			break;
	}

	auto calculate_clipping_rect = [] (core::dimension2di src,
			v2s32 steppos) -> core::rect<s32> {

		// Create basic rectangle
		core::rect<s32> rect(0, 0,
			src.Width  - std::abs(steppos.X) * src.Width / 2,
			src.Height - std::abs(steppos.Y) * src.Height / 2
		);
		// Move rectangle left or down
		if (steppos.X == -1)
			rect += v2s32(src.Width / 2, 0);
		if (steppos.Y == -1)
			rect += v2s32(0, src.Height / 2);
		return rect;
	};
	// Rectangles for 1/2 the actual value to display
	core::rect<s32> srchalfrect, dsthalfrect;
	// Rectangles for 1/2 the "off state" texture
	core::rect<s32> srchalfrect2, dsthalfrect2;

	if (count % 2 == 1 || maxcount % 2 == 1) {
		// Need to draw halves: Calculate rectangles
		srchalfrect  = calculate_clipping_rect(srcd, steppos);
		dsthalfrect  = calculate_clipping_rect(dstd, steppos);
		srchalfrect2 = calculate_clipping_rect(srcd, steppos * -1);
		dsthalfrect2 = calculate_clipping_rect(dstd, steppos * -1);
	}

	steppos.X *= dstd.Width;
	steppos.Y *= dstd.Height;

	// Draw full textures
	for (s32 i = 0; i < count / 2; i++) {
		core::rect<s32> srcrect(0, 0, srcd.Width, srcd.Height);
		core::rect<s32> dstrect(0, 0, dstd.Width, dstd.Height);

		dstrect += p;
		draw2DImageFilterScaled(driver, stat_texture,
			dstrect, srcrect, NULL, colors, true);
		p += steppos;
	}

	if (count % 2 == 1) {
		// Draw half a texture
		draw2DImageFilterScaled(driver, stat_texture,
			dsthalfrect + p, srchalfrect, NULL, colors, true);

		if (stat_texture_bg && maxcount > count) {
			draw2DImageFilterScaled(driver, stat_texture_bg,
					dsthalfrect2 + p, srchalfrect2,
					NULL, colors, true);
			p += steppos;
		}
	}

	if (stat_texture_bg && maxcount > count) {
		// Draw "off state" textures
		s32 start_offset;
		if (count % 2 == 1)
			start_offset = count / 2 + 1;
		else
			start_offset = count / 2;
		for (s32 i = start_offset; i < maxcount / 2; i++) {
			core::rect<s32> srcrect(0, 0, srcd.Width, srcd.Height);
			core::rect<s32> dstrect(0, 0, dstd.Width, dstd.Height);

			dstrect += p;
			draw2DImageFilterScaled(driver, stat_texture_bg,
					dstrect, srcrect,
					NULL, colors, true);
			p += steppos;
		}

		if (maxcount % 2 == 1) {
			draw2DImageFilterScaled(driver, stat_texture_bg,
				dsthalfrect + p, srchalfrect, NULL, colors, true);
		}
	}
}
void Hud::drawHotbar(const v2s32 &pos, const v2f &offset, u16 dir, const v2f &align)
{
	if (g_touchcontrols)
		g_touchcontrols->resetHotbarRects();

	InventoryList *mainlist = inventory->getList("main");
	if (mainlist == NULL) {
		// Silently ignore this. We may not be initialized completely.
		return;
	}

	u16 playeritem = player->getWieldIndex();
#if IS_VOPI_ENGINE
	v2s32 screen_offset(offset.X, offset.Y - m_hud_padding);
#else
	v2s32 screen_offset(offset.X, offset.Y);
#endif

	s32 hotbar_itemcount = player->getMaxHotbarItemcount();
	const s32 slot_size = m_hotbar_imagesize + m_padding * 2;
	s32 width = hotbar_itemcount * slot_size;

#if IS_VOPI_ENGINE
	// On touch: reserve room on the right for the inventory button. The button
	// is intentionally a little smaller than a full hotbar slot (the slot has
	// 2*m_padding around the icon; the button has 1*m_padding) and the gap
	// between hotbar and button is tightened to 1*m_padding too, so the touch
	// composition feels tight without dwarfing the hotbar visually.
	// Shift the hotbar's center leftward by half of (button + gap) so the
	// composition (hotbar + gap + button) stays centered on screen.
	const bool touch_active = g_touchcontrols != nullptr;
	const s32 inv_button_size = m_hotbar_imagesize + m_padding;
	const s32 inv_gap = m_padding;
	const s32 inv_button_total = inv_button_size + inv_gap;
	v2s32 hotbar_pos = pos;
	if (touch_active)
		hotbar_pos.X -= inv_button_total / 2;
#else
	const v2s32 hotbar_pos = pos;
#endif

	const v2u32 &window_size = RenderingEngine::getWindowSize();
	// When touch is active the inventory button also occupies horizontal
	// space; fold it into the max-width check so we don't accidentally fit
	// the hotbar on screen while the button overflows.
#if IS_VOPI_ENGINE
	const s32 effective_width = touch_active ? width + inv_button_total : width;
#else
	const s32 effective_width = width;
#endif
	if ((float) effective_width / (float) window_size.X <=
			g_settings->getFloat("hud_hotbar_max_width")) {
		drawItems(hotbar_pos, screen_offset, hotbar_itemcount, align, 0,
			mainlist, playeritem + 1, dir, true);
	} else {
		v2s32 upper_pos = hotbar_pos - v2s32(0, m_hotbar_imagesize + m_padding);

		drawItems(upper_pos, screen_offset, hotbar_itemcount / 2, align, 0,
			mainlist, playeritem + 1, dir, true);
		drawItems(hotbar_pos, screen_offset, hotbar_itemcount, align,
			hotbar_itemcount / 2, mainlist, playeritem + 1, dir, true);
	}
#if IS_VOPI_ENGINE
	// Cache the hotbar's top edge for anchor_above_hotbar HUD elements (read by
	// Hud::getImageElementRect / drawLuaElements) AND reused as the inventory
	// button's top below. The hotbar is bottom-aligned (align.Y = -1), so its top
	// is hotbar_pos.Y plus the scaled screen offset minus one slot box. In a
	// two-row layout the lower row stays at hotbar_pos, so elements anchor to the
	// lower row's top -- acceptable. Kept unconditional (not gated on touch) so
	// anchor_above_hotbar HUD elements get a valid value on desktop too.
	m_hotbar_top_y = hotbar_pos.Y
			+ (s32) std::round(screen_offset.Y * m_scale_factor)
			- slot_size;

	// Anchor the inventory touch button at the right edge of the (lower) row,
	// reusing the cached top edge. Derived from the same math drawItems uses for
	// placement so the button stays aligned across hud_scaling,
	// hud_hotbar_bottom_margin, and screen rotations.
	if (touch_active) {
		const s32 row_y_top = m_hotbar_top_y;
		const s32 row_right = hotbar_pos.X
				+ (s32) std::round(screen_offset.X * m_scale_factor)
				+ width / 2; // align.X = 0 centers width around hotbar_pos.X
		const s32 inv_left = row_right + inv_gap;
		// Vertically center the (slightly smaller) button against the slot.
		const s32 inv_top = row_y_top + (slot_size - inv_button_size) / 2;
		g_touchcontrols->setInventoryButtonRect(core::recti(
				inv_left, inv_top,
				inv_left + inv_button_size, inv_top + inv_button_size));
	}
#endif
}


void Hud::drawCrosshair()
{
	auto draw_image_crosshair = [this] (video::ITexture *tex) {
		core::dimension2di orig_size(tex->getOriginalSize());
		// Integer scaling to avoid artifacts, floor instead of round since too
		// small looks better than too large in this case.
		core::dimension2di scaled_size = orig_size * std::max(std::floor(m_scale_factor), 1.0f);

		core::rect<s32> src_rect(orig_size);
		core::position2d pos(m_displaycenter.X - scaled_size.Width / 2,
				m_displaycenter.Y - scaled_size.Height / 2);
		core::rect<s32> dest_rect(pos, scaled_size);

		video::SColor colors[] = { crosshair_argb, crosshair_argb,
				crosshair_argb, crosshair_argb };

		draw2DImageFilterScaled(driver, tex, dest_rect, src_rect,
				nullptr, colors, true);
	};

	if (pointing_at_object) {
		if (use_object_crosshair_image) {
			draw_image_crosshair(tsrc->getTexture("object_crosshair.png"));
		} else {
			s32 line_size = core::round32(OBJECT_CROSSHAIR_LINE_SIZE * m_scale_factor);

			driver->draw2DLine(
					m_displaycenter - v2s32(line_size, line_size),
					m_displaycenter + v2s32(line_size, line_size),
					crosshair_argb);
			driver->draw2DLine(
					m_displaycenter + v2s32(line_size, -line_size),
					m_displaycenter + v2s32(-line_size, line_size),
					crosshair_argb);
		}

		return;
	}

	if (use_crosshair_image) {
		draw_image_crosshair(tsrc->getTexture("crosshair.png"));
	} else {
		s32 line_size = core::round32(CROSSHAIR_LINE_SIZE * m_scale_factor);

		driver->draw2DLine(m_displaycenter - v2s32(line_size, 0),
				m_displaycenter + v2s32(line_size, 0), crosshair_argb);
		driver->draw2DLine(m_displaycenter - v2s32(0, line_size),
				m_displaycenter + v2s32(0, line_size), crosshair_argb);
	}
}

void Hud::setSelectionPos(const v3f &pos, const v3s16 &camera_offset)
{
	m_camera_offset = camera_offset;
	m_selection_pos = pos;
	m_selection_pos_with_offset = pos - intToFloat(camera_offset, BS);
}

void Hud::drawSelectionMesh()
{
	if (m_mode == HIGHLIGHT_NONE || (m_mode == HIGHLIGHT_HALO && !m_selection_mesh))
		return;
	driver->setMaterial(m_selection_material);
	const core::matrix4 oldtransform = driver->getTransform(video::ETS_WORLD);

	core::matrix4 translate;
	translate.setTranslation(m_selection_pos_with_offset);
	core::matrix4 rotation;
	rotation.setRotationRadians(m_selection_rotation_radians);
	driver->setTransform(video::ETS_WORLD, translate * rotation);

	if (m_mode == HIGHLIGHT_BOX) {
		// Draw 3D selection boxes
		for (auto & selection_box : m_selection_boxes) {
			u32 r = (selectionbox_argb.getRed() *
					m_selection_mesh_color.getRed() / 255);
			u32 g = (selectionbox_argb.getGreen() *
					m_selection_mesh_color.getGreen() / 255);
			u32 b = (selectionbox_argb.getBlue() *
					m_selection_mesh_color.getBlue() / 255);
			driver->draw3DBox(selection_box, video::SColor(255, r, g, b));
		}
	} else if (m_mode == HIGHLIGHT_HALO && m_selection_mesh) {
		// Draw selection mesh
		setMeshColor(m_selection_mesh, m_selection_mesh_color);
		video::SColor face_color(0,
			MYMIN(255, m_selection_mesh_color.getRed() * 1.5),
			MYMIN(255, m_selection_mesh_color.getGreen() * 1.5),
			MYMIN(255, m_selection_mesh_color.getBlue() * 1.5));
		setMeshColorByNormal(m_selection_mesh, m_selected_face_normal,
			face_color);
		u32 mc = m_selection_mesh->getMeshBufferCount();
		for (u32 i = 0; i < mc; i++) {
			scene::IMeshBuffer *buf = m_selection_mesh->getMeshBuffer(i);
			driver->drawMeshBuffer(buf);
		}
	}
	driver->setTransform(video::ETS_WORLD, oldtransform);
}

enum Hud::BlockBoundsMode Hud::toggleBlockBounds()
{
	m_block_bounds_mode = static_cast<BlockBoundsMode>(m_block_bounds_mode + 1);

	if (m_block_bounds_mode > BLOCK_BOUNDS_NEAR) {
		m_block_bounds_mode = BLOCK_BOUNDS_OFF;
	}
	return m_block_bounds_mode;
}

void Hud::disableBlockBounds()
{
	m_block_bounds_mode = BLOCK_BOUNDS_OFF;
}

void Hud::drawBlockBounds()
{
	if (m_block_bounds_mode == BLOCK_BOUNDS_OFF) {
		return;
	}

	driver->setMaterial(m_block_bounds_material);

	u16 mesh_chunk_size = std::max<u16>(1, g_settings->getU16("client_mesh_chunk"));

	v3s16 block_pos = getContainerPos(player->getStandingNodePos(), MAP_BLOCKSIZE);

	v3f cam_offset = intToFloat(client->getCamera()->getOffset(), BS);

	v3f half_node = v3f(BS, BS, BS) / 2.0f;
	v3f base_corner = intToFloat(block_pos * MAP_BLOCKSIZE, BS) - cam_offset - half_node;

	s16 radius = m_block_bounds_mode == BLOCK_BOUNDS_NEAR ?
			rangelim(g_settings->getU16("show_block_bounds_radius_near"), 0, 1000) : 0;

	for (s16 x = -radius; x <= radius + 1; x++)
	for (s16 y = -radius; y <= radius + 1; y++) {
		// Red for mesh chunk edges, yellow for other block edges.
		auto choose_color = [&](s16 x_base, s16 y_base) {
			// See also MeshGrid::isMeshPos().
			// If the block is mesh pos, it means it's at the (-,-,-) corner of
			// the mesh. And we're drawing a (-,-) edge of this block. Hence,
			// it is an edge of the mesh grid.
			return (x + x_base) % mesh_chunk_size == 0
					&& (y + y_base) % mesh_chunk_size == 0 ?
				video::SColor(255, 255, 0, 0) :
				video::SColor(255, 255, 255, 0);
		};

		v3f pmin = v3f(x, y,    -radius) * MAP_BLOCKSIZE * BS;
		v3f pmax = v3f(x, y, 1 + radius) * MAP_BLOCKSIZE * BS;

		driver->draw3DLine(
			base_corner + pmin,
			base_corner + pmax,
			choose_color(block_pos.X, block_pos.Y)
		);
		driver->draw3DLine(
			base_corner + v3f(pmin.X, pmin.Z, pmin.Y),
			base_corner + v3f(pmax.X, pmax.Z, pmax.Y),
			choose_color(block_pos.X, block_pos.Z)
		);
		driver->draw3DLine(
			base_corner + v3f(pmin.Z, pmin.X, pmin.Y),
			base_corner + v3f(pmax.Z, pmax.X, pmax.Y),
			choose_color(block_pos.Y, block_pos.Z)
		);
	}
}

void Hud::updateSelectionMesh(const v3s16 &camera_offset)
{
	m_camera_offset = camera_offset;
	if (m_mode != HIGHLIGHT_HALO)
		return;

	if (m_selection_mesh) {
		m_selection_mesh->drop();
		m_selection_mesh = NULL;
	}

	if (m_selection_boxes.empty()) {
		// No pointed object
		return;
	}

	// New pointed object, create new mesh.

	// Texture UV coordinates for selection boxes
	static f32 texture_uv[24] = {
		0,0,1,1,
		0,0,1,1,
		0,0,1,1,
		0,0,1,1,
		0,0,1,1,
		0,0,1,1
	};

	// Use single halo box instead of multiple overlapping boxes.
	// Temporary solution - problem can be solved with multiple
	// rendering targets, or some method to remove inner surfaces.
	// Thats because of halo transparency.

	aabb3f halo_box(100.0, 100.0, 100.0, -100.0, -100.0, -100.0);
	m_halo_boxes.clear();

	for (const auto &selection_box : m_selection_boxes) {
		halo_box.addInternalBox(selection_box);
	}

	m_halo_boxes.push_back(halo_box);
	m_selection_mesh = convertNodeboxesToMesh(
		m_halo_boxes, texture_uv, 0.5);
}

void Hud::resizeHotbar() {
	const v2u32 &window_size = RenderingEngine::getWindowSize();

	if (m_screensize != window_size) {
		m_hotbar_imagesize = floor(HOTBAR_IMAGE_SIZE *
			RenderingEngine::getDisplayDensity() + 0.5);
		m_hotbar_imagesize *= m_hud_scaling;
		m_padding = m_hotbar_imagesize / 12;
		m_screensize = window_size;
		m_displaycenter = v2s32(m_screensize.X/2,m_screensize.Y/2);
#if IS_VOPI_ENGINE
		// Seed the hotbar top-edge cache (used by anchor_above_hotbar elements)
		// so it is valid before the first drawHotbar and survives drawHotbar's
		// mainlist==NULL early-return. drawHotbar refines it to the exact edge
		// (incl. bottom margin) each frame the hotbar is drawn.
		m_hotbar_top_y = (s32) m_screensize.Y - (m_hotbar_imagesize + m_padding * 2);
#endif
	}
}
