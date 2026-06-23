// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2020 Jean-Patrick Guerrero <jeanpatrick.guerrero@gmail.com>

#pragma once

#include "ICameraSceneNode.h"
#include "StyleSpec.h"
#include <AnimatedMeshSceneNode.h>
#include <IGUIElement.h>
#include <IGUIEnvironment.h>
#include <string>
#include <vector>


class GUIScene : public gui::IGUIElement
{
public:
	GUIScene(gui::IGUIEnvironment *env, scene::ISceneManager *smgr,
		 gui::IGUIElement *parent, core::recti rect, s32 id = -1);

	~GUIScene();

	/// @param mesh does not get consumed, mesh->drop() must still be called afterward
	scene::AnimatedMeshSceneNode *setMesh(scene::IAnimatedMesh *mesh = nullptr);

	void setTexture(u32 idx, video::ITexture *texture);
	void setBackgroundColor(const video::SColor &color) noexcept { m_bgcolor = color; };
	void setFrameLoop(f32 begin, f32 end);
	void setAnimationSpeed(f32 speed);
	void enableMouseControl(bool enable) noexcept { m_mouse_ctrl = enable; };
	void setRotation(v2f rot) noexcept { m_custom_rot = rot; };
	void enableContinuousRotation(bool enable) noexcept { m_inf_rot = enable; };
	void setStyles(const std::array<StyleSpec, StyleSpec::NUM_STATES> &styles);

#if IS_VOPI_ENGINE
	// VOPI extension — attach a secondary mesh to a named bone of the
	// primary mesh. Mirrors entity:set_attach behavior in-world so
	// wearable previews can use the same .obj files as world-side
	// attachments. The bone must exist on the primary mesh set via
	// setMesh(); if it doesn't, the call logs an error and returns
	// nullptr without spawning the node.
	//
	// Textures are applied per attachment-mesh material slot, identical
	// settings to setTexture() (alpha-blended, nearest-neighbour, no
	// backface culling). The `textures` vector is indexed by
	// mesh->getTextureSlot(material_index); under-supplied entries log a
	// warning, missing slots fall back to the mesh's default material.
	//
	// Returned pointer is owned by the scene manager — DO NOT drop() it.
	// We also retain it in m_attachments for cleanup tracking via
	// clearAttachments() (also called from setMesh()). The whole subtree
	// is also implicitly destroyed when the scene manager is dropped in
	// ~GUIScene; clearAttachments() is the explicit path used between
	// setMesh swaps.
	scene::AnimatedMeshSceneNode *addAttachment(
		scene::IAnimatedMesh *mesh,
		const std::vector<video::ITexture *> &textures,
		const std::string &bone_name,
		const v3f &position, const v3f &rotation, const v3f &scale);

	void clearAttachments();
#endif

	virtual void draw();
	virtual bool OnEvent(const SEvent &event);

private:
	void calcOptimalDistance();
	void updateTargetPos();
	void updateCamera(scene::ISceneNode *target);
	void setCameraRotation(v3f rot);
	/// @return true indicates that the rotation was corrected
	bool correctBounds(v3f &rot);
	void cameraLoop();

	void updateCameraPos() { m_cam_pos = m_cam->getPosition(); };
	v3f getCameraRotation() const { return (m_cam_pos - m_target_pos).getHorizontalAngle(); };
	void rotateCamera(const v3f &delta) { setCameraRotation(getCameraRotation() + delta); };

	scene::ISceneManager *m_smgr;
	video::IVideoDriver *m_driver;
	scene::ICameraSceneNode *m_cam;
	scene::ISceneNode *m_target = nullptr;
	scene::AnimatedMeshSceneNode *m_mesh = nullptr;

#if IS_VOPI_ENGINE
	// Secondary meshes attached to bones of m_mesh. Owned by m_smgr; we
	// hold raw observer pointers only so clearAttachments() can selectively
	// remove our nodes (without disturbing other smgr children) and so
	// calcOptimalDistance() can union their aabb with the primary's.
	// Validity: each pointer remains valid until clearAttachments() is
	// called (which runs from setMesh swap and from ~GUIScene via the
	// IGUIElement base destructor path).
	std::vector<scene::AnimatedMeshSceneNode *> m_attachments;
#endif

	f32 m_cam_distance = 50.f;

	u64 m_last_time = 0;

	v3f m_cam_pos;
	v3f m_target_pos;
	v3f m_last_target_pos;
	// Cursor positions
	v2f m_curr_pos;
	v2f m_last_pos;
	// Initial rotation
	v2f m_custom_rot;

	bool m_mouse_ctrl = true;
	bool m_update_cam = false;
	bool m_inf_rot    = false;
	bool m_initial_rotation = true;

	video::SColor m_bgcolor;
};
